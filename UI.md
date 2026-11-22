# User interface

The machine can be run from two places at once: the **3.5" touch display with a joystick** on the machine,
and the **website** in any browser on the same network (`http://pickandplace.local`). Both use the same
settings and show the same state, so a change made on one appears on the other straight away.

Back to the [README](README.md) · setup and building: [SOFTWARE.md](SOFTWARE.md)

- [Display](#display)
  - [Controls](#controls)
  - [Home screen](#home-screen)
  - [Menu](#menu)
  - [Screen by screen](#screen-by-screen)
  - [Display preferences](#display-preferences)
- [Website](#website)
- [Settings reference](#settings-reference)

---

## Display

ILI9488 display with XPT2046 touch, drawn at 480 × 320, plus a hall-effect joystick with a push button.

### Controls

| Input | Action |
|---|---|
| Tap | Press a button, open a row |
| Drag | Scroll a list |
| Joystick up / down | Move the focus |
| Joystick left | Back |
| Joystick button | Press the focused item |
| Hold the joystick button | Back (hold time: `joy_hold_ms`) |
| Joystick on Move / Teach / Cameras | Jogs X / Y in proportion to how far you push; the button switches to Z / E1 |

### Home screen

The title bar shows the IP address. Below it are a status chip (`IDLE`, `RUNNING`, `PAUSED`, `AUTO DETECT`,
`MOVING`, `ALARM`) and pills for `HOMED` and the pump (`PUMP 80%`, `PUMP OFF`, `NO PUMP`). If the machine is
not homed, a warning with a **Home** button appears.

| Tile | Opens |
|---|---|
| **Jobs** (or **Job** while one is running) | Job list, or the running job with pause / skip / stop |
| **Feeders** | Feeder list, auto detect, pockets |
| **Move** | Jog and home |
| **Menu** | Everything else (see below) |

If the motion board raises an alarm, an alarm screen with **Clear alarm** and **Home all** takes over. If the
Octopus doesn't answer, a "Controller not answering" screen offers **WiFi** and **Retry**.

### Menu

```
Menu
├── Home all                 X + Y, then Z, then E1
├── Cameras                  live picture, measure, lights
├── Nozzles                  rack, change, names
├── Calibration              cameras, vacuum, joystick
├── Pump, valve & fans       switch every output by hand
├── Testing                  outputs, sensors, motors
├── Settings                 every setting, searchable
├── Network & website        WiFi, website password
└── Motors off               releases all motors (the machine is then not homed)
```

### Screen by screen

#### Jobs
List of the jobs stored on the Pi (`~/pap-data/jobs`, uploaded via the website). Pick one to **Run** or
**Delete** it. While a job runs: the current line, **Skip** (the current part), **Stop**, and **Run again**
at the end.

#### Feeders
- **Auto detect**: homes E1, drives the feeder gantry along the rail, stops at every feeder sensor and reads
  the feeder's QR code with the top camera.
- **Add feeder** by hand. For each feeder:
  - its position on the rail (E1)
  - **Advance**: teeth of the feed wheel per advance
  - **Part ID** from the QR code (tap to read it now)
  - **Go to feeder**, **Test advance**, **Delete feeder**
- **Pockets**: **Teach new pocket** (jog the nozzle there and save), **Move here**, **Teach again**, **Pick this
  one next**, **Delete pocket**.

#### Move
Buttons for X± / Y± / E1±, **Home**, **E1 = 0**, and **Save here** when the screen was opened to teach a
position. The joystick jogs as well.

#### Nozzles
- Which nozzle is on the head, **Put the nozzle back**, and change to any slot
- **Teach slot 1** and **Teach the last slot**: the slots in between are spaced evenly
- **Push-on / park height** (seesaw Z)
- **Nozzle sensor order**: trigger the slots one after another so each sensor bit gets its slot
- **Touch off on the load cell**

If `nozzle_count` is 0, the screen says *No nozzle rack* and points to the setting.

#### Cameras
Live picture of the top or bottom camera, light on / off, **measure the part** (bottom camera), **find a
mark** (top camera), and **save a picture**. The joystick moves the head. Anything the camera finds is outlined
in green for 4 s: a circle around a fiducial or the nozzle tip, a rotated box around the part, a box around a
tape pocket, the outline of a QR code.

#### Calibration
| Step | What you do |
|---|---|
| Bottom camera | mm per pixel: nozzle tip over the camera |
| Top camera | mm per pixel: camera centred above a mark |
| Top camera offset 1/2 | put the nozzle tip exactly on a mark |
| Top camera offset 2/2 | move the camera roughly above that mark |
| Rotation direction | a part on the nozzle over the bottom camera |
| Vacuum sensor | **Baseline** (pump off), **Nozzle open**, **Nozzle blocked** → pick threshold |
| Joystick | **Centre**, **Range (5 s)**, test |
| Nozzle sensor order | trigger the slots in order |
| Sensorless homing | measure, set the stall threshold, test |
| All values | jumps to `cam_*`, `vac_*`, `joy_*`, `vision_*` in Settings |

#### Sensorless homing (from Calibration or Testing)
Choose an axis (tap = next axis), **Jog −10 mm** / **Jog +10 mm** away from home, and watch the StallGuard
value while it runs free. **Use the suggestion** sets the threshold to 35 % of the lowest free-running value.
You can also set the threshold for X2 and the **Homing current** (`home_cur`), then **Test: home this axis**.
**Stop** halts the motors at once.

#### Pump, valve & fans
Switches for the vacuum pump (with **Pump power** and **Pump connected**), the valve (Octopus HE1), the head
camera fan (FAN2), the housing fan (FAN3), the Pi housing fan (FAN4), the top camera light (FAN0) and the
bottom camera light (NeoPixel ring). **All off** switches everything off.

#### Testing
- **Outputs**: as above.
- **Sensors**, all live:
  - Z flag, E-stop and feeder sensor
  - StallGuard values of X, X2, Y and the gantry
  - force on the load cell pad (HX711)
  - vacuum (BMP280: hPa, drop, part on the nozzle)
  - every nozzle slot sensor
  - stick X / Y and the stick button (GPIO17)
- **Read a QR code** wherever the top camera is looking now.
- **Test motors**: turns X, Y, Z, E0, E1, ROT or PASTE by a set number of degrees (`test_speed_<ax>`).
- **Homing**: homes X, Y, Z or the gantry on its own.
- **Touch**: **Calibrate** (tap 4 crosses), **Touch test** (where it sees the finger, and how hard),
  **Minimum pressure**.

#### Settings
A search box at the top (by name, key or description), then every setting in groups. Numbers open the number
editor, switches toggle, and choices open a list. Below the groups:

| Row | What |
|---|---|
| Display | **Brightness**, **Screen off after** (no touch or joystick), **Screen off now**, **Turn screen 180°** (touch turns with it) |
| Touch | calibration and minimum pressure |
| Network & website | see below |
| Data folder | where the jobs and settings live |
| Motion board SD card | copy jobs from the Octopus microSD card to the Pi |
| Reload settings | after editing the files on a PC |

All settings are listed in the [settings reference](#settings-reference) below.

#### Network & website
**Networks** (scan and connect), **WPS** (press the router's WPS button), **Other network …** for a hidden
network, **Forget network**, **New website password** (logs every browser out) and **Restart the Pi**. The
current website password is shown here too.

### Display preferences

Touch calibration, brightness, rotation and the screen-off time belong to the display, not to the machine.
They are stored in `~/pap-data/ui.txt`.

---

## Website

Log in with the password shown on the display (Menu → Network & website, also in `~/pap-data/web.txt`).

| Page | What it does |
|---|---|
| **Dashboard** | State, position, homed / pump / alarm, the running job |
| **Jobs** | Upload, run, pause and stop jobs |
| **Feeders** | Feeder list, auto detect, pockets, *Teach QR position* |
| **Move** | Jog buttons, home, and the live **Sensors** panel |
| **Nozzles** | Rack, change nozzles, slot positions |
| **Cameras** | Live top / bottom camera with the green outlines, lights, measure, save pictures |
| **Calibrate** | Every calibration in one page, in the order to do it (below) |
| **Display** | Live mirror of the machine display; clicks count as touches |
| **Settings** | The same settings as the display, with a search box |
| **Console** | Type protocol commands and see the raw replies |

**Calibrate** shows the live camera, jog buttons and the position on the left. On the right are the sections,
in this order:

1. Axis steps per mm (move, measure, correct)
2. Head reach and paste tip offset
3. Bottom camera
4. Top camera and its offset
5. Load cell (tare, force, pad position and height, touch off)
6. Nozzle rack positions and heights
7. Vacuum sensor
8. Paste extruder (test dot)
9. Feeder QR position and reject position
10. Joystick

Each step either measures something or takes over the current position. The settings that a section changes
are listed under it and can be edited there.

---

## Settings reference

All settings as they appear on the display and the website, in the same groups and order. The list is made
from `core/ctrl/settings/catalog.cpp`; new settings are added there.

- **Stored on Pi**: saved on the Raspberry Pi in `~/pap-data`.
- **Stored on Octopus**: saved on the motion board and sent to it when changed.

### Job & placing

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Travel speed | `travel_speed` | mm/s | 100 | 0.1 … 100000 | Moves between parts when the job sets no speed | Pi |
| Pick height | `pick_height` | mm | 2 | -50 … 400 | Nozzle tip above the bed at a pocket | Pi |
| Pick wait | `pick_ms` | ms | 150 | 0 … 60000 | After the valve opens, before lifting | Pi |
| Place wait | `place_ms` | ms | 100 | 0 … 60000 | After the valve closes, before lifting | Pi |
| Paste head reach | `head1_reach` | mm | 10 | 0 … 200 | Seesaw travel from level until the paste tip touches the bed | Pi |
| Nozzle reach | `head2_reach` | mm | 10 | 0 … 200 | Seesaw travel from level until the nozzle touches the bed | Pi |
| Paste tip offset X | `head1_offset_x` | mm | 0 | -200 … 200 | Paste tip relative to the nozzle | Pi |
| Paste tip offset Y | `head1_offset_y` | mm | 0 | -200 … 200 | Paste tip relative to the nozzle | Pi |
| Drop point X | `reject_x` | mm | -1 | -1 … 1000 | Where skipped parts are dropped; -1 = where they are | Pi |
| Drop point Y | `reject_y` | mm | -1 | -1 … 1000 | Where skipped parts are dropped; -1 = where they are | Pi |

### Paste

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Paste output | `paste_output` |  | motor | none / motor | What pushes the paste: the extruder motor, or nothing (no dispenser fitted) | Pi |
| Syringe inner diameter | `syringe_d` | mm | 10 | 0.1 … 100 | Turns microlitres into plunger travel | Octopus |
| Paste speed | `paste_speed` | ul/s | 1 | 0.001 … 1000 | While a dot is pushed out | Octopus |
| Pull back after a dot | `paste_retract` | ul | 0.2 | 0 … 100 | Against oozing | Pi |

### Vacuum & outputs

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Pump connected | `pump_used` |  | on | on / off | Off = no vacuum pump on FAN5: pump commands (jobs, buttons) keep the output off | Octopus |
| Pump power | `pump_power` | % | 100 | 0 … 100 | Pump output when switched on | Octopus |
| Check the vacuum after a pick | `vac_check` |  | on | on / off | Needs the BMP280 in the vacuum line | Pi |
| Pressure with the pump off | `vac_baseline` | hPa | 1013.25 | 300 … 1200 | Set by Calibration > Vacuum | Pi |
| Pressure drop with a part | `vac_pick_min` | hPa | 150 | 1 … 900 | Below this the pick counts as failed | Pi |
| Pick retries | `vac_retries` |  | 1 | 0 … 5 | Pick again this often before skipping the part | Pi |
| Wait before checking | `vac_wait_ms` | ms | 150 | 0 … 5000 | After the valve opens | Pi |
| Head camera fan | `cam_fan` | % | 50 | 0 … 100 | Octopus FAN2, always on (also after an e-stop) | Octopus |
| Housing fan | `box_fan` | % | 100 | 0 … 100 | Octopus FAN3, rear fan of the control box, always on | Octopus |
| Pi housing fan | `pi_fan` | % | 100 | 0 … 100 | Octopus FAN4, 30 mm fan of the Raspberry Pi housing, always on | Octopus |

### Nozzle changer

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Slots in the rack | `nozzle_count` |  | 5 | 0 … 8 | 0 = no changer, max 8 | Pi |
| Slot 1 X | `nozzle_first_x` | mm | 0 | -1000 … 2000 | Machine position of the first slot | Pi |
| Slot 1 Y | `nozzle_first_y` | mm | 0 | -1000 … 2000 | Machine position of the first slot | Pi |
| Last slot X | `nozzle_last_x` | mm | 0 | -1000 … 2000 | The slots between are spaced evenly | Pi |
| Last slot Y | `nozzle_last_y` | mm | 0 | -1000 … 2000 | The slots between are spaced evenly | Pi |
| Push-on height | `nozzle_z_pick` | mm | -8 | -50 … 50 | Seesaw Z that pushes a nozzle on (negative = nozzle side down) | Pi |
| Park height | `nozzle_z_drop` | mm | -8 | -50 … 50 | Seesaw Z at which a nozzle sits in its slot | Pi |
| Travel height at the rack | `nozzle_z_safe` | mm | 0 | -50 … 50 | Seesaw Z for moving to and from the rack | Pi |
| Slide X | `nozzle_slide_x` | mm | 0 | -50 … 50 | Sideways slide into / out of a slot; 0 = straight down and up | Pi |
| Slide Y | `nozzle_slide_y` | mm | 0 | -50 … 50 | Sideways slide into / out of a slot | Pi |
| Speed at the rack | `nozzle_speed` | mm/s | 5 | 0.5 … 100 | Moves into and out of the slots | Pi |
| Pause while pushed on | `nozzle_wait_ms` | ms | 200 | 0 … 5000 |  | Pi |
| Check with the slot sensors | `nozzle_check` |  | on | on / off | Every step of a change is checked | Pi |
| Sensor bit 0 = nozzle present | `nozzle_sensor_invert` |  | off | on / off | Turn on if the sensors read the other way | Pi |
| Sensor bit of slot 1 … 8 | `nozzle_bit_1 … nozzle_bit_8` |  | 0 … 7 | 0 … 7 | Set by Calibration > Nozzle sensor order | Pi |
| Measure the nozzle after a change | `nozzle_calibrate` |  | on | on / off | Bottom camera finds the tip and stores its offset | Pi |
| Touch off after a change | `nozzle_touchoff` |  | on | on / off | Load cell measures the tip height and stores it (needs a load cell) | Pi |

### Load cell

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Load cell fitted | `loadcell` |  | off | on / off | HX711 on the Octopus EXP1: measures the nozzle tip height | Pi |
| Pad X | `loadcell_x` | mm | 0 | -1000 … 2000 | Machine position with the vacuum nozzle over the pad | Pi |
| Pad Y | `loadcell_y` | mm | 0 | -1000 … 2000 | Machine position with the vacuum nozzle over the pad | Pi |
| Pad height | `loadcell_height` | mm | 0 | -50 … 200 | Pad surface above the bed (same zero as the board) | Pi |
| First touch speed | `loadcell_speed` | mm/s | 3 | 0.1 … 50 | Seesaw speed for the first, rough touch | Pi |
| Second touch speed | `loadcell_slow` | mm/s | 0.3 | 0.02 … 10 | Slow, precise touch: this one is measured | Pi |
| Lift between touches | `loadcell_backoff` | mm | 0.5 | 0.1 … 10 |  | Pi |
| Search depth | `loadcell_depth` | mm | 4 | 0.5 … 30 | How far past the expected contact it searches | Pi |
| Trigger force | `lc_trigger` | g | 30 | 1 … 5000 | The seesaw stops at this force | Octopus |
| Scale | `lc_scale` | counts/g | 420 | -100000 … 100000 | Set by LOADCELL CAL <grams> (Sensors) | Octopus |

### Feeders

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Feed wheel teeth | `feeder_wheel_teeth` |  | 20 | 1 … 200 | Teeth of the feed wheel; feeders advance by whole teeth | Pi |
| Feed motor speed | `e0_speed` | °/s | 180 | 1 … 3600 | E0 while advancing a feeder | Octopus |
| Gantry speed | `e1_speed` | mm/s | 30 | 0.1 … 1000 | E1 in jobs | Octopus |
| Read QR codes in auto detect | `feeder_qr` |  | on | on / off | Top camera reads each feeder's code | Pi |
| QR position X at E1 = 0 | `feeder_qr_x0` | mm | 0 | -1000 … 2000 | Set by Teach QR position | Pi |
| QR position Y at E1 = 0 | `feeder_qr_y0` | mm | 0 | -1000 … 2000 | Set by Teach QR position | Pi |
| QR moves in X per mm of E1 | `feeder_qr_dx` |  | 1 | -10 … 10 | Rail along X: 1 | Pi |
| QR moves in Y per mm of E1 | `feeder_qr_dy` |  | 0 | -10 … 10 | Rail along X: 0 | Pi |

### Vision

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Correct parts with the bottom camera | `vision_parts` |  | on | on / off | Offset and rotation of every part | Pi |
| Measure board fiducials | `vision_fiducials` |  | off | on / off | At job start, from [board] fiducial = x, y | Pi |
| Correct pockets with the top camera | `vision_pockets` |  | off | on / off | Finds the pocket before every pick | Pi |
| Photo of every placed part | `vision_place_check` |  | off | on / off | Saved in captures/ | Pi |
| Pause when the camera finds nothing | `vision_required` |  | off | on / off | Off: the part is placed without correction | Pi |
| Largest accepted offset | `vision_max_offset` | mm | 2 | 0.01 … 50 | Parts further off are skipped | Pi |
| Largest accepted angle | `vision_max_angle` | ° | 20 | 0.1 … 90 | Parts turned further are skipped | Pi |
| Fiducial diameter | `fid_diameter` | mm | 1 | 0.1 … 20 |  | Pi |
| Fiducial search radius | `fid_search` | mm | 3 | 0.2 … 50 |  | Pi |
| Pocket search radius | `pocket_search` | mm | 2 | 0.2 … 50 |  | Pi |
| Calibration move | `cal_step` | mm | 3 | 0.1 … 50 | How far the head moves during a camera calibration | Pi |

### Bottom camera

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Device | `cam_bottom_dev` |  | /dev/video0 | text | /dev/video0 or better /dev/v4l/by-id/... | Pi |
| Picture width | `cam_bottom_w` | px | 1280 | 160 … 4096 |  | Pi |
| Picture height | `cam_bottom_h` | px | 720 | 120 … 4096 |  | Pi |
| Exposure | `cam_bottom_exposure` |  | -1 | -1 … 10000 | -1 = automatic | Pi |
| Detection threshold | `cam_bottom_thresh` |  | -1 | -1 … 255 | -1 = automatic | Pi |
| Dark parts on a bright background | `cam_bottom_invert` |  | off | on / off |  | Pi |
| Rotation direction | `cam_bottom_rot_sign` |  | 1 | -1 … 1 | +1 / -1, set by Calibration > Rotation direction | Pi |
| Light brightness | `light_bottom` | % | 100 | 0 … 100 | NeoPixel ring while the bottom camera looks | Pi |
| Camera position X | `cam_bottom_x` | mm | 0 | -1000 … 2000 | Head position with the nozzle over the camera | Pi |
| Camera position Y | `cam_bottom_y` | mm | 0 | -1000 … 2000 |  | Pi |
| Camera position Z | `cam_bottom_z` | mm | 0 | -200 … 200 | Seesaw Z for the picture | Pi |
| Rotation centre X | `cam_bottom_cx` | px | -1 | -1 … 8192 | -1 = picture centre (set by the calibration) | Pi |
| Rotation centre Y | `cam_bottom_cy` | px | -1 | -1 … 8192 |  | Pi |
| Pixel > mm matrix | `cam_bottom_m00 / m01 / m10 / m11` |  | 0.02 / 0 / 0 / 0.02 | -10 … 10 | Scale, rotation and skew (set by the calibration) | Pi |

### Top camera

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Device | `cam_top_dev` |  | /dev/video2 | text | /dev/video2 or better /dev/v4l/by-id/... | Pi |
| Picture width | `cam_top_w` | px | 1280 | 160 … 4096 |  | Pi |
| Picture height | `cam_top_h` | px | 720 | 120 … 4096 |  | Pi |
| Exposure | `cam_top_exposure` |  | -1 | -1 … 10000 | -1 = automatic | Pi |
| Detection threshold | `cam_top_thresh` |  | -1 | -1 … 255 | -1 = automatic | Pi |
| Light while looking | `light_top` | % | 100 | 0 … 100 | Top light during pictures | Pi |
| Camera offset X | `cam_top_off_x` | mm | 0 | -500 … 500 | Camera centre minus nozzle (Calibration > Top camera offset) | Pi |
| Camera offset Y | `cam_top_off_y` | mm | 0 | -500 … 500 |  | Pi |
| Settle time before a picture | `cam_settle_ms` | ms | 150 | 0 … 5000 |  | Pi |
| Pixel > mm matrix | `cam_top_m00 / m01 / m10 / m11` |  | 0.02 / 0 / 0 / 0.02 | -10 … 10 | Scale, rotation and skew (set by the calibration) | Pi |

### Lights

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Top light (always on) | `light_top_idle` | % | 100 | 0 … 100 | Goes dark by itself while the bottom light is on | Octopus |
| LEDs in the bottom ring | `neo_count` |  | 24 | 0 … 256 | NeoPixel ring on the Octopus RGB header | Octopus |
| Ring colour red | `neo_r` |  | 255 | 0 … 255 |  | Octopus |
| Ring colour green | `neo_g` |  | 255 | 0 … 255 |  | Octopus |
| Ring colour blue | `neo_b` |  | 255 | 0 … 255 |  | Octopus |

### Homing

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| X / Y homing | `home_xy` |  | sensorless | none / switch / sensorless | Sensorless (DIAG jumpers 0 / 1 / 7, tmc_sg_x / _x2 / _y), or switches on DIAG0 / DIAG1 / DIAG7 | Octopus |
| Homing speed | `home_speed` | mm/s | 40 | 0.1 … 500 | First, fast approach | Octopus |
| Homing slow speed | `home_slow` | mm/s | 3 | 0.1 … 100 | Second, precise approach | Octopus |
| Pull-off | `home_pulloff` | mm | 3 | 0 … 50 | Back off the switch after homing | Octopus |
| X homing current | `home_cur` | % | 60 | 10 … 100 | Of the X motor currents, while seeking a stall | Octopus |
| Ignore stalls at the start | `home_blank` | mm | 2 | 0 … 50 | Sensorless homing: while the motors accelerate | Octopus |
| Max X skew | `home_skew_max` | mm | 2 | 0.1 … 50 | Homing fails if one X side runs on this far alone | Octopus |
| X2 trim | `x2_offset` | mm | 0 | -3 … 3 | Second X motor moves this far alone after homing, to square the gantry | Octopus |
| Seesaw homing | `z_home` |  | flag | none / flag | Half flag on DIAG2, or level by hand | Octopus |
| Seesaw travel | `z_travel` | mm | 40 | 1 … 500 | Search range for the flag | Octopus |
| Flag search speed | `z_flag_speed` | mm/s | 4 | 0.1 … 100 |  | Octopus |
| Flag slow speed | `z_flag_slow` | mm/s | 0.5 | 0.05 … 50 |  | Octopus |
| Flag back-off | `z_flag_backoff` | mm | 2 | 0.1 … 50 |  | Octopus |
| Gantry homing | `e1_home` |  | sensorless | none / switch / sensorless | Sensorless (DIAG jumper 6, tmc_sg_e1), or a switch on DIAG6 | Octopus |
| Gantry homing direction | `e1_home_dir` |  | -1 | -1 … 1 | -1 = towards E1 min, +1 = towards max | Octopus |
| Gantry homing speed | `e1_home_speed` | mm/s | 20 | 0.1 … 500 |  | Octopus |
| Gantry pull-off | `e1_home_pulloff` | mm | 2 | 0 … 50 |  | Octopus |

### Machine size & jogging

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| X travel | `travel_x` | mm | 300 | 1 … 5000 | Moves beyond are refused | Octopus |
| Y travel | `travel_y` | mm | 300 | 1 … 5000 |  | Octopus |
| Gantry travel | `travel_e1` | mm | 500 | 1 … 5000 | Also the length auto detect searches | Octopus |
| Jog speed | `jog_speed` | mm/s | 30 | 0.1 … 1000 | Default for JOG and the website | Octopus |
| Nozzle rotation speed | `rot_speed` | °/s | 180 | 1 … 3600 |  | Octopus |
| Cornering | `junction_dev` | mm | 0.02 | 0.001 … 1 | Junction deviation: higher = faster corners | Octopus |

### Joystick

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Jog speed at full deflection | `joy_jog_speed` | mm/s | 50 | 0.1 … 500 | Move / Teach / Cameras screens | Pi |
| Dead zone | `joy_deadzone` | % | 8 | 0 … 60 | Around the centre | Pi |
| Button hold time | `joy_hold_ms` | ms | 800 | 200 … 5000 | Held this long = back | Pi |
| Reverse X | `joy_invert_x` |  | off | on / off |  | Pi |
| Reverse Y | `joy_invert_y` |  | off | on / off |  | Pi |
| Swap X and Y | `joy_swap` |  | off | on / off |  | Pi |
| Raw X / Y min, centre, max | `joy_x_min … joy_y_max` |  | – | 0 … 32767 | From Calibration > Joystick | Pi |

### Display & sound

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| Beep | `beep` |  | on | on / off | Display and job beeps | Pi |
| Display brightness | `backlight` | % | 100 | 5 … 100 |  | Pi |

### Safety & inputs

| Setting | Key | Unit | Default | Range | What it does | Stored on |
|---|---|---|---|---|---|---|
| E-stop input | `estop_input` |  | 5 | -1 … 7 | DIAG input of the e-stop button, -1 = none | Octopus |
| Link watchdog | `watchdog_ms` | ms | 2000 | 0 … 60000 | A move stops when the Pi is silent this long (pump and valve stay on; 0 = off) | Octopus |
| Invert DIAG0 (X stall) | `in_invert_0` |  | on | on / off | Switch to GND = hit; invert for NC switches / StallGuard | Octopus |
| Invert DIAG1 (X2 stall) | `in_invert_1` |  | on | on / off | Switch to GND = hit; invert for NC switches / StallGuard | Octopus |
| Invert DIAG2 (Z flag) | `in_invert_2` |  | off | on / off | Switch to GND = hit; invert for NC switches / StallGuard | Octopus |
| Invert DIAG3 (Z max) | `in_invert_3` |  | off | on / off | Switch to GND = hit; invert for NC switches / StallGuard | Octopus |
| Invert DIAG4 (Feeder sensor) | `in_invert_4` |  | off | on / off | Switch to GND = hit; invert for NC switches / StallGuard | Octopus |
| Invert DIAG5 (E-stop) | `in_invert_5` |  | off | on / off | Switch to GND = hit; invert for NC switches / StallGuard | Octopus |
| Invert DIAG6 (Gantry stall) | `in_invert_6` |  | on | on / off | Switch to GND = hit; invert for NC switches / StallGuard | Octopus |
| Invert DIAG7 (Y stall) | `in_invert_7` |  | on | on / off | Switch to GND = hit; invert for NC switches / StallGuard | Octopus |

### Axes

One group per motor on the display (`Axis X`, `Axis Y`, …). All of them are stored on the Octopus. Replace `<ax>` in the key with `x`, `y`, `z`, `rot`, `paste`, `e0`, `e1` or `x2`.

| Setting | Key | X | Y | Z (seesaw) | ROT (nozzle) | PASTE | E0 (feeder drive) | E1 (gantry) | Range |
|---|---|---|---|---|---|---|---|---|---|
| Steps per unit | `steps_<ax>` | 80 /mm | 80 /mm | 400 /mm | 8.889 /° | 400 /mm | 8.889 /° | 80 /mm | 0.001 … 100000 |
| Top speed | `speed_<ax>` | 150 mm/s | 150 mm/s | 5 mm/s | 360 °/s | 10 mm/s | 360 °/s | 50 mm/s | 0.001 … 100000 |
| Acceleration | `accel_<ax>` | 500 mm/s² | 500 mm/s² | 100 mm/s² | 1440 °/s² | 50 mm/s² | 1440 °/s² | 500 mm/s² | 0.001 … 1000000 |
| Reverse direction | `invert_<ax>` | off | off | off | off | off | off | off | on / off |
| Motor test speed | `test_speed_<ax>` | 180 °/s | 180 °/s | 180 °/s | 180 °/s | 180 °/s | 180 °/s | 180 °/s | 1 … 3600 °/s |
| Motor current (RMS) | `tmc_cur_<ax>` | 600 mA | 600 mA | 600 mA | 600 mA | 600 mA | 600 mA | 600 mA | 50 … 2000 mA |
| Hold current | `tmc_hold_<ax>` | 50 % | 50 % | 50 % | 50 % | 50 % | 50 % | 50 % | 0 … 100 % of run current |
| Microsteps | `tmc_ms_<ax>` | 16 | 16 | 16 | 16 | 16 | 16 | 16 | 1, 2, 4 … 256 |
| StealthChop (quiet) | `tmc_stealth_<ax>` | on | on | on | on | on | on | on | on / off (off = SpreadCycle, more torque) |
| StallGuard threshold | `tmc_sg_<ax>` | 80 | 80 | 0 | 0 | 0 | 0 | 80 | 0 … 255 (higher = more sensitive) |

**X2 (second X motor)** follows X: it takes X's steps, speed, acceleration and microsteps. It only has its own `invert_x2` (default **on**, so both X motors push the gantry the same way), `tmc_cur_x2`, `tmc_hold_x2`, `tmc_stealth_x2` and `tmc_sg_x2` (default 80).

