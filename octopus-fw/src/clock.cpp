/**
 * clock.cpp - system clock for the Octopus: 12 MHz crystal -> 168 MHz core, 48 MHz USB
 * -----------------------------------------------------------------------------
 * Replaces the weak SystemClock_Config() of the variant (black_f407ze / nucleo_f446ze, 8 MHz).
 * PLL: 12 MHz / M 6 = 2 MHz, x N 168 = 336 MHz, / P 2 = 168 MHz SYSCLK, / Q 7 = 48 MHz USB.
 * APB1 = 42 MHz (timers 84 MHz), APB2 = 84 MHz (timers 168 MHz).
 * -----------------------------------------------------------------------------
 */
#include <Arduino.h>
#include "bootlog.h"

extern "C" void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {};
    RCC_ClkInitTypeDef clk = {};

    clockReport.entryCr = RCC->CR;
    clockReport.entryCfgr = RCC->CFGR;
    clockReport.entryPll = RCC->PLLCFGR;

    // Started straight from the ROM bootloader (dfu-util ":leave") its PLL still runs SYSCLK with
    // other settings, and HAL_RCC_OscConfig refuses to change a PLL in use. Only then: back to HSI,
    // PLL off. A normal start (BTT bootloader) doesn't go through here.
    const uint32_t PLL_FIELDS = RCC_PLLCFGR_PLLM | RCC_PLLCFGR_PLLN | RCC_PLLCFGR_PLLP | RCC_PLLCFGR_PLLSRC | RCC_PLLCFGR_PLLQ;
    const uint32_t PLL_WANT = (6u << RCC_PLLCFGR_PLLM_Pos) | (168u << RCC_PLLCFGR_PLLN_Pos) | (0u << RCC_PLLCFGR_PLLP_Pos) |
                              RCC_PLLCFGR_PLLSRC_HSE | (7u << RCC_PLLCFGR_PLLQ_Pos);
    if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL && (RCC->PLLCFGR & PLL_FIELDS) != PLL_WANT)
    {
        RCC->CR |= RCC_CR_HSION;
        for (uint32_t i = 0; i < 200000 && !(RCC->CR & RCC_CR_HSIRDY); i++)
            __NOP();
        RCC->CFGR &= ~RCC_CFGR_SW; // SYSCLK = HSI
        for (uint32_t i = 0; i < 200000 && (RCC->CFGR & RCC_CFGR_SWS); i++)
            __NOP();
        RCC->CR &= ~RCC_CR_PLLON;
        for (uint32_t i = 0; i < 200000 && (RCC->CR & RCC_CR_PLLRDY); i++)
            __NOP();
    }

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 6;
    osc.PLL.PLLN = 168;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
#ifdef RCC_PLLCFGR_PLLR
    osc.PLL.PLLR = 2; // F446: PLLR output unused, but 0 is not a valid divider
#endif
    // On failure: no Error_Handler (it hangs before anything can report it); keep the clock
    // as it is and let BOOTLOG.TXT on the card tell (bootlog.h)
    clockReport.oscRc = HAL_RCC_OscConfig(&osc);
    if (clockReport.oscRc != HAL_OK)
        return;

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    clockReport.clkRc = HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
}
