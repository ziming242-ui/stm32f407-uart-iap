#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"

void FreeRTOS_SysTick_Handler(void);

void NMI_Handler(void)
{
    for (;;) {
    }
}

void HardFault_Handler(void)
{
    for (;;) {
    }
}

void MemManage_Handler(void)
{
    for (;;) {
    }
}

void BusFault_Handler(void)
{
    for (;;) {
    }
}

void UsageFault_Handler(void)
{
    for (;;) {
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        FreeRTOS_SysTick_Handler();
    }
}
