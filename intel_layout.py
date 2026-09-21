"""
Intel panel v2 -- rebuilt to match Stellar's actual design system exactly
(same color constants, same margins, same ring-only jack/button/knob
style with the real widget drawn on top in C++, same double-stroke micro
label technique, same full-width "section" row convention with a rotated
side title in a reserved margin) rather than an approximate, independently
invented style. v1 used solid filled jacks/buttons and different colors --
this version borrows the actual constants and helper patterns from
stellar_layout.py directly.

Deliberate structural exception, kept from earlier discussion: the TRIG
row is one Stellar-style full-width section, but internally divided into
4 channel groups (vertical divider lines) so each TRG jack stays visually
paired with its own rate-div/depth button pair -- that grouping was an
explicit earlier design decision, preserved here inside the new style.
"""
from gen_panel import text_to_path
import json

HP = 5.08
PANEL_H = 128.5
PANEL_W = 14 * HP  # 71.12mm -- explicitly matched to Stellar's own size

# --- Exact same palette as Stellar (stellar_layout.py) ---
BG, BG2 = "#F5F1E9", "#EDE7DC"
BORDER = "#8A7F6A"
TEXT_DIM = "#4A4438"
TEXT_BRIGHT = "#2A2620"
MICRO = "#2E281F"
SLOT = "#DDD6C6"
BRASS = "#8A6423"
MAROON = "#8A2A2A"
NAVY = "#2E4A6E"
RATE_COLOR = "#6B63C7"   # new to Intel (rate-div button ring) -- kept distinct from the family palette above since it's a new function, chosen close in saturation/value so it doesn't clash
DEPTH_COLOR = "#1D7A5C"  # new to Intel (depth button ring), same reasoning

R = {"knob": 2.8, "port": 26 / 2 / (75 / 25.4), "light": 1.0}
BUTTON_HALF = 2.3
BTN_GAP = 1.0
LABEL_GAP = 1.0
LABEL_H = 1.6
GAP = 5.0
SIDE_TITLE_W = 5.0
MIN_MARGIN = 1.4
OUTER_MARGIN = 4.5

x0 = OUTER_MARGIN
full_w = PANEL_W - 2 * x0

svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {PANEL_W} {PANEL_H}" width="{PANEL_W}mm" height="{PANEL_H}mm">']
svg.append(f'<defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="{BG}"/><stop offset="100%" stop-color="{BG2}"/></linearGradient></defs>')
svg.append(f'<rect x="0" y="0" width="{PANEL_W}" height="{PANEL_H}" fill="url(#bg)"/>')
svg.append(f'<rect x="0.5" y="0.5" width="{PANEL_W-1}" height="{PANEL_H-1}" fill="none" stroke="{BORDER}" stroke-width="0.4" opacity="0.6"/>')

def txt(text, x, y, size, color, anchor="start", tracking=1.05):
    p, w = text_to_path(text, x, y, size, tracking=tracking, anchor=anchor)
    return f'<g fill="{color}">{p}</g>', w

def add(s):
    svg.append(s)

def micro(text, x, y, color=MICRO, size=1.1):
    p, _ = txt(text, x, y, size, color, anchor="middle")
    add(p)
    p2, _ = txt(text, x + 0.04, y, size, color, anchor="middle")
    add(p2)

# --- Title, same formula as Stellar: cleared past the left LINK light ---
title_x = x0 + (R["light"] + 0.6) * 2 + 1.5
p, _ = txt("INTEL", title_x, 7.2, 4.4, TEXT_BRIGHT)
add(p)
p, _ = txt("modulation + fx for command", title_x, 10.4, 1.9, TEXT_DIM)
add(p)
TITLE_H = 12.5

