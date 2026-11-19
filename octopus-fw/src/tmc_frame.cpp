#include "tmc_frame.h"

#include <math.h>

namespace tmc
{
    // CRC8 with polynomial x^8 + x^2 + x + 1, bits processed LSB first (datasheet 4.2)
    uint8_t crc8(const uint8_t *data, uint8_t len)
    {
        uint8_t crc = 0;
        for (uint8_t i = 0; i < len; i++)
        {
            uint8_t b = data[i];
            for (uint8_t j = 0; j < 8; j++)
            {
                if ((crc >> 7) ^ (b & 0x01))
                    crc = (uint8_t)((crc << 1) ^ 0x07);
                else
                    crc = (uint8_t)(crc << 1);
                b >>= 1;
            }
        }
        return crc;
    }

    void writeFrame(uint8_t out[8], uint8_t addr, uint8_t reg, uint32_t value)
    {
        out[0] = 0x05;
        out[1] = addr;
        out[2] = (uint8_t)(reg | 0x80);
        out[3] = (uint8_t)(value >> 24);
        out[4] = (uint8_t)(value >> 16);
        out[5] = (uint8_t)(value >> 8);
        out[6] = (uint8_t)value;
        out[7] = crc8(out, 7);
    }

    void readFrame(uint8_t out[4], uint8_t addr, uint8_t reg)
    {
        out[0] = 0x05;
        out[1] = addr;
        out[2] = (uint8_t)(reg & 0x7F);
        out[3] = crc8(out, 3);
    }

    bool parseReply(const uint8_t in[8], uint8_t reg, uint32_t &value)
    {
        if (in[0] != 0x05 || in[1] != 0xFF || (in[2] & 0x7F) != reg || crc8(in, 7) != in[7])
            return false;
        value = ((uint32_t)in[3] << 24) | ((uint32_t)in[4] << 16) | ((uint32_t)in[5] << 8) | in[6];
        return true;
    }

    uint8_t mres(uint16_t microsteps)
    {
        uint8_t r = 8; // full step
        uint16_t ms = 1;
        while (ms < microsteps && r > 0)
        {
            ms <<= 1;
            r--;
        }
        return r;
    }

    float currentScale(float ma, float rsense, uint8_t &cs, bool &vsense)
    {
        // I_rms = (CS + 1) / 32 * Vfs / (Rsense + 0.02) / sqrt(2)
        const float r = rsense + 0.02f;
        float c = 32.0f * 1.41421356f * (ma / 1000.0f) * r / 0.325f - 1.0f;
        vsense = false;
        if (c < 16.0f)
        {
            // Low current: the 0.18 V range gives a finer scale
            vsense = true;
            c = 32.0f * 1.41421356f * (ma / 1000.0f) * r / 0.180f - 1.0f;
        }
        if (c > 31.0f)
            c = 31.0f;
        if (c < 0.0f)
            c = 0.0f;
        cs = (uint8_t)lroundf(c);
        const float vfs = vsense ? 0.180f : 0.325f;
        return (cs + 1) / 32.0f * vfs / r / 1.41421356f * 1000.0f;
    }

    uint32_t gconf(const Config &c)
    {
        uint32_t g = 0;
        // I_scale_analog = 0: internal reference (ignore the VREF pot)
        g |= c.stealth ? 0 : (1u << 2); // en_SpreadCycle
        g |= 1u << 6;                    // pdn_disable: PDN_UART is the UART
        g |= 1u << 7;                    // mstep_reg_select: microsteps from MRES
        g |= 1u << 8;                    // multistep_filt
        return g;
    }

    uint32_t chopconf(const Config &c)
    {
        uint8_t cs;
        bool vsense;
        currentScale(c.currentMa, c.rsense, cs, vsense);
        uint32_t v = 0x10000053;            // TOFF 3, HSTRT 5, HEND 0, TBL 0, intpol
        v &= ~(0x0Fu << 24);
        v |= (uint32_t)mres(c.microsteps) << 24;
        if (vsense)
            v |= 1u << 17;
        return v;
    }

    uint32_t iholdIrun(const Config &c)
    {
        uint8_t cs;
        bool vsense;
        currentScale(c.currentMa, c.rsense, cs, vsense);
        uint8_t hold = (uint8_t)lroundf(cs * c.holdPercent / 100.0f);
        if (hold > 31)
            hold = 31;
        return (uint32_t)hold | ((uint32_t)cs << 8) | (8u << 16); // IHOLDDELAY 8
    }
}
