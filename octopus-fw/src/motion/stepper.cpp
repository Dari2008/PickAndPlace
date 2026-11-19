/**
 * stepper.cpp - see stepper.h. Segment preparation unchanged from ../../../lib/Motion/stepper.cpp
 * (Grbl 1.1); timers, pins and locking rewritten for the STM32F407. GPLv3.
 */
#include "stepper.h"
#include "planner.h"
#include "homing_gate.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#define DT_SEGMENT (1.0f / (ACCELERATION_TICKS_PER_SECOND * 60.0f))
#define REQ_MM_INCREMENT_SCALAR 1.25f

#define RAMP_ACCEL 0
#define RAMP_CRUISE 1
#define RAMP_DECEL 2
#define RAMP_DECEL_OVERRIDE 3

#define PREP_FLAG_RECALCULATE bit(0)
#define PREP_FLAG_DECEL_OVERRIDE bit(1)

#define STEP_CONTROL_EXECUTE_HOLD bit(0)
#define STEP_CONTROL_END_MOTION bit(1)

#define MAX_AMASS_LEVEL 3
#define AMASS_LEVEL1 (STEP_TIMER_HZ / 8000)
#define AMASS_LEVEL2 (STEP_TIMER_HZ / 4000)
#define AMASS_LEVEL3 (STEP_TIMER_HZ / 2000)

typedef struct
{
    uint32_t steps[N_AXIS];
    uint32_t step_event_count;
    uint8_t direction_bits;
} st_block_t;
static st_block_t st_block_buffer[SEGMENT_BUFFER_SIZE - 1];

typedef struct
{
    uint16_t n_step;
    uint16_t cycles_per_tick;
    uint8_t st_block_index;
    uint8_t amass_level;
} segment_t;
static segment_t segment_buffer[SEGMENT_BUFFER_SIZE];

typedef struct
{
    uint32_t counter[N_AXIS];
    uint32_t steps[N_AXIS];
    uint16_t step_count;
    uint8_t step_outbits;
    uint8_t dir_outbits;
    uint8_t exec_block_index;
    st_block_t *exec_block;
    segment_t *exec_segment;
} stepper_t;
static stepper_t st;

static volatile uint8_t segment_buffer_tail;
static volatile uint8_t segment_buffer_head;
static uint8_t segment_next_head;

static volatile bool running;
static uint8_t dir_written;
static volatile uint8_t dir_invert_mask;
static volatile bool motors_on;

static volatile int32_t sys_position[N_AXIS];
static HomingGate gate;
static volatile uint8_t alarm_code;

static volatile uint8_t prep_lock_depth;
static volatile uint8_t step_control;

static plan_block_t *pl_block;
static st_block_t *st_prep_block;

typedef struct
{
    uint8_t st_block_index;
    uint8_t recalculate_flag;
    float dt_remainder;
    float steps_remaining;
    float step_per_mm;
    float req_mm_increment;
    uint8_t ramp_type;
    float mm_complete;
    float current_speed;
    float maximum_speed;
    float exit_speed;
    float accelerate_until;
    float decelerate_after;
} st_prep_t;
static st_prep_t prep;

static inline uint8_t st_next_block_index(uint8_t i) { return (++i == SEGMENT_BUFFER_SIZE - 1) ? 0 : i; }

// ---------------------------------------------------------------------------
// Pins: BSRR writes, ports looked up once
// ---------------------------------------------------------------------------
struct FastPin
{
    GPIO_TypeDef *port;
    uint32_t mask;
    void init(PinName pn, uint32_t mode)
    {
        pinMode(pinNametoDigitalPin(pn), mode);
        port = get_GPIO_Port(STM_PORT(pn));
        mask = 1u << STM_PIN(pn);
    }
    inline void set() const { port->BSRR = mask; }
    inline void clr() const { port->BSRR = mask << 16; }
    inline void write(bool v) const { port->BSRR = v ? mask : (mask << 16); }
    inline bool read() const { return (port->IDR & mask) != 0; }
};
static FastPin pin_step[N_AXIS], pin_dir[N_AXIS], pin_en[N_AXIS];
static FastPin pin_in[N_INPUTS];
static volatile uint8_t input_invert;

