#include "tmc.h"
#include "settings.h"
#include "tmc_frame.h"

#include <Arduino.h>
#include <math.h>

TmcBus tmcs;

static const uint32_t BAUD = 57600;

namespace
{
    struct UPin
    {
        GPIO_TypeDef *port;
        uint32_t pin;     // bit number
        uint32_t mask;
        void init(PinName pn)
        {
            pinMode(pinNametoDigitalPin(pn), INPUT_PULLUP);
            port = get_GPIO_Port(STM_PORT(pn));
            pin = STM_PIN(pn);
            mask = 1u << pin;
        }
        // MODER: 00 input, 01 output
        inline void output() { port->MODER = (port->MODER & ~(3u << (pin * 2))) | (1u << (pin * 2)); }
        inline void input() { port->MODER &= ~(3u << (pin * 2)); }
        inline void hi() { port->BSRR = mask; }
        inline void lo() { port->BSRR = mask << 16; }
        inline bool read() const { return (port->IDR & mask) != 0; }
    };
    UPin upins[N_AXIS];
    uint32_t bitCycles;
    inline uint32_t now() { return DWT->CYCCNT; }
    inline void waitUntil(uint32_t t)
    {
        while ((int32_t)(now() - t) < 0)
        {
        }
    }

    tmc::Config configFor(uint8_t axis)
    {
        tmc::Config c;
        c.currentMa = cfg::axis(cfg::TMC_CUR, axis);
        c.holdPercent = cfg::axis(cfg::TMC_HOLD, axis);
        c.microsteps = (uint16_t)cfg::axis(cfg::TMC_MS, axis == X2_AXIS ? (uint8_t)X_AXIS : axis); // X2 takes every X step
        c.stealth = cfg::axis(cfg::TMC_STEALTH, axis) > 0.5f;
        c.sgthrs = (uint8_t)cfg::axis(cfg::TMC_SG, axis);
        return c;
    }
}

void TmcBus::begin()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    bitCycles = SystemCoreClock / BAUD;
    for (uint8_t a = 0; a < N_AXIS; a++)
    {
        upins[a].init(DRIVER_PINS[a].uart);
        upins[a].hi();
    }
    delay(5);
    for (uint8_t a = 0; a < N_AXIS; a++)
        configure(a);
}

void TmcBus::sendBytes(uint8_t axis, const uint8_t *data, uint8_t len)
{
    UPin &p = upins[axis];
    p.hi();
    p.output();
    for (uint8_t i = 0; i < len; i++)
    {
        uint16_t frame = (uint16_t)(data[i] << 1) | 0x200; // start 0, 8 data LSB first, stop 1
        __disable_irq();
        uint32_t t = now();
        for (uint8_t b = 0; b < 10; b++)
        {
            if (frame & 1)
                p.hi();
            else
                p.lo();
            frame >>= 1;
            t += bitCycles;
            waitUntil(t);
        }
        __enable_irq();
    }
    p.hi();
    p.input(); // released: pull-up keeps it high, the driver may answer now
}

bool TmcBus::receiveBytes(uint8_t axis, uint8_t *data, uint8_t len)
{
    UPin &p = upins[axis];
    for (uint8_t i = 0; i < len; i++)
    {
        // Wait for the start bit (falling edge); the TMC answers after SENDDELAY (8 bit times) + a little
        const uint32_t timeout = now() + bitCycles * 40;
        while (p.read())
            if ((int32_t)(now() - timeout) > 0)
                return false;
        __disable_irq();
        uint32_t t = now() + bitCycles + bitCycles / 2; // middle of data bit 0
        uint8_t v = 0;
        for (uint8_t b = 0; b < 8; b++)
        {
            waitUntil(t);
            if (p.read())
                v |= (uint8_t)(1u << b);
            t += bitCycles;
        }
        waitUntil(t); // stop bit
        __enable_irq();
        data[i] = v;
    }
    return true;
}

bool TmcBus::writeReg(uint8_t axis, uint8_t reg, uint32_t value)
{
    if (axis >= N_AXIS)
        return false;
    uint8_t f[8];
    tmc::writeFrame(f, 0, reg, value);
    sendBytes(axis, f, 8);
    delayMicroseconds(bitCycles * 12 / (SystemCoreClock / 1000000)); // bus turnaround
    return true;
}

bool TmcBus::readReg(uint8_t axis, uint8_t reg, uint32_t &value)
{
    if (axis >= N_AXIS)
        return false;
    for (uint8_t attempt = 0; attempt < 2; attempt++)
    {
        uint8_t req[4], rep[8];
        tmc::readFrame(req, 0, reg);
        sendBytes(axis, req, 4);
        if (receiveBytes(axis, rep, 8) && tmc::parseReply(rep, reg, value))
            return true;
        delay(2);
    }
    return false;
}

bool TmcBus::configure(uint8_t axis, float runPercent)
{
    if (axis >= N_AXIS)
        return false;
    tmc::Config c = configFor(axis);
    c.currentMa *= runPercent / 100.0f;
    uint32_t cnt0 = 0, cnt1 = 0;
    const bool counted = readReg(axis, tmc::IFCNT, cnt0);
    writeReg(axis, tmc::GCONF, tmc::gconf(c));
    writeReg(axis, tmc::CHOPCONF, tmc::chopconf(c));
    writeReg(axis, tmc::IHOLD_IRUN, tmc::iholdIrun(c));
    writeReg(axis, tmc::TPOWERDOWN, 20);
    // StallGuard (DIAG output) works between TCOOLTHRS and TPWMTHRS: on for every speed when SGTHRS > 0
    writeReg(axis, tmc::TCOOLTHRS, c.sgthrs ? 0xFFFFF : 0);
    writeReg(axis, tmc::TPWMTHRS, 0);
    writeReg(axis, tmc::SGTHRS, c.sgthrs);
    // The interface counter counts accepted writes: 7 more = all arrived
    present_[axis] = counted && readReg(axis, tmc::IFCNT, cnt1) && (uint8_t)(cnt1 - cnt0) == 7;
    return present_[axis];
}

bool TmcBus::status(uint8_t axis, TmcStatus &s)
{
    s = TmcStatus();
    if (axis >= N_AXIS)
        return false;
    const tmc::Config c = configFor(axis);
    uint8_t cs;
    bool vsense;
    s.currentMa = tmc::currentScale(c.currentMa, c.rsense, cs, vsense);
    s.microsteps = (uint16_t)(256 >> tmc::mres(c.microsteps));
    uint32_t ioin, drv, sg;
    if (!readReg(axis, tmc::IOIN, ioin))
        return false;
    s.ok = true;
    s.version = (uint8_t)(ioin >> 24);
    if (readReg(axis, tmc::DRV_STATUS, drv))
    {
        s.drvStatus = drv;
        s.overTemp = drv & (1u << 1);
        s.preWarn = drv & (1u << 0);
        s.shortA = drv & ((1u << 2) | (1u << 4));
        s.shortB = drv & ((1u << 3) | (1u << 5));
        s.openA = drv & (1u << 6);
        s.openB = drv & (1u << 7);
        s.stealth = drv & (1u << 30);
        s.standstill = drv & (1u << 31);
    }
    if (readReg(axis, tmc::SG_RESULT, sg))
        s.sgResult = (uint16_t)(sg & 0x3FF);
    return true;
}
