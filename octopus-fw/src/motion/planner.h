/**
 * planner.h - move queue with look-ahead and junction-deviation cornering
 * -----------------------------------------------------------------------------
 * Ported from Grbl 1.1 planner.c via ../../../lib/Motion (ATmega version):
 * 8 axes, run-time steps/unit, speeds and accelerations (settings).
 * Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
 * Copyright (c) 2009-2011 Simen Svale Skogsrud, (c) 2011 Jens Geisler
 * GNU General Public License v3 or later.
 *
 * Not thread-safe: call plan_* only with the prep tick locked (st_prep_lock).
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "mconfig.h"

typedef struct
{
    uint32_t steps[N_AXIS];
    uint32_t step_event_count;
    uint8_t direction_bits; // bit set = axis moves in negative direction

    float entry_speed_sqr;
    float max_entry_speed_sqr;
    float max_junction_speed_sqr;
    float nominal_speed;
    float acceleration;
    float millimeters;
} plan_block_t;

void plan_init();
void plan_reset();
bool plan_buffer_line(const float *target, float feed_rate);
void plan_discard_current_block();
plan_block_t *plan_get_current_block();
float plan_get_exec_block_exit_speed_sqr();
bool plan_check_full_buffer();
uint8_t plan_get_block_buffer_count();
uint8_t plan_get_block_buffer_available();
void plan_sync_position();
int32_t plan_get_position_steps(uint8_t axis);

// Run-time axis parameters (units: mm or deg; speeds per second)
extern float axis_steps_per_mm[N_AXIS];
void plan_set_steps(uint8_t axis, float steps_per_unit);
void plan_set_max_rate(uint8_t axis, float units_per_s);
float plan_get_max_rate(uint8_t axis);
void plan_set_acceleration(uint8_t axis, float units_per_s2);
void plan_set_junction_deviation(float mm);
