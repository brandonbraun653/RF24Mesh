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

    bool Mesh::begin(const uint8_t channel,
                     const NRF24L::DataRate dataRate,
                     const NRF24L::PowerAmplitude pwr,
                     uint32_t timeout)
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
            //TODO: Get rid of the malloc call
            addressList = (AddressList *)malloc(2 * sizeof(AddressList));
            addressListTop = 0;

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
        auto type = network.update();
        if (meshNetworkAddress == MESH_DEFAULT_ADDRESS)
        {
            return type;
        }

        /*------------------------------------------------
        Do we need to do some DHCP processing?
        ------------------------------------------------*/
        if (type == RF24Network::MessageType::NETWORK_REQ_ADDRESS)
        {
            processDHCPRequest = true;
        }

        /*------------------------------------------------
        Assuming we are the master device, get some work done
        ------------------------------------------------*/
        if (getNodeID() == MESH_MASTER_NODE_ID)
        {
            /*------------------------------------------------
            Create a new header object and give it the frame buffer payload
            ------------------------------------------------*/
            RF24Network::Header header;
            memcpy(&header.payload, network.frameBuffer, sizeof(RF24Network::Header));

            /*------------------------------------------------
            Process lookup requests?
            ------------------------------------------------*/
            if ((type == RF24Network::MessageType::MESH_ADDR_LOOKUP) || (type == RF24Network::MessageType::MESH_ID_LOOKUP))
            {
                /*------------------------------------------------
                //TODO: Add comment on why this is necessary
                ------------------------------------------------*/
                header.payload.dstNode = header.payload.srcNode;

                if (type == RF24Network::MessageType::MESH_ADDR_LOOKUP)
                {
                    int16_t returnAddr = getAddress(network.frameBuffer[sizeof(RF24Network::Header)]);
                    network.write(header, &returnAddr, sizeof(returnAddr));
                }
                else
                {
                    int16_t returnAddr = getNodeID(network.frameBuffer[sizeof(RF24Network::Header)]);
                    network.write(header, &returnAddr, sizeof(returnAddr));
                }
            }
            /*------------------------------------------------
            Maybe we need to release an address?
            ------------------------------------------------*/
            else if (type == RF24Network::MessageType::MESH_ADDR_RELEASE)
            {
                for (uint8_t i = 0; i < addressListTop; i++)
                {
                    if (addressList[i].address == header.payload.srcNode)
                    {
                        addressList[i].address = 0;
                    }
                }
            }
            /*------------------------------------------------
            Maybe we need to confirm an address?
            ------------------------------------------------*/
            else if (type == RF24Network::MessageType::MESH_ADDR_CONFIRM)
            {
                if (header.payload.srcNode == lastAddress)
                {
                    setAddress(lastID, lastAddress);
                }
            }
        }

        return type;
    }

    bool Mesh::write(const void *const data,
                     const RF24Network::MessageType msgType,
                     const size_t size,
                     const uint8_t nodeID)
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

    bool Mesh::writeTo(const uint16_t node,
                       const void *const data,
                       const RF24Network::MessageType msgType,
                       const size_t size)
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

            for (uint8_t i = 0; i < addressListTop; i++)
            {
                if (addressList[i].nodeID == nodeID)
                {
                    address = addressList[i].address;
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
        memcpy(&address, network.frameBuffer + sizeof(RF24Network::Header), sizeof(address));
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
            for (uint8_t i = 0; i < addressListTop; i++)
            {
                if (addressList[i].address == address)
                {
                    return addressList[i].nodeID;
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
        IF_SERIAL_DEBUG(printf("%lu: MSH Poll\r\n", radio.millis()); );

        /*------------------------------------------------
        Reach out to another radio, any radio at all
        ------------------------------------------------*/
        RF24Network::Header header(RF24Network::MULTICAST_NODE, RF24Network::MessageType::NETWORK_POLL);
        network.multicast(header, nullptr, 0, level);

        /*------------------------------------------------
        Initialize some variables...
        ------------------------------------------------*/
        uint8_t pollCount = 0;
        uint32_t timer = radio.millis();
        uint16_t contactNode[MESH_MAXPOLLS];

        for (uint8_t i = 0; i < MESH_MAXPOLLS; i++)
        {
            contactNode[i] = RF24Network::EMPTY_LOGICAL_ADDRESS;
        }

        /*------------------------------------------------
        Wait for a radio (contact node) to respond
        ------------------------------------------------*/
        while (true)
        {
            /*------------------------------------------------
            Has the networking layer received a poll response from some node?
            If so, store that node's address in a buffer.
            ------------------------------------------------*/
            if (network.update() == RF24Network::MessageType::NETWORK_POLL)
            {
                RF24Network::Header::Payload_t payload;
                memcpy(&payload, network.frameBuffer, sizeof(payload));

                contactNode[pollCount] = payload.srcNode;
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
        An adjacent radio was found! Try to get it to forward the network address request.
        ------------------------------------------------*/
        IF_SERIAL_DEBUG(printf("%lu MSH: Got poll from level [%d], count [%d]\n", radio.millis(), level, pollCount););

        RF24Network::MessageType type = RF24Network::MessageType::MIN_USER_DEFINED_HEADER_TYPE;
        for (uint8_t i = 0; i < pollCount; i++)
        {
            /*------------------------------------------------
            Prevent us from trying to contact an invalid node
            ------------------------------------------------*/
            if (!network.isValidNetworkAddress(contactNode[i]))
            {
                continue;
            }

            /*------------------------------------------------
            Request an address via the contact node
            ------------------------------------------------*/
            header.payload.msgType = static_cast<uint8_t>(RF24Network::MessageType::NETWORK_REQ_ADDRESS);
            header.payload.reserved = getNodeID();
            header.payload.dstNode = contactNode[i];
            header.payload.srcNode = network.getLogicalAddress();

            /*------------------------------------------------
            Do a direct write (no ack) to the contact node. Include the nodeId and address.
            ------------------------------------------------*/
            network.write(header, 0, 0, contactNode[i]);
            IF_SERIAL_DEBUG(printf("%lu MSH: Request address from node [0%o]\n", radio.millis(), contactNode[i]););

            /*------------------------------------------------
            Wait for a response from our contact node
            ------------------------------------------------*/
            timer = radio.millis();
            while ((radio.millis() - timer) < 225)
            {
                type = network.update();

                /*------------------------------------------------
                Did we get any response at all? Break out of the while and for loop.
                ------------------------------------------------*/
                if (type == RF24Network::MessageType::NETWORK_ADDR_RESPONSE)
                {
                    i = pollCount;
                    break;
                }
            }
            radio.delayMilliseconds(5);
        }

        /*------------------------------------------------
        No response from one of those nodes we found before?
        ------------------------------------------------*/
        if (type != RF24Network::MessageType::NETWORK_ADDR_RESPONSE)
        {
            oopsies = ErrorType::NO_RESPONSE;
            return false;
        }

        /*------------------------------------------------
        Pull the new address out from the network frame buffer
        ------------------------------------------------*/
        uint16_t newAddress = 0;
        memcpy(&newAddress, network.frameBuffer + sizeof(RF24Network::Header), sizeof(newAddress));

        /*------------------------------------------------
        Check that the address received was valid
        ------------------------------------------------*/
        if (!newAddress || (network.frameBuffer[7] != getNodeID()))
        {
            IF_SERIAL_DEBUG(printf("%lu: Response discarded, wrong node 0%o from node 0%o sending node 0%o id %d\r\n",
                radio.millis(), newAddress, header.payload.srcNode, MESH_DEFAULT_ADDRESS, network.frameBuffer[7]););

            oopsies = ErrorType::FAILED_ADDR_REQUEST;
            return false;
        }

        /*------------------------------------------------
        Internally assign the new address to the mesh
        ------------------------------------------------*/
        IF_SERIAL_DEBUG(printf("%lu: Set current address 0%o to new address 0%o\r\n", radio.millis(), meshNetworkAddress, newAddress););
        meshNetworkAddress = newAddress;

        /*------------------------------------------------
        Verify to the master node that we received the address
        ------------------------------------------------*/
        radio.stopListening();
        radio.delayMilliseconds(10);

        network.setAddress(meshNetworkAddress);
        header.payload.dstNode = MESH_MASTER_NODE_ID;
        header.payload.msgType = static_cast<uint8_t>(RF24Network::MessageType::MESH_ADDR_CONFIRM);

        uint8_t registerAddrCount = 0u;
        while (!network.write(header, nullptr, 0))
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

        return true;
    }

    void Mesh::setNodeID(const uint8_t nodeID)
    {
        this->nodeID = nodeID;
    }

    void Mesh::setAddress(uint8_t nodeID, uint16_t address)
    {
        uint8_t position = addressListTop;

        /*------------------------------------------------
        Search through the address list for the position of the nodeID
        ------------------------------------------------*/
        for (uint8_t i = 0; i < addressListTop; i++)
        {
            if (addressList[i].nodeID == nodeID)
            {
                position = i;
                break;
            }
        }

        /*------------------------------------------------
        Update the address list information
        ------------------------------------------------*/
        addressList[position].nodeID = nodeID;
        addressList[position].address = address;

        /*------------------------------------------------
        If we are at the top of the list, grab some more memory, nom nom.
        ------------------------------------------------*/
        if (position == addressListTop)
        {
            addressListTop++;

            //TODO: Get rid of realloc
            addressList = (AddressList *)realloc(addressList, (addressListTop + 1) * sizeof(AddressList));
        }
    }

    void Mesh::DHCP()
    {
        /*------------------------------------------------
        Only process the DHCP reqests as flagged in the update() function
        ------------------------------------------------*/
        if (!processDHCPRequest)
        {
            return;
        }

        processDHCPRequest = false;

        /*------------------------------------------------
        Get the unique ID of the requester
        ------------------------------------------------*/
        RF24Network::Header header;
        memcpy(&header.payload, network.frameBuffer, sizeof(RF24Network::Header));

        uint8_t fromID = header.payload.reserved;
        if (!fromID)
        {
            IF_SERIAL_DEBUG(printf("MSH: DHCP invalid id 0 rcvd\n"););
            return;
        }

        uint16_t fromNode = 0;
        uint8_t shiftVal = 0;
        bool extraChild = 0;

        /*------------------------------------------------
        Process if the message came from a child's descendant
        ------------------------------------------------*/
        if (header.payload.srcNode != MESH_DEFAULT_ADDRESS)
        {
            fromNode = header.payload.srcNode;
            uint16_t temp = fromNode;
            uint8_t numOctalAddrDigits = 0;

            /*------------------------------------------------
            Figure out how many digits there are in the octal address
            ------------------------------------------------*/
            while (temp)
            {
                /*------------------------------------------------
                Octal addresses convert nicely to binary in threes.
                Address 03 = B011
                Address 033 = B011011
                ------------------------------------------------*/
                temp >>= RF24Network::OCTAL_TO_BIN_BITSHIFT;
                numOctalAddrDigits++;
            }

            /*------------------------------------------------
            Now we know how many bits to shift when adding a child node 1-5 (B001 to B101) to any address
            ------------------------------------------------*/
            shiftVal = numOctalAddrDigits * RF24Network::OCTAL_TO_BIN_BITSHIFT;
        }
        /*------------------------------------------------
        Otherwise the request is coming from level 1 and we need to add a child to the master
        ------------------------------------------------*/
        else
        {
            extraChild = true;
        }

        IF_SERIAL_DEBUG(printf("%lu MSH: Rcv addr req from_id %d\r\n", radio.millis(), fromNode););

        /*------------------------------------------------
        Generate a new address and assign it to the requesting node
        ------------------------------------------------*/
        uint16_t newAddress;
        for (int i = MESH_MAX_CHILDREN + extraChild; i > 0; i--)
        {
            /*------------------------------------------------
            Calculate a new address
            ------------------------------------------------*/
            newAddress = fromNode | (i << shiftVal);
            if (!newAddress)
            {
                continue;
            }

            /*------------------------------------------------
            Make sure the new address hasn't already been assigned
            ------------------------------------------------*/
            bool found = false;
            for (uint8_t i = 0; i < this->addressListTop; i++)
            {
                IF_SERIAL_DEBUG(printf("ID: %d ADDR: 0%o\r\n", addressList[i].nodeID, addressList[i].address););

                if (((addressList[i].address == newAddress) && (addressList[i].nodeID != fromID)) || newAddress == MESH_DEFAULT_ADDRESS)
                {
                    found = true;
                    break;
                }
            }

            /*------------------------------------------------
            We have an unassigned address. Let the requester know.
            ------------------------------------------------*/
            if (!found)
            {
                header.payload.msgType = static_cast<uint8_t>(RF24Network::MessageType::NETWORK_ADDR_RESPONSE);
                header.payload.dstNode = header.payload.srcNode;

                radio.delayMilliseconds(10);

                /*------------------------------------------------
                This is a routed request to Master (ie master forwards it to the proper node)
                ------------------------------------------------*/
                if (header.payload.srcNode != MESH_DEFAULT_ADDRESS)
                {
                    //Is NOT node 01 to 05
                    radio.delayMilliseconds(2);
                    network.write(header, &newAddress, sizeof(newAddress));
                }
                /*------------------------------------------------
                Otherwise send it to the next node
                ------------------------------------------------*/
                else
                {
                    radio.delayMilliseconds(2);
                    network.write(header, &newAddress, sizeof(newAddress), header.payload.dstNode);
                }

                /*------------------------------------------------
                Wait for the requesting node to tell us to confirm the address
                ------------------------------------------------*/
                uint32_t timer = radio.millis();
                lastAddress = newAddress;
                lastID = fromID;
                while (network.update() != RF24Network::MessageType::MESH_ADDR_CONFIRM)
                {
                    if ((radio.millis() - timer) > network.routeTimeout)
                    {
                        oopsies = ErrorType::TIMEOUT;
                        return;
                    }
                }

                setAddress(fromID, newAddress);
                IF_SERIAL_DEBUG(printf("Sent to 0%o phys: 0%o new: 0%o id: %d\r\n",
                    header.payload.dstNode, MESH_DEFAULT_ADDRESS, newAddress, header.payload.reserved););
                break;
            }
            else
            {
                IF_SERIAL_DEBUG(printf("not allocated\r\n"););
            }
        }
    }

} /* !RF24Mesh */
