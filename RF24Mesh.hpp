/********************************************************************************
*   RF24Mesh.hpp
*       Provides the mesh networking interface.
*
*   2019 | Brandon Braun | brandonbraun653@gmail.com
********************************************************************************/
#pragma once
#ifndef RF24MESH_HPP
#define RF24MESH_HPP

/* C++ Includes */
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <vector>

/* NRF Includes */
#include "nrf24l01.hpp"
#include "RF24Network.hpp"

/* Mesh Headers */
#include "RF24MeshDefinitions.hpp"

namespace RF24Mesh
{
    struct NodeAddress
    {
        uint8_t id;              /**< User assigned ID number to identify (name) the node. Has nothing to do with position in tree. */
        uint16_t logicalAddress; /**< Logical network address (octal) that identifies where the node is in the tree. */
    };

    class Mesh
    {
    public:
        /**
        *   Construct the mesh object
        *
        *   @param[in]  radio      The underlying radio driver instance
        *   @param[in]  network    The underlying network instance
        */
        Mesh(NRF24L::NRF24L01 &radio, RF24Network::Network &network);

        /**
        *   Configures the mesh and requests an address
        *
        *   @param[in]  channel     The radio channel (1-127) default:97
        *   @param[in]  data_rate   The data rate (RF24_250KBPS,RF24_1MBPS,RF24_2MBPS) default:RF24_1MBPS
        *   @param[in]  timeout     How long to attempt address renewal in milliseconds default:60000
        *   @return True if success, False if not
        */
        bool begin(const uint8_t channel = MESH_DEFAULT_CHANNEL,
                   const NRF24L::DataRate dataRate = NRF24L::DataRate::DR_1MBPS,
                   const NRF24L::PowerAmplitude pwr = NRF24L::PowerAmplitude::MAX,
                   const uint32_t timeout = MESH_RENEWAL_TIMEOUT);

        /**
        *   Keeps the network up to date. Must be called at regular intervals.
        *
        *   @return ?
        */
        RF24Network::MessageType update();

        /**
        *   Write a message onto the network
        *
        *   @note Including the nodeID parameter will result in an automatic address lookup being performed.
        *   @note Message types 1-64 (decimal) will NOT be acknowledged by the network, types 65-127 will be. Use as appropriate to manage traffic:
        *   if expecting a response, no ack is needed.
        *
        *   @param[in]  data        Send any type of data of any length (Max length determined by RF24Network layer)
        *   @param[in]  msg_type    The user-defined (1-127) message header_type to send. Used to distinguish between different types of data being transmitted.
        *   @param[in]  size        The size of the data being sent
        *   @param[in]  nodeID      **Optional**: The nodeID of the recipient if not sending to master
        *   @return True if success, False if failed
        */
        bool write(const void *const data,
                   const RF24Network::MessageType msgType,
                   const size_t size,
                   const uint8_t nodeID = 0);

        /**
        *   Write to a specific node by RF24Network address.
        *
        *   @param[in]  node        The node to write to
        *   @param[in]  data        The data to be written
        *   @param[in]  size        The length of the data
        */
        bool writeTo(const uint16_t node,
                     const void *const data,
                     const RF24Network::MessageType msg_type,
                     const size_t size);

        /**
        *   Set a unique nodeID for this node. This value is stored in program memory, so is saved after loss of power.
        *
        *   This should be called before mesh.begin(), or set via serial connection or other methods if configuring a large number of nodes...
        *   @note If using RF24Gateway and/or RF24Ethernet, nodeIDs 0 & 1 are used by the master node.
        *
        *   @param[in]  nodeID      Can be any unique value ranging from 1 to 255.
        *   @return void
        */
        void setNodeID(const uint8_t nodeID);

        /**
        *   Convert an RF24Network address into a nodeId.
        *
        *   @param[in]  address     If no address is provided, returns the local nodeID, otherwise a lookup request is sent to the master node
        *   @return Returns the unique identifier (1-255) or -1 if not found.
        */
        int16_t getNodeID(const uint16_t address = MESH_BLANK_ID);

        /**
        *   Only to be used on the master node. Provides automatic configuration for sensor nodes, similar to DHCP.
        *   Call immediately after calling network.update() to ensure address requests are handled appropriately.
        *
        *   @return void
        */
        void DHCP();

        /**
        *   Tests connectivity of this node to the mesh
        *
        *   @note If this function fails, the radio will be put into standby mode, and will not receive payloads until the address is renewed.
        *
        *   @return Return true if connected, false if mesh not responding after up to 1 second
        */
        bool checkConnection();

