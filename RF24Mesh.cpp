/********************************************************************************
*   File Name:
*       RF24Mesh.cpp
*
*   Description:
*       Implementation of the RF24 Mesh Networking Layer
*
*   2019 | Brandon Braun | brandonbraun653@gmail.com
********************************************************************************/

/* Mesh Includes */
#include "RF24Mesh.hpp"
#include "RF24MeshDefinitions.hpp"

namespace RF24Mesh
{
    Mesh::Mesh(NRF24L::NRF24L01 &radio, RF24Network::Network &network): radio(radio), network(network)
    {
        this->oopsies = ErrorType::NO_ERROR;
        meshNetworkAddress = MESH_DEFAULT_ADDRESS;
    }

    bool Mesh::begin(const uint8_t channel, const NRF24L::DataRate dataRate, const NRF24L::PowerAmplitude pwr, uint32_t timeout)
    {
        /*------------------------------------------------
        Initialize the network to some default values. Internally also configures
        the radio HW parameters for proper operation.
        ------------------------------------------------*/
        if (!network.begin(channel, MESH_DEFAULT_ADDRESS, dataRate, pwr))
        {
            oopsies = ErrorType::FAILED_INIT;
            return false;
        }

        bool result = true;
        this->radioChannel = channel;
        network.returnSysMsgs = 1;

        /*------------------------------------------------
        Reconfigure the network address based on node ID
        ------------------------------------------------*/
        if (getNodeID() == MESH_MASTER_NODE_ID)
        {
            IF_SERIAL_DEBUG(printf("%lu: MSH Initializing master node\r\n", radio.millis()););
            meshNetworkAddress = MESH_MASTER_NODE_ID;
            result = network.setAddress(meshNetworkAddress);
        }
        else
        {
            IF_SERIAL_DEBUG(printf("%lu: MSH Initializing mesh node\r\n", radio.millis()););

            uint16_t dummyAddr;
            if (!renewAddress(dummyAddr, timeout))
            {
                result = false;
            }
        }

        return result;
    }

    RF24Network::MessageType Mesh::update()
    {
        /*------------------------------------------------
        Nothing to be done because begin() hasn't been called yet
        ------------------------------------------------*/
        if (meshNetworkAddress == MESH_DEFAULT_ADDRESS)
        {
            oopsies = ErrorType::NOT_CONFIGURED;
            return RF24Network::MessageType::NO_MESSAGE;
        }

        /*------------------------------------------------
        Call the network's update function so we keep things running smoothly
        ------------------------------------------------*/
        auto type = network.update();

        /*------------------------------------------------
        Do we need to do some DHCP processing?
        ------------------------------------------------*/
        if (    (type == RF24Network::MessageType::MESH_REQ_ADDRESS)
            ||  (type == RF24Network::MessageType::MESH_ADDR_RESPONSE)
           )
        {
            processDHCP = true;
            DHCPFrame(network.frameBuffer);
        }

        /*------------------------------------------------
        Assuming we are the master device, get some work done
        ------------------------------------------------*/
        if (getNodeID() == MESH_MASTER_NODE_ID)
        {
            /*------------------------------------------------
            Pull out the header information from the network layer
            ------------------------------------------------*/
            RF24Network::Header header(network.frameBuffer);

            /*------------------------------------------------
            Process lookup requests?
            ------------------------------------------------*/
            if ((type == RF24Network::MessageType::MESH_ADDR_LOOKUP) || (type == RF24Network::MessageType::MESH_ID_LOOKUP))
            {
                lookupDHCPAddress(type, header.data.dstNode);
            }
            /*------------------------------------------------
            Maybe we need to release an address?
            ------------------------------------------------*/
            else if (type == RF24Network::MessageType::MESH_ADDR_RELEASE)
            {
                releaseDHCPAddress(header.data.srcNode);
            }
            /*------------------------------------------------
            Maybe we need to confirm an address?
            ------------------------------------------------*/
            else if (type == RF24Network::MessageType::MESH_ADDR_CONFIRM)
            {
                confirmDHCPAddress(header.data.srcNode);
            }
        }

        return type;
    }

