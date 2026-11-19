#include "bootlog.h"
#include "app.h"

#include <Arduino.h>
#include <STM32SD.h>
#include <stdio.h>

ClockReport clockReport = {};

namespace
{
    const char *const PATH = "BOOTLOG.TXT";
    const uint32_t FAULT_MAGIC = 0xFA017000;
    bool ok = false;

    void backupWritable()
    {
        __HAL_RCC_PWR_CLK_ENABLE();
        PWR->CR |= PWR_CR_DBP;
    }

    void write(const char *line)
    {
        if (!ok)
            return;
        File f = SD.open(PATH, FILE_WRITE);
        if (!f)
        {
            ok = false;
            return;
        }
        f.seek(f.size());
        f.print(line);
        f.print("\r\n");
        f.close(); // after every line: nothing lost when it hangs later
    }
}

// HardFault: remember where for the next start (the reset button keeps the backup registers)
extern "C" void bootlogHardFault(uint32_t *frame)
{
    backupWritable();
    RTC->BKP17R = frame[6]; // stacked PC
    RTC->BKP16R = frame[5]; // stacked LR
    RTC->BKP18R = FAULT_MAGIC;
    while (true)
    {
    }
}

extern "C" __attribute__((naked)) void HardFault_Handler()
{
    __asm volatile("tst lr, #4      \n"
                   "ite eq          \n"
                   "mrseq r0, msp   \n"
                   "mrsne r0, psp   \n"
                   "b bootlogHardFault\n");
}

namespace bootlog
{
    void begin()
    {
        const uint32_t csr = RCC->CSR, fault = RTC->BKP18R, pc = RTC->BKP17R, lr = RTC->BKP16R;
        backupWritable();
        RTC->BKP18R = 0;
        RCC->CSR |= RCC_CSR_RMVF; // clear the reset flags for the next start

        const uint32_t detect = pinNametoDigitalPin(PC_14);
        pinMode(detect, INPUT_PULLUP);
        if (digitalRead(detect) != LOW || !SD.begin(detect))
            return;
        ok = true;
        File f = SD.open(PATH);
        if (f)
        {
            const bool big = f.size() > 32768;
            f.close();
            if (big)
                SD.remove(PATH);
        }

        char b[220];
        write("");
        snprintf(b, sizeof(b), "=== start %s %s (%s), built %s %s", FIRMWARE_NAME, FIRMWARE_VERSION,
#if defined(STM32F446xx)
                 "F446",
#else
                 "F407",
#endif
                 __DATE__, __TIME__);
        write(b);
        snprintf(b, sizeof(b), "reset cause:%s%s%s%s%s%s (CSR %08lx)", (csr & RCC_CSR_PORRSTF) ? " power-on" : "",
                 (csr & RCC_CSR_PINRSTF) ? " pin/button" : "", (csr & RCC_CSR_SFTRSTF) ? " software" : "",
                 (csr & RCC_CSR_IWDGRSTF) ? " watchdog" : "", (csr & RCC_CSR_BORRSTF) ? " brown-out" : "",
                 (csr & RCC_CSR_LPWRRSTF) ? " low-power" : "", (unsigned long)csr);
        write(b);
        if (fault == FAULT_MAGIC)
        {
            snprintf(b, sizeof(b), "PREVIOUS START CRASHED: HardFault at pc %08lx lr %08lx", (unsigned long)pc,
                     (unsigned long)lr);
            write(b);
        }
        const ClockReport &c = clockReport;
        snprintf(b, sizeof(b), "clock left by the bootloader: CR %08lx CFGR %08lx PLLCFGR %08lx", (unsigned long)c.entryCr,
                 (unsigned long)c.entryCfgr, (unsigned long)c.entryPll);
        write(b);
        const uint32_t src = RCC->CFGR & RCC_CFGR_SWS;
        snprintf(b, sizeof(b),
                 "clock now: OscConfig %s, ClockConfig %s, SYSCLK %lu Hz from %s, HSE %s, PLL %s, PLLCFGR %08lx, VTOR %08lx",
                 c.oscRc ? "FAILED" : "ok", c.clkRc ? "FAILED" : "ok", (unsigned long)HAL_RCC_GetSysClockFreq(),
                 src == RCC_CFGR_SWS_PLL ? "PLL" : src == RCC_CFGR_SWS_HSE ? "HSE" : "HSI",
                 (RCC->CR & RCC_CR_HSERDY) ? "ready" : "NOT READY", (RCC->CR & RCC_CR_PLLRDY) ? "locked" : "off",
                 (unsigned long)RCC->PLLCFGR, (unsigned long)SCB->VTOR);
        write(b);
    }

    void step(const char *what)
    {
        char b[120];
        snprintf(b, sizeof(b), "%6lu ms  %s", (unsigned long)millis(), what);
        write(b);
    }

    void poll()
    {
        static const uint32_t AT[] = {1000, 3000, 10000, 30000};
        static uint8_t next = 0;
        if (!ok || next >= sizeof(AT) / sizeof(AT[0]) || millis() < AT[next])
            return;
        next++;
        // OTG FS core registers: GOTGCTL bit 19 = B-session valid (VBUS seen), DCFG address, DSTS bit 0 = suspended
        USB_OTG_DeviceTypeDef *dev = (USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_FS + USB_OTG_DEVICE_BASE);
        const uint32_t gotg = USB_OTG_FS->GOTGCTL, gint = USB_OTG_FS->GINTSTS, gccfg = USB_OTG_FS->GCCFG;
        const uint32_t dsts = dev->DSTS, dcfg = dev->DCFG, dctl = dev->DCTL;
        char b[220];
        snprintf(b, sizeof(b),
                 "%6lu ms  USB: vbus-session %s, address %lu, suspended %s, soft-disconnect %s, port open %s "
                 "(GOTGCTL %08lx GINTSTS %08lx GCCFG %08lx DSTS %08lx DCTL %08lx)",
                 (unsigned long)millis(), (gotg & (1UL << 19)) ? "yes" : "no", (unsigned long)((dcfg >> 4) & 0x7F),
                 (dsts & 1) ? "yes" : "no", (dctl & 2) ? "yes" : "no", SerialUSB ? "yes" : "no", (unsigned long)gotg,
                 (unsigned long)gint, (unsigned long)gccfg, (unsigned long)dsts, (unsigned long)dctl);
        write(b);
    }
}
