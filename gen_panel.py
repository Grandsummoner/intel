"""Generates res/Intel.svg from the LAYOUT dict in intel_layout.py.

Labels are converted to real vector glyph paths via fontTools, matching
Command/Stellar's own approach -- NOT plain SVG <text> elements. This is a
correction, not a stylistic choice: VCV Rack's panel SVGs are parsed by
NanoSVG, which does not render <text> at all, only vector shapes. An
earlier version of this script used plain <text> and every label silently
vanished in the real renderer -- this file is the fix.
"""
import fontTools.ttLib
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.ttLib import TTFont
from intel_layout import LAYOUT, PANEL_W, PANEL_H

FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT_REGULAR = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_ITALIC = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf"

_font_cache = {}

def text_to_path(text, x, y, size_mm, font_path=FONT_BOLD, tracking=1.08, anchor="start"):
    if font_path not in _font_cache:
        _font_cache[font_path] = TTFont(font_path)
    font = _font_cache[font_path]
    glyf = font['glyf']
    cmap = font.getBestCmap()
    units_per_em = font['head'].unitsPerEm
    scale = size_mm / units_per_em

    hmtx = font['hmtx']
    total_width = 0
    glyph_data = []
    for ch in text:
        if ch == ' ':
            try:
                adv = hmtx['space'][0]
            except Exception:
                adv = units_per_em * 0.3
            glyph_data.append((None, adv))
            total_width += adv * tracking
            continue
        gid = cmap.get(ord(ch))
        if gid is None:
            continue
        adv = hmtx[gid][0]
        glyph_data.append((gid, adv))
        total_width += adv * tracking
    total_width_mm = total_width * scale

    start_x = x
    if anchor == "middle":
        start_x = x - total_width_mm / 2
    elif anchor == "end":
        start_x = x - total_width_mm

    cursor = 0.0
    paths = []
    for gid, adv in glyph_data:
        if gid is not None:
            pen = SVGPathPen(glyf)
            glyf[gid].draw(pen, glyf)
            d = pen.getCommands()
            if d:
                px = start_x + cursor * scale
                paths.append(f'<path d="{d}" transform="translate({px:.3f},{y:.3f}) scale({scale:.6f},-{scale:.6f})"/>')
        cursor += adv * tracking
    return "\n".join(paths), total_width_mm


CREAM = "#ece7db"
BORDER = "#a89a78"
BOX_BORDER = "#c8bfa0"
INK = "#2b2b28"
LABEL = "#6b6a63"
BRASS = "#8a6d3b"
KNOB_FILL = "#e8e2cd"
ACCENT = "#d4537e"
JACK_BODY = "#3a3a36"
RATE_COLOR = "#7f77dd"
DEPTH_COLOR = "#1d9e75"

def mm(v):
    return f"{v:.3f}"

def jack(x, y, ring):
    return (f'<circle cx="{mm(x)}" cy="{mm(y)}" r="1.55" fill="{JACK_BODY}" '
            f'stroke="{ring}" stroke-width="0.55"/>'
            f'<circle cx="{mm(x)}" cy="{mm(y)}" r="0.5" fill="#111"/>')

def knob(x, y, ring=BRASS):
    return (f'<circle cx="{mm(x)}" cy="{mm(y)}" r="2.6" fill="{KNOB_FILL}" '
            f'stroke="{ring}" stroke-width="0.4"/>'
            f'<line x1="{mm(x)}" y1="{mm(y)}" x2="{mm(x)}" y2="{mm(y-2.0)}" '
            f'stroke="{INK}" stroke-width="0.4"/>')

def button(x, y, w=6.0, h=2.4):
    bx, by = x - w / 2, y - h / 2
    return (f'<rect x="{mm(bx)}" y="{mm(by)}" width="{mm(w)}" height="{mm(h)}" '
            f'rx="0.4" fill="{CREAM}" stroke="{BRASS}" stroke-width="0.3"/>')

def small_button(x, y, fill, w=2.2, h=2.2):
    bx, by = x - w / 2, y - h / 2
    return (f'<rect x="{mm(bx)}" y="{mm(by)}" width="{mm(w)}" height="{mm(h)}" '
            f'rx="0.3" fill="{fill}"/>')

def label_group(x, y, s, size=1.6, anchor="middle", fill=LABEL, font=FONT_REGULAR):
    d, _ = text_to_path(s, x, y, size, font_path=font, anchor=anchor)
    return f'<g fill="{fill}">{d}</g>'

parts = []
parts.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{mm(PANEL_W)}mm" '
             f'height="{mm(PANEL_H)}mm" viewBox="0 0 {mm(PANEL_W)} {mm(PANEL_H)}">')
parts.append(f'<rect x="0" y="0" width="{mm(PANEL_W)}" height="{mm(PANEL_H)}" fill="{CREAM}"/>')
parts.append(f'<rect x="0.5" y="0.5" width="{mm(PANEL_W-1)}" height="{mm(PANEL_H-1)}" '
             f'rx="1.2" fill="none" stroke="{BORDER}" stroke-width="0.3"/>')

