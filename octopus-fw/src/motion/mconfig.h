/**
 * mconfig.h - fixed internals of the motion core (Grbl-derived, GPLv3, see ../../../lib/Motion/LICENSE.md)
 * -----------------------------------------------------------------------------
 * User values (steps/unit, speeds, accelerations, travel, homing) are settings
 * (settings.h) and change at run time. Units inside the planner are Grbl's:
 * units/min and units/min^2 (mm or degrees).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include "../pap_board.h"

// Step timer: TIM6 (basic timer, APB1 x2 = 84 MHz) with prescaler 4 -> 21 MHz ticks.
#define STEP_TIMER_HZ 21000000UL
// Segment preparation tick (TIM7)
#define PREP_TICKS_PER_SECOND 1000

// Fastest step rate the ISR delivers (per axis tick); a move asking for more runs at this rate.
#define MAX_STEP_RATE 80000.0f
// Step pulse width (TMC2209 needs >= 100 ns). The pins are set at ISR entry and cleared at its end.
#define STEP_PULSE_NS 1000
// DIR setup before the next step (TMC2209: 20 ns)
#define DIR_SETUP_NS 200

#define BLOCK_BUFFER_SIZE 32
#define SEGMENT_BUFFER_SIZE 10
#define ACCELERATION_TICKS_PER_SECOND 100
#define MINIMUM_JUNCTION_SPEED 0.0f // units/min
#define MINIMUM_FEED_RATE 1.0f      // units/min

#define MM_PER_S_TO_MM_PER_MIN(v) ((v) * 60.0f)
#define MM_PER_S2_TO_MM_PER_MIN2(v) ((v) * 3600.0f)
#define SOME_LARGE_VALUE 1.0e+38f

// Alarm reasons
#define ALARM_NONE 0
#define ALARM_HARD_LIMIT 1
#define ALARM_ESTOP 2
#define ALARM_LINK 3