        /**
        *   Reconnect to the mesh and renew the current RF24Network address. Used to re-establish a connection to the mesh
        *   if physical location etc. has changed, or a routing node goes down.
        *
        *   @note Currently times out after 1 minute if address renewal fails. Network writes should not be attempted if address renewal fails.
        *
        *   @note If all nodes are set to verify connectivity/reconnect at a specified period, leaving the master offline for this length of time should result
        *   in complete network/mesh reconvergence.
        *
        *   @param[in]  timeout     How long to attempt address renewal in milliseconds default:60000
        *   @return Returns the newly assigned RF24Network address
        */
        bool renewAddress(uint16_t &newAddress, const uint32_t timeout = MESH_RENEWAL_TIMEOUT);

        /**
        *   Releases the currently assigned address lease. Useful for nodes that will be sleeping etc.
        *
        *   @note Nodes should ensure that addresses are releases successfully prior to renewal.
        *
        *   @return Returns true if successfully released, false if not
        */
        bool releaseAddress();

        /**
        *   Convert a nodeID into an RF24Network address (octal). This results in a lookup request being sent to the master node.
        *
        *   @param[in]   nodeID      The unique identifier (1-255) of the node
        *   @return RF24Network address of the node or -1 if not found or lookup failed.
        */
        int16_t getAddress(const uint8_t nodeID);

        /**
        *   Change the active radio channel after the mesh has been started.
        *
        *   @param[in]  channel     The new channel to be set
        *   @return void
        */
        void setChannel(const uint8_t channel);

        /**
        *   Allow child nodes to discover and attach to this node.
        *
        *   @param[in]  allow       True to allow children, False to prevent children from attaching automatically
        */
        void setChild(const bool allow);

        /**
        *   Set/change a nodeID/RF24Network Address pair manually on the master node.
        *   TODO: Does this mean the master is keeping track of who is on the network? I guess so...
        *
        *   @param[in]  nodeID      The nodeID to assign
        *   @param[in]  address     The octal RF24Network address to assign
        *   @return void
        */
        void setAddress(const uint8_t nodeID, const uint16_t address);

        uint16_t meshNetworkAddress; /**< The assigned RF24Network (Octal) address of this node */

        /**
        *   Tracks the assigned address metainformation for nodes on the network. Since this is a
        *   vector, calls to new are occuring in the background and a memory manager that can handle
        *   this behavior without eventually fragmenting the memory is required.
        *
        *   It is highly suggested to use the FreeRTOS heap_4 or heap_5 memory allocator/manager.
        */
        std::vector<NodeAddress> addressList;

        ErrorType oopsies = ErrorType::NO_ERROR;

    private:
        bool processDHCP;
        uint8_t nodeID;
        uint8_t radioChannel;
        uint16_t lastID;
        uint16_t lastAddress;
        uint32_t lastSaveTime;
        uint32_t lastFileSave;

        RF24Network::Network &network;
        NRF24L::NRF24L01 &radio;

        /**
        *   Buffers for storing information regarding DHCP processing
        */
        RF24Network::Frame DHCPFrame;

        /**
        *   Releases an address from the current address list.
        *
        *   @note Only used on the Master node
        *
        *   @param[in]  address     The logical (octal) network address to be released
        *   @return void
        */
        void releaseDHCPAddress(const uint16_t address);

        /**
        *   Confirms that an address recently assigned to a given node is correct
        *
        *   @note Only used on the Master node
        *
        *   @param[in]  address     The logical (octal) network address of a node which we are confirming
        *   @return void
        */
        void confirmDHCPAddress(const uint16_t address);

        /**
        *   Looks up either an address or an ID and sends the result to the destination node
        *
        *   @note Only used on the Master node
        *
        *   @param[in]  lookupType  What kind of lookup to execute (Address or ID)
        *   @param[in]  dstAddress  The logical (octal) network address where we are sending lookup results
        *   @return void
        */
        void lookupDHCPAddress(const RF24Network::MessageType lookupType, const uint16_t dstAddress);

        /**
        *   Assigns a new address to the requesting node
        *
        *   @note Only used on the Master node
        *
        *   @param[in]
        */
        void assignDHCPAddress();

        /**
        *   Broadcasts to all multicast levels to find available nodes
        *
        *   @param[in]  header      TODO
        *   @param[in]  level       TODO
        *   @param[in]  address     TODO
        *   @return TODO
        */
        bool findNodes(RF24Network::Header &header, uint8_t level, uint16_t *address);

        /**
        *   Actual requesting of the address once a contact node is discovered or supplied
        *
        *   @param[in]  level       TODO
        *   @return TODO
        */
        bool requestAddress(uint8_t level);

        /**
        *   Waits for data to become available
        *
        *   @param[in]  timeout     TODO
        *   @return TODO
        */
        bool waitForAvailable(uint32_t timeout);
    };

} /* !RF24Mesh */

#endif