link_r_cx, link_r_cy = PANEL_W - OUTER_MARGIN - R["light"], OUTER_MARGIN + R["light"]
add(f'<circle cx="{link_r_cx}" cy="{link_r_cy}" r="{R["light"]}" fill="{NAVY}" opacity="0.85"/>')
add(f'<circle cx="{link_r_cx}" cy="{link_r_cy}" r="{R["light"]+0.6}" fill="none" stroke="{BORDER}" stroke-width="0.3" opacity="0.4"/>')
link_l_cx, link_l_cy = OUTER_MARGIN + R["light"], OUTER_MARGIN + R["light"]
add(f'<circle cx="{link_l_cx}" cy="{link_l_cy}" r="{R["light"]}" fill="{NAVY}" opacity="0.85"/>')
add(f'<circle cx="{link_l_cx}" cy="{link_l_cy}" r="{R["light"]+0.6}" fill="none" stroke="{BORDER}" stroke-width="0.3" opacity="0.4"/>')

sections = {}
def section(name, x, y, w, h, title, color=TEXT_DIM, border_color=None):
    bc = border_color or BORDER
    sections[name] = (x + SIDE_TITLE_W, y, w - SIDE_TITLE_W, h, title)
    add(f'<rect x="{x+SIDE_TITLE_W}" y="{y}" width="{w-SIDE_TITLE_W}" height="{h}" rx="1.0" fill="{SLOT}" opacity="0.9" stroke="{bc}" stroke-width="0.5" stroke-opacity="0.8"/>')
    cx, cy = x + SIDE_TITLE_W / 2 + 0.6, y + h / 2
    max_size = 1.7
    available = h - 2.0
    _, measured_w = text_to_path(title, 0, 0, max_size, tracking=1.1, anchor="middle")
    size = max_size if measured_w <= available else max(1.0, max_size * available / measured_w)
    p, w = text_to_path(title, 0, 0, size, tracking=1.1, anchor="middle")
    p2, _ = text_to_path(title, 0.04, 0, size, tracking=1.1, anchor="middle")
    add(f'<g fill="{color}" transform="translate({cx},{cy}) rotate(-90)">{p}{p2}</g>')

def row_height(r, extra=0.0):
    return extra + 2 * r + LABEL_GAP + LABEL_H + 2 * MIN_MARGIN

def centered_y(box_y, box_h, r, extra=0.0):
    block_h = extra + 2 * r
    return box_y + (box_h - block_h - LABEL_GAP - LABEL_H) / 2 + extra + r

# --- Row heights ---
port_pad = R["port"] + 0.3 + 1.2
meter_h = 8.0 + LABEL_GAP + LABEL_H + 2 * MIN_MARGIN
io_h = row_height(R["port"])
btn_half = BUTTON_HALF
knob_half = R["knob"] + 0.8
trig_label_h = LABEL_GAP + LABEL_H
trig_btn_h = LABEL_GAP + btn_half * 2
trig_cap_h = LABEL_GAP + 2 * 1.5
trig_h = 2 * R["port"] + trig_label_h + trig_btn_h + trig_cap_h + 2 * MIN_MARGIN
btn_sub_h = btn_half * 2 + LABEL_GAP + LABEL_H + 2 * MIN_MARGIN
knob_sub_h = knob_half * 2 + LABEL_GAP + LABEL_H + 2 * MIN_MARGIN
fx_h = btn_sub_h + knob_sub_h + 1.2

y = TITLE_H
content_h = meter_h + io_h + trig_h + fx_h
num_gaps = 3
GAP = (PANEL_H - OUTER_MARGIN - TITLE_H - content_h) / num_gaps
section("meter", x0, y, full_w, meter_h, "METER"); y += meter_h + GAP
section("io", x0, y, full_w, io_h, "I/O"); y += io_h + GAP
section("trig", x0, y, full_w, trig_h, "TRIG", color=BRASS, border_color=BRASS); y += trig_h + GAP
section("fx", x0, y, full_w, fx_h, "FX", color=MAROON, border_color=MAROON); y += fx_h

margin = PANEL_H - y - OUTER_MARGIN
print(f"PANEL {PANEL_W:.1f}mm (14HP) x {PANEL_H}mm -- content ends {y:.1f}mm, bottom slack {margin:.2f}mm")
assert y + OUTER_MARGIN <= PANEL_H, f"OVERFLOW by {y+OUTER_MARGIN-PANEL_H:.1f}mm"