    bool Mesh::write(const void *const data, const RF24Network::MessageType msgType, const size_t size, const uint8_t nodeID)
    {
        if (meshNetworkAddress == MESH_DEFAULT_ADDRESS)
        {
            oopsies = ErrorType::NOT_CONFIGURED;
            return false;
        }


        int16_t toNode = 0;
        int32_t lookupTimeout = radio.millis() + MESH_LOOKUP_TIMEOUT;
        uint32_t retryDelay = 50;

        if (nodeID)
        {
            while ((toNode = getAddress(nodeID)) < 0)
            {
                //TODO: Add better naming to this -2
                if (radio.millis() > lookupTimeout || toNode == -2)
                {
                    return false;
                }

                retryDelay += 50;
                radio.delayMilliseconds(retryDelay);
            }
        }
        return writeTo(toNode, data, msgType, size);
    }

    bool Mesh::writeTo(const uint16_t node, const void *const data, const RF24Network::MessageType msgType, const size_t size)
    {
        if (meshNetworkAddress == MESH_DEFAULT_ADDRESS)
        {
            oopsies = ErrorType::NOT_CONFIGURED;
            return false;
        }

        RF24Network::Header header(node, msgType);
        return network.write(header, data, size);
    }

    void Mesh::setChannel(const uint8_t channel)
    {
        radioChannel = channel;
        radio.setChannel(radioChannel);
        radio.startListening();
    }

    void Mesh::setChild(const bool allow)
    {
        if(allow)
        {
            network.networkFlags &= ~static_cast<uint8_t>(RF24Network::FlagType::NO_POLL);
        }
        else
        {
            network.networkFlags |= static_cast<uint8_t>(RF24Network::FlagType::NO_POLL);
        }
    }

    bool Mesh::checkConnection()
    {
        uint8_t attempts = 3;
        bool result = false;

        /*------------------------------------------------
        Make a few attempts to get some kind of response from the network.
        ------------------------------------------------*/
        while (attempts-- && (meshNetworkAddress != MESH_DEFAULT_ADDRESS))
        {
            /*------------------------------------------------
            Keep the mesh network processing subsystem alive
            ------------------------------------------------*/
            update();

            /*------------------------------------------------
            Have we received any data?
            ------------------------------------------------*/
            if (radio.rxFifoFull() || (network.networkFlags & static_cast<uint8_t>(RF24Network::FlagType::HOLD_INCOMING)))
            {
                return true;
            }

            /*------------------------------------------------
            Try pinging the master node
            ------------------------------------------------*/
            RF24Network::Header header(MESH_MASTER_NODE_ID, RF24Network::MessageType::NETWORK_PING);
            result = network.write(header, nullptr, 0);
            if (result)
            {
                break;
            }

            /*------------------------------------------------
            Oooh magic delay. Very suspicious.
            ------------------------------------------------*/
            radio.delayMilliseconds(103);
        }

        /*------------------------------------------------
        We failed, disconnect entirely by going to standby mode
        ------------------------------------------------*/
        if (!result)
        {
            radio.stopListening();
        }

        return result;
    }

    int16_t Mesh::getAddress(uint8_t nodeID)
    {
        /*------------------------------------------------
        We are the master, look up address through our address list
        ------------------------------------------------*/
        if (getNodeID() == MESH_MASTER_NODE_ID)
        {
            uint16_t address = 0;

            for (uint8_t i = 0; i < addressList.size(); i++)
            {
                if (addressList[i].id == nodeID)
                {
                    address = addressList[i].logicalAddress;
                    return address;
                }
            }

            oopsies = ErrorType::NOT_CONFIGURED;
            return -1;
        }

        /*------------------------------------------------
        We haven't been initialized
        ------------------------------------------------*/
        if (meshNetworkAddress == MESH_DEFAULT_ADDRESS)
        {
            oopsies = ErrorType::NOT_CONFIGURED;
            return -1;
        }

        /*------------------------------------------------
        User specified master node but we are not that node
        ------------------------------------------------*/
        if (nodeID == MESH_MASTER_NODE_ID)
        {
            oopsies = ErrorType::INVALID_PARAM;
            return 0;
        }

        /*------------------------------------------------
        By this point, we are a non-master node connected to the mesh. We
        need to ask master for our address assignment.
        ------------------------------------------------*/
        RF24Network::Header header(MESH_MASTER_NODE_ID, RF24Network::MessageType::MESH_ADDR_LOOKUP);
        if (network.write(header, &nodeID, sizeof(nodeID) + 1))
        {
            uint32_t timer = radio.millis(), timeout = 150;
            while (network.update() != RF24Network::MessageType::MESH_ADDR_LOOKUP)
            {
                if ((radio.millis() - timer) > timeout)
                {
                    oopsies = ErrorType::FAILED_ADDR_LOOKUP;
                    return -1;
                }
            }
        }
        else
        {
            oopsies = ErrorType::FAILED_WRITE;
            return -1;
        }

        /*------------------------------------------------
        Pull our assigned address from the network frame buffer
        ------------------------------------------------*/
        int16_t address = 0;
        memcpy(&address, network.frameBuffer.begin() + sizeof(RF24Network::Header_t), sizeof(address));
        return address >= 0 ? address : -2;
    }

