#ifdef DEBUG
#include "SysprogsProfiler.h"
#endif

#include <Chimera/chimera.hpp>
#include <Chimera/config.hpp>
#include <Chimera/threading.hpp>
#include <Chimera/utilities.hpp>
#include <Chimera/gpio.hpp>

#include "mesh_node.hpp"

using namespace Chimera::Threading;
using namespace Chimera::GPIO;

void ledThread(void *argument);

int main(void)
{
    ChimeraInit();

    #ifdef DEBUG
    //InitializeSamplingProfiler();
    InitializeInstrumentingProfiler();
    #endif

    addThread(ledThread, "led", 150, NULL, 2, NULL);
    addThread(meshNodeThread, "mesh", 2000, NULL, 3, NULL);

    startScheduler();

    /* Should never reach here as scheduler should be running */
    for(;;)
    {

    }
}

void ledThread(void* argument)
{
    GPIOClass led;
    led.init(Port::PORTA, 5);
    led.setMode(Drive::OUTPUT_PUSH_PULL, false);
    led.setState(State::LOW);

    signalThreadSetupComplete();

    TickType_t lastTimeWoken = xTaskGetTickCount();
    for (;;)
    {
        led.setState(State::LOW);
        vTaskDelayUntil(&lastTimeWoken, pdMS_TO_TICKS(500));
        led.setState(State::HIGH);
        vTaskDelayUntil(&lastTimeWoken, pdMS_TO_TICKS(500));
    }
}