static HardwareTimer *step_timer;
static HardwareTimer *prep_timer;
static uint32_t pulse_cycles, dir_cycles;

static inline uint32_t cycles_now() { return DWT->CYCCNT; }

void inputs_init()
{
    for (uint8_t i = 0; i < N_INPUTS; i++)
        pin_in[i].init(INPUT_PINS[i], INPUT); // the board has 10 k pull-ups to 3.3 V
}

void inputs_set_invert(uint8_t mask) { input_invert = mask; }

static volatile bool loadcell_hit;
void inputs_set_loadcell(bool hit) { loadcell_hit = hit; }

bool input_hit(uint8_t n)
{
    if (n == IN_LOADCELL)
        return loadcell_hit;
    if (n >= N_INPUTS)
        return false;
    // Switch to GND closes -> LOW = hit (in_invert_n = 0). StallGuard DIAG is HIGH on a stall (invert 1).
    const bool low = !pin_in[n].read();
    return ((input_invert >> n) & 1) ? !low : low;
}

uint8_t inputs_hit_mask()
{
    uint8_t m = 0;
    for (uint8_t i = 0; i < N_INPUTS; i++)
        if (input_hit(i))
            m |= (uint8_t)bit(i);
    return m;
}

static inline void write_dir_pins(uint8_t bits)
{
    for (uint8_t a = 0; a < N_AXIS; a++)
        pin_dir[a].write(bits & bit(a));
}

static inline void clear_step_pins()
{
    for (uint8_t a = 0; a < N_AXIS; a++)
        pin_step[a].clr();
}

void st_enable_motors(bool on)
{
    motors_on = on;
    for (uint8_t a = 0; a < N_AXIS; a++)
        pin_en[a].write(!on); // active LOW
}

bool st_motors_enabled() { return motors_on; }

void st_set_dir_invert(uint8_t mask) { dir_invert_mask = mask; }

static void step_isr();
static void prep_isr();

void st_init()
{
    // Cycle counter for the pulse width
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    pulse_cycles = (uint32_t)((uint64_t)SystemCoreClock * STEP_PULSE_NS / 1000000000ULL);
    dir_cycles = (uint32_t)((uint64_t)SystemCoreClock * DIR_SETUP_NS / 1000000000ULL);

    for (uint8_t a = 0; a < N_AXIS; a++)
    {
        pin_step[a].init(DRIVER_PINS[a].step, OUTPUT);
        pin_dir[a].init(DRIVER_PINS[a].dir, OUTPUT);
        pin_en[a].init(DRIVER_PINS[a].en, OUTPUT);
    }
    clear_step_pins();
    st_enable_motors(false);

    step_timer = new HardwareTimer(TIM6);
    step_timer->pause();
    step_timer->setPrescaleFactor(HAL_RCC_GetPCLK1Freq() * 2 / STEP_TIMER_HZ); // 84 MHz / 4
    step_timer->setOverflow(0xFFFF, TICK_FORMAT);
    step_timer->setInterruptPriority(0, 0);
    step_timer->attachInterrupt(step_isr);
    TIM6->CR1 |= TIM_CR1_ARPE; // a new period starts with the next tick, never mid-count

    prep_timer = new HardwareTimer(TIM7);
    prep_timer->pause();
    prep_timer->setOverflow(PREP_TICKS_PER_SECOND, HERTZ_FORMAT);
    prep_timer->setInterruptPriority(6, 0);
    prep_timer->attachInterrupt(prep_isr);

    prep_lock_depth = 0;
    st_reset();
    prep_timer->resume();
}

void st_wake_up()
{
    st_enable_motors(true);
    st.step_outbits = 0;
    running = true;
    step_timer->setCount(0);
    TIM6->ARR = 200; // first tick after ~10 us, then the segment's period
    step_timer->resume();
}

void st_go_idle()
{
    step_timer->pause();
    running = false;
}