    int16_t Mesh::getNodeID(const uint16_t address)
    {
        if (address == MESH_BLANK_ID)
        {
            return nodeID;
        }
        else if (address == 0)
        {
            return 0;
        }

        /*------------------------------------------------
        Are we the master node?
        ------------------------------------------------*/
        if (!meshNetworkAddress)
        {
            /*------------------------------------------------
            Look through our address list to find the node id
            ------------------------------------------------*/
            for (uint8_t i = 0; i < addressList.size(); i++)
            {
                if (addressList[i].logicalAddress == address)
                {
                    return addressList[i].id;
                }
            }
        }
        else
        {
            /*------------------------------------------------
            We haven't been initialized yet.
            ------------------------------------------------*/
            if (meshNetworkAddress == MESH_DEFAULT_ADDRESS)
            {
                return -1;
            }

            /*------------------------------------------------
            Ask the master node for the address
            ------------------------------------------------*/
            RF24Network::Header header(MESH_MASTER_NODE_ID, RF24Network::MessageType::MESH_ID_LOOKUP);

            if (network.write(header, &address, sizeof(address)))
            {
                int16_t ID;
                uint32_t timer = radio.millis();
                constexpr uint32_t timeout = 500;

                /*------------------------------------------------
                Wait for the network response
                ------------------------------------------------*/
                while (network.update() != RF24Network::MessageType::MESH_ID_LOOKUP)
                {
                    if ((radio.millis() - timer) > timeout)
                    {
                        oopsies = ErrorType::TIMEOUT;
                        return -1;
                    }
                }

                /*------------------------------------------------
                Pull the id out from the frame buffer
                ------------------------------------------------*/
                memcpy(&ID, &network.frameBuffer[sizeof(RF24Network::Header)], sizeof(ID));
                return ID;
            }
        }

        return -1;
    }

    bool Mesh::releaseAddress()
    {
        /*------------------------------------------------
        Have we obtained an address from the master node yet?
        ------------------------------------------------*/
        if (meshNetworkAddress == MESH_DEFAULT_ADDRESS)
        {
            oopsies = ErrorType::NOT_CONFIGURED;
            return false;
        }

        /*------------------------------------------------
        Inform the master node that it can release our address
        ------------------------------------------------*/
        RF24Network::Header header(MESH_MASTER_NODE_ID, static_cast<uint8_t>(RF24Network::MessageType::MESH_ADDR_RELEASE));
        if (network.write(header, nullptr, 0))
        {
            network.setAddress(MESH_DEFAULT_ADDRESS);
            meshNetworkAddress = MESH_DEFAULT_ADDRESS;
            return true;
        }
        return false;
    }

