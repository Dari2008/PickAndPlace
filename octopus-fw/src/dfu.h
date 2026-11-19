/**
 * dfu.h - jump into the STM32's built-in USB DFU bootloader (no BOOT0 jumper needed)
 * -----------------------------------------------------------------------------
 * The DFU command calls dfu_enter(): USB soft-disconnect, every peripheral reset
 * (RCC reset registers), clocks back to HSI, then a jump into the system memory
 * bootloader at 0x1FFF0000. The board shows up as 0483:df11 "STM32 BOOTLOADER"; the Pi
 * flashes it with dfu-util (pap-core --flash). Nothing of this runs at start-up.
 * The BTT bootloader in the first 32 KiB is never touched.
 * A microSD card in the slot can keep the ROM bootloader off USB (its USART3 = PC10/PC11
 * = the card's SDIO D2/D3): take it out for flashing.
 * -----------------------------------------------------------------------------
 */
#pragma once

void dfu_enter(); // does not return