# LINK lights
ll, lr = LAYOUT["link_left"], LAYOUT["link_right"]
parts.append(f'<circle cx="{mm(ll["x"])}" cy="{mm(ll["y"])}" r="1.1" fill="#d8cfa8" stroke="{BRASS}" stroke-width="0.3"/>')
parts.append(f'<circle cx="{mm(lr["x"])}" cy="{mm(lr["y"])}" r="1.1" fill="#d8cfa8" stroke="{BRASS}" stroke-width="0.3"/>')

# Title
parts.append(label_group(9.5, 14.5, "INTEL", size=4.4, anchor="start", fill=INK, font=FONT_BOLD))
parts.append(label_group(9.5, 19.0, "modulation + fx for command", size=1.9, anchor="start"))

# Meter
m = LAYOUT["meter"]
parts.append(f'<rect x="{mm(m["x"])}" y="{mm(m["y"])}" width="{mm(m["w"])}" height="{mm(m["h"])}" '
             f'rx="0.8" fill="#dcd6c4" stroke="{BOX_BORDER}" stroke-width="0.25"/>')
led_w = (m["w"] - 2) / m["n_leds"] - 0.4
for i in range(m["n_leds"]):
    lx = m["x"] + 1 + i * (led_w + 0.4)
    parts.append(f'<rect x="{mm(lx)}" y="{mm(m["y"]+1.2)}" width="{mm(led_w)}" height="{mm(m["h"]-2.4)}" fill="#3a3a36"/>')
parts.append(f'<g transform="rotate(-90 2.0 {mm(m["y"]+m["h"]/2)})">'
             + label_group(2.0, m["y"] + m["h"]/2, "METER", size=1.5) + '</g>')

# I/O row
io = LAYOUT["io"]
parts.append(f'<rect x="12.5" y="{mm(io["y"]-4.5)}" width="{mm(PANEL_W-25)}" height="13" rx="1" fill="none" stroke="{BOX_BORDER}" stroke-width="0.25"/>')
for j in io["jacks"]:
    ring = "#7fb0e0" if "EXT" in j["name"] else "#c78a4a"
    parts.append(jack(j["x"], io["y"], ring))
    parts.append(label_group(j["x"], io["label_y"], j["label"], size=1.4))

# TRIG sub-panels
t = LAYOUT["trig"]
for ch in t["channels"]:
    bx = ch["x"] - t["box_w"] / 2
    parts.append(f'<rect x="{mm(bx)}" y="{mm(t["box_y"])}" width="{mm(t["box_w"])}" height="{mm(t["box_h"])}" '
                 f'rx="0.8" fill="none" stroke="{BRASS}" stroke-width="0.25"/>')
    parts.append(jack(ch["x"], t["jack_y"], ACCENT))
    parts.append(label_group(ch["x"], t["jack_y"]+3.2, f"TRG {ch['n']}", size=1.5, fill=INK, font=FONT_BOLD))
    rdx, ddx = t["button_dx"]
    parts.append(small_button(ch["x"]+rdx, t["button_y"], RATE_COLOR))
    parts.append(small_button(ch["x"]+ddx, t["button_y"], DEPTH_COLOR))
    parts.append(f'<line x1="{mm(bx+0.8)}" y1="{mm(t["caption_y1"]-1.6)}" x2="{mm(bx+t["box_w"]-0.8)}" y2="{mm(t["caption_y1"]-1.6)}" '
                 f'stroke="{BOX_BORDER}" stroke-width="0.2" stroke-dasharray="0.5,0.5"/>')
    parts.append(label_group(ch["x"], t["caption_y1"], f"cmd: {ch['cmd_link']}", size=1.15, fill="#8a8a83", font=FONT_ITALIC))
    parts.append(label_group(ch["x"], t["caption_y2"], "(hidden)", size=1.15, fill="#8a8a83", font=FONT_ITALIC))
parts.append(label_group(PANEL_W/2, t["box_y"]+t["box_h"]+3.0,
                   "purple = rate-div  \u00b7  green = depth  \u00b7  dashed = invisible link, adjacent-to-command only",
                   size=1.05, fill="#8a8a83"))

# FX buttons
fxb = LAYOUT["fx_buttons"]
for b in fxb["buttons"]:
    parts.append(button(b["x"], fxb["y"]))
    parts.append(label_group(b["x"], fxb["y"]+0.6, b["label"], size=1.4, fill=INK))
parts.append(label_group(PANEL_W/2, fxb["y"]+4.6,
                   "DELAY \u2192 REVERB always on, buttons page the shared knobs \u00b7 SHIM = reverb only \u00b7 SYNC = either page",
                   size=1.0, fill="#8a8a83"))

# FX knobs
fxk = LAYOUT["fx_knobs"]
for k in fxk["knobs"]:
    ring = ACCENT if k.get("accent") else BRASS
    parts.append(knob(k["x"], fxk["y"], ring=ring))
    parts.append(label_group(k["x"], fxk["label_y"], k["label"], size=1.4))

parts.append(label_group(PANEL_W/2, PANEL_H-3.0, "14HP", size=1.6, fill="#8a8a83"))
parts.append('</svg>')

with open("res/Intel.svg", "w") as f:
    f.write("\n".join(parts))
print("wrote res/Intel.svg")
