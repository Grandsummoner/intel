"""
Stellar panel layout v7 -- back to buttons-on-top/knobs-below per
voice (reverting the v6 single-long-row experiment, which cost 24HP),
plus AUX row changes:
  - EXT-IN collapses from 2 jacks (L/R) to ONE stereo jack -- a mono
    cable normals to both Master L and Master R internally, standard
    VCV convention, no need for two separate input jacks.
  - EXT-IN moves to the leftmost position in the AUX row, swapping with
    NOISE. AUX row is now: EXT-IN, NOISE, L OUT, R OUT -- 4 jacks,
    matching I/O 1 and I/O 2's own 4-jack width for a consistent grid
    across all three I/O-style rows.
  Row 1: V/OCT 1, GATE 1, SUB 1 (out, -1 oct), MOD 1
  Row 2: V/OCT 2, GATE 2, SUB 2 (out, -2 oct), MOD 2
  Row 3 (AUX): EXT-IN, NOISE, L OUT, R OUT
  Row 4: VOICE 1 -- 4 buttons (top sub-row), 5 knobs (bottom sub-row)
  Row 5: VOICE 2 -- same
"""
from gen_panel import text_to_path
import json

HP = 5.08
PANEL_H = 128.5

R = {
    "knob":     2.8,
    "port":     26 / 2 / (75/25.4),
    "light":    1.0,
}
BUTTON_HALF = 2.3
BTN_GAP = 1.0

BG, BG2 = "#F5F1E9", "#EDE7DC"
BORDER = "#8A7F6A"
TEXT_DIM = "#4A4438"
TEXT_BRIGHT = "#2A2620"
MICRO = "#2E281F"
SLOT = "#DDD6C6"
BRASS = "#8A6423"
MAROON = "#8A2A2A"
NAVY = "#2E4A6E"

LABEL_GAP = 1.0
LABEL_H = 1.6
GAP = 5.0
SIDE_TITLE_W = 5.0
MIN_MARGIN = 1.4
OUTER_MARGIN = 4.5

def row_height(r, extra=0.0):
    block_h = extra + 2*r
    return block_h + LABEL_GAP + LABEL_H + 2*MIN_MARGIN

def centered_y(box_y, box_h, r, extra=0.0):
    block_h = extra + 2*r
    block_top = box_y + (box_h - block_h - LABEL_GAP - LABEL_H) / 2
    return block_top + extra + r

port_pad = R["port"] + 0.3 + 1.2
knob_half = R["knob"] + 0.8
btn_half = BUTTON_HALF
pad_side = 4.0

io_w_needed = 4 * 10.5 + 2*port_pad   # all three I/O-style rows now have 4 jacks each

btn_sub_h = BUTTON_HALF*2 + LABEL_GAP + LABEL_H + 2*MIN_MARGIN
knob_sub_h = knob_half*2 + LABEL_GAP + LABEL_H + 2*MIN_MARGIN
extra_row_h = 1.6   # reduced -- a 3rd I/O-style row (AUX) was added, so less slack remains to fill per voice row
voice_row_h = btn_sub_h + knob_sub_h + extra_row_h

btn_row_w = BUTTON_HALF*8 + BTN_GAP*3
knob_pitch_est = 8.5
knob_row_w = knob_pitch_est*4 + knob_half*2   # (n-1) pitches + the two end knobs' own radii, not radius*n -- fixed a double-counted estimate
voice_content_w = max(btn_row_w, knob_row_w)
voice_w_needed = voice_content_w + 2*pad_side