    bool Mesh::renewAddress(uint16_t &newAddress, const uint32_t timeout)
    {
        /*------------------------------------------------
        This isn't necessarily an error condition, but the radio should be
        clear of data before attempting to get a new address.
        ------------------------------------------------*/
        if (radio.available())
        {
            oopsies = ErrorType::PENDING_DATA;
            return false;
        }

        bool result = true;
        uint8_t reqCounter = 0u;
        uint8_t totalReqs = 0u;
        radio.stopListening();

        /*------------------------------------------------
        Prevent holds from blocking. This may cause some data loss, but
        we really need a new address.
        ------------------------------------------------*/
        network.networkFlags |= static_cast<uint8_t>(RF24Network::FlagType::BYPASS_HOLDS);
        radio.delayMilliseconds(10);

        /*------------------------------------------------
        Reset the network back to default addressing
        ------------------------------------------------*/
        network.setAddress(MESH_DEFAULT_ADDRESS);
        meshNetworkAddress = MESH_DEFAULT_ADDRESS;

        /*------------------------------------------------
        Request a new address from the Master node. If found, the internal
        meshNetworkAddress variable gets updated.
        ------------------------------------------------*/
        uint32_t start = radio.millis();
        while (!requestAddress(reqCounter))
        {
            /*------------------------------------------------
            Make sure we aren't stuck here forever
            ------------------------------------------------*/
            if ((radio.millis() - start) > timeout)
            {
                oopsies = ErrorType::TIMEOUT;
                result = false;
                break;
            }

            /*------------------------------------------------
            Delay using auto-backoff as the number of attempts increase
            ------------------------------------------------*/
            radio.delayMilliseconds(50 + ((totalReqs + 1) * (reqCounter + 1)) * 2);
            reqCounter++;
            reqCounter = reqCounter % 4;

            totalReqs++;
            totalReqs = totalReqs % 10;
        }

        /*------------------------------------------------
        Let the user know what the new address is
        ------------------------------------------------*/
        if(result)
        {
            newAddress = meshNetworkAddress;
        }
        else
        {
            newAddress = MESH_BLANK_ID;
        }

        /*------------------------------------------------
        Turn off the bypass hold flag
        ------------------------------------------------*/
        network.networkFlags &= ~static_cast<uint8_t>(RF24Network::FlagType::BYPASS_HOLDS);

        return result;
    }

