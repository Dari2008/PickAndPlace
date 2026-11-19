#include "dfu.h"

#include <Arduino.h>

static const uint32_t SYSMEM = 0x1FFF0000; // F405/407/446: system memory bootloader

// Busy-wait without SysTick (interrupts are off): bounded, ~1 ms at most per step
static void spinUntil(bool (*cond)())
{
    for (uint32_t i = 0; i < 200000 && !cond(); i++)
        __NOP();
}

void dfu_enter()
{
    // Host sees the CDC port go away now, not when the ROM takes over the USB core
    USB_OTG_DeviceTypeDef *dev = (USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_FS + USB_OTG_DEVICE_BASE);
    dev->DCTL |= USB_OTG_DCTL_SDIS;
    delay(20);

    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    // Every peripheral back to its reset state (timers, UARTs, DMA, USB, GPIO), like after a reset
    RCC->AHB1RSTR = 0xFFFFFFFF;
    RCC->AHB2RSTR = 0xFFFFFFFF;
    RCC->APB1RSTR = 0xFFFFFFFF;
    RCC->APB2RSTR = 0xFFFFFFFF;
    __DSB();
    RCC->AHB1RSTR = 0;
    RCC->AHB2RSTR = 0;
    RCC->APB1RSTR = 0;
    RCC->APB2RSTR = 0;

    // Clocks: HSI as SYSCLK, PLL and HSE off, no prescalers (the ROM sets up its own)
    RCC->CR |= RCC_CR_HSION;
    spinUntil([] { return (RCC->CR & RCC_CR_HSIRDY) != 0; });
    RCC->CFGR = 0;
    spinUntil([] { return (RCC->CFGR & RCC_CFGR_SWS) == 0; });
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSEON | RCC_CR_CSSON);
    spinUntil([] { return (RCC->CR & (RCC_CR_PLLRDY | RCC_CR_HSERDY)) == 0; });
    RCC->CIR = 0;

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
    SCB->VTOR = 0;
    __DSB();
    __ISB();

    const uint32_t sp = *(volatile uint32_t *)SYSMEM;
    void (*boot)() = (void (*)()) * (volatile uint32_t *)(SYSMEM + 4);
    __set_MSP(sp);
    __enable_irq();
    boot();
    while (true)
    {
    }
}
