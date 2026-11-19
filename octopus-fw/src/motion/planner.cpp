/**
 * planner.cpp - see planner.h. Logic unchanged from ../../../lib/Motion/planner.cpp
 * (Grbl 1.1), parameters are run-time arrays. GPLv3.
 */
#include "planner.h"
#include "stepper.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

float axis_steps_per_mm[N_AXIS] = {80, 80, 400, 8.889f, 400, 8.889f, 80, 8.889f};
static float axis_max_rate[N_AXIS];     // units/min
static float axis_acceleration[N_AXIS]; // units/min^2
static float junction_deviation = 0.02f;

static plan_block_t block_buffer[BLOCK_BUFFER_SIZE];
static uint8_t block_buffer_tail;
static uint8_t block_buffer_head;
static uint8_t next_buffer_head;
static uint8_t block_buffer_planned;

typedef struct
{
    int32_t position[N_AXIS];
    float previous_unit_vec[N_AXIS];
    float previous_nominal_speed;
} planner_t;
static planner_t pl;

static inline uint8_t plan_next_block_index(uint8_t i) { return (++i == BLOCK_BUFFER_SIZE) ? 0 : i; }
static inline uint8_t plan_prev_block_index(uint8_t i) { return (i == 0) ? BLOCK_BUFFER_SIZE - 1 : i - 1; }

static float convert_delta_vector_to_unit_vector(float *vector)
{
    float magnitude = 0.0f;
    for (uint8_t idx = 0; idx < N_AXIS; idx++)
        if (vector[idx] != 0.0f)
            magnitude += vector[idx] * vector[idx];
    magnitude = sqrtf(magnitude);
    const float inv_magnitude = 1.0f / magnitude;
    for (uint8_t idx = 0; idx < N_AXIS; idx++)
        vector[idx] *= inv_magnitude;
    return magnitude;
}

static float limit_value_by_axis_maximum(const float *max_value, const float *unit_vec)
{
    float limit_value = SOME_LARGE_VALUE;
    for (uint8_t idx = 0; idx < N_AXIS; idx++)
        if (unit_vec[idx] != 0.0f)
            limit_value = fminf(limit_value, fabsf(max_value[idx] / unit_vec[idx]));
    return limit_value;
}

static void planner_recalculate()
{
    uint8_t block_index = plan_prev_block_index(block_buffer_head);
    if (block_index == block_buffer_planned)
        return;

    float entry_speed_sqr;
    plan_block_t *next;
    plan_block_t *current = &block_buffer[block_index];

    current->entry_speed_sqr = fminf(current->max_entry_speed_sqr, 2.0f * current->acceleration * current->millimeters);

    block_index = plan_prev_block_index(block_index);
    if (block_index == block_buffer_planned)
    {
        if (block_index == block_buffer_tail)
            st_update_plan_block_parameters();
    }
    else
    {
        while (block_index != block_buffer_planned)
        {
            next = current;
            current = &block_buffer[block_index];
            block_index = plan_prev_block_index(block_index);
            if (block_index == block_buffer_tail)
                st_update_plan_block_parameters();
            if (current->entry_speed_sqr != current->max_entry_speed_sqr)
            {
                entry_speed_sqr = next->entry_speed_sqr + 2.0f * current->acceleration * current->millimeters;
                current->entry_speed_sqr = (entry_speed_sqr < current->max_entry_speed_sqr) ? entry_speed_sqr
                                                                                            : current->max_entry_speed_sqr;
            }
        }
    }

    next = &block_buffer[block_buffer_planned];
    block_index = plan_next_block_index(block_buffer_planned);
    while (block_index != block_buffer_head)
    {
        current = next;
        next = &block_buffer[block_index];
        if (current->entry_speed_sqr < next->entry_speed_sqr)
        {
            entry_speed_sqr = current->entry_speed_sqr + 2.0f * current->acceleration * current->millimeters;
            if (entry_speed_sqr < next->entry_speed_sqr)
            {
                next->entry_speed_sqr = entry_speed_sqr;
                block_buffer_planned = block_index;
            }
        }
        if (next->entry_speed_sqr == next->max_entry_speed_sqr)
            block_buffer_planned = block_index;
        block_index = plan_next_block_index(block_index);
    }
}

void plan_set_steps(uint8_t axis, float steps_per_unit)
{
    if (axis < N_AXIS && steps_per_unit > 0.0f)
        axis_steps_per_mm[axis] = steps_per_unit;
}

void plan_set_max_rate(uint8_t axis, float units_per_s)
{
    if (axis < N_AXIS && units_per_s > 0.0f)
        axis_max_rate[axis] = MM_PER_S_TO_MM_PER_MIN(units_per_s);
}

float plan_get_max_rate(uint8_t axis) { return axis < N_AXIS ? axis_max_rate[axis] / 60.0f : 0.0f; }

void plan_set_acceleration(uint8_t axis, float units_per_s2)
{
    if (axis < N_AXIS && units_per_s2 > 0.0f)
        axis_acceleration[axis] = MM_PER_S2_TO_MM_PER_MIN2(units_per_s2);
}

void plan_set_junction_deviation(float mm)
{
    if (mm > 0.0f)
        junction_deviation = mm;
}

void plan_init()
{
    for (uint8_t a = 0; a < N_AXIS; a++)
    {
        if (axis_max_rate[a] <= 0.0f)
            axis_max_rate[a] = MM_PER_S_TO_MM_PER_MIN(100.0f);
        if (axis_acceleration[a] <= 0.0f)
            axis_acceleration[a] = MM_PER_S2_TO_MM_PER_MIN2(500.0f);
    }
    plan_reset();
}

