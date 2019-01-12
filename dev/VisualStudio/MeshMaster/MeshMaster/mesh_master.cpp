/********************************************************************************
*   mesh_master.cpp
*       Sandbox for testing out the mesh master node functionalities. This was
*       originally written for the STM32F767ZI Nucleo dev board.
*
*   2019 | Brandon Braun | brandonbraun653@gmail.com
********************************************************************************/

/* FreeRTOS Includes */
#include "FreeRTOS.h"
#include "task.h"

/* Chimera Includes */
#include <Chimera/gpio.hpp>
#include <Chimera/spi.hpp>
#include <Chimera/threading.hpp>

/* NRF24 Includes */
#include "nrf24l01.hpp"
#include "RF24Network.hpp"
#include "RF24Mesh.hpp"

/* Project Includes */
#include "mesh_master.hpp"

using namespace NRF24L;
using namespace RF24Network;
using namespace RF24Mesh;

using namespace Chimera;
using namespace Chimera::Threading;
using namespace Chimera::GPIO;
using namespace Chimera::SPI;


static NRF24L01 radio;
static Network network(radio);
static Mesh mesh(radio, network);


void meshMasterThread(void *argument)
{
    Setup spiSetup;
    SPIClass_sPtr spi;
    GPIOClass_sPtr chip_enable;

    spi = std::make_shared<SPIClass>(3);

    spiSetup.clockFrequency = 12000000;
    spiSetup.bitOrder = BitOrder::MSB_FIRST;
    spiSetup.clockMode = ClockMode::MODE0;
    spiSetup.mode = Chimera::SPI::Mode::MASTER;

    spiSetup.CS.pin = 7;
    spiSetup.CS.port = Port::PORTF;
    spiSetup.CS.alternate = Thor::Definitions::GPIO::NOALTERNATE;
    spiSetup.CS.mode = Drive::OUTPUT_PUSH_PULL;
    spiSetup.CS.state = State::HIGH;

    spi->setChipSelectControlMode(ChipSelectMode::MANUAL);

    spi->init(spiSetup);
    spi->setPeripheralMode(SubPeripheral::TXRX, SubPeripheralMode::BLOCKING);

    chip_enable = std::make_shared<GPIOClass>();
    chip_enable->init(Port::PORTF, 6);
    chip_enable->setMode(Drive::OUTPUT_PUSH_PULL, false);
    chip_enable->setState(State::HIGH);

    signalThreadSetupComplete();
    TickType_t lastTimeWoken = xTaskGetTickCount();

    uint32_t displayTimer = 0u;

    radio = NRF24L01(spi, chip_enable);

    printf("Initializing master node\r\n");
    mesh.setNodeID(0);
    printf("Master Node ID: %d\r\n", mesh.getNodeID());

    mesh.begin();

    for(;;)
    {
        mesh.update();
        mesh.DHCP();

        if (network.available())
        {
            Header header;
            network.peek(header);

            uint32_t data = 0u;
            switch (header.type)
            {
            case static_cast<uint8_t>(RF24Network::MessageType::M):
                network.read(header, &data, sizeof(data));
                printf("%lu\r\n", data);

            default:
                network.read(header, 0, 0);
                printf("%d\r\n", static_cast<uint8_t>(header.type));
            }
        }

        if((millis() - displayTimer) > 5000)
        {
            displayTimer = millis();
            printf("\r\n**********Assigned Addresses**********\r\n");
            for(int i=0; i <mesh.addressListTop; i++)
            {
                printf("NodeID: %d, Network Address: 0%o", mesh.addressList[i].nodeID, mesh.addressList[i].address);
            }
            printf("**************************************\r\n");
        }

        vTaskDelayUntil(&lastTimeWoken, pdMS_TO_TICKS(5));
    }
}
