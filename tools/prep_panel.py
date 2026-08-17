#!/usr/bin/env python3
"""Strip guide layers from a panel SVG before Inkscape converts it.

Panels carry construction layers — `components` holds a shape per pot, jack and
LED so the widget coordinates can be read off the drawing. Those must not ship:
Rack would render them on top of the finished panel.

Removing the group outright rather than setting `display:none` on it, for two
reasons: the shipped file gets smaller, and it does not depend on Rack's SVG
parser honouring `display` — which it does today, but that is a detail of a
vendored parser rather than a promise.

This has to run *before* Inkscape, because the layers are identified by
`inkscape:label` and `--export-plain-svg` drops the whole `inkscape:` namespace
on the way out.

    python tools/prep_panel.py in.svg out.svg [label ...]

With no labels given, strips `components`.

There is also a post-Inkscape check:

    python tools/prep_panel.py --check out.svg

which reports any <text> that survived object-to-path *and would actually draw
something*. Empty text objects are ignored: Inkscape leaves them as <text>
because there is no glyph to convert, and a `grep -q "<text"` therefore cried
wolf on every build over one stray empty label in the drawing.
"""

import sys
import xml.etree.ElementTree as ET

SVG = "http://www.w3.org/2000/svg"
INKSCAPE = "http://www.inkscape.org/namespaces/inkscape"
SODIPODI = "http://sodipodi.sourceforge.net/DTD/sodipodi-0.0.dtd"
XLINK = "http://www.w3.org/1999/xlink"

LABEL = f"{{{INKSCAPE}}}label"
GROUP = f"{{{SVG}}}g"


def strip(parent, labels, removed):
    """Depth-first, so a guide layer nested inside another group is still found."""
    for child in list(parent):
        if child.tag == GROUP and child.get(LABEL) in labels:
            removed.append(child.get(LABEL))
            parent.remove(child)
            continue
        strip(child, labels, removed)


def text_content(el):
    """All character data under `el`, whitespace collapsed."""
    return "".join(el.itertext()).strip()


def check(path):
    """Warn about <text> that will not render in Rack. 0 if the panel is clean."""
    tree = ET.parse(path)
    bad = [
        el
        for el in tree.getroot().iter(f"{{{SVG}}}text")
        if text_content(el)
    ]
    if not bad:
        return 0
    print(f"  WARNING: {path} still contains {len(bad)} <text> element(s) with "
          f"content - Rack will not render them.")
    for el in bad[:5]:
        print(f"           id={el.get('id')!r}  {text_content(el)[:40]!r}")
    print("           Check for text inside a clip path or a <defs> block.")
    return 0  # advisory: a panel that still builds should still install


def main(argv):
    if len(argv) >= 3 and argv[1] == "--check":
        return check(argv[2])

    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2

    src, dst = argv[1], argv[2]
    labels = set(argv[3:]) or {"components"}

    # Registered so the output keeps readable prefixes instead of ns0:, ns1:.
    # Inkscape would cope either way, but a human may open the temp file.
    ET.register_namespace("", SVG)
    ET.register_namespace("inkscape", INKSCAPE)
    ET.register_namespace("sodipodi", SODIPODI)
    ET.register_namespace("xlink", XLINK)

    tree = ET.parse(src)
    removed = []
    strip(tree.getroot(), labels, removed)
    tree.write(dst, encoding="utf-8", xml_declaration=True)

    if removed:
        print(f"  stripped guide layer(s): {', '.join(sorted(set(removed)))}")
    else:
        # Not an error — a panel need not have guide layers. Said out loud
        # because a typo'd label would otherwise look like it worked.
        print(f"  no layer matched {sorted(labels)} (nothing stripped)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
