# pap-motion: Octopus firmware

Motion firmware for the pick & place machine on a **BTT Octopus V1.1** (STM32F407ZET6 or F446, same pins) with
8 × TMC2209 in UART mode. The Raspberry Pi 5 sends commands over USB; this board only moves, homes, reads
switches and switches outputs. Design: [../docs/superpowers/specs/2026-10-24-pi5-octopus-design.md](../docs/superpowers/specs/2026-10-24-pi5-octopus-design.md),
pinout poster: [../board/octopus/octopus-v1.1-pinout.svg](../board/octopus/octopus-v1.1-pinout.svg). What plugs in where, pin by pin: [../pi/hardware/octopus-wiring.png](../pi/hardware/octopus-wiring.png).

## Build and flash

```
pio run                     # F446 (this machine's board, default) -> .pio/build/octopus_f446/firmware.bin (~120 KB of 480 KB)
pio run -e octopus          # F407 boards -> .pio/build/octopus/firmware.bin
pio test -e native          # unit tests on the PC (needs a gcc in PATH, e.g. C:/Strawberry/c/bin)
```

**Which chip?** Read it off the big chip: STM32F446ZET6 → `octopus_f446`, STM32F407ZET6 → `octopus`. The F407 build
runs on an F446 but never shows up on USB. Both are linked for 0x08008000 behind the BTT bootloader
(`board_build.flash_offset`; the old `board_build.offset` was ignored by the STM32 core and nothing started).

**DFU (BOOT0 jumper):** take the microSD card out first. The ROM bootloader also listens on USART3 = PC10/PC11,
which are the card's SDIO D2/D3: with a card in, it picks that port and USB DFU never appears. Windows needs the
WinUSB driver for "STM32 BOOTLOADER" once (Zadig), then `pio run -t upload`.

**BOOTLOG.TXT:** with a card in the slot, every start appends reset cause, clocks, the setup steps and the USB
state after 1 / 3 / 10 / 30 s ([src/bootlog.h](src/bootlog.h)); a HardFault is reported at the next start (reset button).

Flashing with the BTT bootloader: copy `firmware.bin` to the root of a FAT32 microSD card, insert it into the
Octopus, power on. After ~10 s the file is renamed to `FIRMWARE.CUR`: done. (DFU also works: BOOT0 jumper,
USB-C, `pio run -e octopus -t upload`; that overwrites the bootloader region only if you change the offset.)

From the Pi, without jumper or card: `pap-core --flash` ([../pi/README.md](../pi/README.md) step 6). The `DFU`
command restarts the board into the STM32 ROM USB bootloader (0483:df11, [src/dfu.h](src/dfu.h)); the BTT bootloader
stays. That needs a firmware that already has the command, so the first one goes on by card.

The board shows up on the Pi as `/dev/ttyACM0` (USB CDC, VID 0483 / PID 5740, product "pap-motion").

**F446 board:** the pins are identical; change `board = black_f407ze` to a STM32F446ZE board (e.g. `nucleo_f446ze`)
and set PLLN = 180 in `src/clock.cpp` if you want 180 MHz (168 MHz works too).

## Board setup