layout = {
    "link_left": {"x": round(link_l_cx, 2), "y": round(link_l_cy, 2), "light": "LINK_LEFT_LIGHT"},
    "link_right": {"x": round(link_r_cx, 2), "y": round(link_r_cy, 2), "light": "LINK_RIGHT_LIGHT"},
    "meter": {}, "io": [], "trig": [], "fx_buttons": [], "fx_knobs": [],
}

# --- METER: same SLOT box, custom LED-cell content instead of jacks ---
mx, my, mw, mh, _ = sections["meter"]
n_leds = 14
led_area_w = mw - 2 * MIN_MARGIN
led_w = led_area_w / n_leds - 0.4
led_y = my + MIN_MARGIN
for i in range(n_leds):
    lx = mx + MIN_MARGIN + i * (led_w + 0.4)
    add(f'<rect x="{lx:.3f}" y="{led_y:.3f}" width="{led_w:.3f}" height="6.0" rx="0.3" fill="{MICRO}" opacity="0.85"/>')
layout["meter"] = {"x": round(mx, 2), "y": round(led_y, 2), "w": round(led_area_w, 2), "h": 6.0, "n_leds": n_leds}

# --- I/O row: identical convention to Stellar's io_row() ---
ix, iy, iw, ih, _ = sections["io"]
port_y = centered_y(iy, ih, R["port"])
label_y = port_y + R["port"] + LABEL_GAP + LABEL_H
io_names = ["EXT L", "EXT R", "MST L", "MST R"]
io_params = ["EXT_L_INPUT", "EXT_R_INPUT", "MASTER_L_OUTPUT", "MASTER_R_OUTPUT"]
io_dirs = ["in", "in", "out", "out"]
usable = iw - 2 * port_pad
step = usable / (len(io_names) - 1)
for i, (nm, pnm, dr) in enumerate(zip(io_names, io_params, io_dirs)):
    cx = ix + port_pad + step * i
    rc = NAVY if dr == "in" else BRASS
    add(f'<circle cx="{cx:.3f}" cy="{port_y:.3f}" r="{R["port"]+0.3:.3f}" fill="none" stroke="{rc}" stroke-width="1.6" opacity="0.55"/>')
    micro(nm, cx, label_y)
    layout["io"].append({"x": round(cx, 2), "y": round(port_y, 2), "param": pnm, "dir": dr})

# --- TRIG row: one Stellar-style section, internally divided into 4 channel groups ---
tx, ty, tw, th, _ = sections["trig"]
jack_y = ty + MIN_MARGIN + R["port"]
label_row_y = jack_y + R["port"] + LABEL_GAP + LABEL_H
btn_y = label_row_y + LABEL_GAP + btn_half
cap_y1 = btn_y + btn_half + LABEL_GAP + 1.3
cap_y2 = cap_y1 + 1.6
chan_w = tw / 4
cmd_links = ["rate", "dens", "swing", "entropy"]
for i in range(4):
    cx = tx + chan_w * (i + 0.5)
    if i > 0:
        dx = tx + chan_w * i
        add(f'<line x1="{dx:.3f}" y1="{ty+0.8:.3f}" x2="{dx:.3f}" y2="{ty+th-0.8:.3f}" stroke="{BORDER}" stroke-width="0.3" opacity="0.5"/>')
    add(f'<circle cx="{cx:.3f}" cy="{jack_y:.3f}" r="{R["port"]+0.3:.3f}" fill="none" stroke="{BRASS}" stroke-width="1.6" opacity="0.55"/>')
    micro(f"TRG {i+1}", cx, label_row_y, color=TEXT_BRIGHT, size=1.3)
    rdx, ddx = cx - (btn_half + BTN_GAP / 2 + 0.9), cx + (btn_half + BTN_GAP / 2 + 0.9)
    add(f'<rect x="{rdx-1.1:.3f}" y="{btn_y-1.1:.3f}" width="2.2" height="2.2" rx="0.4" fill="none" stroke="{RATE_COLOR}" stroke-width="0.5" opacity="0.7"/>')
    add(f'<rect x="{ddx-1.1:.3f}" y="{btn_y-1.1:.3f}" width="2.2" height="2.2" rx="0.4" fill="none" stroke="{DEPTH_COLOR}" stroke-width="0.5" opacity="0.7"/>')
    add(f'<line x1="{tx+chan_w*i+1.0:.3f}" y1="{cap_y1-1.4:.3f}" x2="{tx+chan_w*(i+1)-1.0:.3f}" y2="{cap_y1-1.4:.3f}" stroke="{BORDER}" stroke-width="0.2" stroke-dasharray="0.5,0.5" opacity="0.6"/>')
    micro(f"cmd: {cmd_links[i]}", cx, cap_y1, color=TEXT_DIM, size=1.0)
    micro("(hidden)", cx, cap_y2, color=TEXT_DIM, size=1.0)
    layout["trig"].append({"x": round(cx, 2), "jack_y": round(jack_y, 2), "trig_out": f"TRIG{i+1}_OUTPUT",
                            "ratediv_x": round(rdx, 2), "depth_x": round(ddx, 2), "button_y": round(btn_y, 2),
                            "ratediv_param": f"RATEDIV{i+1}_PARAM", "depth_param": f"DEPTH{i+1}_PARAM",
                            "cmd_link": cmd_links[i]})
