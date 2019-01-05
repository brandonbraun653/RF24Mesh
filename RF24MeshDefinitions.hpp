/********************************************************************************
*   RF24MeshDefinitions.hpp
*       Defines and configures various metrics for the mesh network.
*
*   2019 | Brandon Braun | brandonbraun653@gmail.com
********************************************************************************/
#pragma once
#ifndef RF24MESHDEFINITIONS_HPP
#define RF24MESHDEFINITIONS_HPP

/* C++ Includes */
#include <cstdint>

/* NRF24 Networking Driver Includes */
#include "RF24NetworkDefinitions.hpp"

namespace RF24Mesh
{
    /**
    *   Networking message types to identify various messages.
    *   @note These should not conflict with those defined in RF24NetworkDefinitions.hpp
    */
    enum class MessageType : uint8_t
    {
        MESH_ADDR_CONFIRM = 129,

        MESH_ADDR_LOOKUP = 196,
        MESH_ADDR_RELEASE = 197,
        MESH_ID_LOOKUP = 198,
    };

    constexpr uint16_t MESH_BLANK_ID = 65535;

    /*------------------------------------------------
    Generic User Config
    ------------------------------------------------*/
    constexpr uint8_t MESH_MAX_CHILDREN = 4; /** Set 1 to 4 (Default: 4) Restricts the maximum children per node. **/
    //#define MESH_NOMASTER                                     /** This can be set to 0 for all nodes except the master (nodeID 0) to save pgm space **/

    /*------------------------------------------------
    Advanced User Config
    ------------------------------------------------*/
    constexpr uint8_t MESH_DEFAULT_CHANNEL = 97;     /** Radio channel to operate on 1-127. This is normally modified by calling mesh.setChannel() */
    constexpr uint16_t MESH_LOOKUP_TIMEOUT = 3000;   /** How long mesh write will retry address lookups before giving up. This is not used when sending to or from the master node. **/
    constexpr uint16_t MESH_WRITE_TIMEOUT = 5550;    /** UNUSED - How long mesh.write will retry failed payloads. */
    constexpr uint16_t MESH_RENEWAL_TIMEOUT = 60000; /** How long to attempt address renewal */

    /*------------------------------------------------
    Debug Config
    ------------------------------------------------*/
    //#define MESH_DEBUG_MINIMAL                                /** Uncomment for the Master Node to print out address assignments as they are assigned */
    //#define MESH_DEBUG                                        /** Uncomment to enable debug output to serial **/

    /*------------------------------------------------
    Misc Config
    ------------------------------------------------*/
    constexpr uint8_t MESH_MAX_ADDRESSES = 255;    /** Determines the max size of the array used for storing addresses on the Master Node */
    constexpr uint16_t MESH_MIN_SAVE_TIME = 30000; /** Minimum time required before changing nodeID. Prevents excessive writing to EEPROM */
    constexpr uint16_t MESH_DEFAULT_ADDRESS = RF24Network::DEFAULT_ADDRESS;
    constexpr uint16_t MESH_ADDRESS_HOLD_TIME = 30000; /** How long before a released address becomes available */
}

#endif
