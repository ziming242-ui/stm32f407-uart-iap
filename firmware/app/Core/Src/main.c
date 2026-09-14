#include "main.h"

#include "FreeRTOS.h"
#include "app_confirm.h"
#include "board.h"
#include "iap_config.h"
#include "task.h"

static void ControlTask(void *argument)
{
    AppConfirmResult result;
    (void)argument;

    board_log("[APP] control task started\r\n");
    vTaskDelay(pdMS_TO_TICKS(2000u));
    result = app_confirm_running_image();
    switch (result) {
    case APP_CONFIRM_OK:
        board_log("[APP] image confirmed\r\n");
        break;
    case APP_CONFIRM_NOT_PENDING:
        board_log("[APP] no pending image to confirm\r\n");
        break;
    case APP_CONFIRM_META_FULL:
        board_log("[APP] confirm failed: metadata full\r\n");
        break;
    default:
        board_log("[APP] confirm failed: flash\r\n");
        break;
    }

    for (;;) {
        board_log("[APP] heartbeat\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000u));
    }
}

static void VersionTask(void *argument)
{
    (void)argument;
    for (;;) {
        board_log("[APP] version=1, FreeRTOS=V9.0.0\r\n");
        vTaskDelay(pdMS_TO_TICKS(5000u));
    }
}

int main(void)
{
    /* The vector table in this image begins at the Run partition. */
    SCB->VTOR = IAP_RUN_BASE;
    __DSB();
    __ISB();

    board_init();
    board_log("[APP] reset handler reached\r\n");

    if (xTaskCreate(ControlTask, "control", 256u, NULL, 2u, NULL) != pdPASS ||
        xTaskCreate(VersionTask, "version", 256u, NULL, 1u, NULL) != pdPASS) {
        Error_Handler();
    }

    vTaskStartScheduler();
    Error_Handler();
    return 0;
}

void vApplicationMallocFailedHook(void)
{
    Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;) {
    }
}