void st_reset()
{
    st_go_idle();
    clear_step_pins();
    memset(&prep, 0, sizeof(prep));
    memset(&st, 0, sizeof(st));
    st.exec_segment = NULL;
    pl_block = NULL;
    segment_buffer_tail = 0;
    segment_buffer_head = 0;
    segment_next_head = 1;
    step_control = 0;
    dir_written = 0xFF;
}

bool st_is_running() { return running; }
bool st_segments_pending() { return segment_buffer_head != segment_buffer_tail; }

void st_start_if_needed()
{
    if (!running && alarm_code == ALARM_NONE && segment_buffer_head != segment_buffer_tail)
        st_wake_up();
}

void st_prep_lock()
{
    __disable_irq();
    prep_lock_depth++;
    __enable_irq();
}

void st_prep_unlock()
{
    __disable_irq();
    if (prep_lock_depth)
        prep_lock_depth--;
    __enable_irq();
}

int32_t st_get_position_steps(uint8_t axis) { return sys_position[axis]; } // 32-bit reads are atomic
void st_set_position_steps(uint8_t axis, int32_t steps) { sys_position[axis] = steps; }

void st_homing_begin(uint8_t axis_mask, const int8_t *input_per_axis, bool untilClear, uint32_t blankSteps, uint32_t xSkewSteps)
{
    __disable_irq();
    gate.begin(axis_mask, input_per_axis, untilClear, blankSteps, xSkewSteps);
    __enable_irq();
}

void st_homing_end() { gate.active = false; }
uint8_t st_homing_pending() { return gate.lock; }
uint32_t st_homing_x_skew() { return gate.skew; }
bool st_homing_x_skew_failed() { return gate.skewFailed; }

uint8_t st_alarm() { return alarm_code; }
void st_set_alarm(uint8_t code) { alarm_code = code; }
void st_clear_alarm() { alarm_code = ALARM_NONE; }

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------
static void step_isr()
{
    // DIR first (changes only between segments), then the step pulse of the bits computed last tick.
    if (st.dir_outbits != dir_written)
    {
        write_dir_pins(st.dir_outbits);
        dir_written = st.dir_outbits;
        const uint32_t t0 = cycles_now();
        while (cycles_now() - t0 < dir_cycles)
        {
        }
    }
    const uint32_t pulse_start = cycles_now();
    const uint8_t outbits = st.step_outbits;
    if (outbits)
        for (uint8_t a = 0; a < N_AXIS; a++)
            if (outbits & bit(a))
                pin_step[a].set();

    if (st.exec_segment == NULL)
    {
        if (segment_buffer_head != segment_buffer_tail)
        {
            st.exec_segment = &segment_buffer[segment_buffer_tail];
            TIM6->ARR = st.exec_segment->cycles_per_tick - 1;
            st.step_count = st.exec_segment->n_step;

            if (st.exec_block_index != st.exec_segment->st_block_index)
            {
                st.exec_block_index = st.exec_segment->st_block_index;
                st.exec_block = &st_block_buffer[st.exec_block_index];
                const uint32_t half = st.exec_block->step_event_count >> 1;
                for (uint8_t a = 0; a < N_AXIS; a++)
                    st.counter[a] = half;
            }
            // direction_bits: set = negative. DIR HIGH = positive unless inverted.
            st.dir_outbits = ~(mirrorX(st.exec_block->direction_bits) ^ dir_invert_mask) & AXIS_MASK_ALL;

            const uint8_t amass = st.exec_segment->amass_level;
            for (uint8_t a = 0; a < N_AXIS; a++)
                st.steps[a] = st.exec_block->steps[a] >> amass;
        }
        else
        {
            while (cycles_now() - pulse_start < pulse_cycles)
            {
            }
            clear_step_pins();
            st.step_outbits = 0;
            st_go_idle();
            return;
        }
    }

    const uint32_t event_count = st.exec_block->step_event_count;
    uint8_t out = 0;
    for (uint8_t a = 0; a < N_AXIS; a++)
    {
        st.counter[a] += st.steps[a];
        if (st.counter[a] > event_count)
        {
            out |= (uint8_t)bit(a);
            st.counter[a] -= event_count;
        }
    }

    out = gate.filter(mirrorX(out), input_hit);

    const uint8_t dir_bits = st.exec_block->direction_bits;
    for (uint8_t a = 0; a < N_MOVE_AXES; a++)
        if (out & bit(a))
        {
            if (dir_bits & bit(a))
                sys_position[a]--;
            else
                sys_position[a]++;
        }

    st.step_outbits = out;

    if (--st.step_count == 0)
    {
        st.exec_segment = NULL;
        uint8_t tail = segment_buffer_tail + 1;
        if (tail == SEGMENT_BUFFER_SIZE)
            tail = 0;
        segment_buffer_tail = tail;
    }

    if (outbits)
    {
        while (cycles_now() - pulse_start < pulse_cycles)
        {
        }
        clear_step_pins();
    }
}

