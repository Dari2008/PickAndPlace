# Software: Raspberry Pi 5 + BTT Octopus V1.1

The Pi 5 is the main controller: jobs, feeders, settings, the 3.5" display with touch and joystick, the
vacuum sensor, two USB cameras and the website. The Octopus (own firmware, [../octopus-fw](../octopus-fw))
only moves the 8 motors, reads the endstops and switches the 12 V outputs. They talk over USB with the same
text protocol as before ([../protocol.md](../protocol.md)).

Design: [../docs/superpowers/specs/2026-10-24-pi5-octopus-design.md](../docs/superpowers/specs/2026-10-24-pi5-octopus-design.md) ·
Wiring: [hardware/wiring.svg](hardware/wiring.svg) (made by `python hardware/wiring.py hardware/wiring.svg`), Octopus connectors pin by pin: [hardware/octopus-wiring.png](hardware/octopus-wiring.png) (`hardware/octopus_wiring.py`)

```
 browser ──HTTP/WebSocket──> pap-web (TypeScript, pi/web) ──TCP 127.0.0.1:7070──┐
                                                                                 v
 display + touch + joystick <── UI thread ──── pap-core (C++, pi/core) ── controller thread: jobs, feeders,
 BMP280 / ADS1115 (I2C) ───────────────────────┘    │    settings, vision, calibration
 USB cameras (V4L2) ───────────────────────────────-┘    └── USB ──> Octopus (motion, endstops, outputs)
```

| Part | Folder | What |
|---|---|---|
| `pap-core` | `core/` | The ported ATmega job runner / feeders / settings (`ctrl/`), the ported ESP32 display UI (`ui/esp/`) plus the new screens, the Octopus link (`octo/`), cameras and vision without OpenCV (`vision/`), I2C / GPIO / SPI drivers (`hal/`, `dev/`), TCP command port (`api/`) |
| `pap-web` | `web/` | Website: the ESP32 website (`public/`) plus Cameras / Calibration and a display mirror; the server only logs in and passes protocol lines to pap-core |
| `pap-motion` | `../octopus-fw/` | Octopus firmware (PlatformIO) |

## Set up the Pi (Raspberry Pi OS Bookworm, 64-bit)

1. `sudo raspi-config` → Interface Options → enable **SPI** and **I2C**. Set the hostname to `pickandplace`
   (the website is then `http://pickandplace.local`).
2. Plug the carrier board ([../board/](../board/README.md): ADS1115, buzzer, connectors) onto the Pi, or wire the
   same nets by hand as in [hardware/wiring.svg](hardware/wiring.svg). The Pi has its own 5 V / 5 A supply on USB-C
   (with less than 5 A, add `usb_max_current_enable=1` to `/boot/firmware/config.txt`: the cameras need the USB current).
3. Packages: `sudo apt install build-essential nodejs npm`
4. Code to `/home/pi/pap` (this repository), then:
   ```
   cd ~/pap/pi/core && make -j4          # -> build/pap-core   (make NO_UI=1: without display)
   cd ~/pap/pi/web  && npm install && npm run build
   sudo cp ~/pap/pi/deploy/pap-*.service /etc/systemd/system/
   sudo systemctl enable --now pap-core pap-web
   ```
5. Flash the Octopus ([../octopus-fw/README.md](../octopus-fw/README.md)) and connect it to a USB 2 port.
   pap-core finds it by its USB name (`/dev/serial/by-id/*pap-motion*`), or give `--octopus /dev/ttyACM0`.
6. Firmware updates from the Pi afterwards (no jumper, no SD card): `upload.bat` copies the built
   `octopus-fw/.pio/build/octopus/firmware.bin` to `~/pap/firmware/pap-motion.bin`, then
   `sudo systemctl stop pap-core && ~/pap/core/build/pap-core --flash` (or `--flash other.bin`). It sends `DFU`
   (the board restarts into the STM32 ROM bootloader), runs dfu-util, waits for the board and then runs as usual.
   Once:
   ```
   sudo apt install dfu-util
   echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="df11", MODE="0666"' | sudo tee /etc/udev/rules.d/49-stm32-dfu.rules
   sudo udevadm control --reload-rules
   ```
   The firmware on the board must already know `DFU` (from 18 Nov 2026 on): flash that one by microSD card once.

The website password is shown on the display (Menu → Network & website) and stored in `~/pap-data/web.txt`.

## Display and website preview on Windows

Double-click **`ui-preview.cmd`** (or `node pi/tools/ui-preview.cjs`). It builds pap-core in WSL (first time ~2 min),
runs it in simulation and opens two browser tabs:

- the display at http://127.0.0.1:8090: click = touch, drag = scroll, wheel / arrow keys / the on-screen stick =
  joystick, Enter = joystick button, Esc = back. Only display input is sent.
- the website at http://localhost:8080 (pi/web on Windows Node, against the same simulated machine). The login
  password is printed in the console window (also on the display: Settings → Network & website).

Nothing real is controlled. Needs Node.js on Windows and WSL with `build-essential`; the website needs
`cd pi/web && npm install && npm run build` once. Data lives in WSL `~/.pap-preview/` (the example jobs from
`../pap-syntax` are copied into its `jobs/`). Options: `--port`, `--web-port`, `--no-web`, `--no-browser`.

## Running and testing without the machine

```
cd pi/core && make -j8 && make test                     # 104 checks, simulated Octopus + cameras
./build/pap-core --sim --data /tmp/pap --console        # type protocol commands; display renders into memory
cd pi/web && npm install && npm test                    # 6 tests (fake core)
PAP_DATA=/tmp/pap PAP_WEB_PORT=8080 node dist/server.js # website against the simulator: http://localhost:8080
```

`--sim` simulates the Octopus (moves, homing, feeder switch), both cameras (a part on the nozzle, a fiducial)
and the sensors. `PAP_SIM_SPEED=25` runs moves 25 × faster. The website's **Display** page mirrors the screen
(also works with the real display) and accepts clicks as touches.

pap-core options: `--data <dir>` (default `~/pap-data`), `--octopus <tty|sim>`, `--sim`, `--no-display`,
`--api-port 7070`, `--api-bind 127.0.0.1`, `--console`.

## Display

Same screens as the ESP32 (drawn at 480 × 320), plus:

- **Settings**: search on top, then every setting of the Pi and the Octopus in 22 groups (job, paste, vacuum,
  nozzle changer, feeders, vision, cameras, lights, homing, one group per axis, joystick, safety …), each with
  its unit, range and a one-line description; numbers open the number editor, switches toggle, choices open a
  list. The website's Settings page shows the same list with a search box. The list lives in
  `core/ctrl/settings/catalog.cpp` (add new settings there).

- **Menu → Cameras**: live picture of the top / bottom camera, light on / off, measure the part (bottom) or find
  a mark (top), save a picture; the joystick moves the head. Whatever a camera finds is outlined in green in
  the live picture (display and website) for 4 s: a circle around a fiducial or the nozzle tip, a rotated
  rectangle around the part on the nozzle, a box around a tape pocket, the outline of a QR code. This works
  for detections from jobs, calibration and auto detect too. Saved pictures (`/captures`) stay without
  outlines.
- **Menu → Calibration**: bottom camera, top camera, top camera offset (2 steps), rotation direction, vacuum
  sensor, joystick (centre, range, invert).
- **Website → Calibrate**: everything in one tab, in the order to do it. On the left are the live camera
  picture, jog buttons and the position, with links to the sections. The sections are:
  1. axis steps per mm (move, measure, correct)
  2. head reach and paste tip offset
  3. bottom camera
  4. top camera and its offset
  5. load cell (tare, force, pad position and height, touch off)
  6. nozzle rack positions and heights
  7. vacuum sensor
  8. paste extruder (test dot)
  9. feeder QR position and reject position
  10. joystick

  Each step either measures something or takes over the current position. Under each section are the settings
  it changes, and they can be edited there.
- **Move / Teach**: the joystick jogs X / Y proportionally (Z / E1 when that axis is chosen with the button);
  elsewhere up / down moves the focus, left = back, button = press, hold = back.

Display preferences (touch calibration, backlight, rotation) are in `~/pap-data/ui.txt`.

## Jobs on the Pi, the Octopus SD card

Jobs are stored and run on the Pi (`~/pap-data/jobs`; upload them on the website). The microSD in the Octopus
slot can still be read: display Settings → *Motion board SD card*, or website Settings → *Motion board SD card*.
Pick a file and it is copied to `/jobs` on the Pi, then run from there (`OCTOSD LS` / `OCTOSD COPY`).

## Sensors

Menu → Testing → Sensors (and website Move → Sensors) shows everything live: Z flag, E-stop and feeder
sensor, the StallGuard stall signals of X, Y and the gantry (sensorless homing, no switches), the vacuum
(hPa, drop, part on the nozzle), every nozzle slot sensor and the joystick.

## Feeders: QR codes, feeder sensor, teeth