    bool Mesh::requestAddress(const uint8_t level)
    {
        /*------------------------------------------------
        Initialize local variables
        ------------------------------------------------*/
        uint8_t pollCount = 0;                         /* How many successfull polls we've hit */
        uint32_t timer = radio.millis();               /* Start time of this process */
        std::array<uint16_t, MESH_MAXPOLLS> pollNodes; /* Which nodes we've successfully polled against */

        pollNodes.fill(RF24Network::EMPTY_LOGICAL_ADDRESS);

        /*------------------------------------------------
        Reach out to all radios at a given level and see if one responds
        saying that it has a space to join the network.
        ------------------------------------------------*/
        RF24Network::Header header(RF24Network::MULTICAST_ADDRESS, RF24Network::MessageType::NETWORK_POLL);
        network.multicast(header, nullptr, 0, level);

        /*------------------------------------------------
        Wait for a radio (contact node) to respond
        ------------------------------------------------*/
        while (true)
        {
            /*------------------------------------------------
            Has the networking layer received a poll response from some node?
            If so, store that node's logical address in a buffer.
            ------------------------------------------------*/
            if (network.update() == RF24Network::MessageType::NETWORK_POLL)
            {
                header(network.frameBuffer);

                pollNodes[pollCount] = header.data.srcNode;
                pollCount++;
            }

            /*------------------------------------------------
            Have we reached a polling timeout or a max number of polls?
            ------------------------------------------------*/
            if (((radio.millis() - timer) > MESH_POLL_TIMEOUT_MS) || (pollCount >= MESH_MAXPOLLS))
            {
                /*------------------------------------------------
                No nodes responded to our polling requests.
                ------------------------------------------------*/
                if (!pollCount)
                {
                    IF_SERIAL_DEBUG(printf("%lu: MSH No poll from level %d\r\n", radio.millis(), level););
                    oopsies = ErrorType::POLL_FAIL;
                    return false;
                }
                else
                {
                    IF_SERIAL_DEBUG(printf("%lu: MSH Poll OK\r\n", radio.millis()););
                    break;
                }
            }
        }

        /*------------------------------------------------
        An adjacent radio was found! Try to route through each one to reach the master
        and get our new address. Should the Master node respond, the node that response
        came through is stored into 'contactNode'.
        ------------------------------------------------*/
        IF_SERIAL_DEBUG(printf("%lu: MSH Got poll from level [%d], count [%d]\n", radio.millis(), level, pollCount););

        uint16_t contactNode = RF24Network::EMPTY_LOGICAL_ADDRESS;

        for (uint8_t i = 0; i < pollCount; i++)
        {
            /*------------------------------------------------
            Prevent us from trying to contact an invalid node
            ------------------------------------------------*/
            if (!network.isValidNetworkAddress(pollNodes[i]))
            {
                continue;
            }
            uint16_t pollNode = pollNodes[i];

            header.data.msgType = static_cast<uint8_t>(RF24Network::MessageType::MESH_REQ_ADDRESS);
            header.data.reserved = getNodeID();
            header.data.dstNode = pollNodes[i];
            header.data.srcNode = network.getLogicalAddress();

            /*------------------------------------------------
            Send a message back to our poll node requesting an address. The poll node will then
            forward the address up to the master node. The master node will unpack the message
            and see that the request originated from a potential child of the poll node and assign
            the address appropriately, assuming the poll node as the new parent.
            ------------------------------------------------*/
            IF_SERIAL_DEBUG(printf("%lu MSH: Request address from node [0%o]\n", radio.millis(), pollNode););
            network.write(header, &pollNode, sizeof(pollNode), pollNode);

            /*------------------------------------------------
            Wait for a response from Master through our poll node
            ------------------------------------------------*/
            timer = radio.millis();
            while ((radio.millis() - timer) < 225)
            {
                /*------------------------------------------------
                Save which node we talked to succesfully and then break out of the while and for loop.
                ------------------------------------------------*/
                if (network.update() == RF24Network::MessageType::MESH_ADDR_RESPONSE)
                {
                    contactNode = pollNodes[i];
                    i = pollCount;
                    break;
                }
            }

            radio.delayMilliseconds(5);
        }

        /*------------------------------------------------
        No response from any of those nodes we found before?
        ------------------------------------------------*/
        if (contactNode == RF24Network::EMPTY_LOGICAL_ADDRESS)
        {
            oopsies = ErrorType::NO_RESPONSE;
            return false;
        }

        /*------------------------------------------------
        Pull the new address out from the network frame buffer
        ------------------------------------------------*/
        uint16_t newAddress = 0;
        memcpy(&header.data, network.frameBuffer.begin(), sizeof(RF24Network::Header_t));
        memcpy(&newAddress, network.frameBuffer.begin() + sizeof(RF24Network::Header_t), sizeof(newAddress));

        /*------------------------------------------------
        Is the address invalidated? This can happen if:
            1. Address is zero
            2. The destination node is not ourselves (stored in the reserved byte)
        ------------------------------------------------*/
        if (!newAddress || (header.data.reserved != getNodeID()))
        {
            IF_SERIAL_DEBUG(printf("%lu: Response discarded, wrong node 0%o from node 0%o sending node 0%o id %d\r\n",
                radio.millis(), newAddress, header.data.srcNode, MESH_DEFAULT_ADDRESS, header.data.reserved););

            oopsies = ErrorType::FAILED_ADDR_REQUEST;
            return false;
        }

        /*------------------------------------------------
        Let the Master node know we received the address ok
        ------------------------------------------------*/
        header.data.dstNode = MESH_MASTER_NODE_ID;
        header.data.srcNode = getNodeID();
        header.data.msgType = static_cast<uint8_t>(RF24Network::MessageType::MESH_ADDR_CONFIRM);

        uint8_t registerAddrCount = 0u;
        while (!network.write(header, nullptr, 0, contactNode))
        {
            if (registerAddrCount++ >= 6)
            {
                oopsies = ErrorType::FAILED_ADDR_CONFIRM;
                network.setAddress(MESH_DEFAULT_ADDRESS);
                meshNetworkAddress = MESH_DEFAULT_ADDRESS;
                return false;
            }
            radio.delayMilliseconds(3);
        }

        /*------------------------------------------------
        Internally assign the new address
        ------------------------------------------------*/
        IF_SERIAL_DEBUG(printf("%lu: Set current address 0%o to new address 0%o\r\n", radio.millis(), meshNetworkAddress, newAddress););
        meshNetworkAddress = newAddress;

        radio.stopListening();
        radio.delayMilliseconds(10);

        network.setAddress(meshNetworkAddress);

        return true;
    }

    void Mesh::setNodeID(const uint8_t nodeID)
    {
        this->nodeID = nodeID;
    }

