#!/usr/bin/env python3
"""Turn a labelled SVG path into NanoVG calls, in millimetres.

Rack draws widgets from code, so a custom-shaped LED has to exist twice: once as
artwork in the panel SVG, and once as a path the plugin can fill with the light
colour. Transcribing a 1200-character bezier by hand is not a plan, and neither
is eyeballing an approximation — so generate it, the same way panel_coords.py
generates positions.

    python tools/svg_path_to_nvg.py panel-src/AlloyCoil_src.svg LED1 \\
        platform/vcv/PanelLedShape.generated.h AlloyPanelLed [verify.svg ...]

The path is emitted with its bounding box normalised to the origin, so the
widget can scale it to whatever box.size it has. Output is millimetres, because
that is the unit the panel is drawn in and the unit PanelLayout already uses.

Any extra SVGs are **verified, not generated from**: every module on this
platform shares one aperture shape because they share one panel, so the same
label is looked up in each and compared against what was just emitted. A panel
that has not been drawn yet is skipped quietly; one that disagrees is reported
loudly, because the failure it prevents is a plugin drawing Alloy Coil's LED outline
on AlloyFlux's panel and looking almost right.

Only the commands Inkscape actually emits for this shape are handled (M/m, L/l,
H/h, V/v, C/c, S/s, Z/z). Anything else raises rather than silently producing a
plausible-but-wrong outline, which is the same rule panel_coords.py follows.
"""

import re
import sys
import xml.etree.ElementTree as ET

SVG = "http://www.w3.org/2000/svg"
INKSCAPE = "http://www.inkscape.org/namespaces/inkscape"
LABEL = f"{{{INKSCAPE}}}label"

TOKEN = re.compile(r"([A-Za-z])|(-?\d*\.?\d+(?:[eE]-?\d+)?)")
ARGC = {"m": 2, "l": 2, "h": 1, "v": 1, "c": 6, "s": 4, "z": 0}


def parse_path(d):
    """SVG path data -> list of ('move'|'line'|'cubic', absolute coords)."""
    items = [(m.group(1), m.group(2)) for m in TOKEN.finditer(d)]
    out = []
    cur = (0.0, 0.0)
    start = (0.0, 0.0)
    prev_ctrl = None
    cmd = None
    i = 0
    while i < len(items):
        letter, num = items[i]
        if letter is not None:
            cmd = letter
            i += 1
            if cmd.lower() == "z":
                out.append(("close",))
                cur = start
                prev_ctrl = None
            continue
        if cmd is None:
            raise SystemExit("path data starts with a number")

        low = cmd.lower()
        if low not in ARGC:
            raise SystemExit(f"unsupported path command {cmd!r} — extend this script")
        rel = cmd.islower()
        n = ARGC[low]
        vals = []
        while len(vals) < n and i < len(items) and items[i][1] is not None:
            vals.append(float(items[i][1]))
            i += 1
        if len(vals) != n:
            raise SystemExit(f"truncated arguments for {cmd!r}")

        ox, oy = cur if rel else (0.0, 0.0)

        if low == "m":
            cur = (vals[0] + ox, vals[1] + oy)
            out.append(("move", cur))
            start = cur
            prev_ctrl = None
            # A repeated coordinate pair after M/m is an implicit lineto.
            cmd = "l" if rel else "L"
        elif low == "l":
            cur = (vals[0] + ox, vals[1] + oy)
            out.append(("line", cur))
            prev_ctrl = None
        elif low == "h":
            cur = (vals[0] + ox, cur[1])
            out.append(("line", cur))
            prev_ctrl = None
        elif low == "v":
            cur = (cur[0], vals[0] + oy)
            out.append(("line", cur))
            prev_ctrl = None
        elif low == "c":
            c1 = (vals[0] + ox, vals[1] + oy)
            c2 = (vals[2] + ox, vals[3] + oy)
            end = (vals[4] + ox, vals[5] + oy)
            out.append(("cubic", c1, c2, end))
            prev_ctrl = c2
            cur = end
        elif low == "s":
            # Smooth cubic: first control is the reflection of the previous one.
            c1 = (2 * cur[0] - prev_ctrl[0], 2 * cur[1] - prev_ctrl[1]) if prev_ctrl else cur
            c2 = (vals[0] + ox, vals[1] + oy)
            end = (vals[2] + ox, vals[3] + oy)
            out.append(("cubic", c1, c2, end))
            prev_ctrl = c2
            cur = end
    return out


def bbox(segs):
    xs, ys = [], []
    for s in segs:
        for p in s[1:]:
            xs.append(p[0])
            ys.append(p[1])
    return min(xs), min(ys), max(xs), max(ys)