- **Finding feeders:** auto detect (display Feeders → Auto detect, website Feeders, `SCAN`) drives the feeder
  gantry (E1) along the rail. Each feeder's sensor (DIAG4) triggers at the centre of its pickup area: that E1
  position is the feeder's `pos`. Then the head moves the top camera over the feeder's **QR code** and reads it.
- **QR text:** `F07;name=100n 0402;part=C1525;teeth=2` (the first field is the feeder ID; the rest optional).
- **Where the codes are:** jog the top camera onto one feeder's code, then website Feeders → feeder → *Teach QR
  position* (`FEEDER QR TEACH <n>`). For a rail along X that's all (`feeder_qr_dx` = 1); otherwise set
  `feeder_qr_dx` / `_dy`. X / Y must be homed for the scan (the head carries the camera).
- **Advance:** the feed wheel has `feeder_wheel_teeth` = 20 teeth; set each feeder's *teeth per advance*
  (2 teeth = 36°). The old degree value still works when teeth = 0.

## Camera lights

- Bottom: NeoPixel ring on the Octopus RGB header (5 V / DATA / GND), `neo_count` LEDs.
- Top: Octopus FAN0 (12 V LEDs), always on (`light_top_idle`), off by itself whenever the bottom light is on.
- A plain 12 V bottom ring instead of the NeoPixels: FAN1, follows the bottom light.

## Vacuum pump

- 12 V pump (400 mA max) on Octopus FAN5, voltage jumper on VIN (the 12 V supply). `pump_power` % when on.
- **Pump connected** (`pump_used`, Settings → Vacuum & outputs, display: Pump, valve & fans): off = no pump
  fitted. Every pump command (job, buttons, website, vacuum calibration) then leaves FAN5 off and answers ok, so
  jobs run on with just the valve (e.g. with an outside vacuum source). The home screen shows NO PUMP, the website
  "not connected". Stored on the Octopus like the other output settings.

## Fans

- Head camera fan: Octopus FAN2, always on at `cam_fan` % (default 50, Settings → Vacuum & outputs).
- Housing fan (control box rear wall): FAN3, always on at `box_fan` % (default 100).
- Pi housing fan (30 mm, 12 V, in `../models/pi-housing`): FAN4 with its voltage jumper on 12 V, always on at
  `pi_fan` % (default 100).
- Both keep running through an e-stop. There is no part fan / aux output any more (not a 3D printer).

## Nozzle changer

A rack of push-on nozzles (`nozzle_count` slots, max 8). Each slot has a sensor; the tool changer board
([../../ToolChanger](../../ToolChanger)) sends them to the Octopus as one byte, 5 times a second, on one wire to the
TFT header (pin 2, plus GND pin 4 and 5 V pin 5). Set it up on the display (Menu → Nozzles) or the website (Nozzles):

1. Teach slot 1 and the last slot (jog the nozzle holder over them); the slots between are spaced evenly.
2. Push-on height (`nozzle_z_pick`) and park height (`nozzle_z_drop`): seesaw Z, negative = nozzle side down.
   If the slots need the nozzle slid in / out sideways, set `nozzle_slide_x` / `_y`.
3. Name the nozzles (`CN040`, …) and set a tip offset only if a nozzle is off-centre (default 0 / 0).
4. Sensor order: Calibration → Nozzle sensor order on the display, or Calibrate → Nozzle rack on the website. Trigger
   the slots in order, 1 first (nozzle in or out, or a finger on the sensor). Each new bit that changes goes to the
   next slot, and `nozzle_bit_n` is stored once every slot has its bit. Cancel leaves the old order. Then check
   `NOZZLES`: `present=1` for every slot with a nozzle; if it reads the other way, `SET nozzle_sensor_invert 1`.
5. Calibrate the bottom camera first (Calibration order below): after every pick-up the head goes over the
   bottom camera, finds the nozzle tip and stores its offset (`nozzle_calibrate` = 1).

Changing: put the mounted nozzle back into its slot (lower to the park height, lift: the slot keeps it, its
sensor must see it), then pick the new one up (sensor must see it first; lower to the push-on height, lift:
the slot must be empty). A check waits up to 1 s for the sensor byte to catch up. Any check that fails stops with
an error, and so does a silent tool changer board. Jobs: `nozzle = CN040` in `[job]` or per
`[part]`; the machine changes before a pick when the part needs another nozzle, and adds that nozzle's tip
offset to the pick and place positions.

## Load cell (nozzle tip height)

A load cell under a small pad, read by an HX711 board on the Octopus EXP1, one connector (DT = EXP1 pin 3, SCK = EXP1
pin 4, GND = EXP1 pin 9, VCC = 3.3 V from a small LDO fed from EXP1 pin 10 (5 V); RATE pin to VCC). The Octopus stops the seesaw itself the moment
the tip presses the pad (`PROBE z=`), so the stop does not wait for the Pi.