    void Mesh::setAddress(const uint8_t nodeID, const uint16_t address)
    {
        uint8_t position = addressList.size();

        /*------------------------------------------------
        Search through the address list for the position of the nodeID
        ------------------------------------------------*/
        for (uint8_t i = 0; i < addressList.size(); i++)
        {
            if (addressList[i].id == nodeID)
            {
                position = i;
                break;
            }
        }

        /*------------------------------------------------
        Update the address list information
        ------------------------------------------------*/
        if (position == addressList.size())
        {
            addressList.push_back({nodeID, address});
        }
        else
        {
            addressList[position].id = nodeID;
            addressList[position].logicalAddress = address;
        }
    }

    void Mesh::DHCP()
    {
        /*------------------------------------------------
        Only process the DHCP reqests as flagged in the update() function
        ------------------------------------------------*/
        if (!processDHCP)
        {
            return;
        }

        processDHCP = false;

        /*------------------------------------------------
        Cast to mask complexity and aide readability
        ------------------------------------------------*/
        auto frame = reinterpret_cast<RF24Network::Frame_t*>(&DHCPFrame.data);
        auto header = reinterpret_cast<RF24Network::Header_t*>(&DHCPFrame.data.header);

        /*------------------------------------------------
        Get the unique ID of the requester
        ------------------------------------------------*/
        if (!header->reserved)
        {
            IF_SERIAL_DEBUG(printf("MSH: DHCP invalid id 0 rcvd\n"););
            return;
        }
        IF_SERIAL_DEBUG(printf("%lu MSH: Received address request from 0%o\r\n", radio.millis(), header->srcNode););

        /*------------------------------------------------
        Process address requests from a child node
        ------------------------------------------------*/
        if (header->msgType == static_cast<uint8_t>(RF24Network::MessageType::MESH_REQ_ADDRESS))
        {
            /*------------------------------------------------
            If we aren't the master node, we need to foward the message
            ------------------------------------------------*/
            if (this->nodeID != MESH_MASTER_NODE_ID)
            {
                /*------------------------------------------------
                The first two bytes of the address request message indicates the parent node
                (from the perspective of the requestor) that was used to initiate the process.
                ------------------------------------------------*/
                uint16_t parentNode;
                memcpy(&parentNode, frame->message.begin(), sizeof(parentNode));

                /*------------------------------------------------
                Do we also happen to be the parent node? Assign an extra byte that allows
                the master to know what address slots we have available.
                ------------------------------------------------*/
                if(network.getLogicalAddress() == parentNode)
                {
                    frame->message[3] = network.childBitField();
                }

                /*------------------------------------------------
                Forward directly to the master node
                ------------------------------------------------*/
                header->srcNode = network.getLogicalAddress();
                header->dstNode = MESH_MASTER_NODE_ID;

                RF24Network::Header headerClass;
                headerClass = *header;

                network.write(headerClass, frame->message.begin(), frame->message.size(), MESH_MASTER_NODE_ID);
            }
            else
            {
                assignDHCPAddress();
            }
        }

        if (header->msgType == static_cast<uint8_t>(RF24Network::MessageType::MESH_ADDR_RESPONSE))
        {
            /*------------------------------------------------
            The first byte of the address request indicates the poll node that was
            used to initiate the process. If that is us, keep track of the new assignment
            for our child, then forward the message on directly. (unwrap the
            ------------------------------------------------*/
//            if (network.getLogicalAddress() == DHCPFrame.message[0])
//            {
//            }

//            uint16_t requester = DEFAULT_LOGICAL_ADDRESS;
//            if (requester != this->logicalNodeAddress)
//            {
//                header.payload.dstNode = requester;
//                writeDirect(header.payload.dstNode, MessageType::USER_TX_TO_PHYSICAL_ADDRESS);
//                radio.delayMilliseconds(10);
//                writeDirect(header.payload.dstNode, MessageType::USER_TX_TO_PHYSICAL_ADDRESS);
//                continue;
//            }
        }
    }

    void Mesh::releaseDHCPAddress(const uint16_t address)
    {
        for (uint8_t i = 0; i < addressList.size(); i++)
        {
            if (addressList[i].logicalAddress == address)
            {
                addressList[i].logicalAddress = MESH_EMPTY_ADDRESS;
            }
        }
    }

    void Mesh::confirmDHCPAddress(const uint16_t address)
    {
        if (address == lastAddress)
        {
            setAddress(lastID, lastAddress);
        }
    }