def load(src, label, strict):
    """-> (segments in mm with bbox at the origin, width, height), or None.

    `strict` distinguishes the file being generated from (must parse) from a
    file being verified (a panel that has not been drawn yet is not an error).
    """
    root = ET.parse(src).getroot()
    wmm = float(re.sub(r"[^\d.]", "", root.get("width", "0")) or 0)
    vb = [float(x) for x in re.split(r"[,\s]+", root.get("viewBox", "").strip()) if x]
    if not wmm or len(vb) != 4 or not vb[2]:
        raise SystemExit(f"{src}: need both a width in mm and a viewBox")
    k = wmm / vb[2]  # user units -> mm

    el = None
    for p in root.iter(f"{{{SVG}}}path"):
        if p.get(LABEL) == label:
            el = p
            break
    if el is None:
        if strict:
            raise SystemExit(f"no <path> labelled {label!r} in {src}")
        return None
    if el.get("transform"):
        msg = f"{src}: {label!r} carries a transform — flatten it in Inkscape first"
        if strict:
            raise SystemExit(msg)
        print(f"  ! {msg}")
        return None

    segs = parse_path(el.get("d"))
    x0, y0, x1, y1 = bbox(segs)
    norm = [
        (s[0], *[((p[0] - x0) * k, (p[1] - y0) * k) for p in s[1:]]) for s in segs
    ]
    return norm, (x1 - x0) * k, (y1 - y0) * k


def compare(a, b):
    """Max deviation in mm between two normalised segment lists, or None."""
    if len(a) != len(b):
        return None
    worst = 0.0
    for sa, sb in zip(a, b):
        if sa[0] != sb[0] or len(sa) != len(sb):
            return None
        for pa, pb in zip(sa[1:], sb[1:]):
            worst = max(worst, abs(pa[0] - pb[0]), abs(pa[1] - pb[1]))
    return worst


def main(argv):
    if len(argv) < 5:
        sys.stderr.write(__doc__)
        return 2
    src, label, dst, ns = argv[1], argv[2], argv[3], argv[4]
    verify = argv[5:]

    segs, w, h = load(src, label, strict=True)

    def mm(p):
        return p  # already normalised by load()

    lines = []
    for s in segs:
        if s[0] == "move":
            lines.append("    nvgMoveTo(vg, {:.4f}f, {:.4f}f);".format(*mm(s[1])))
        elif s[0] == "line":
            lines.append("    nvgLineTo(vg, {:.4f}f, {:.4f}f);".format(*mm(s[1])))
        elif s[0] == "cubic":
            a, b, c = mm(s[1]), mm(s[2]), mm(s[3])
            lines.append(
                "    nvgBezierTo(vg, {:.4f}f, {:.4f}f, {:.4f}f, {:.4f}f, "
                "{:.4f}f, {:.4f}f);".format(*a, *b, *c)
            )
        elif s[0] == "close":
            lines.append("    nvgClosePath(vg);")

    body = "\n".join(lines)
    out = f"""#pragma once

// GENERATED by tools/svg_path_to_nvg.py — do not edit.
//   source : {src}
//   path   : {label}
//
// Regenerate with:  make led-shape
//
// Coordinates are millimetres, bounding box normalised to the origin, so a
// widget scales this to its own box.size. See platform/vcv/PanelLed.hpp.

#include <nanovg.h>

namespace {ns}
{{

/// Bounding box of the shape as drawn on the panel, in mm.
static constexpr float kWidthMm  = {w:.4f}f;
static constexpr float kHeightMm = {h:.4f}f;

/// Emits the outline into the current NanoVG path. Call between nvgBeginPath()
/// and nvgFill(); scale first if the widget is not exactly kWidthMm wide.
inline void path(NVGcontext *vg)
{{
{body}
}}

}} // namespace {ns}
"""
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    print(f"{dst}: {label} from {src} — {w:.3f} x {h:.3f} mm, {len(lines)} segments")

    # Every panel on this platform must draw the same aperture, or the plugin
    # renders one module's outline on the other's artwork and looks almost right.
    rc = 0
    for v in verify:
        got = load(v, label, strict=False)
        if got is None:
            print(f"  - {v}: no {label!r} yet, skipped")
            continue
        vsegs, vw, vh = got
        dev = compare(segs, vsegs)
        if dev is None:
            print(f"  ! {v}: {label!r} has a DIFFERENT outline "
                  f"({len(vsegs)} segments vs {len(segs)}, {vw:.3f} x {vh:.3f} mm)")
            rc = 1
        elif dev > 0.01:
            print(f"  ! {v}: {label!r} differs by up to {dev:.3f} mm")
            rc = 1
        else:
            print(f"  ok {v}: {label!r} matches (max {dev:.4f} mm)")
    if rc:
        print("  The panels disagree. Either make them identical, or give each "
              "module its own generated shape.")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
