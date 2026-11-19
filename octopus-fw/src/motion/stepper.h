/**
 * stepper.h - step generation: segment buffer, Bresenham, AMASS (STM32F407)
 * -----------------------------------------------------------------------------
 * Ported from Grbl 1.1 stepper.c via ../../../lib/Motion (ATmega version). GPLv3.
 *
 *   TIM6 (21 MHz ticks, NVIC priority 0)  step ISR: Bresenham over 8 axes, one step event per tick.
 *                                          Step pins go high at ISR entry and low at its end (>= 1 us).
 *   TIM7 (1 kHz, NVIC priority 6)         segment preparation (st_prep_buffer), skipped while locked.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>
#include "mconfig.h"

void st_init();
void st_reset();
void st_wake_up();
void st_go_idle();

void st_prep_buffer();
void st_update_plan_block_parameters();

void st_begin_hold();
bool st_hold_generation_done();

void st_prep_lock();
void st_prep_unlock();

bool st_is_running();
bool st_segments_pending();
void st_start_if_needed();

void st_enable_motors(bool on);
bool st_motors_enabled();
void st_set_dir_invert(uint8_t mask); // bit set = flip that axis' DIR pin

int32_t st_get_position_steps(uint8_t axis);
void st_set_position_steps(uint8_t axis, int32_t steps);

// Homing / probing: while active, the ISR drops further steps of an axis once
// its watched input reaches the wanted state, and clears its bit in the pending mask.
//   input[axis]   0..7 = DIAG input to watch for that axis
//   untilClear    false: stop when the input becomes "hit"; true: stop when it releases
//   blankSteps    inputs are ignored for this many steps (sensorless: DIAG while accelerating)
//   xSkewSteps    with X and X2 in the mask: both stop if one runs this far on alone, 0 = no limit
void st_homing_begin(uint8_t axis_mask, const int8_t *input_per_axis, bool untilClear = false, uint32_t blankSteps = 0,
                     uint32_t xSkewSteps = 0);
void st_homing_end();
uint8_t st_homing_pending();
uint32_t st_homing_x_skew();     // steps one X side ran on after the other stopped
bool st_homing_x_skew_failed(); // xSkewSteps was exceeded

uint8_t st_alarm();
void st_set_alarm(uint8_t code);
void st_clear_alarm();

// Inputs (DIAG0..7) in "hit" logic: raw level XOR invert (settings in_invert_n). ISR-safe.
void inputs_init();
void inputs_set_invert(uint8_t mask);
bool input_hit(uint8_t n);
uint8_t inputs_hit_mask();
// Virtual input IN_LOADCELL (set from the main loop by the load cell code)
void inputs_set_loadcell(bool hit);
