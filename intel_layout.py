"""
Single source of truth for Intel's panel geometry. Generates BOTH
res/Intel.svg (the artwork) and intel_layout.json (a dump any dev/AI can
diff against src/Intel.cpp's hardcoded widget coordinates) from the SAME
dict below -- this is the fix for the coordinate-desync bug class that hit
Command and Stellar (a layout change that only touches one file, or one
row, silently drifting out of sync with the other). Whenever a coordinate
below changes, both outputs regenerate together, and src/Intel.cpp should
be resynced against the fresh JSON before shipping.

Deliberate simplification vs. Command/Stellar: this uses plain SVG <text>
for labels instead of the fontTools-glyph-to-path pipeline those two use.
That means Intel's panel depends on the renderer having a reasonable
sans-serif font available, rather than being fully self-contained vector
art. Fine for a first pass / getting this compiling and testable -- worth
converting to real vector paths later to match the family's own standard.
"""
import json

HP = 14
PANEL_W = HP * 5.08       # 71.12mm, matches Stellar exactly
PANEL_H = 128.5           # matches Stellar's declared height; widget must
                          # NOT set box.size manually (that was the crash
                          # bug) -- let setPanel() auto-size from this SVG.

# Same 4-column jack rhythm Stellar's own I/O rows use, reused here for
# family-wide cable alignment.
COL_X = [15.4, 30.51, 45.61, 60.72]

LAYOUT = {
    "panel": {"hp": HP, "w_mm": PANEL_W, "h_mm": PANEL_H},
    "link_left":  {"x": 5.5,  "y": 5.5, "light": "LINK_LEFT_LIGHT"},
    "link_right": {"x": 65.62, "y": 5.5, "light": "LINK_RIGHT_LIGHT"},
    "meter": {"x": 5.0, "y": 24.0, "w": 61.12, "h": 8.0, "n_leds": 14},
    "io": {
        "y": 40.0, "label_y": 46.0,
        "jacks": [
            {"name": "EXT_L_INPUT",    "x": COL_X[0], "label": "EXT L"},
            {"name": "EXT_R_INPUT",    "x": COL_X[1], "label": "EXT R"},
            {"name": "MASTER_L_OUTPUT","x": COL_X[2], "label": "MST L"},
            {"name": "MASTER_R_OUTPUT","x": COL_X[3], "label": "MST R"},
        ],
    },
    "trig": {
        "jack_y": 56.0, "button_y": 63.0, "caption_y1": 70.0, "caption_y2": 73.4,
        "box_y": 51.0, "box_h": 26.0, "box_w": 15.4, "box_gap": 0.71,
        "channels": [
            {"n": 1, "jack": "TRIG1_OUTPUT", "x": COL_X[0], "cmd_link": "rate"},
            {"n": 2, "jack": "TRIG2_OUTPUT", "x": COL_X[1], "cmd_link": "dens"},
            {"n": 3, "jack": "TRIG3_OUTPUT", "x": COL_X[2], "cmd_link": "swing"},
            {"n": 4, "jack": "TRIG4_OUTPUT", "x": COL_X[3], "cmd_link": "entropy"},
        ],
        "button_dx": [-2.6, 2.6],  # rate-div (left), depth (right), offset from jack x
    },
    "fx_buttons": {
        "y": 84.0,
        "buttons": [
            {"name": "DELAY_PARAM",   "x": COL_X[0], "label": "DELAY"},
            {"name": "REVERB_PARAM",  "x": COL_X[1], "label": "REVERB"},
            {"name": "SHIMMER_PARAM", "x": COL_X[2], "label": "SHIM"},
            {"name": "SYNC_PARAM",    "x": COL_X[3], "label": "SYNC"},
        ],
    },
    "fx_knobs": {
        "y": 104.0, "label_y": 116.0,
        "knobs": [
            {"name": "MIX_PARAM",    "x": 11.0,  "label": "MIX"},
            {"name": "TIME_PARAM",   "x": 25.75, "label": "TIME"},
            {"name": "REGEN_PARAM",  "x": 40.5,  "label": "REGEN"},
            {"name": "TONE_PARAM",   "x": 55.25, "label": "TONE"},
            {"name": "SPEED_PARAM",  "x": 66.62, "label": "SPEED", "accent": True},
        ],
    },
}

if __name__ == "__main__":
    with open("intel_layout.json", "w") as f:
        json.dump(LAYOUT, f, indent=2)
    print("wrote intel_layout.json")
