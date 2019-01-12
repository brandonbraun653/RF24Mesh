/********************************************************************************
*   mesh_node.cpp
*       Sandbox for testing out the mesh node functionalities. This was
*       originally written for the STM32F446RE Nucleo dev board.
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
#include "mesh_node.hpp"

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

struct payload_t
{
    uint32_t ms;
    uint32_t counter;
};

void
meshNodeThread(void *argument)
{
    Setup spiSetup;
    SPIClass_sPtr spi;
    GPIOClass_sPtr chip_enable;

    spi = std::make_shared<SPIClass>(3);

    spiSetup.clockFrequency = 12000000;
    spiSetup.bitOrder = BitOrder::MSB_FIRST;
    spiSetup.clockMode = ClockMode::MODE0;
    spiSetup.mode = Chimera::SPI::Mode::MASTER;

    spiSetup.CS.pin = 15;
    spiSetup.CS.port = Port::PORTA;
    spiSetup.CS.alternate = Thor::Definitions::GPIO::NOALTERNATE;
    spiSetup.CS.mode = Drive::OUTPUT_PUSH_PULL;
    spiSetup.CS.state = State::HIGH;

    spi->setChipSelectControlMode(ChipSelectMode::MANUAL);

    spi->init(spiSetup);
    spi->setPeripheralMode(SubPeripheral::TXRX, SubPeripheralMode::BLOCKING);

    chip_enable = std::make_shared<GPIOClass>();
    chip_enable->init(Port::PORTC, 1);
    chip_enable->setMode(Drive::OUTPUT_PUSH_PULL, false);
    chip_enable->setState(State::HIGH);

    signalThreadSetupComplete();
    TickType_t lastTimeWoken = xTaskGetTickCount();


    radio = NRF24L01(spi, chip_enable);
    uint32_t displayTimer = 0u;

    mesh.setNodeID(1);
    printf("Connecting to the mesh...\r\n");
    mesh.begin();

    for (;;)
    {
        mesh.update();

        if ((millis() - displayTimer) >= 1000)
        {
            displayTimer = millis();

            if (!mesh.write(&displayTimer, RF24Network::MessageType::M, sizeof(displayTimer)))
            {
                if (!mesh.checkConnection())
                {
                    printf("Renewing address...\r\r");

                    uint16_t addr;
                    if (mesh.renewAddress(addr, 5000))
                    {
                        printf("Got address %d\r\n", addr);
                    }
                    else
                    {
                        printf("Address renewal failed\r\n");
                    }
                }
                else
                {
                    printf("Send fail, connection ok\r\n");
                }
            }
            else
            {
                printf("Send ok: %lu\r\n", displayTimer);
            }
        }

        while (network.available())
        {
            Header header;
            payload_t payload;

            network.read(header, &payload, sizeof(payload));
            printf("Received packet #%lu at %lu mS\r\n", payload.counter, payload.ms);
        }

        vTaskDelayUntil(&lastTimeWoken, pdMS_TO_TICKS(25));
    }
}
