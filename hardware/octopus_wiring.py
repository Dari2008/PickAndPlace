# Generates octopus-wiring.svg: python octopus_wiring.py [octopus-wiring.svg]
# What plugs into which connector of the BTT Octopus V1.1, pin by pin.
# Pins: ../../octopus-fw/src/pap_board.h, loadcell.cpp, lights.cpp; connector order and photo coordinates:
# ../../board/octopus/octopus_pinout.py. The Pi side is on wiring.svg.
import base64
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 2400, 1470
FONT = "font-family='Segoe UI, Helvetica, Arial, sans-serif'"
out = []
e = out.append

V12, V5, V33, GND, SIG, STALL, FREE, MOT = '#e8590c', '#d62828', '#e07a00', '#222222', '#1d4ed8', '#7c3aed', '#b9bdc2', '#c2255c'
A, B_, C_, D_, E_, F_, G_ = '#e03131', '#c2255c', '#e67700', '#d6336c', '#0b7285', '#5c940d', '#3b5bdb'


def esc(t):
    return str(t).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def text(x, y, t, size=15, anchor='start', color='#222', weight='normal'):
    e(f"<text x='{x}' y='{y}' {FONT} font-size='{size}' text-anchor='{anchor}' fill='{color}' font-weight='{weight}' dominant-baseline='middle'>{esc(t)}</text>")


