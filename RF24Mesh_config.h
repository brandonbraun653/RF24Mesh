#pragma once
#ifndef __RF24MESH_CONFIG_H__
#define __RF24MESH_CONFIG_H__

/* C++ Headers */
#include <cstdint>

/*------------------------------------------------
Network/Mesh Response Types:
The network will determine whether to automatically acknowledge payloads based on their type
RF24Mesh uses pre-defined system types for interacting with RF24Network at the system level
------------------------------------------------*/

/* Network ACK types */
constexpr uint16_t MESH_ADDR_CONFIRM = 129;


/* No Network ACK types */
constexpr uint16_t MESH_ADDR_LOOKUP = 196;
constexpr uint16_t MESH_ADDR_RELEASE = 197;
constexpr uint16_t MESH_ID_LOOKUP = 198;
constexpr uint16_t MESH_BLANK_ID = 65535;

/*------------------------------------------------
Generic User Config
------------------------------------------------*/
#define MESH_MAX_CHILDREN 4                                 /** Set 1 to 4 (Default: 4) Restricts the maximum children per node. **/
//#define MESH_NOMASTER                                     /** This can be set to 0 for all nodes except the master (nodeID 0) to save pgm space **/

/*------------------------------------------------
Advanced User Config
------------------------------------------------*/
#define MESH_LOOKUP_TIMEOUT 3000                            /** How long mesh write will retry address lookups before giving up. This is not used when sending to or from the master node. **/
#define MESH_WRITE_TIMEOUT 5550                             /** UNUSED - How long mesh.write will retry failed payloads. */
#define MESH_DEFAULT_CHANNEL 97                             /** Radio channel to operate on 1-127. This is normally modified by calling mesh.setChannel() */
#define MESH_RENEWAL_TIMEOUT 60000                          /** How long to attempt address renewal */

/*------------------------------------------------
Debug Config
------------------------------------------------*/
//#define MESH_DEBUG_MINIMAL                                /** Uncomment for the Master Node to print out address assignments as they are assigned */
//#define MESH_DEBUG                                        /** Uncomment to enable debug output to serial **/

/*------------------------------------------------
Misc Config
------------------------------------------------*/
#define MESH_MIN_SAVE_TIME 30000                            /** Minimum time required before changing nodeID. Prevents excessive writing to EEPROM */
#define MESH_DEFAULT_ADDRESS NETWORK_DEFAULT_ADDRESS
#define MESH_MAX_ADDRESSES 255                              /** Determines the max size of the array used for storing addresses on the Master Node */
//#define MESH_ADDRESS_HOLD_TIME 30000                      /** How long before a released address becomes available */ 

  
#endif