content_w_needed = max(io_w_needed, voice_w_needed)
x0 = OUTER_MARGIN
PANEL_W = content_w_needed + 2*x0 + SIDE_TITLE_W
WIDTH_HP = int(-(-PANEL_W // HP))
PANEL_W = WIDTH_HP * HP
full_w = PANEL_W - 2*x0

svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {PANEL_W} {PANEL_H}" width="{PANEL_W}mm" height="{PANEL_H}mm">']
svg.append(f'<defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="{BG}"/><stop offset="100%" stop-color="{BG2}"/></linearGradient></defs>')
svg.append(f'<rect x="0" y="0" width="{PANEL_W}" height="{PANEL_H}" fill="url(#bg)"/>')
svg.append(f'<rect x="0.5" y="0.5" width="{PANEL_W-1}" height="{PANEL_H-1}" fill="none" stroke="{BORDER}" stroke-width="0.4" opacity="0.6"/>')

def txt(text, x, y, size, color, anchor="start", tracking=1.05):
    p, w = text_to_path(text, x, y, size, tracking=tracking, anchor=anchor)
    return f'<g fill="{color}">{p}</g>', w

def add(s): svg.append(s)

title_x = x0 + (R["light"]+0.6)*2 + 1.5   # cleared past the new left LINK light -- was colliding with the "S"
p, _ = txt("STELLAR", title_x, OUTER_MARGIN + 3.2, 3.4, TEXT_BRIGHT)
add(p)
p, _ = txt("dual-voice companion", title_x, OUTER_MARGIN + 5.9, 1.5, TEXT_DIM)
add(p)
TITLE_H = OUTER_MARGIN + 8.4

link_r_cx, link_r_cy = PANEL_W - OUTER_MARGIN - R["light"], OUTER_MARGIN + R["light"]
add(f'<circle cx="{link_r_cx}" cy="{link_r_cy}" r="{R["light"]}" fill="{NAVY}" opacity="0.85"/>')
add(f'<circle cx="{link_r_cx}" cy="{link_r_cy}" r="{R["light"]+0.6}" fill="none" stroke="{BORDER}" stroke-width="0.3" opacity="0.4"/>')

# Mirrored LEFT-side LINK light -- Stellar is an expander, and VCV lets
# an expander attach on either side of its target (leftExpander or
# rightExpander), so a single right-side-only light would go dark if
# Command sits on Stellar's LEFT instead. One light per side, each
# reflecting that specific side's actual connection state.
link_l_cx, link_l_cy = OUTER_MARGIN + R["light"], OUTER_MARGIN + R["light"]
add(f'<circle cx="{link_l_cx}" cy="{link_l_cy}" r="{R["light"]}" fill="{NAVY}" opacity="0.85"/>')
add(f'<circle cx="{link_l_cx}" cy="{link_l_cy}" r="{R["light"]+0.6}" fill="none" stroke="{BORDER}" stroke-width="0.3" opacity="0.4"/>')

sections = {}
def section(name, x, y, w, h, title, color=TEXT_DIM, border_color=None):
    bc = border_color or BORDER
    sections[name] = (x + SIDE_TITLE_W, y, w - SIDE_TITLE_W, h, title)
    add(f'<rect x="{x+SIDE_TITLE_W}" y="{y}" width="{w-SIDE_TITLE_W}" height="{h}" rx="1.0" fill="{SLOT}" opacity="0.9" stroke="{bc}" stroke-width="0.5" stroke-opacity="0.8"/>')
    cx, cy = x + SIDE_TITLE_W/2 + 0.6, y + h/2
    max_size = 1.7
    available = h - 2.0
    _, measured_w = text_to_path(title, 0, 0, max_size, tracking=1.1, anchor="middle")
    size = max_size if measured_w <= available else max(1.0, max_size * available / measured_w)
    p, w = text_to_path(title, 0, 0, size, tracking=1.1, anchor="middle")
    p2, _ = text_to_path(title, 0.04, 0, size, tracking=1.1, anchor="middle")
    add(f'<g fill="{color}" transform="translate({cx},{cy}) rotate(-90)">{p}{p2}</g>')

y = TITLE_H
io_h = row_height(R["port"])
section("io1", x0, y, full_w, io_h, "I/O 1", color=BRASS, border_color=BRASS); y += io_h + GAP
section("io2", x0, y, full_w, io_h, "I/O 2", color=MAROON, border_color=MAROON); y += io_h + GAP
section("io3", x0, y, full_w, io_h, "AUX");   y += io_h + GAP

section("voice1", x0, y, full_w, voice_row_h, "VOICE 1", color=BRASS, border_color=BRASS); y += voice_row_h + GAP
section("voice2", x0, y, full_w, voice_row_h, "VOICE 2", color=MAROON, border_color=MAROON); y += voice_row_h

margin = PANEL_H - y - OUTER_MARGIN
print(f"PANEL {PANEL_W:.1f}mm ({WIDTH_HP}HP) x {PANEL_H}mm -- content ends {y:.1f}mm, bottom slack {margin:.2f}mm (target 0)")
assert y + OUTER_MARGIN <= PANEL_H, f"OVERFLOW by {y+OUTER_MARGIN-PANEL_H:.1f}mm"

def micro(text, x, y, color=MICRO, size=1.2):
    p, _ = txt(text, x, y, size, color, anchor="middle")
    add(p)
    p2, _ = txt(text, x + 0.04, y, size, color, anchor="middle")
    add(p2)

layout = {"io1": [], "io2": [], "io3": [], "voice1": [], "voice2": [],
          "link_left": {"x": round(link_l_cx,2), "y": round(link_l_cy,2), "light": "LINK_LEFT_LIGHT"},
          "link_right": {"x": round(link_r_cx,2), "y": round(link_r_cy,2), "light": "LINK_RIGHT_LIGHT"}}

def io_row(section_name, names, params, dirs, out_list):
    ix, iy, iw, ih, _ = sections[section_name]
    port_y = centered_y(iy, ih, R["port"])
    label_y = port_y + R["port"] + LABEL_GAP + LABEL_H
    n = len(names)
    usable = iw - 2*port_pad
    step = usable / (n - 1)
    for i, (nm, pnm, dr) in enumerate(zip(names, params, dirs)):
        cx = ix + port_pad + step * i
        rc = NAVY if dr == "in" else BRASS
        add(f'<circle cx="{cx}" cy="{port_y}" r="{R["port"]+0.3}" fill="none" stroke="{rc}" stroke-width="1.6" opacity="0.55"/>')
        micro(nm, cx, label_y, size=1.15)
        out_list.append({"x": round(cx,2), "y": round(port_y,2), "param": pnm, "dir": dr})

io_row("io1", ["V/OCT 1","GATE 1","MOD 1","SUB 1"],
       ["VOCT1_INPUT","GATE1_INPUT","MOD1_INPUT","SUB1_OUTPUT"],
       ["in","in","in","out"], layout["io1"])
io_row("io2", ["V/OCT 2","GATE 2","MOD 2","SUB 2"],
       ["VOCT2_INPUT","GATE2_INPUT","MOD2_INPUT","SUB2_OUTPUT"],
       ["in","in","in","out"], layout["io2"])
io_row("io3", ["EXT-IN","NOISE","L OUT","R OUT"],
       ["EXTIN_INPUT","NOISE_OUTPUT","MASTER_L_OUTPUT","MASTER_R_OUTPUT"],
       ["in","out","out","out"], layout["io3"])

def voice_row(section_name, accent, out_list):
    vx, vy, vw, vh, _ = sections[section_name]
    startx = vx + pad_side
    usable = vw - 2*pad_side

    top_cy = vy + MIN_MARGIN + btn_half
    bot_cy = vy + btn_sub_h + extra_row_h + MIN_MARGIN + knob_half

    names = ["AN","FM","SS","PL"]
    params = ["AN_PARAM","FM_PARAM","SS_PARAM","PL_PARAM"]
    step = usable / (len(names) - 1)
    for i, (nm, pnm) in enumerate(zip(names, params)):
        cx = startx + step * i
        add(f'<rect x="{cx-btn_half}" y="{top_cy-btn_half}" width="{btn_half*2}" height="{btn_half*2}" rx="0.7" fill="none" stroke="{accent}" stroke-width="0.45" opacity="0.55"/>')
        micro(nm, cx, top_cy + btn_half + LABEL_GAP + LABEL_H, size=1.1)
        out_list.append({"x": round(cx,2), "y": round(top_cy,2), "param": pnm, "kind": "btn"})

    knob_names = ["ATTACK","DECAY","SUSTAIN","RELEASE","CUTOFF"]
    knob_params = ["ATTACK_PARAM","DECAY_PARAM","SUSTAIN_PARAM","RELEASE_PARAM","CUTOFF_PARAM"]
    kstep = usable / (len(knob_names) - 1)
    for i, (nm, pnm) in enumerate(zip(knob_names, knob_params)):
        cx = startx + kstep * i
        add(f'<circle cx="{cx}" cy="{bot_cy}" r="{knob_half}" fill="none" stroke="{accent}" stroke-width="0.45" opacity="0.55"/>')
        micro(nm, cx, bot_cy + knob_half + LABEL_GAP + LABEL_H, size=1.1)
        out_list.append({"x": round(cx,2), "y": round(bot_cy,2), "param": pnm, "kind": "knob"})

voice_row("voice1", BRASS, layout["voice1"])
voice_row("voice2", MAROON, layout["voice2"])

with open("stellar_layout.json", "w") as f:
    json.dump(layout, f, indent=2)
with open("res/Stellar.svg", "w") as f:
    f.write("\n".join(svg) + "\n</svg>")
