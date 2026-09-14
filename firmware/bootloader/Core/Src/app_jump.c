#include "app_jump.h"

#include "board.h"
#include "stm32f4xx.h"

#define SRAM_BASE_ADDRESS 0x20000000u
#define SRAM_END_ADDRESS  0x20020000u

typedef void (*ApplicationEntry)(void);

IapError iap_app_vectors_read_and_validate(uint32_t app_base,
                                           uint32_t image_size,
                                           uint32_t *initial_msp,
                                           uint32_t *reset_handler)
{
    volatile const uint32_t *vectors;
    uint32_t image_end;
    uint32_t handler_code;

    if (initial_msp == NULL || reset_handler == NULL ||
        app_base != IAP_RUN_BASE || image_size < 8u ||
        image_size > IAP_RUN_SIZE) {
        return IAP_ERR_VECTOR;
    }
    image_end = app_base + image_size;
    if (image_end < app_base || image_end > IAP_RUN_BASE + IAP_RUN_SIZE) {
        return IAP_ERR_VECTOR;
    }

    vectors = (volatile const uint32_t *)(uintptr_t)app_base;
    *initial_msp = vectors[0];
    *reset_handler = vectors[1];
    handler_code = *reset_handler & ~1u;

    if (*initial_msp < SRAM_BASE_ADDRESS ||
        *initial_msp > SRAM_END_ADDRESS ||
        (*initial_msp & 7u) != 0u) {
        return IAP_ERR_VECTOR;
    }
    if ((*reset_handler & 1u) == 0u || handler_code < app_base ||
        handler_code >= image_end) {
        return IAP_ERR_VECTOR;
    }
    return IAP_ERR_OK;
}

void iap_jump_to_application(uint32_t app_base,
                             uint32_t initial_msp,
                             uint32_t reset_handler)
{
    ApplicationEntry entry =
        (ApplicationEntry)(uintptr_t)reset_handler;
    uint32_t index;

    board_prepare_for_app();
    __disable_irq();
    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    for (index = 0u; index < 8u; ++index) {
        NVIC->ICER[index] = 0xFFFFFFFFu;
        NVIC->ICPR[index] = 0xFFFFFFFFu;
    }
    SCB->VTOR = app_base;
    __DSB();
    __ISB();
    __set_CONTROL(0u);
    __set_PSP(0u);
    __set_MSP(initial_msp);
    __DSB();
    __ISB();
    __enable_irq();
    entry();

    for (;;) {
    }
}