1. Settings → Load cell: `loadcell` on, `loadcell_x` / `_y` = machine position with the vacuum nozzle over the
   pad, `loadcell_height` = pad surface above the bed (same zero as the board surface).
2. Calibrate the force once: Console `LOADCELL TARE`, put a known weight on the pad, `LOADCELL CAL 100`
   (grams). Only the trigger force depends on it (`lc_trigger`, 30 g).
3. Nozzles → *Touch off on the load cell* (`NOZZLE TOUCH`): measures the nozzle on the head. Without a nozzle
   changer it sets `head2_reach`; with one it stores that nozzle's `dz` (tip height compared to `head2_reach`).
4. `nozzle_touchoff` = 1 (default): every nozzle change ends with a touch-off, after the bottom camera has
   centred the tip. Jobs use `head2_reach + dz` for every pick and place height.

## New settings (`GET` / `SET`, website → Settings)

| Key | Default | Meaning |
|---|---|---|
| `cam_bottom_dev`, `cam_top_dev` | `/dev/video0`, `/dev/video2` | camera devices (better: `/dev/v4l/by-id/...`) |
| `cam_*_w`, `cam_*_h`, `cam_*_exposure`, `cam_*_thresh` | 1280, 720, -1, -1 | size, exposure (-1 auto), threshold (-1 Otsu) |
| `cam_*_m00` `m01` `m10` `m11` | 0.02 / 0 / 0 / 0.02 | pixel → mm matrix: scale, rotation **and skew** (calibration) |
| `cam_bottom_cx`, `cam_bottom_cy` | -1 | nozzle centre in the bottom picture (calibration) |
| `cam_bottom_x` `_y` `_z` | 0 | where the nozzle is over the bottom camera |
| `cam_top_off_x`, `cam_top_off_y` | 0 | top camera relative to the nozzle |
| `cam_bottom_rot_sign`, `cam_bottom_invert`, `cam_settle_ms` | 1, 0, 150 | rotation direction, dark parts, settle time |
| `vac_baseline`, `vac_pick_min`, `vac_check`, `vac_retries`, `vac_wait_ms` | 1013.25, 150, 1, 1, 150 | vacuum sensor |
| `joy_x_min/_center/_max`, `joy_y_*` | 0 / 13200 / 26400 | joystick calibration (raw ADS1115) |
| `joy_deadzone`, `joy_invert_x/_y`, `joy_swap`, `joy_jog_speed`, `joy_hold_ms` | 8, 0, 0, 50, 800 | joystick behaviour |
| `vision_parts`, `vision_fiducials`, `vision_pockets`, `vision_place_check`, `vision_required` | 1, 0, 0, 0, 0 | what the cameras do in a job |
| `nozzle_count`, `nozzle_first_x/_y`, `nozzle_last_x/_y`, `nozzle_z_pick`, `nozzle_z_drop`, `nozzle_z_safe`, `nozzle_slide_x/_y`, `nozzle_speed`, `nozzle_wait_ms`, `nozzle_sensor_invert`, `nozzle_check` | 5, 0…, -8, -8, 0, 0, 5, 200, 0, 1 | nozzle changer (above, protocol.md) |
| `loadcell`, `loadcell_x/_y`, `loadcell_height`, `loadcell_speed`, `loadcell_slow`, `loadcell_backoff`, `loadcell_depth`, `nozzle_touchoff` | 0, 0, 0, 3, 0.3, 0.5, 4, 1 | load cell touch-off (above) |

Motion settings (`steps_x`, `tmc_cur_x`, …) are forwarded to the Octopus and stored in `~/pap-data/machine.txt`;
pap-core sends them again whenever the Octopus connects.

## Calibration order (display or website → Cameras)

1. Joystick: Centre (let go), then Range (circle the stick for 5 s).
2. Vacuum: Baseline with the pump off; pump on with the nozzle open, then blocked (sets `vac_pick_min`).
3. Bottom camera: nozzle tip in the bottom camera's picture, light on → **CAL BOTTOM** (scale, skew, rotation
   centre, `cam_bottom_x/_y/_z`).
4. Top camera: camera centred above a mark on the bed → **CAL TOP**.
5. Top camera offset: nozzle tip exactly on a mark → step 1/2; camera roughly above it → step 2/2.
6. Rotation direction: a part on the nozzle over the bottom camera → **CAL ROTSIGN**.


**Made By AI**