void plan_reset()
{
    memset(&pl, 0, sizeof(pl));
    block_buffer_tail = 0;
    block_buffer_head = 0;
    next_buffer_head = 1;
    block_buffer_planned = 0;
}

void plan_discard_current_block()
{
    if (block_buffer_head != block_buffer_tail)
    {
        const uint8_t block_index = plan_next_block_index(block_buffer_tail);
        if (block_buffer_tail == block_buffer_planned)
            block_buffer_planned = block_index;
        block_buffer_tail = block_index;
    }
}

plan_block_t *plan_get_current_block()
{
    if (block_buffer_head == block_buffer_tail)
        return NULL;
    return &block_buffer[block_buffer_tail];
}

float plan_get_exec_block_exit_speed_sqr()
{
    const uint8_t block_index = plan_next_block_index(block_buffer_tail);
    if (block_index == block_buffer_head)
        return 0.0f;
    return block_buffer[block_index].entry_speed_sqr;
}

bool plan_check_full_buffer() { return block_buffer_tail == next_buffer_head; }

uint8_t plan_get_block_buffer_count()
{
    if (block_buffer_head >= block_buffer_tail)
        return block_buffer_head - block_buffer_tail;
    return BLOCK_BUFFER_SIZE - (block_buffer_tail - block_buffer_head);
}

uint8_t plan_get_block_buffer_available() { return (BLOCK_BUFFER_SIZE - 1) - plan_get_block_buffer_count(); }

bool plan_buffer_line(const float *target, float feed_rate)
{
    plan_block_t *block = &block_buffer[block_buffer_head];
    memset(block, 0, sizeof(plan_block_t));

    int32_t target_steps[N_AXIS];
    float unit_vec[N_AXIS];

    for (uint8_t idx = 0; idx < N_AXIS; idx++)
    {
        target_steps[idx] = lroundf(target[idx] * axis_steps_per_mm[idx]);
        const int32_t delta_steps = target_steps[idx] - pl.position[idx];
        block->steps[idx] = labs(delta_steps);
        if (block->steps[idx] > block->step_event_count)
            block->step_event_count = block->steps[idx];
        unit_vec[idx] = delta_steps / axis_steps_per_mm[idx];
        if (delta_steps < 0)
            block->direction_bits |= bit(idx);
    }

    if (block->step_event_count == 0)
        return false;

    block->millimeters = convert_delta_vector_to_unit_vector(unit_vec);
    block->acceleration = limit_value_by_axis_maximum(axis_acceleration, unit_vec);

    const float rapid_rate = limit_value_by_axis_maximum(axis_max_rate, unit_vec);
    float nominal_speed = (feed_rate < rapid_rate) ? feed_rate : rapid_rate;
    if (nominal_speed < MINIMUM_FEED_RATE)
        nominal_speed = MINIMUM_FEED_RATE;
    block->nominal_speed = nominal_speed;

    if (block_buffer_head == block_buffer_tail)
    {
        block->entry_speed_sqr = 0.0f;
        block->max_junction_speed_sqr = 0.0f;
    }
    else
    {
        float junction_unit_vec[N_AXIS];
        float junction_cos_theta = 0.0f;
        for (uint8_t idx = 0; idx < N_AXIS; idx++)
        {
            junction_cos_theta -= pl.previous_unit_vec[idx] * unit_vec[idx];
            junction_unit_vec[idx] = unit_vec[idx] - pl.previous_unit_vec[idx];
        }
        if (junction_cos_theta > 0.999999f)
            block->max_junction_speed_sqr = MINIMUM_JUNCTION_SPEED * MINIMUM_JUNCTION_SPEED;
        else if (junction_cos_theta < -0.999999f)
            block->max_junction_speed_sqr = SOME_LARGE_VALUE;
        else
        {
            convert_delta_vector_to_unit_vector(junction_unit_vec);
            const float junction_acceleration = limit_value_by_axis_maximum(axis_acceleration, junction_unit_vec);
            const float sin_theta_d2 = sqrtf(0.5f * (1.0f - junction_cos_theta));
            block->max_junction_speed_sqr =
                fmaxf(MINIMUM_JUNCTION_SPEED * MINIMUM_JUNCTION_SPEED,
                      (junction_acceleration * junction_deviation * sin_theta_d2) / (1.0f - sin_theta_d2));
        }
    }

    const float prev_nominal = pl.previous_nominal_speed;
    block->max_entry_speed_sqr = (nominal_speed > prev_nominal) ? prev_nominal * prev_nominal : nominal_speed * nominal_speed;
    if (block->max_entry_speed_sqr > block->max_junction_speed_sqr)
        block->max_entry_speed_sqr = block->max_junction_speed_sqr;

    pl.previous_nominal_speed = nominal_speed;
    memcpy(pl.previous_unit_vec, unit_vec, sizeof(unit_vec));
    memcpy(pl.position, target_steps, sizeof(target_steps));

    block_buffer_head = next_buffer_head;
    next_buffer_head = plan_next_block_index(block_buffer_head);

    planner_recalculate();
    return true;
}

void plan_sync_position()
{
    for (uint8_t idx = 0; idx < N_AXIS; idx++)
        pl.position[idx] = st_get_position_steps(idx);
}

int32_t plan_get_position_steps(uint8_t axis) { return pl.position[axis]; }