static void prep_isr()
{
    if (prep_lock_depth)
        return; // the foreground is in the planner; next tick
    st_prep_buffer();
    st_start_if_needed();
}

// ---------------------------------------------------------------------------
// Segment preparation (unchanged Grbl logic)
// ---------------------------------------------------------------------------
void st_update_plan_block_parameters()
{
    if (pl_block != NULL)
    {
        prep.recalculate_flag |= PREP_FLAG_RECALCULATE;
        pl_block->entry_speed_sqr = prep.current_speed * prep.current_speed;
        pl_block = NULL;
    }
}

void st_begin_hold()
{
    step_control |= STEP_CONTROL_EXECUTE_HOLD;
    st_update_plan_block_parameters();
}

bool st_hold_generation_done()
{
    return (step_control & STEP_CONTROL_END_MOTION) || plan_get_current_block() == NULL;
}

void st_prep_buffer()
{
    if (step_control & STEP_CONTROL_END_MOTION)
        return;

    while (segment_buffer_tail != segment_next_head)
    {
        if (pl_block == NULL)
        {
            pl_block = plan_get_current_block();
            if (pl_block == NULL)
                return;

            if (prep.recalculate_flag & PREP_FLAG_RECALCULATE)
            {
                prep.recalculate_flag &= ~PREP_FLAG_RECALCULATE;
            }
            else
            {
                prep.st_block_index = st_next_block_index(prep.st_block_index);
                st_prep_block = &st_block_buffer[prep.st_block_index];
                st_prep_block->direction_bits = pl_block->direction_bits;
                for (uint8_t idx = 0; idx < N_AXIS; idx++)
                    st_prep_block->steps[idx] = pl_block->steps[idx] << MAX_AMASS_LEVEL;
                st_prep_block->step_event_count = pl_block->step_event_count << MAX_AMASS_LEVEL;

                prep.steps_remaining = (float)pl_block->step_event_count;
                prep.step_per_mm = prep.steps_remaining / pl_block->millimeters;
                prep.req_mm_increment = REQ_MM_INCREMENT_SCALAR / prep.step_per_mm;
                prep.dt_remainder = 0.0f;

                if ((step_control & STEP_CONTROL_EXECUTE_HOLD) || (prep.recalculate_flag & PREP_FLAG_DECEL_OVERRIDE))
                {
                    prep.current_speed = prep.exit_speed;
                    pl_block->entry_speed_sqr = prep.exit_speed * prep.exit_speed;
                    prep.recalculate_flag &= ~PREP_FLAG_DECEL_OVERRIDE;
                }
                else
                {
                    prep.current_speed = sqrtf(pl_block->entry_speed_sqr);
                }
            }

            prep.mm_complete = 0.0f;
            const float inv_2_accel = 0.5f / pl_block->acceleration;

            if (step_control & STEP_CONTROL_EXECUTE_HOLD)
            {
                prep.ramp_type = RAMP_DECEL;
                const float decel_dist = pl_block->millimeters - inv_2_accel * pl_block->entry_speed_sqr;
                if (decel_dist < 0.0f)
                    prep.exit_speed = sqrtf(pl_block->entry_speed_sqr - 2.0f * pl_block->acceleration * pl_block->millimeters);
                else
                {
                    prep.mm_complete = decel_dist;
                    prep.exit_speed = 0.0f;
                }
            }
            else
            {
                prep.ramp_type = RAMP_ACCEL;
                prep.accelerate_until = pl_block->millimeters;

                const float exit_speed_sqr = plan_get_exec_block_exit_speed_sqr();
                prep.exit_speed = sqrtf(exit_speed_sqr);

                const float nominal_speed = pl_block->nominal_speed;
                const float nominal_speed_sqr = nominal_speed * nominal_speed;
                const float intersect_distance =
                    0.5f * (pl_block->millimeters + inv_2_accel * (pl_block->entry_speed_sqr - exit_speed_sqr));

                if (pl_block->entry_speed_sqr > nominal_speed_sqr)
                {
                    prep.accelerate_until = pl_block->millimeters - inv_2_accel * (pl_block->entry_speed_sqr - nominal_speed_sqr);
                    if (prep.accelerate_until <= 0.0f)
                    {
                        prep.ramp_type = RAMP_DECEL;
                        prep.exit_speed = sqrtf(pl_block->entry_speed_sqr - 2.0f * pl_block->acceleration * pl_block->millimeters);
                        prep.recalculate_flag |= PREP_FLAG_DECEL_OVERRIDE;
                    }
                    else
                    {
                        prep.decelerate_after = inv_2_accel * (nominal_speed_sqr - exit_speed_sqr);
                        prep.maximum_speed = nominal_speed;
                        prep.ramp_type = RAMP_DECEL_OVERRIDE;
                    }
                }
                else if (intersect_distance > 0.0f)
                {
                    if (intersect_distance < pl_block->millimeters)
                    {
                        prep.decelerate_after = inv_2_accel * (nominal_speed_sqr - exit_speed_sqr);
                        if (prep.decelerate_after < intersect_distance)
                        {
                            prep.maximum_speed = nominal_speed;
                            if (pl_block->entry_speed_sqr == nominal_speed_sqr)
                                prep.ramp_type = RAMP_CRUISE;
                            else
                                prep.accelerate_until -= inv_2_accel * (nominal_speed_sqr - pl_block->entry_speed_sqr);
                        }
                        else
                        {
                            prep.accelerate_until = intersect_distance;
                            prep.decelerate_after = intersect_distance;
                            prep.maximum_speed = sqrtf(2.0f * pl_block->acceleration * intersect_distance + exit_speed_sqr);
                        }
                    }
                    else
                    {
                        prep.ramp_type = RAMP_DECEL;
                    }
                }
                else
                {
                    prep.accelerate_until = 0.0f;
                    prep.maximum_speed = prep.exit_speed;
                }
            }
        }

        segment_t *prep_segment = &segment_buffer[segment_buffer_head];
        prep_segment->st_block_index = prep.st_block_index;

        float dt_max = DT_SEGMENT;
        float dt = 0.0f;
        float time_var = dt_max;
        float mm_var;
        float speed_var;
        float mm_remaining = pl_block->millimeters;
        float minimum_mm = mm_remaining - prep.req_mm_increment;
        if (minimum_mm < 0.0f)
            minimum_mm = 0.0f;

        do
        {
            switch (prep.ramp_type)
            {
            case RAMP_DECEL_OVERRIDE:
                speed_var = pl_block->acceleration * time_var;
                if (prep.current_speed - prep.maximum_speed <= speed_var)
                {
                    mm_remaining = prep.accelerate_until;
                    time_var = 2.0f * (pl_block->millimeters - mm_remaining) / (prep.current_speed + prep.maximum_speed);
                    prep.ramp_type = RAMP_CRUISE;
                    prep.current_speed = prep.maximum_speed;
                }
                else
                {
                    mm_remaining -= time_var * (prep.current_speed - 0.5f * speed_var);
                    prep.current_speed -= speed_var;
                }
                break;
            case RAMP_ACCEL:
                speed_var = pl_block->acceleration * time_var;
                mm_remaining -= time_var * (prep.current_speed + 0.5f * speed_var);
                if (mm_remaining < prep.accelerate_until)
                {
                    mm_remaining = prep.accelerate_until;
                    time_var = 2.0f * (pl_block->millimeters - mm_remaining) / (prep.current_speed + prep.maximum_speed);
                    prep.ramp_type = (mm_remaining == prep.decelerate_after) ? RAMP_DECEL : RAMP_CRUISE;
                    prep.current_speed = prep.maximum_speed;
                }
                else
                    prep.current_speed += speed_var;
                break;
            case RAMP_CRUISE:
                mm_var = mm_remaining - prep.maximum_speed * time_var;
                if (mm_var < prep.decelerate_after)
                {
                    time_var = (mm_remaining - prep.decelerate_after) / prep.maximum_speed;
                    mm_remaining = prep.decelerate_after;
                    prep.ramp_type = RAMP_DECEL;
                }
                else
                    mm_remaining = mm_var;
                break;
            default:
                speed_var = pl_block->acceleration * time_var;
                if (prep.current_speed > speed_var)
                {
                    mm_var = mm_remaining - time_var * (prep.current_speed - 0.5f * speed_var);
                    if (mm_var > prep.mm_complete)
                    {
                        mm_remaining = mm_var;
                        prep.current_speed -= speed_var;
                        break;
                    }
                }
                time_var = 2.0f * (mm_remaining - prep.mm_complete) / (prep.current_speed + prep.exit_speed);
                mm_remaining = prep.mm_complete;
                prep.current_speed = prep.exit_speed;
            }

            dt += time_var;
            if (dt < dt_max)
                time_var = dt_max - dt;
            else if (mm_remaining > minimum_mm)
            {
                dt_max += DT_SEGMENT;
                time_var = dt_max - dt;
            }
            else
                break;
        } while (mm_remaining > prep.mm_complete);

        const float step_dist_remaining = prep.step_per_mm * mm_remaining;
        const float n_steps_remaining = ceilf(step_dist_remaining);
        const float last_n_steps_remaining = ceilf(prep.steps_remaining);
        prep_segment->n_step = (uint16_t)(last_n_steps_remaining - n_steps_remaining);

        if (prep_segment->n_step == 0 && (step_control & STEP_CONTROL_EXECUTE_HOLD))
        {
            step_control |= STEP_CONTROL_END_MOTION;
            return;
        }

        dt += prep.dt_remainder;
        const float inv_rate = dt / (last_n_steps_remaining - step_dist_remaining); // min/step

        uint32_t cycles = (uint32_t)ceilf((STEP_TIMER_HZ * 60.0f) * inv_rate);
        static const uint32_t MIN_CYCLES = (uint32_t)(STEP_TIMER_HZ / MAX_STEP_RATE);
        if (cycles < MIN_CYCLES)
            cycles = MIN_CYCLES;

        if (cycles < AMASS_LEVEL1)
            prep_segment->amass_level = 0;
        else
        {
            if (cycles < AMASS_LEVEL2)
                prep_segment->amass_level = 1;
            else if (cycles < AMASS_LEVEL3)
                prep_segment->amass_level = 2;
            else
                prep_segment->amass_level = 3;
            cycles >>= prep_segment->amass_level;
            prep_segment->n_step <<= prep_segment->amass_level;
        }
        prep_segment->cycles_per_tick = (cycles < (1UL << 16)) ? cycles : 0xFFFF;

        segment_buffer_head = segment_next_head;
        if (++segment_next_head == SEGMENT_BUFFER_SIZE)
            segment_next_head = 0;

        pl_block->millimeters = mm_remaining;
        prep.steps_remaining = n_steps_remaining;
        prep.dt_remainder = (n_steps_remaining - step_dist_remaining) * inv_rate;

        if (mm_remaining == prep.mm_complete)
        {
            if (mm_remaining > 0.0f)
            {
                step_control |= STEP_CONTROL_END_MOTION;
                return;
            }
            pl_block = NULL;
            plan_discard_current_block();
        }
    }
}
