/**
 * board.h - BTT Octopus V1.1 pins and the axis assignment of the pick & place machine
 * -----------------------------------------------------------------------------
 * Pins from the BTT Octopus schematic / pin diagram (V1.0 = V1.1) and Klipper's
 * generic-bigtreetech-octopus-v1.1.cfg. Poster: ../board/octopus/octopus-v1.1-pinout.svg
 *
 *   Axis numbers are not slot numbers: X2 stays the last axis, DRIVER_PINS below maps axis to slot.
 *   slot  connector  axis              units
 *   DRV0  MOTOR0     X                 mm, sensorless homing (DIAG jumper 0)
 *   DRV1  MOTOR1     X2 (second X)     follows X, own stall on DIAG jumper 1 (motion/homing_gate.h)
 *   DRV2  MOTOR2_1   PASTE (extruder)  mm of plunger travel (PASTE takes microlitres)
 *   DRV3  MOTOR3     ROT (nozzle)      deg
 *   DRV4  MOTOR4     Z (seesaw)        mm, 0 = level   (Z and PASTE swapped slots on 18 Nov 2026)
 *   DRV5  MOTOR5     E0 (feeder feed)  deg
 *   DRV6  MOTOR6     E1 (gantry)       mm, sensorless homing (DIAG jumper 6)
 *   DRV7  MOTOR7     Y                 mm, sensorless homing (DIAG jumper 7)
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <stdint.h>

// ---- axes ------------------------------------------------------------------------
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2
#define ROT_AXIS 3
#define PASTE_AXIS 4
#define E0_AXIS 5
#define E1_AXIS 6
#define X2_AXIS 7     // not a planner axis: the step ISR mirrors X onto it
#define N_AXIS 8
#define N_MOVE_AXES 7 // axes that take commands: all but X2, which must stay the last one

#ifndef bit
#define bit(n) (1UL << (n))
#endif
#define AXIS_MASK_ALL ((1u << N_AXIS) - 1)

// Protocol names of the axes (MOTOR, JOG, SETPOS, STATUS, settings suffixes)
extern const char *const AXIS_NAME[N_AXIS]; // "x" "y" "z" "rot" "paste" "e0" "e1" "x2"
int axisByName(const char *name);           // -1 if unknown (any case)

// ---- inputs (endstop connectors DIAG0..7: 5V, GND, signal; 10 k pull-up to 3.3 V on the board)
#define N_INPUTS 8
#define IN_X_MIN 0   // DIAG0 PG6
#define IN_X2_MIN 1  // DIAG1 PG9   second X motor: TMC2209 DIAG via jumper 1 (StallGuard), or a switch
#define IN_Z_FLAG 2  // DIAG2 PG10  seesaw half flag
#define IN_Z_MAX 3   // DIAG3 PG11  optional
#define IN_FEEDER 4  // DIAG4 PG12  feeder / pocket switch (SCAN probe)
#define IN_ESTOP 5   // DIAG5 PG13  e-stop button (opens or closes, see in_invert_5)
#define IN_E1_HOME 6 // DIAG6 PG14  gantry: TMC2209 DIAG via jumper 6 (StallGuard), or a switch
#define IN_Y_MIN 7   // DIAG7 PG15
extern const char *const INPUT_NAME[N_INPUTS];
#define IN_LOADCELL 8 // virtual input: load cell force above lc_trigger (src/loadcell.h), for PROBE z=

// ---- nozzle rack sensors: one byte from the tool changer board on the TFT header, see toolbus.h
#define N_NOZZLE_INPUTS 8

// ---- outputs (low-side MOSFETs, HIGH = on)
// Fan headers with their voltage jumper on 12 V. No part fan / aux any more (10 Nov 2026: not a 3D printer).
#define N_OUTPUTS 7
#define OUT_PUMP 0    // FAN5 PD15 vacuum pump (12 V, 400 mA max; voltage jumper on VIN; was HE0 until 13 Nov 2026)
#define OUT_VALVE 1   // HE1 PA3
#define OUT_LIGHT_B 2 // FAN1 PE5  bottom-camera light (a plain 12 V ring; the NeoPixel ring is on the RGB header)
#define OUT_LIGHT_T 3 // FAN0 PA8  top-camera light (12 V LEDs)
#define OUT_CAM_FAN 4 // FAN2 PD12 head camera fan: always on at cam_fan %
#define OUT_BOX_FAN 5 // FAN3 PD13 housing fan (control box rear wall): always on at box_fan %
#define OUT_PI_FAN 6  // FAN4 PD14 Raspberry Pi housing fan (30 mm, 12 V, on the MDF): always on at pi_fan %
extern const char *const OUTPUT_NAME[N_OUTPUTS];

#ifndef PAP_NATIVE
#include <Arduino.h>

struct DriverPins
{
    PinName en, step, dir, uart;
};
// EN is active LOW. UART = the slot's CS/UART pin (PDN_UART with the UART jumper).
static constexpr DriverPins DRIVER_PINS[N_AXIS] = {
    {PF_14, PF_13, PF_12, PC_4},  // X:  DRV0
    {PE_0, PE_6, PA_14, PD_3},    // Y:  DRV7 (PA14 = SWCLK: SWD is gone once this runs)
    {PG_2, PF_9, PF_10, PF_2},    // Z:     DRV4
    {PA_0, PG_4, PC_1, PC_7},     // ROT:   DRV3
    {PG_5, PF_11, PG_3, PC_6},    // PASTE: DRV2
    {PF_1, PC_13, PF_0, PE_4},    // DRV5
    {PD_4, PE_2, PE_3, PE_1},     // DRV6
    {PF_15, PG_0, PG_1, PD_11},   // X2: DRV1
};
static constexpr PinName INPUT_PINS[N_INPUTS] = {PG_6, PG_9, PG_10, PG_11, PG_12, PG_13, PG_14, PG_15};
static constexpr PinName OUTPUT_PINS[N_OUTPUTS] = {PD_15, PA_3, PE_5, PA_8, PD_12, PD_13, PD_14};
// Unused MOSFET outputs, held LOW: BED PA1, HE0 PA2, HE2 PB10, HE3 PB11
static constexpr PinName UNUSED_MOSFETS[] = {PA_1, PA_2, PB_10, PB_11};
#endif
