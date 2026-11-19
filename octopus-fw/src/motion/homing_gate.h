/**
 * homing_gate.h - step filter of the step ISR during homing and probing, and the second X motor
 * -----------------------------------------------------------------------------
 * X has two motors with a driver each: DRV0 (X) and DRV1 (X2). The planner only knows
 * X; the ISR copies every X step and direction to X2, so the two can never drift apart
 * in normal moves and X2 adds nothing to the move length.
 *
 * While homing, each side stops on its own input. The side that stalls first holds,
 * the other runs on to its stop, which squares the gantry. Nothing here touches the
 * hardware, so test/test_native runs it on the PC.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>
#include "../pap_board.h"

#define X_PAIR ((uint8_t)(bit(X_AXIS) | bit(X2_AXIS)))

// Step or direction bits with X2 forced to whatever X does
inline uint8_t mirrorX(uint8_t bits)
{
    return (bits & bit(X_AXIS)) ? (uint8_t)(bits | bit(X2_AXIS)) : (uint8_t)(bits & ~bit(X2_AXIS));
}

struct HomingGate
{
    volatile bool active = false;
    volatile bool untilClear = false;
    volatile bool skewFailed = false;
    volatile uint8_t lock = 0;      // axes still running; a cleared bit = that axis stopped
    int8_t input[N_AXIS] = {0};     // input watched per axis, -1 = none
    volatile uint32_t blank = 0;    // steps left before the inputs count (DIAG is not reliable while accelerating)
    volatile uint32_t skewLimit = 0; // steps one X side may run on alone, 0 = no limit
    volatile uint32_t skew = 0;     // steps it did run on alone

    void begin(uint8_t mask, const int8_t *inputs, bool clear, uint32_t blankSteps, uint32_t skewSteps)
    {
        for (uint8_t a = 0; a < N_AXIS; a++)
            input[a] = inputs ? inputs[a] : -1;
        untilClear = clear;
        lock = mask;
        blank = blankSteps;
        skewLimit = skewSteps;
        skew = 0;
        skewFailed = false;
        active = true;
    }

    // out = the step bits of this tick, X2 already mirrored. Returns the bits allowed through.
    template <class Hit>
    uint8_t filter(uint8_t out, Hit hit)
    {
        if (!active)
            return out;
        if (blank)
        {
            if (out & lock)
                blank--;
        }
        else
            for (uint8_t a = 0; a < N_AXIS; a++)
            {
                if (!(lock & bit(a)) || input[a] < 0)
                    continue;
                const bool h = hit((uint8_t)input[a]);
                if (untilClear ? !h : h)
                    lock &= (uint8_t)~bit(a);
            }
        const uint8_t sides = lock & X_PAIR;
        if (skewLimit && sides && sides != X_PAIR && (out & bit(X_AXIS)) && ++skew > skewLimit)
        {
            lock &= (uint8_t)~X_PAIR; // one side ran too far alone: stop before the gantry racks
            skewFailed = true;
        }
        return out & lock;
    }
};