| What | Where |
|---|---|
| X / second X / PASTE / ROT / Z / E0 / E1 / Y motors | MOTOR0 / 1 / 2_1 / 3 / 4 / 5 / 6 / 7 (Z and PASTE swapped 18 Nov 2026) |
| TMC2209 modules | DRV0 … DRV7, **one UART jumper** under every driver (lower left position) |
| Sensorless homing X (both motors) / gantry / Y | **DIAG jumpers 0, 1, 6 and 7** fitted (driver DIAG → DIAG0 / 1 / 6 / 7 input), nothing plugged into these four connectors. Defaults: `home_xy sensorless`, `e1_home sensorless`, `in_invert_0/1/6 1`, `tmc_sg_x/_y/_e1 80` (StallGuard needs StealthChop; tune `tmc_sg_*` if homing stops early or rams the end) |
| Seesaw flag | DIAG2, switch to GND (`z_home flag`, default) — the only homing switch |
| Feeder switch (SCAN) | DIAG4 |
| E-stop button | DIAG5 (normally open to GND; NC buttons: `SET in_invert_5 1`) |
| Nozzle rack sensors | Tool changer board ([../../ToolChanger](../../ToolChanger), CH32V003) on the **TFT header**: its TX to pin 2 (RX, PA10), GND pin 4, 5 V pin 5. One byte 5 times a second, one bit per sensor, 9600 8N1 ([src/toolbus.h](src/toolbus.h)). A value counts once it arrived twice in a row. `noz_ok=0` in `SENSORS` / `STATUS`: silent for 1 s, `noz` = 0. The Pi decides which bit is which slot and what "nozzle present" means. |
| Load cell (HX711 board, nozzle touch-off) | All on **EXP1**, one connector: DT = **EXP1 pin 3** (PE9), SCK = **EXP1 pin 4** (PE10), GND = EXP1 pin 9, VCC = **3.3 V** from a small LDO (MCP1700-3302 or AMS1117-3.3, 1 µF in and out) fed from **EXP1 pin 10** (5 V). The HX711 logic runs at its supply: 3.3 V matches the STM32. An LDO, not a buck converter: switching ripple is noise in the 24-bit reading. Tie the HX711 RATE pin to VCC for 80 samples/s. Optional: without it `PROBE z=` / `LOADCELL` answer "no load cell" |
| Vacuum pump (12 V, 400 mA max) | **FAN5** header (voltage jumper on **VIN** = the 12 V supply; the jumper's 12 V position runs ~1 V low): `pump_power` % (PWM). `pump_used 0` = no pump connected: PUMP answers ok and keeps the output off (`pump_used` in `STATUS`). Was HE0 until 13 Nov 2026 |
| Valve | HE1 (12 V, + and − switched) |
| Bottom camera light: NeoPixel / WS2812 ring | **RGB header** (5 V, DATA = PB0 via the board's 5 V buffer, GND); `neo_count` LEDs (24), colour `neo_r` / `neo_g` / `neo_b`. **FAN1** follows the same brightness (for a plain 12 V ring; voltage jumper on 12 V) |
| Top camera light | **FAN0** header (12 V LEDs, its voltage jumper on 12 V; was HE3 until 10 Nov 2026): on at `light_top_idle` % (100) from power-on, dark by itself while the bottom light is on |
| Head camera fan | **FAN2** (voltage jumper to the fan's voltage): always on at `cam_fan` % (50), also through an e-stop |
| Housing fan (control box rear wall) | **FAN3** (voltage jumper to the fan's voltage): always on at `box_fan` % (100), also through an e-stop |
| Raspberry Pi housing fan (30 mm, 12 V, `../models/pi-housing`) | **FAN4** (voltage jumper on 12 V): always on at `pi_fan` % (100), also through an e-stop |
| (no part fan / aux) | removed 10 Nov 2026: not a 3D printer. HE0, HE2, HE3 and BED are held off |
| Power | 12 V into MOTOR, BED and MAIN (MAIN is officially > 14.1 V: the 12 V rail runs ~1 V low) |

## Protocol (summary)

Text lines as in [../protocol.md](../protocol.md): every command answers records, then `ok` or `error ...`.

| Command | Effect |
|---|---|
| `INFO`, `HELP`, `STATUS`, `REPORT <ms>`, `PING`, `WAIT` | general; `!status` events every `<ms>` |
| `DFU` | everything off, `ok`, restart into the ROM USB bootloader for dfu-util (`pap-core --flash`) |
| `HOME [XY] [X] [Y] [Z] [E1] [ALL]` | without words: seesaw flag (if `z_home flag`), X / Y, gantry (if `e1_home` ≠ none); `X` / `Y` home one axis alone (testing) |
| `SGTEST <x\|y\|e1> [mm] [speed=] [cur=]` | StallGuard while the axis runs mm (50) away from home and back at the homing speed / current (`home_cur`, now used by X, Y and the gantry): `sgtest axis= n= min= avg= max= suggest=` (X: also x2). `suggest` = 35 % of the free-running minimum for `tmc_sg_*` (DIAG fires at SG_RESULT ≤ 2 × tmc_sg) |
| `MOVE x= y= z= e0= e1= rot= paste= s= speed=` | absolute, any axes; `JOG` the same relative |
| `E1 <mm>`, `E0 <deg>`, `ROTATE <deg>`, `PASTE <µl>` | gantry absolute, feeder relative, nozzle absolute, paste µl |
| `MOTOR <axis> <deg>` | motor-shaft test turn at `test_speed_<axis>` |
| `SETPOS <axis>=<v> …`, `POS` | define / read positions |
| `MOTORS ON\|OFF`, `HALT`, `ESTOP`, `CLEAR` | `ESTOP` always works, also during `HOME` / `PROBE` |
| `SDLS [folder]` | microSD in the Octopus slot (read only): `file name= size= dir=0\|1` … — refused while motion runs |
| `SDREAD <file> [offset]` | up to ~1 KB of the file as `\|<line>` lines, then `sdread offset=<next> size= eof=0\|1` |
| `SENSORS` | `sensors x_min= x2_min= z_flag= z_max= feeder= estop= e1_home= y_min= noz_ok= noz1= … noz8= lc= load=` (1 = hit; `x_min` / `y_min` / `e1_home` are the stall signals when homing sensorless; `nozN` = raw signal of nozzle slot N) |
| `PUMP`, `VALVE`, `LIGHT top\|bottom <0-100\|ON\|OFF>` (top also `IDLE`), `OUTPUTS` | outputs; the top light goes dark while the bottom light is on. The NeoPixel ring is written only while no motion runs |
| `GET [key]`, `SET key value` / `SET k=v …`, `DEFAULTS` | settings (the Pi sends them on connect) |
| `PROBE e1=<mm> [speed=]` | gantry until the feeder switch closes: `probe hit=1 e1=…` |
| `PROBE z=<z> [speed=]` | seesaw towards `z` until the load cell feels `lc_trigger` grams (tared at the start): `probe hit=1 z=… g=<peak>` |
| `LOADCELL [TARE \| CAL <grams>]` | `loadcell ok= g= raw= tare= scale= rate=`; `CAL`: TARE first, put the weight on, sets `lc_scale` |
| `TMC [INIT] [axis]`, `TMCREG <axis> <reg> [value]` | driver status / raw registers |

Safety: link watchdog (`watchdog_ms`, default 2000: no line from the Pi while moving or with pump / valve on →
stop, outputs off, `!alarm text="link lost"`), e-stop input (`estop_input`, default DIAG5), `CLEAR` after alarms.

### Two X motors

X has a motor and a driver on each side beam: DRV0 / MOTOR0 (`x`) and DRV1 / MOTOR1 (`x2`). The planner only knows X; the step
interrupt copies every X step and direction to DRV1 ([src/motion/homing_gate.h](src/motion/homing_gate.h)). So `x2` takes no
`MOVE`, `JOG`, `SETPOS` or `MOTOR` commands. Only `invert_x2`, `tmc_cur_x2`, `tmc_hold_x2`, `tmc_stealth_x2` and `tmc_sg_x2` do
anything; steps, speed, acceleration and microsteps are those of X.

`HOME` runs X, then Y. Both X motors drive towards the stops at `home_cur` % current, stalls are ignored for the first
`home_blank` mm, and each side stops on its own DIAG, so the gantry ends square against the two stops. If one side runs more than
`home_skew_max` mm on alone, both stop and homing fails. `x_skew` in `STATUS` is how far apart they stopped. `x2_offset` moves
X2 alone after the pull-off, to square the gantry if the stops are not.

First start: **take one X belt off**, or the motors fight if `invert_x2` is wrong (default 1 since 18 Nov 2026: the motors face each other). `MOTOR x 90` must turn both so they would push the
gantry the same way, else flip `invert_x2`. Then with both belts on, set `tmc_sg_x` and `tmc_sg_x2` one at a time: higher =
more sensitive. Watch `sg=` in `TMC x` / `TMC x2` while moving, and `x_min=` / `x2_min=` in `SENSORS`. Too high stops early, too low
leaves the motor grinding on the stop and homing fails on `home_skew_max`.

Settings (see `src/settings.h`): per axis `steps_ speed_ accel_ invert_ test_speed_ tmc_cur_ tmc_hold_ tmc_ms_
tmc_stealth_ tmc_sg_` + `x y z rot paste e0 e1 x2`; global `travel_x/y/e1, home_xy (none|switch|sensorless),
home_speed, home_slow, home_pulloff, home_cur, home_blank, home_skew_max, x2_offset, z_home (none|flag), z_travel, z_flag_*, e1_home (none|switch|sensorless),
e1_home_dir, e1_home_speed, e1_home_pulloff, estop_input, watchdog_ms, pump_power, syringe_d, paste_speed,
rot_speed, e0_speed, e1_speed, jog_speed, junction_dev, in_invert_0..7, light_top_idle, neo_*, lc_scale,
lc_trigger`.

## Not tested on hardware yet

Built and unit-tested on the PC only. First power-up checklist: `INFO`, `SENSORS` (press every switch), `TMC`
(every driver `ok=1 configured=1`), `MOTOR x 90` per axis (direction → `invert_`), then `HOME`.
