#pragma once
#ifndef __RF24MESH_H__
#define __RF24MESH_H__

/* C++ Headers */
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>

/* NRF Library Headers */
#include <nrf24l01.hpp>
#include <RF24Network.h>

/* Mesh Headers */
#include "RF24Mesh_config.h"


class RF24Network;


class RF24Mesh
{
public:

    /**
    *   Construct the mesh object
    *
    *   @param[in]  _radio      The underlying radio driver instance
    *   @param[in]  _network    The underlying network instance
    */
    RF24Mesh(NRF24L::NRF24L01& _radio, RF24Network& _network);

    /**
     * Configures the mesh and requests an address
     *
     *  @param[in]  channel     The radio channel (1-127) default:97
     *  @param[in]  data_rate   The data rate (RF24_250KBPS,RF24_1MBPS,RF24_2MBPS) default:RF24_1MBPS
     *  @param[in]  timeout     How long to attempt address renewal in milliseconds default:60000
     *  @return True if success, False if not
     */
    bool begin(uint8_t channel = MESH_DEFAULT_CHANNEL, NRF24L::DataRate data_rate = NRF24L::DataRate::DR_1MBPS, uint32_t timeout = MESH_RENEWAL_TIMEOUT);

    /**
     *  Keeps the network up to date. Must be called at regular intervals.
     *
     *  @return ?
     */
    uint8_t update();

    /**
     *  Write a message onto the network
     *
     *  @note Including the nodeID parameter will result in an automatic address lookup being performed.
     *  @note Message types 1-64 (decimal) will NOT be acknowledged by the network, types 65-127 will be. Use as appropriate to manage traffic:
     *  if expecting a response, no ack is needed.
     *
     *  @param[in]  data        Send any type of data of any length (Max length determined by RF24Network layer)
     *  @param[in]  msg_type    The user-defined (1-127) message header_type to send. Used to distinguish between different types of data being transmitted.
     *  @param[in]  size        The size of the data being sent
     *  @param[in]  nodeID      **Optional**: The nodeID of the recipient if not sending to master
     *  @return True if success, False if failed
     */
    bool write(const void* data, uint8_t msg_type, size_t size, uint8_t nodeID = 0);

    /**
     *  Set a unique nodeID for this node. This value is stored in program memory, so is saved after loss of power.
     *
     *  This should be called before mesh.begin(), or set via serial connection or other methods if configuring a large number of nodes...
     *  @note If using RF24Gateway and/or RF24Ethernet, nodeIDs 0 & 1 are used by the master node.
     *
     *  @param[in]  nodeID      Can be any unique value ranging from 1 to 255.
     *  @return void
     */
    void setNodeID(uint8_t nodeID);

    /**
     *  Only to be used on the master node. Provides automatic configuration for sensor nodes, similar to DHCP.
     *  Call immediately after calling network.update() to ensure address requests are handled appropriately.
     *
     *  @return void
     */
    void DHCP();

   /**
    *   Convert an RF24Network address into a nodeId.
    *
    *   @param[in]  address     If no address is provided, returns the local nodeID, otherwise a lookup request is sent to the master node
    *   @return Returns the unique identifier (1-255) or -1 if not found.
    */
    int16_t getNodeID(uint16_t address = MESH_BLANK_ID);

    /**
     *  Tests connectivity of this node to the mesh
     *
     *  @note If this function fails, the radio will be put into standby mode, and will not receive payloads until the address is renewed.
     *
     *  @return Return true if connected, false if mesh not responding after up to 1 second
     */
    bool checkConnection();

    /**
     *  Reconnect to the mesh and renew the current RF24Network address. Used to re-establish a connection to the mesh 
     *  if physical location etc. has changed, or a routing node goes down.
     *
     *  @note Currently times out after 1 minute if address renewal fails. Network writes should not be attempted if address renewal fails.
     *
     *  @note If all nodes are set to verify connectivity/reconnect at a specified period, leaving the master offline for this length of time should result
     *  in complete network/mesh reconvergence.
     *
     *  @param[in]  timeout     How long to attempt address renewal in milliseconds default:60000
     *  @return Returns the newly assigned RF24Network address
     */
    uint16_t renewAddress(uint32_t timeout = MESH_RENEWAL_TIMEOUT);

    /**
     *  Releases the currently assigned address lease. Useful for nodes that will be sleeping etc.
     *
     *  @note Nodes should ensure that addresses are releases successfully prior to renewal.
     *
     *  @return Returns true if successfully released, false if not
     */
    bool releaseAddress();

    /**
     * Convert a nodeID into an RF24Network address (octal). This results in a lookup request being sent to the master node.
     *
     * @param[in]   nodeID      The unique identifier (1-255) of the node
     * @return RF24Network address of the node or -1 if not found or lookup failed.
     */
    int16_t getAddress(uint8_t nodeID);

    /**
     *  Write to a specific node by RF24Network address.
     *  
     *  @param[in]  node        The node to write to
     *  @param[in]  data        The data to be written
     *  @param[in]  size        The length of the data
     */
    bool writeTo(uint16_t node, const void* data, uint8_t msg_type, size_t size);

    /**
     *  Change the active radio channel after the mesh has been started.
     *
     *  @param[in]  channel     The new channel to be set
     *  @return void
     */
    void setChannel(uint8_t channel);

    /**
     *  Allow child nodes to discover and attach to this node.
     *
     *  @param[in]  allow       True to allow children, False to prevent children from attaching automatically
     */
    void setChild(bool allow);

    /**
     *  Set/change a nodeID/RF24Network Address pair manually on the master node.
     *  TODO: Does this mean the master is keeping track of who is on the network? I guess so...
     *  
     *  @param[in]  nodeID      The nodeID to assign
     *  @param[in]  address     The octal RF24Network address to assign
     *  @return void
     */
    void setAddress(uint8_t nodeID, uint16_t address);

    void saveDHCP();
    void loadDHCP();


    
    uint16_t mesh_address;      /**< The assigned RF24Network (Octal) address of this node */


    struct AddressList
    {
        uint8_t nodeID;         /**< NodeIDs and addresses are stored in the addrList array using this structure */
        uint16_t address;       /**< NodeIDs and addresses are stored in the addrList array using this structure */
    };

    uint8_t addrListTop;        /**< The number of entries in the assigned address list */
    AddressList *addressList;   /**< Pointer used for dynamic memory allocation of address list*/
    

private:
    bool doDHCP; /**< Indicator that an address request is available */
    uint8_t nodeID; /**< TODO */
    uint8_t radio_channel;
    uint16_t lastID;
    uint16_t lastAddress;
    uint32_t lastSaveTime;
    uint32_t lastFileSave;

    RF24Network& network;
    NRF24L::NRF24L01& radio;
    
    /**
     *  Broadcasts to all multicast levels to find available nodes
     *  
     *  @param[in]  header      TODO
     *  @param[in]  level       TODO
     *  @param[in]  address     TODO
     *  @return TODO
     */
    bool findNodes(RF24NetworkHeader& header, uint8_t level, uint16_t *address);

    /**
     *  Actual requesting of the address once a contact node is discovered or supplied
     *
     *  @param[in]  level       TODO
     *  @return TODO
     */
    bool requestAddress(uint8_t level);

    /**
     *  Waits for data to become available
     *  
     *  @param[in]  timeout     TODO
     *  @return TODO
     */
    bool waitForAvailable(uint32_t timeout);
    

};

#endif