    void Mesh::lookupDHCPAddress(const RF24Network::MessageType lookupType, const uint16_t dstAddress)
    {
        RF24Network::Header header(dstAddress);
        const size_t dataOffset = sizeof(RF24Network::Header_t);

        if (lookupType == RF24Network::MessageType::MESH_ADDR_LOOKUP)
        {
            int16_t returnAddr = getAddress(network.frameBuffer[dataOffset]);
            network.write(header, &returnAddr, sizeof(returnAddr));
        }
        else
        {
            int16_t returnAddr = getNodeID(network.frameBuffer[dataOffset]);
            network.write(header, &returnAddr, sizeof(returnAddr));
        }
    }

    void Mesh::assignDHCPAddress()
    {
        /*------------------------------------------------
        Cast to mask complexity and aide readability
        ------------------------------------------------*/
        auto frameData = reinterpret_cast<RF24Network::Frame_t*>(&DHCPFrame.data);
        auto headerData = reinterpret_cast<RF24Network::Header_t*>(&DHCPFrame.data.header);

        /*------------------------------------------------
        The first two bytes of the address request message indicates the parent node
        (from the perspective of the requestor) that was used to initiate the process.
        ------------------------------------------------*/
        uint16_t parentNode = 0u;
        uint8_t availableAddr = 0u;
        memcpy(&parentNode, frameData->message.begin(), sizeof(parentNode));

        if ((headerData->dstNode == MESH_MASTER_NODE_ID) && (headerData->srcNode == MESH_DEFAULT_ADDRESS))
        {
            /*------------------------------------------------
            We (master) ARE the parent node.
            ------------------------------------------------*/
            availableAddr = network.childBitField();
        }
        else
        {
            /*------------------------------------------------
            The parent node is somewhere else in the network. The data will
            be attached to the message.
            ------------------------------------------------*/
            availableAddr = frameData->message[3];
        }

        /*------------------------------------------------
        Generate a new address and assign it to the requesting node
        ------------------------------------------------*/
        uint16_t newAddress = 0u;

        //Find first bit that is zero
        uint8_t idx = 0;
        for (uint8_t i = 0; i < CHAR_BIT; i++)
        {
            if (!(availableAddr & (1u << i)))
            {
                idx = i + 1;
                break;
            }
        }

        auto level = RF24Network::Node::getLevel(parentNode);

        newAddress = parentNode | ((idx & RF24Network::OCTAL_MASK) << (level * RF24Network::OCTAL_TO_BIN_BITSHIFT));


        headerData->msgType = static_cast<uint8_t>(RF24Network::MessageType::MESH_ADDR_RESPONSE);
        headerData->dstNode = headerData->srcNode;

        radio.delayMilliseconds(10);

        /*------------------------------------------------
        This is a routed request to Master (ie master forwards it to the proper node)
        ------------------------------------------------*/
        uint8_t nodeID = headerData->reserved;
        uint16_t srcNode = headerData->srcNode;

        RF24Network::Header headerClass;
        headerClass = *headerData;

        if (headerData->srcNode != MESH_DEFAULT_ADDRESS)
        {
            network.write(headerClass, &newAddress, sizeof(newAddress));
        }
        else
        {
            network.write(headerClass, &newAddress, sizeof(newAddress), headerData->dstNode);
        }

        /*------------------------------------------------
        Wait for the requesting node to tell us to confirm the address
        ------------------------------------------------*/
        uint32_t timer = radio.millis();
        lastAddress = newAddress;
        lastID = srcNode;

        while (network.update() != RF24Network::MessageType::MESH_ADDR_CONFIRM)
        {
            if ((radio.millis() - timer) > network.routeTimeout)
            {
                oopsies = ErrorType::TIMEOUT;
                IF_SERIAL_DEBUG(printf("%lu: MSH Timeout waiting for address confirmation from ID: 0%o\r\n", radio.millis(), srcNode););
                return;
            }
        }

        /*------------------------------------------------
        Update the internal address information
        ------------------------------------------------*/
        setAddress(nodeID, newAddress);
        IF_SERIAL_DEBUG(printf("%lu: MSH Sent to 0%o phys: 0%o new: 0%o id: %d\r\n",
            radio.millis(), headerData->dstNode, MESH_DEFAULT_ADDRESS, newAddress, headerData->reserved););
    }

} /* !RF24Mesh */
