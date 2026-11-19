# Generates wiring.svg: python wiring.py [wiring.svg]
# Raspberry Pi 5 + BTT Octopus V1.1 pick & place: Pi 40-pin header -> 3.5" ILI9488 / XPT2046 (SPI0),
# ADS1115 (joystick) + BMP280 (vacuum) on I2C1, joystick button, buzzer, Octopus over USB, power, and
# the Octopus motor / input / output assignment. Pin numbers match ../core/ui/config.h and ../core/dev/.
import sys

W, H = 2200, 1780
out = []
e = out.append
FONT = "font-family='Segoe UI, Helvetica, Arial, sans-serif'"


def esc(t):
    return str(t).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def text(x, y, t, size=15, anchor='start', color='#222', weight='normal'):
    e(f"<text x='{x}' y='{y}' {FONT} font-size='{size}' text-anchor='{anchor}' fill='{color}' font-weight='{weight}' dominant-baseline='middle'>{esc(t)}</text>")


def box(x, y, w, h, fill, stroke='#333', r=10, sw=2):
    e(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='{r}' fill='{fill}' stroke='{stroke}' stroke-width='{sw}'/>")


def line(pts, color, w=3):
    d = ' '.join(f"{x},{y}" for x, y in pts)
    e(f"<polyline points='{d}' fill='none' stroke='{color}' stroke-width='{w}' stroke-linejoin='round' stroke-linecap='round'/>")


# Net colours (the same colour on the header and at the module = one wire)
C = {
    'SPI': '#1d4ed8', 'TFT': '#7c3aed', 'TOUCH': '#0891b2', 'I2C': '#059669', 'JOY': '#ca8a04',
    'BUZ': '#db2777', '5V': '#d62828', '3V3': '#e07a00', 'GND': '#222222', 'FREE': '#c8c8c8',
}

# Pi 5 header: pin -> (name, net group, what it connects to)
PINS = {
    1: ('3V3', '3V3', 'ADS1115 VDD, BMP280 VCC + CSB, joystick +, touch'),
    2: ('5V', '5V', 'display VCC (module LDO)'),
    3: ('GPIO2 SDA1', 'I2C', 'ADS1115 SDA, BMP280 SDA'),
    4: ('5V', '5V', 'buzzer +'),
    5: ('GPIO3 SCL1', 'I2C', 'ADS1115 SCL, BMP280 SCL'),
    6: ('GND', 'GND', 'display GND'),
    9: ('GND', 'GND', 'I2C modules GND, joystick GND'),
    11: ('GPIO17', 'JOY', 'joystick SW (to GND, internal pull-up)'),
    12: ('GPIO18', 'TFT', 'display LED (backlight)'),
    13: ('GPIO27', 'BUZ', 'buzzer: 1 kΩ -> NPN base'),
    14: ('GND', 'GND', 'buzzer NPN emitter'),
    16: ('GPIO23', 'TOUCH', 'T_IRQ (optional)'),
    17: ('3V3', '3V3', ''),
    18: ('GPIO24', 'TFT', 'display DC/RS'),
    19: ('GPIO10 MOSI', 'SPI', 'display SDI + T_DIN'),
    20: ('GND', 'GND', ''),
    21: ('GPIO9 MISO', 'SPI', 'T_DO only (display SDO open)'),
    22: ('GPIO25', 'TFT', 'display RESET'),
    23: ('GPIO11 SCLK', 'SPI', 'display SCK + T_CLK'),
    24: ('GPIO8 CE0', 'TFT', 'display CS'),
    25: ('GND', 'GND', ''),
    26: ('GPIO7 CE1', 'TOUCH', 'T_CS'),
}
DEFAULT = {7: 'GPIO4', 8: 'GPIO14 TXD', 10: 'GPIO15 RXD', 15: 'GPIO22', 27: 'ID_SD', 28: 'ID_SC', 29: 'GPIO5', 30: 'GND',
           31: 'GPIO6', 32: 'GPIO12', 33: 'GPIO13', 34: 'GND', 35: 'GPIO19', 36: 'GPIO16', 37: 'GPIO26', 38: 'GPIO20',
           39: 'GND', 40: 'GPIO21'}

e(f"<svg xmlns='http://www.w3.org/2000/svg' width='{W}' height='{H}' viewBox='0 0 {W} {H}'>")
e(f"<rect width='{W}' height='{H}' fill='#fbfbf8'/>")
text(40, 40, 'Pick & Place - Raspberry Pi 5 + BTT Octopus V1.1: wiring', 30, weight='bold')
text(40, 76, 'Pi = display, touch, joystick, vacuum sensor, cameras, website, jobs.  Octopus = all motors, Z flag, E-stop, MOSFET outputs.  '
     'Same colour = same wire. Pin numbers = physical header pins.', 16, color='#555')
text(40, 100, 'ADS1115, buzzer and the connectors below are on the carrier board ../../board (schematic.png); this sheet shows the same nets as loose wiring.', 15, color='#7b2cbf')

# ---------------------------------------------------------------- Pi header (centre left)
hx, hy = 560, 150
box(hx - 30, hy - 30, 220, 20 * 38 + 60, '#0b6b3a', '#063', 14)
text(hx + 80, hy - 12, 'Raspberry Pi 5  40-pin', 15, 'middle', 'white', 'bold')
for row in range(20):
    for col in range(2):
        pin = row * 2 + col + 1
        cx = hx + 40 + col * 80
        cy = hy + 22 + row * 38
        name, grp, what = PINS.get(pin, (DEFAULT.get(pin, ''), 'FREE', ''))
        if pin not in PINS and DEFAULT.get(pin) == 'GND':
            grp = 'GND'
        colr = C[grp]
        e(f"<circle cx='{cx}' cy='{cy}' r='13' fill='{colr if pin in PINS else '#e9e9e9'}' stroke='#111' stroke-width='1.5'/>")
        text(cx, cy + 1, pin, 12, 'middle', 'white' if pin in PINS else '#666', 'bold')
        # label towards the outside
        if col == 0:
            line([(cx - 14, cy), (hx - 60, cy)], colr if pin in PINS else '#ddd', 2)
            text(hx - 66, cy - 7, name, 14, 'end', colr if pin in PINS else '#999', 'bold' if pin in PINS else 'normal')
            if what:
                text(hx - 66, cy + 10, what, 11.5, 'end', '#555')
        else:
            line([(cx + 14, cy), (hx + 220, cy)], colr if pin in PINS else '#ddd', 2)
            text(hx + 226, cy - 7, name, 14, 'start', colr if pin in PINS else '#999', 'bold' if pin in PINS else 'normal')
            if what:
                text(hx + 226, cy + 10, what, 11.5, 'start', '#555')


def module(x, y, w, title, sub, rows, fill):
    """rows: (module pin, wire to, net group)"""
    h = 64 + len(rows) * 28 + 10
    box(x, y, w, h, fill)
    text(x + 16, y + 24, title, 18, weight='bold')
    text(x + 16, y + 46, sub, 12.5, color='#555')
    for i, (p, to, grp) in enumerate(rows):
        yy = y + 76 + i * 28
        e(f"<rect x='{x + 14}' y='{yy - 11}' width='14' height='22' rx='3' fill='{C[grp]}'/>")
        text(x + 38, yy, p, 14, weight='bold')
        text(x + 150, yy, to, 13.5, color='#333')
    return y + h


# ---------------------------------------------------------------- modules (right column)
mx = 1060
y = module(mx, 120, 520, '3.5" ILI9488 SPI display + XPT2046 touch', 'SPI0 (dtparam=spi=on); display /dev/spidev0.0, touch /dev/spidev0.1', [
    ('VCC', '5V  pin 2 (module with 3.3 V regulator)', '5V'),
    ('GND', 'GND pin 6', 'GND'),
    ('CS', 'GPIO8 CE0  pin 24', 'TFT'),
    ('RESET', 'GPIO25  pin 22', 'TFT'),
    ('DC/RS', 'GPIO24  pin 18', 'TFT'),
    ('SDI(MOSI)', 'GPIO10  pin 19', 'SPI'),
    ('SCK', 'GPIO11  pin 23', 'SPI'),
    ('LED', 'GPIO18  pin 12 (via the module transistor)', 'TFT'),
    ('SDO(MISO)', 'not connected (ILI9488 does not release it)', 'FREE'),
    ('T_CLK', 'GPIO11  pin 23 (with SCK)', 'SPI'),
    ('T_CS', 'GPIO7 CE1  pin 26', 'TOUCH'),
    ('T_DIN', 'GPIO10  pin 19 (with SDI)', 'SPI'),
    ('T_DO', 'GPIO9 MISO  pin 21', 'SPI'),
    ('T_IRQ', 'GPIO23  pin 16 (optional)', 'TOUCH'),
], '#eef2ff')
y2 = module(mx, y + 20, 520, 'ADS1115 (joystick X / Y)', 'I2C1 address 0x48 (ADDR -> GND), gain ±4.096 V', [
    ('VDD', '3V3  pin 1', '3V3'), ('GND', 'GND  pin 9', 'GND'), ('SDA', 'GPIO2  pin 3', 'I2C'), ('SCL', 'GPIO3  pin 5', 'I2C'),
    ('ADDR', 'GND', 'GND'), ('A0', 'joystick VRx', 'JOY'), ('A1', 'joystick VRy', 'JOY'),
], '#ecfdf5')
y3 = module(mx, y2 + 20, 520, 'BMP280 3.3 V (vacuum line pressure)', 'I2C1 address 0x76 (SDO -> GND); sensor inside a small sealed box on the vacuum hose', [
    ('VCC', '3V3  pin 1', '3V3'), ('GND', 'GND  pin 9', 'GND'), ('SDA', 'GPIO2  pin 3', 'I2C'), ('SCL', 'GPIO3  pin 5', 'I2C'),
    ('CSB', '3V3 (selects I2C)', '3V3'), ('SDO', 'GND (address 0x76)', 'GND'),
], '#ecfdf5')

mx2 = 1610
y4 = module(mx2, 120, 560, 'PS2 joystick module', 'powered from 3.3 V: centre = 1.65 V = ~13200 counts (joy_*_center)', [
    ('+5V', '3V3  pin 1 (NOT 5 V: ADS1115 inputs max VDD)', '3V3'), ('GND', 'GND  pin 9', 'GND'),
    ('VRx', 'ADS1115 A0', 'JOY'), ('VRy', 'ADS1115 A1', 'JOY'), ('SW', 'GPIO17  pin 11 (pressed = LOW)', 'JOY'),
], '#fefce8')
y5 = module(mx2, y4 + 20, 560, 'Buzzer (active, 5 V)', 'MMBT2222A low-side switch, 1N4148W across a magnetic buzzer', [
    ('buzzer +', '5V  pin 4', '5V'), ('buzzer -', 'NPN collector', 'BUZ'), ('NPN base', '1 kΩ to GPIO27  pin 13', 'BUZ'),
    ('NPN emitter', 'GND  pin 14', 'GND'),
], '#fdf2f8')
y6 = module(mx2, y5 + 20, 560, 'USB', 'Pi 5: two USB 3 (blue) + two USB 2 (black)', [
    ('USB 3', 'bottom camera (part rotation / offset)', 'SPI'),
    ('USB 3', 'top camera (fiducials, pockets, teaching)', 'SPI'),
    ('USB 2', 'Octopus USB-C (STM32 USB CDC, /dev/ttyACM0)', 'SPI'),
    ('', 'Octopus: leave the USB 5 V power jumper OFF', 'FREE'),
], '#eef2ff')

# ---------------------------------------------------------------- power (left top)
px, py = 40, 960
box(px, py, 380, 360, '#fff5f5')
text(px + 16, py + 26, 'Power', 20, weight='bold')
rows = [
    ('12 V / 40 A supply', '#d62828'),
    ('  -> Octopus MOTOR power (drivers)', '#333'),
    ('  -> Octopus BED + MAIN power', '#333'),
    ('     (MAIN at 12 V: 12 V rail ~1 V low, OK)', '#777'),
    ('  TVS SMBJ15A + 100 uF at the Octopus input', '#333'),
    ('     (motor back-EMF on the 12 V rail)', '#777'),
    ('Pi 5: own 5 V / 5 A supply on USB-C', '#d62828'),
    ('  (27 W supply or 12 V -> 5.1 V buck module)', '#777'),
    ('Carrier board: 5 V + 3.3 V from the Pi', '#333'),
    ('All GNDs common (Octopus, Pi).', '#333'),
    ('Pump / valve / top light: FAN5 / HE1 / FAN0,', '#333'),
    ('  bottom light: NeoPixel ring (RGB header).', '#333'),
]
for i, (t, c) in enumerate(rows):
    text(px + 20, py + 64 + i * 24, t, 14.5, color=c)

# ---------------------------------------------------------------- Octopus table (bottom)
ox, oy = 40, 1360
box(ox, oy, W - 80, 380, '#f4f4f5')
text(ox + 16, oy + 28, 'BTT Octopus V1.1 (STM32F407ZET6, 8 x TMC2209 in UART mode) - own firmware ../../octopus-fw', 20, weight='bold')
cols = [('Slot', 90), ('Connector', 130), ('Axis', 200), ('Homing / note', 420)]
motors = [
    ('DRV0', 'MOTOR0', 'X side 1  (x, mm)', 'sensorless: DIAG jumper 0 ON'),
    ('DRV1', 'MOTOR1', 'X side 2  (follows x)', 'sensorless: DIAG jumper 1 ON'),
    ('DRV2', 'MOTOR2_1', 'paste extruder  (paste, µl)', 'none'),
    ('DRV3', 'MOTOR3', 'nozzle rotation  (rot, °)', 'none, SETPOS rot=0'),
    ('DRV4', 'MOTOR4', 'Z seesaw  (z, mm)', 'half flag -> DIAG2 (z_home = flag)'),
    ('DRV5', 'MOTOR5', 'feeder feed  (e0, °)', 'none'),
    ('DRV6', 'MOTOR6', 'feeder gantry  (e1, mm)', 'sensorless: DIAG jumper 6 ON'),
    ('DRV7', 'MOTOR7', 'Y  (y, mm)', 'sensorless: DIAG jumper 7 ON'),
]
x0 = ox + 20
yy = oy + 66
cx = x0
for name, w in cols:
    text(cx, yy, name, 14, color='#555', weight='bold')
    cx += w
for i, r in enumerate(motors):
    cx = x0
    for (name, w), v in zip(cols, r):
        text(cx, yy + 30 + i * 28, v, 14)
        cx += w
text(x0, yy + 30 + 8 * 28 + 10, 'All driver slots: UART jumper fitted (no MS jumpers). DIAG jumpers on slots 0, 1, 6 and 7 (sensorless X both sides, gantry, Y), nowhere else.', 13, color='#555')

ix = ox + 980
text(ix, yy, 'Inputs (endstop connectors: only the Z flag and the E-stop are switches)', 14, color='#555', weight='bold')
inputs = ['DIAG0  X side 1 StallGuard (jumper, nothing plugged in)', 'DIAG1  X side 2 StallGuard (jumper, nothing plugged in)', 'DIAG2  Z flag (switch to GND)', 'DIAG3  Z max (optional)', 'DIAG4  feeder sensor (triggers at each feeder centre)',
          'DIAG5  E-STOP button (NO to GND; NC button: SET in_invert_5 1)', 'DIAG6  gantry StallGuard (jumper)', 'DIAG7  Y StallGuard (jumper, nothing plugged in)',
          'TFT header: tool changer board, RX pin 2 (PA10), GND pin 4, 5 V pin 5', '   slot sensors as one byte, 9600 baud; pin by pin: octopus-wiring.png',
          'Load cell HX711 on EXP1: DT = pin 3 (PE9), SCK = pin 4 (PE10), GND = pin 9', '   VCC = 3.3 V LDO from EXP1 pin 10 (5 V); RATE pin to VCC (80/s)']
for i, t in enumerate(inputs):
    text(ix, yy + 30 + i * 22, t, 14)
ox2 = ix + 560
text(ox2, yy, 'Outputs (12 V MOSFETs)', 14, color='#555', weight='bold')
outs = ['FAN5  vacuum pump (12 V, jumper on VIN)', 'HE1  valve', 'RGB header (5V, DATA PB0, GND)', '   NeoPixel ring = bottom camera light', 'FAN0  top camera light (12 V, always on,',
        '   off while the bottom light is on)', 'FAN2  head camera fan (cam_fan %)', 'FAN3  housing fan (box_fan %)', 'FAN4  Pi housing fan 30 mm (pi_fan %)']
for i, t in enumerate(outs):
    text(ox2, yy + 30 + i * 26, t, 14)

e('</svg>')
path = sys.argv[1] if len(sys.argv) > 1 else 'wiring.svg'
open(path, 'w', encoding='utf-8').write('\n'.join(out))
print('wrote', path)