def rect(x, y, w, h, fill, stroke='#333', r=8, sw=2):
    e(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='{r}' fill='{fill}' stroke='{stroke}' stroke-width='{sw}'/>")


def line(x0, y0, x1, y1, color, w=3):
    e(f"<line x1='{x0}' y1='{y0}' x2='{x1}' y2='{y1}' stroke='{color}' stroke-width='{w}' stroke-linecap='round'/>")


def dot(x, y, r, fill, stroke='none'):
    e(f"<circle cx='{x}' cy='{y}' r='{r}' fill='{fill}' stroke='{stroke}' stroke-width='1.5'/>")


def card(x, y, w, h, tag, title, color):
    rect(x, y, w, h, 'white', color, 10, 2.5)
    e(f"<path d='M{x},{y+10} a10,10 0 0 1 10,-10 h{w-20} a10,10 0 0 1 10,10 v28 h-{w} z' fill='{color}'/>")
    dot(x + 24, y + 19, 13, 'white')
    text(x + 24, y + 20, tag, 16, 'middle', color, 'bold')
    text(x + 46, y + 20, title, 18, color='white', weight='bold')


def rows(x, y, cols, data, step=27, wire_from=None, wire_to=None):
    """data = (cells..., wire colour or None). Cells go to the x offsets in cols; the wire runs between the two offsets."""
    for i, r in enumerate(data):
        yy = y + i * step
        if i % 2 == 0:
            e(f"<rect x='{x - 8}' y='{yy - step / 2}' width='{cols[-1] + 8}' height='{step}' fill='#f6f7f8'/>")
        col = r[-1]
        for c, cx in zip(r[:-1], cols):
            bold = cx == cols[0]
            text(x + cx, yy, c, 14, weight='bold' if bold else 'normal', color='#222' if col else '#8b9096')
        if wire_from is not None and col:
            line(x + wire_from, yy, x + wire_to, yy, col, 3)
            dot(x + wire_from, yy, 4, col)
            dot(x + wire_to, yy, 4, col)


def header(x, y, tag, pins):
    """2 x 5 header. pins[n] = (label, colour or None). Column gap 46, row gap 40; odd pins left."""
    rect(x, y, 96, 214, '#2b2f33', '#111', 6, 2)
    for n in range(1, 11):
        col = (n - 1) % 2
        row = (n - 1) // 2
        cx, cy = x + 26 + col * 44, y + 27 + row * 40
        label, colour = pins[n]
        dot(cx, cy, 15, colour or '#e4e6e8', '#fff' if colour else '#9aa0a6')
        text(cx, cy + 1, n, 14, 'middle', '#fff' if colour else '#555', 'bold')
        tx = x - 12 if col == 0 else x + 108
        text(tx, cy, label, 13, 'end' if col == 0 else 'start', '#222' if colour else '#8b9096', 'bold' if colour else 'normal')
    text(x + 48, y + 232, tag, 15, 'middle', '#222', 'bold')


e(f"<svg xmlns='http://www.w3.org/2000/svg' width='{W}' height='{H}' viewBox='0 0 {W} {H}'>")
e(f"<rect width='{W}' height='{H}' fill='#fbfbf8'/>")
text(30, 38, 'Pick & Place - BTT Octopus V1.1: what plugs in where', 30, weight='bold')
text(30, 74, 'Every connector in use, pin by pin. The letters on the photo match the cards. The Pi side (display, joystick, vacuum sensor) is on wiring.png.', 16, color='#555')
lx = 1420
for label, c in (('12 V', V12), ('5 V', V5), ('3.3 V', V33), ('GND', GND), ('signal', SIG), ('motor', MOT)):
    line(lx, 74, lx + 34, 74, c, 4)
    text(lx + 42, 74, label, 15)
    lx += 112

# ---- A power
y = 110
card(30, y, 760, 330, 'A', 'Power in: 8-pole screw terminal, top to bottom', A)
rows(46, y + 60, (0, 44, 150, 440), [
    ('1', 'GND', '', 'Supply V−', GND),
    ('2', 'VM', '', 'Supply V+ 12 V (motors, fuse F1)', V12),
    ('3', 'GND', '', 'Supply V−', GND),
    ('4', 'VIN', '', 'Supply V+ 12 V (logic, HE, fans, fuse F3)', V12),
    ('5', 'GND', '', 'Supply V−', GND),
    ('6', 'VBI', '', 'Supply V+ 12 V (fuse F2)', V12),
    ('7', 'VBO', '', 'nothing (bed output, unused)', None),
    ('8', 'BED−', '', 'nothing (bed output, unused)', None),
], wire_from=140, wire_to=430)
text(46, y + 286, 'At the input: TVS SMBJ15A + 100 µF across V+ / V− (motor back-EMF on the 12 V rail).', 14, color='#555')
text(46, y + 308, 'The Pi has its own 5 V / 5 A supply on USB-C. All grounds common.', 14, color='#555')

# ---- B motors
y = 456
card(30, y, 760, 396, 'B', 'Motors: slot, connector, DIAG jumper', B_)
rows(46, y + 60, (0, 74, 192, 290, 560), [
    ('DRV0', 'MOTOR0', 'jumper ON', 'X motor, side 1', '', MOT),
    ('DRV1', 'MOTOR1', 'jumper ON', 'X motor, side 2', '', MOT),
    ('DRV2', 'MOTOR2_1', 'none', 'Paste extruder', '', MOT),
    ('DRV3', 'MOTOR3', 'none', 'Nozzle rotation', '', MOT),
    ('DRV4', 'MOTOR4', 'none', 'Z seesaw', '', MOT),
    ('DRV5', 'MOTOR5', 'none', 'Feeder drive (E0)', '', MOT),
    ('DRV6', 'MOTOR6', 'jumper ON', 'Feeder gantry (E1)', '', MOT),
    ('DRV7', 'MOTOR7', 'jumper ON', 'Y motor', '', MOT),
])
text(46, y + 292, 'Motor plug: pin 1 + 2 = coil A, pin 3 + 4 = coil B (1 A1, 2 A2, 3 B2, 4 B1).', 14, color='#555')
text(46, y + 316, 'Every driver: 1 UART jumper, lower left under the socket. No MS jumpers.', 14, color='#555')
text(46, y + 340, 'DIAG jumpers: slots 0, 1, 6 and 7 only (bottom right of the board).', 14, color='#555')
text(46, y + 364, 'MOTOR2_2 stays empty. Set the X2 direction with one belt off, see octopus-fw/README.md.', 14, color='#555')

# ---- photo
kx = 780 / 831
px, py = 810, 110
img = base64.b64encode(open(os.path.join(HERE, '..', '..', 'board', 'octopus', 'octopus-photo.jpg'), 'rb').read()).decode()
e(f"<image x='{px}' y='{py}' width='780' height='{512 * kx}' preserveAspectRatio='none' href='data:image/jpeg;base64,{img}'/>")


def mark(x0, y0, x1, y1, tag, color, tx=None, ty=None):
    ax, ay, bx, by = px + x0 * kx, py + y0 * kx, px + x1 * kx, py + y1 * kx
    e(f"<rect x='{ax}' y='{ay}' width='{bx - ax}' height='{by - ay}' rx='3' fill='{color}' fill-opacity='.22' stroke='{color}' stroke-width='3'/>")
    if tag:
        cx, cy = (ax + bx) / 2 if tx is None else px + tx * kx, (ay + by) / 2 if ty is None else py + ty * kx
        dot(cx, cy, 15, color, 'white')
        text(cx, cy + 1, tag, 16, 'middle', 'white', 'bold')


mark(12, 76, 82, 466, 'A', A)
for name, cxm in (('M0', 127), ('M1', 192), ('M2', 262), ('M3', 403), ('M4', 472), ('M5', 541), ('M6', 609), ('M7', 679)):
    mark(cxm - 30, 8, cxm + 30, 43, '', B_)
mark(97, 8, 97, 8, 'B', B_, 410, 27)
mark(591, 368, 683, 395, '', C_)
mark(578, 397, 702, 507, 'C', C_)
mark(724, 8, 751, 80, 'D', D_)
mark(592, 162, 637, 262, 'E', E_)
mark(748, 102, 773, 152, 'F', F_)
mark(99, 436, 307, 489, 'G', G_)
mark(317, 468, 574, 508, 'G', G_)
mark(535, 410, 574, 422, 'G', G_, 512, 416)
mark(783, 99, 826, 146, 'H', '#343a40')

# ---- check card under the photo
y = py + 512 * kx + 16
card(810, y, 780, 852 - y, '!', 'Before power-on', '#c92a2a')
for i, t in enumerate((
        'Find the pins on the TFT header and EXP1 first: see the yellow box. Plugged in reversed, 5 V lands on an input.',
        'Fan voltage jumpers carry the rails directly: a wrong position shorts 5 V / 12 V / VIN.',
        'Octopus USB 5 V power jumper OFF. The board is powered from the 12 V terminal.',
        'All signal pins are 3.3 V logic. DIAG inputs and PA10 are 5 V tolerant; ADC pins (T0-T3, TB) are not.')):
    text(826, y + 58 + i * 25, t, 14)
dot(838, y + 182, 15, '#343a40', 'white')
text(838, y + 183, 'H', 16, 'middle', 'white', 'bold')
text(864, y + 182, 'USB-C to a USB 2 port of the Pi 5: the only link between the two boards.', 15, weight='bold')
text(864, y + 208, 'Shows up on the Pi as /dev/ttyACM0.', 14, color='#555')

# ---- C inputs
y = 110
card(1610, y, 760, 344, 'C', 'DIAG inputs (3-pin: 5 V, GND, signal)', C_)
rows(1626, y + 60, (0, 74, 150), [
    ('DIAG0', 'PG6', 'X stall, side 1: plug NOTHING in (jumper does it)', STALL),
    ('DIAG1', 'PG9', 'X stall, side 2: plug NOTHING in', STALL),
    ('DIAG2', 'PG10', 'Z home sensor on the head: 5 V, GND, signal', SIG),
    ('DIAG3', 'PG11', 'free (optional Z max)', None),
    ('DIAG4', 'PG12', 'Feeder sensor: 5 V, GND, signal', SIG),
    ('DIAG5', 'PG13', 'E-stop button: GND + signal, normally open', SIG),
    ('DIAG6', 'PG14', 'Gantry stall: plug NOTHING in', STALL),
    ('DIAG7', 'PG15', 'Y stall: plug NOTHING in', STALL),
])
text(1626, y + 292, 'On the board: D0-D3 top row, D4-D7 bottom row. A sensor that reads backwards:', 14, color='#555')
text(1626, y + 314, 'SET in_invert_2 / _4 1. Normally closed E-stop: SET in_invert_5 1.', 14, color='#555')

# ---- G outputs
y = 470
card(1610, y, 760, 382, 'G', 'Outputs: screw terminals and fan headers', G_)
rows(1626, y + 58, (0, 74, 150), [
    ('HE0', 'PA2', 'free (held off; the pump moved to FAN5)', None),
    ('HE1', 'PA3', 'Valve: + and −, 12 V', V12),
    ('HE2', 'PB10', 'free (held off)', None),
    ('HE3', 'PB11', 'free (held off)', None),
    ('FAN0', 'PA8', 'Top camera light, 12 V: jumper on 12 V', V12),
    ('FAN1', 'PE5', 'free (or a plain 12 V bottom ring instead of NeoPixels)', None),
    ('FAN2', 'PD12', 'Head camera fan: always on, cam_fan % (50)', V12),
    ('FAN3', 'PD13', 'Housing fan (box rear wall): always on, box_fan % (100)', V12),
    ('FAN4', 'PD14', 'Pi housing fan 30 mm, 12 V: always on, pi_fan % (100)', V12),
    ('FAN5', 'PD15', 'Vacuum pump 12 V, 400 mA: jumper on VIN, pump_power %', V12),
    ('FAN6', 'always on', 'Driver cooling fan 50 x 50, 12 V', V12),
    ('FAN7', 'always on', 'Driver cooling fan 40 x 40, 12 V', V12),
    ('RGB 1', '', 'NeoPixel ring GND', GND),
    ('RGB 2', 'PB0', 'NeoPixel ring DIN (24 LEDs, bottom camera light)', SIG),
    ('RGB 3', '', 'NeoPixel ring 5 V', V5),
], step=20)
text(1626, y + 366, 'Each fan header has its own voltage jumper (FAN0: 12 V, FAN5: VIN). HE0, HE2, HE3 are held off.', 14, color='#555')

# ---- D tool changer board on the TFT header
y = 868
card(30, y, 1160, 580, 'D', 'TFT header (5-pin): tool changer board, one data wire', D_)
rows(46, y + 66, (0, 74, 180, 480, 1120), [
    ('Pin 1', 'RST', '', 'leave open: resets the board', '', None),
    ('Pin 2', 'RX PA10', '', 'CH32V003 pin 8, TX: the sensor byte', '', SIG),
    ('Pin 3', 'TX PA9', '', 'free', '', None),
    ('Pin 4', 'GND', '', 'Board GND (CH32V003 pin 2, all sensors)', '', GND),
    ('Pin 5', '5 V', '', 'Board VDD (CH32V003 pin 4, all sensors)', '', V5),
], step=30, wire_from=170, wire_to=470)
text(46, y + 232, 'Tool changer board: CH32V003J4M6 (SOP-8), sensor outputs to these pins', 15, weight='bold')
for i, (a, b) in enumerate((('Pin 1 PD6', 'bit 0'), ('Pin 3 PA2', 'bit 1'), ('Pin 5 PC1', 'bit 2'), ('Pin 6 PC2', 'bit 3'), ('Pin 7 PC4', 'bit 4'))):
    text(46 + i * 222, y + 262, a, 14, weight='bold')
    text(46 + i * 222 + 84, y + 262, b, 14, color='#555')
text(46, y + 292, 'Any sensor on any bit: Calibration > Nozzle sensor order learns which bit is which slot.', 14, color='#555')
text(46, y + 316, 'UART 9600 8N1, one byte 5 times a second, one bit per sensor. Silent for 1 s = sensors not readable, rack moves stop.', 14, color='#555')
text(46, y + 340, 'To flash the board: unplug the data wire, power it, upload within 3 s. Pin 8 is also the programming pin.', 14, color='#555')
text(46, y + 364, 'EXP2 and the I2C header are free now: nothing plugs in there.', 14, weight='bold')
yy = y + 390
rect(46, yy, 1128, 172, '#fff8e6', '#e0a800', 8, 1.5)
text(62, yy + 24, 'How to find the pins for certain', 16, weight='bold')
for i, t_ in enumerate((
        '1. Nothing plugged into TFT or EXP1. Power the board, set the meter to volts, black lead on any GND screw terminal.',
        '2. TFT header: the pin reading 5 V is pin 5. The pin beside it reading 0 V is pin 4. Two further along is pin 2, the data pin.',
        '3. EXP1: the pin reading 5 V is pin 10, the pin paired with it reading 0 V is pin 9.',
        '4. Pins 1 / 2 are the pair at the far end. Pin 3 (DT) is next to pin 1, pin 4 (SCK) next to pin 2.',
        '5. Once it is wired: set nozzle_count, then Calibration > Nozzle sensor order, and trigger the slots in order.')):
    text(62, yy + 52 + i * 23, t_, 14)

# ---- E EXP1 (+ F the 3.3 V regulator): the whole load cell on one connector
y = 868
card(1210, y, 1160, 580, 'E', 'EXP1: load cell HX711 board (one connector)', E_)
header(1340, y + 66, 'EXP1, pin side', {1: ('PE8', None), 2: ('PE7', None), 3: ('PE9', SIG), 4: ('PE10', SIG), 5: ('PE12', None), 6: ('PE13', None),
                                        7: ('PE14', None), 8: ('PE15', None), 9: ('GND', GND), 10: ('5 V', V5)})
rows(1620, y + 66, (0, 74, 150, 400, 700), [
    ('Pin 1', 'PE8', '', 'free', '', None),
    ('Pin 2', 'PE7', '', 'free', '', None),
    ('Pin 3', 'PE9', '', 'HX711 DT (DOUT)', '', SIG),
    ('Pin 4', 'PE10', '', 'HX711 SCK', '', SIG),
    ('Pin 5', 'PE12', '', 'free', '', None),
    ('Pin 6', 'PE13', '', 'free', '', None),
    ('Pin 7', 'PE14', '', 'free', '', None),
    ('Pin 8', 'PE15', '', 'free', '', None),
    ('Pin 9', 'GND', '', 'HX711 GND, regulator GND', '', GND),
    ('Pin 10', '5 V', '', 'regulator IN (never to the HX711)', '', V5),
], step=30, wire_from=140, wire_to=390)
yy = y + 390
e(f"<rect x='1226' y='{yy}' width='1128' height='172' rx='8' fill='#f1f8ea' stroke='{F_}' stroke-width='2'/>")
dot(1250, yy + 24, 13, F_)
text(1250, yy + 25, 'F', 15, 'middle', 'white', 'bold')
text(1272, yy + 24, '3.3 V regulator on the HX711 side: LDO, not a buck (ripple = noise)', 16, weight='bold')
for i, (a, b, c, col) in enumerate((('IN', 'EXP1-10', '5 V, 1 uF to GND', V5), ('GND', 'EXP1-9', 'shared with the HX711', GND),
                                    ('OUT', '3.3 V', 'HX711 VCC, and its RATE pin (80 samples/s)', V33),
                                    ('Part', '', 'MCP1700-3302 or AMS1117-3.3, 1 uF on OUT', None))):
    yr = yy + 56 + i * 28
    text(1242, yr, a, 14, weight='bold', color='#222' if col else '#8b9096')
    text(1312, yr, b, 14, color='#222' if col else '#8b9096')
    text(1400 if col else 1400, yr, c, 14, color='#222' if col else '#8b9096')
    if col:
        line(1712, yr, 1760, yr, col, 3)
text(1800, yy + 24, 'Load cell to HX711 (usual wire colours, check yours)', 15, weight='bold')
for i, (a, b, col) in enumerate((('Red', 'E+', '#d62828'), ('Black', 'E−', GND), ('White', 'A−', '#9aa0a6'), ('Green', 'A+', '#2f9e44'))):
    yr = yy + 56 + i * 28
    text(1800, yr, a, 14, weight='bold')
    line(1870, yr, 1960, yr, col, 3)
    text(1972, yr, b, 14)
text(2030, yy + 56, 'Never 5 V on this board:', 14, color='#c92a2a', weight='bold')
text(2030, yy + 80, 'DT and SCK run at its supply,', 14, color='#555')
text(2030, yy + 104, '3.3 V matches the STM32.', 14, color='#555')

e("</svg>")
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'octopus-wiring.svg')
open(path, 'w', encoding='utf-8').write('\n'.join(out))
print('wrote', path)