add(f'<line x1="{tx:.3f}" y1="{ty:.3f}" x2="{tx:.3f}" y2="{ty+th:.3f}" stroke="none"/>')  # (no-op, keeps structure explicit)

# --- FX section: buttons on top, knobs below -- same bundling as Stellar's voice_row() ---
fx_x, fx_y, fx_w, fx_h2, _ = sections["fx"]
fx_top_cy = fx_y + MIN_MARGIN + btn_half
fx_bot_cy = fx_y + btn_sub_h + MIN_MARGIN + knob_half
fx_pad = 4.0
fx_usable = fx_w - 2 * fx_pad
fx_btn_names = ["DELAY", "REVERB", "SHIM", "SYNC"]
fx_btn_params = ["DELAY_PARAM", "REVERB_PARAM", "SHIMMER_PARAM", "SYNC_PARAM"]
bstep = fx_usable / (len(fx_btn_names) - 1)
for i, (nm, pnm) in enumerate(zip(fx_btn_names, fx_btn_params)):
    cx = fx_x + fx_pad + bstep * i
    add(f'<rect x="{cx-btn_half:.3f}" y="{fx_top_cy-btn_half:.3f}" width="{btn_half*2:.3f}" height="{btn_half*2:.3f}" rx="0.7" fill="none" stroke="{MAROON}" stroke-width="0.45" opacity="0.55"/>')
    micro(nm, cx, fx_top_cy + btn_half + LABEL_GAP + LABEL_H)
    layout["fx_buttons"].append({"x": round(cx, 2), "y": round(fx_top_cy, 2), "param": pnm})

fx_knob_names = ["MIX", "TIME", "REGEN", "TONE", "SPEED"]
fx_knob_params = ["MIX_PARAM", "TIME_PARAM", "REGEN_PARAM", "TONE_PARAM", "SPEED_PARAM"]
kstep = fx_usable / (len(fx_knob_names) - 1)
for i, (nm, pnm) in enumerate(zip(fx_knob_names, fx_knob_params)):
    cx = fx_x + fx_pad + kstep * i
    accent = NAVY if nm == "SPEED" else MAROON
    add(f'<circle cx="{cx:.3f}" cy="{fx_bot_cy:.3f}" r="{knob_half:.3f}" fill="none" stroke="{accent}" stroke-width="0.45" opacity="0.55"/>')
    micro(nm, cx, fx_bot_cy + knob_half + LABEL_GAP + LABEL_H)
    layout["fx_knobs"].append({"x": round(cx, 2), "y": round(fx_bot_cy, 2), "param": pnm})

with open("intel_layout.json", "w") as f:
    json.dump(layout, f, indent=2)
with open("res/Intel.svg", "w") as f:
    f.write("\n".join(svg) + "\n</svg>")
print("wrote res/Intel.svg and intel_layout.json")
