"""Godwit centre-stack demo stand, cut from 1/4 in plywood.

A free-standing bench stand shaped like a car's centre stack. It has a
double-DIN opening for the Godwit double-DIN enclosure (CAD/DoubleDin) and,
below it, a climate-control opening for the two dials. The face leans back
like a dash. Every part is a flat plywood panel cut on a CNC router with a
1/4 in cutter, then glued.

Every dimension lives in the `Design` spreadsheet. Each sketch vertex is
held by DistanceX/DistanceY constraints bound to the sheet, so changing the
thickness, tilt or sizes moves the joints with them. Segment COUNTS (how many
fingers a joint has) are fixed in JOINTS below; change those and rebuild.

Joinery
    The sheet (about 5.2 mm) is thinner than the cutter (6.35 mm), so no slot
    the width of the sheet can be cut. Two joint types avoid needing one:

    Box joints   Every outer corner. The notches are open at the panel edge
                 and wider than the cutter. CAM adds dogbones to the inside
                 corners.
    T-joints     The cage decks into the sides, and the cheeks into the
                 decks. The slots are `ws` wide (wider than the cutter, so
                 wider than the sheet) and alternate sides: the first tab
                 of a joint bears on one wall and the second on the
                 other, so together they hold the panel with no play.
                 The slots overrun the tab ends by ws/2 to clear the
                 cutter's corner radius; the fascia sets the depth position.

Coordinates
    World      X across (centred), Y towards the back, Z up; the base
               underside is Z = 0 and the fascia face meets the base top at
               Y = 0.
    Stack      the fascia's own frame, used by the fascia, top, decks,
               cheeks and dial plates: u = X, w = into the stand from the
               fascia face, v = up the face from the base top. The `Stack`
               App::Part carries the tilt.

Every Body is drawn flat (Z 0..thk) in the coordinates of the frame it sits
in, so its link placement is just an axis swap plus a thickness offset, and
the CAM jobs only translate clones onto the sheet.

Run inside FreeCAD (stage by stage keeps each call short):

    import sys; sys.path.insert(0, "<repo>/CAD/CenterStack")
    import build_center_stack as cs
    doc = cs.start()
    for p in cs.PARTS: cs.build_part(doc, p)
    cs.build_assembly(doc); cs.add_refs(doc); cs.tidy(doc)
"""

import os

import FreeCAD as App
import Part
import Sketcher

V = App.Vector
HERE = os.path.dirname(os.path.abspath(__file__))
DOC_NAME = "GodwitCenterStack"
FILE = os.path.join(HERE, DOC_NAME + ".FCStd")
SHEET = "Design"
EXPORTS = os.path.normpath(os.path.join(HERE, "..", "exports"))

# alias, value, note. Rows that start with "#" are section headings.
# Aliases avoid FreeCAD unit names (t, W, H, ...), which the sheet rejects.
PARAMS = [
    ("#", "Sheet stock", ""),
    ("thk", "5.2 mm", "MEASURED sheet thickness. ProWood 1/4 in sande is sold as 5.2 mm; caliper yours and set it"),
    ("fit", "0.2 mm", "box-joint clearance between neighbouring fingers"),
    ("tool_d", "6.35 mm", "cutter diameter (1/4 in O-flute)"),
    ("ws", "7 mm", "T-joint slot width; must exceed the cutter, so it exceeds the sheet (slots alternate sides)"),
    ("bear", "0.1 mm", "clearance on the slot wall a tab bears against"),
    ("#", "Stand", ""),
    ("stack_w", "250 mm", "overall width: fascia, base, top and rear brace"),
    ("stack_h", "295 mm", "fascia height, measured up its face"),
    ("tilt", "15 deg", "fascia lean back from vertical"),
    ("base_d", "280 mm", "fascia foot to the back of the stand"),
    ("base_f", "15 mm", "base plinth in front of the fascia"),
    ("foot_gap", "0.5 mm", "fascia bottom edge clearance above the base"),
    ("rear_h", "90 mm", "rear brace height; the back is open above it for the plugs"),
    ("v_e", "=thk * tan(tilt) + foot_gap", "fascia bottom edge, up its face"),
    ("top_w", "=(base_d - stack_h * sin(tilt)) / cos(tilt)", "top panel depth, so it meets the back of the sides"),
    ("y_sb0", "=thk / cos(tilt)", "front end of the side/base joint"),
    ("#", "Double-DIN opening and cage", ""),
    ("din_ap_w", "182 mm", "double-DIN opening width (ISO 7736 180 mm unit + clearance)"),
    ("din_ap_h", "104 mm", "double-DIN opening height (100 mm unit + clearance)"),
    ("v_din", "198 mm", "opening centre, up the face from the base top"),
    ("ch_w", "178 mm", "Godwit chassis width (DoubleDin Design.din_w)"),
    ("ch_h", "100 mm", "Godwit chassis height (DoubleDin Design.din_h)"),
    ("ch_clr", "0.5 mm", "cage clearance round the chassis, per side"),
    ("enc_face", "15 mm", "bezel front to chassis flange rear (DoubleDin bez_d + fl_t): where the fascia face sits"),
    ("cage_d", "140 mm", "cage depth behind the fascia"),
    ("cheek_x", "=ch_w / 2 + ch_clr", "cheek inner face from the centre line"),
    ("vdl", "=v_din - ch_h / 2 - ch_clr - thk", "lower deck underside"),
    ("vdu", "=v_din + ch_h / 2 + ch_clr", "upper deck underside"),
    ("tab_l", "30 mm", "T-joint tab length"),
    ("tab_w0", "=thk + 20 mm", "first cage tab start, behind the fascia face"),
    ("tab_w1", "=thk + 90 mm", "second cage tab start"),
    ("m5_z", "35 mm", "chassis M5 bracket rows above/below the DIN axis (DoubleDin mnt_z)"),
    ("m5_y0", "40 mm", "first M5 column from the bezel front (DoubleDin mnt_y0)"),
    ("m5_y1", "140 mm", "last M5 column (mnt_y0 + 5 x mnt_yp)"),
    ("m5_w", "7 mm", "M5 slot width in the cheeks"),
    ("m5_ext", "3 mm", "slot overrun past the first and last column"),
    ("acc_w", "14 mm", "screwdriver access slot width in the sides"),
    ("#", "Climate opening and dial plates", ""),
    ("cl_ap_w", "182 mm", "climate opening width (matches the DIN opening)"),
    ("cl_ap_h", "78 mm", "climate opening height (1.5-DIN 75 mm + clearance; clears a 66 mm Waveshare knob)"),
    ("v_cl", "79 mm", "climate opening centre, up the face"),
    ("pl_lip", "18 mm", "dial plate overlap behind the fascia, each side"),
    ("pl_w", "=cl_ap_w + 2 * pl_lip", "dial plate width"),
    ("pl_h", "=cl_ap_h + 2 * pl_lip", "dial plate height"),
    ("bolt_d", "8 mm", "dial plate bolt holes (M5/M6 + washers); 8 mm so the cutter can profile them"),
    ("bolt_x", "=cl_ap_w / 2 + pl_lip / 2", "bolt X (+/-)"),
    ("bolt_v", "=cl_ap_h / 2 + pl_lip / 2", "bolt offset above/below the opening centre"),
    ("dial_x", "52 mm", "dial centres either side of the centre line"),
    ("crow_d", "48.4 mm", "CrowPanel 1.28in push-fit hole (body is 48 mm across its whole depth)"),
    ("ws_d", "66.6 mm", "Waveshare 1.8in knob locating recess (knob is 66 mm across)"),
    ("ws_pk", "2 mm", "Waveshare recess depth"),
    ("ws_cw", "40 mm", "Waveshare cable hole width, under the knob"),
    ("ws_ch", "30 mm", "Waveshare cable hole height"),
]


def P(a):
    return "Design." + a


T = P("thk")
HW = "Design.stack_w / 2"

# Box joints: (start, end, segment count). Segments are numbered from the
# start; the OWNER of the even segments (0, 2, ...) is noted. A panel that owns
# a segment carries the finger there; the other panel is notched.
JOINTS = {
    "FS": (P("v_e"), P("stack_h"), 7),      # fascia / side, along v. Fascia owns even.
    "FT": ("-" + HW, HW, 5),                 # fascia / top, along u. Fascia owns even.
    "TS": (T, P("top_w"), 5),                # top / side, along w. Side owns even.
    "SB": (P("y_sb0"), P("base_d"), 7),      # side / base, along Y. Base owns even.
    "RB": ("-" + HW, HW, 5),                 # rear / base, along X. Base owns even.
    "RS": (T, P("rear_h"), 3),               # rear / side, along Z. Side owns even.
}

COLOURS = {
    "Fascia": (0.80, 0.66, 0.46), "Side": (0.85, 0.72, 0.52), "Base": (0.78, 0.64, 0.44),
    "Top": (0.86, 0.73, 0.54), "Rear": (0.78, 0.64, 0.44), "Deck": (0.74, 0.60, 0.40),
    "Cheek": (0.70, 0.56, 0.37), "DialPlateCrow": (0.30, 0.31, 0.33),
    "DialPlateWave": (0.30, 0.31, 0.33),
}

PARTS = ["Fascia", "Side", "Base", "Top", "Rear", "Deck", "Cheek", "DialPlateCrow", "DialPlateWave"]


# ---------------------------------------------------------------- helpers

def ev(doc, expr):
    q = doc.getObject(SHEET).evalExpression(expr)
    return float(getattr(q, "Value", q))


def sk_(k, j):
    s0, s1, n = JOINTS[j]
    if k == 0:
        return s0
    if k == n:
        return s1
    return "(%s) + %d * ((%s) - (%s)) / %d" % (s0, k, s1, s0, n)


# Stack-frame (w, v) to the side panel's local (Y, Z)
def fmap(w, v):
    return ("(%s) * sin(Design.tilt) + (%s) * cos(Design.tilt)" % (v, w),
            "Design.thk + (%s) * cos(Design.tilt) - (%s) * sin(Design.tilt)" % (v, w))


def corner(a, b):
    """Intersection of two edge lines. A line is (kind, expr) with kind
    x / y (panel axes) or Fw / Fv (stack frame, side panel only)."""
    d = dict([a, b])
    if "x" in d and "y" in d:
        return d["x"], d["y"]
    if "Fw" in d and "Fv" in d:
        return fmap(d["Fw"], d["Fv"])
    if "y" in d and "Fw" in d:
        z, w = d["y"], d["Fw"]
        v = "((%s) - Design.thk + (%s) * sin(Design.tilt)) / cos(Design.tilt)" % (z, w)
        return fmap(w, v)[0], z
    if "x" in d and "Fv" in d:
        y, v = d["x"], d["Fv"]
        w = "((%s) - (%s) * sin(Design.tilt)) / cos(Design.tilt)" % (y, v)
        return y, fmap(w, v)[1]
    raise ValueError("no intersection for %s / %s" % (a, b))


def jedge(j, own_even, rev, owned, notch):
    """A box-jointed edge. owned/notch are the edge lines when this panel
    carries the finger / is notched."""
    return dict(j=j, own_even=own_even, rev=rev, owned=owned, notch=notch)


def plain(line):
    return dict(j=None, owned=line)


def _owns(e, k):
    return (k % 2 == 0) == e["own_even"]


def _line(e, k):
    return e["owned"] if _owns(e, k) else e["notch"]


def _first_last(e):
    if e["j"] is None:
        return e["owned"], e["owned"]
    n = JOINTS[e["j"]][2]
    ks = list(range(n))
    if e["rev"]:
        ks.reverse()
    return _line(e, ks[0]), _line(e, ks[-1])


def _pt(line, s):
    """Point on an edge line at parameter s along it."""
    kind, val = line
    if kind == "x":
        return val, s
    if kind == "y":
        return s, val
    if kind == "Fw":
        return fmap(val, s)
    if kind == "Fv":
        return fmap(s, val)
    raise ValueError(kind)


def outline(edges):
    """Vertex expressions round a closed outline of plain and jointed edges."""
    pts = []
    n_e = len(edges)
    for i, e in enumerate(edges):
        prev_last = _first_last(edges[i - 1])[1]
        pts.append(corner(prev_last, _first_last(e)[0]))
        if e["j"] is None:
            continue
        n = JOINTS[e["j"]][2]
        bounds = range(1, n)
        if e["rev"]:
            bounds = reversed(list(bounds))
        g = "Design.fit / 2"
        for b in bounds:
            # the vertex shifts towards whichever segment this panel owns
            s = "(%s) %s %s" % (sk_(b, e["j"]), "+" if _owns(e, b) else "-", g)
            before, after = (b, b - 1) if e["rev"] else (b - 1, b)
            pts.append(_pt(_line(e, before), s))
            pts.append(_pt(_line(e, after), s))
    return pts


def rect(x0, x1, y0, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def frect(w0, w1, v0, v1):
    """Stack-frame rectangle mapped into the side panel."""
    return [fmap(w0, v0), fmap(w1, v0), fmap(w1, v1), fmap(w0, v1)]


def _named(sk, idx, name, expr):
    sk.renameConstraint(idx, name)
    sk.setExpression("Constraints." + name, expr)


def poly(sk, pre, pts):
    """Closed polygon; every vertex pinned to the sheet by DistanceX/Y."""
    doc = sk.Document
    vals = [(ev(doc, x), ev(doc, y)) for x, y in pts]
    keep = [i for i in range(len(pts))
            if abs(vals[i][0] - vals[i - 1][0]) > 1e-6 or abs(vals[i][1] - vals[i - 1][1]) > 1e-6]
    pts = [pts[i] for i in keep]
    vals = [vals[i] for i in keep]
    n = len(pts)
    g0 = sk.GeometryCount
    for i in range(n):
        a, b = vals[i], vals[(i + 1) % n]
        sk.addGeometry(Part.LineSegment(V(a[0], a[1], 0), V(b[0], b[1], 0)))
    c0 = sk.ConstraintCount
    cons = [Sketcher.Constraint("Coincident", g0 + i, 2, g0 + (i + 1) % n, 1) for i in range(n)]
    for i, (x, y) in enumerate(vals):
        cons.append(Sketcher.Constraint("DistanceX", -1, 1, g0 + i, 1, x))
        cons.append(Sketcher.Constraint("DistanceY", -1, 1, g0 + i, 1, y))
    sk.addConstraint(cons)
    for i, (x, y) in enumerate(pts):
        _named(sk, c0 + n + 2 * i, "%s%dx" % (pre, i), x)
        _named(sk, c0 + n + 2 * i + 1, "%s%dy" % (pre, i), y)
    return n


def circle(sk, pre, x, y, d):
    doc = sk.Document
    xv, yv, dv = (ev(doc, e) for e in (x, y, d))
    g = sk.addGeometry(Part.Circle(V(xv, yv, 0), V(0, 0, 1), dv / 2))
    c0 = sk.ConstraintCount
    sk.addConstraint([Sketcher.Constraint("Diameter", g, dv),
                      Sketcher.Constraint("DistanceX", -1, 1, g, 3, xv),
                      Sketcher.Constraint("DistanceY", -1, 1, g, 3, yv)])
    _named(sk, c0, pre + "_d", d)
    _named(sk, c0 + 1, pre + "_x", x)
    _named(sk, c0 + 2, pre + "_y", y)


def new_body(doc, name):
    old = doc.getObject(name)
    if old is not None:
        old.removeObjectsFromDocument()
        doc.removeObject(name)
    return doc.addObject("PartDesign::Body", name)


def xy_sketch(body, name, z=None):
    sk = body.newObject("Sketcher::SketchObject", name)
    xy = [f for f in body.Origin.OriginFeatures if f.Role == "XY_Plane"][0]
    sk.AttachmentSupport = [(xy, "")]
    sk.MapMode = "FlatFace"
    if z:
        sk.setExpression(".AttachmentOffset.Base.z", z)
    return sk


# ---------------------------------------------------------------- part shapes

def shape_fascia():
    edges = [
        plain(("y", P("v_e"))),
        jedge("FS", True, False, ("x", HW), ("x", HW + " - " + T)),
        jedge("FT", True, True, ("y", P("stack_h")), ("y", P("stack_h") + " - " + T)),
        jedge("FS", True, True, ("x", "-" + HW), ("x", "-" + HW + " + " + T)),
    ]
    holes = [
        rect("-Design.din_ap_w / 2", "Design.din_ap_w / 2",
             "Design.v_din - Design.din_ap_h / 2", "Design.v_din + Design.din_ap_h / 2"),
        rect("-Design.cl_ap_w / 2", "Design.cl_ap_w / 2",
             "Design.v_cl - Design.cl_ap_h / 2", "Design.v_cl + Design.cl_ap_h / 2"),
    ]
    return outline(edges), holes, _bolt_circles()


def _bolt_circles():
    return [("%sDesign.bolt_x" % sx, "Design.v_cl %s Design.bolt_v" % sv, P("bolt_d"))
            for sx in ("", "-") for sv in ("+", "-")]


def shape_top():
    edges = [
        jedge("FT", False, False, ("y", "0 mm"), ("y", T)),
        jedge("TS", False, False, ("x", HW), ("x", HW + " - " + T)),
        plain(("y", P("top_w"))),
        jedge("TS", False, True, ("x", "-" + HW), ("x", "-" + HW + " + " + T)),
    ]
    return outline(edges), [], []


def shape_side():
    edges = [
        jedge("SB", False, False, ("y", "0 mm"), ("y", T)),
        jedge("RS", True, False, ("x", P("base_d")), ("x", P("base_d") + " - " + T)),
        jedge("TS", True, True, ("Fv", P("stack_h")), ("Fv", P("stack_h") + " - " + T)),
        jedge("FS", False, True, ("Fw", "0 mm"), ("Fw", T)),
    ]
    holes = []
    for vd in (P("vdl"), P("vdu")):
        for j, tw in enumerate((P("tab_w0"), P("tab_w1"))):
            w0 = "%s - Design.ws / 2" % tw
            w1 = "%s + Design.tab_l + Design.ws / 2" % tw
            if j == 0:      # deck underside bears on the lower wall
                v0 = "%s - Design.bear" % vd
                v1 = "%s - Design.bear + Design.ws" % vd
            else:           # deck top bears on the upper wall
                v0 = "%s + Design.thk + Design.bear - Design.ws" % vd
                v1 = "%s + Design.thk + Design.bear" % vd
            holes.append(frect(w0, w1, v0, v1))
    # screwdriver access to the cheek M5 screws
    w0, w1 = _m5_span()
    for s in ("+", "-"):
        vc = "Design.v_din %s Design.m5_z" % s
        holes.append(frect(w0, w1, "%s - Design.acc_w / 2" % vc, "%s + Design.acc_w / 2" % vc))
    return outline(edges), holes, []


def _m5_span():
    return ("Design.m5_y0 - Design.enc_face - Design.m5_ext",
            "Design.m5_y1 - Design.enc_face + Design.m5_ext")


def shape_base():
    edges = [
        plain(("y", "-" + P("base_f"))),
        jedge("SB", True, False, ("x", HW), ("x", HW + " - " + T)),
        jedge("RB", True, True, ("y", P("base_d")), ("y", P("base_d") + " - " + T)),
        jedge("SB", True, True, ("x", "-" + HW), ("x", "-" + HW + " + " + T)),
    ]
    return outline(edges), [], []


def shape_rear():
    edges = [
        jedge("RB", False, False, ("y", "0 mm"), ("y", T)),
        jedge("RS", False, False, ("x", HW), ("x", HW + " - " + T)),
        plain(("y", P("rear_h"))),
        jedge("RS", False, True, ("x", "-" + HW), ("x", "-" + HW + " + " + T)),
    ]
    return outline(edges), [], []


def _tabs():
    return [(P("tab_w0"), "%s + Design.tab_l" % P("tab_w0")),
            (P("tab_w1"), "%s + Design.tab_l" % P("tab_w1"))]


def shape_deck():
    a, e = HW + " - " + T, HW
    w_end = "Design.thk + Design.cage_d"
    (t0a, t0b), (t1a, t1b) = _tabs()
    right = [(a, T), (a, t0a), (e, t0a), (e, t0b), (a, t0b), (a, t1a), (e, t1a), (e, t1b), (a, t1b), (a, w_end)]
    left = [("-(%s)" % x, y) for x, y in reversed(right)]
    holes = []
    for s in ("", "-"):
        for j, (ta, tb) in enumerate(_tabs()):
            if j == 0:      # cheek inner face bears
                u0 = "Design.cheek_x - Design.bear"
                u1 = "Design.cheek_x - Design.bear + Design.ws"
            else:           # cheek outer face bears
                u0 = "Design.cheek_x + Design.thk + Design.bear - Design.ws"
                u1 = "Design.cheek_x + Design.thk + Design.bear"
            if s:
                u0, u1 = "-(%s)" % u1, "-(%s)" % u0
            holes.append(rect(u0, u1, "%s - Design.ws / 2" % ta, "%s + Design.ws / 2" % tb))
    return right + left, holes, []


def shape_cheek():
    lo, lo_b = P("vdl"), "Design.vdl + Design.thk"
    hi, hi_t = P("vdu"), "Design.vdu + Design.thk"
    w0, w_end = T, "Design.thk + Design.cage_d"
    (t0a, t0b), (t1a, t1b) = _tabs()
    pts = [(w0, lo_b), (t0a, lo_b), (t0a, lo), (t0b, lo), (t0b, lo_b), (t1a, lo_b), (t1a, lo),
           (t1b, lo), (t1b, lo_b), (w_end, lo_b), (w_end, hi), (t1b, hi), (t1b, hi_t), (t1a, hi_t),
           (t1a, hi), (t0b, hi), (t0b, hi_t), (t0a, hi_t), (t0a, hi), (w0, hi)]
    m0, m1 = _m5_span()
    holes = []
    for s in ("+", "-"):
        vc = "Design.v_din %s Design.m5_z" % s
        holes.append(rect(m0, m1, "%s - Design.m5_w / 2" % vc, "%s + Design.m5_w / 2" % vc))
    return pts, holes, []


def _plate_outline():
    return rect("-Design.pl_w / 2", "Design.pl_w / 2",
                "Design.v_cl - Design.pl_h / 2", "Design.v_cl + Design.pl_h / 2")


def shape_dial_crow():
    circles = _bolt_circles() + [("%sDesign.dial_x" % s, P("v_cl"), P("crow_d")) for s in ("", "-")]
    return _plate_outline(), [], circles


def shape_dial_wave():
    holes = [rect("%sDesign.dial_x - Design.ws_cw / 2" % s, "%sDesign.dial_x + Design.ws_cw / 2" % s,
                  "Design.v_cl - Design.ws_ch / 2", "Design.v_cl + Design.ws_ch / 2") for s in ("", "-")]
    return _plate_outline(), holes, _bolt_circles()


SHAPES = {"Fascia": shape_fascia, "Side": shape_side, "Base": shape_base, "Top": shape_top,
          "Rear": shape_rear, "Deck": shape_deck, "Cheek": shape_cheek,
          "DialPlateCrow": shape_dial_crow, "DialPlateWave": shape_dial_wave}


# ---------------------------------------------------------------- stages

def start(path=FILE):
    if DOC_NAME in App.listDocuments():
        doc = App.getDocument(DOC_NAME)
    elif os.path.exists(path):
        doc = App.openDocument(path)
    else:
        doc = App.newDocument(DOC_NAME)
        doc.saveAs(path)
    sh = doc.getObject(SHEET) or doc.addObject("Spreadsheet::Sheet", SHEET)
    sh.set("A1", "Parameter")
    sh.set("B1", "Value")
    sh.set("C1", "Notes")
    for c in ("A1", "B1", "C1"):
        sh.setStyle(c, "bold")
    row = 2
    for alias, value, note in PARAMS:
        if alias == "#":
            sh.set("A%d" % row, "'" + value)
            sh.setStyle("A%d" % row, "bold")
            row += 1
            continue
        sh.set("A%d" % row, alias)
        sh.set("B%d" % row, value if value.startswith("=") else "=" + value)
        if sh.getAlias("B%d" % row) != alias:
            sh.setAlias("B%d" % row, alias)
        sh.set("C%d" % row, note)
        row += 1
    sh.setColumnWidth("A", 110)
    sh.setColumnWidth("C", 520)
    doc.recompute()
    return doc


def build_part(doc, name):
    pts, holes, circles = SHAPES[name]()
    b = new_body(doc, name)
    sk = xy_sketch(b, "sk_" + name)
    poly(sk, "o", pts)
    for i, h in enumerate(holes):
        poly(sk, "h%d_" % i, h)
    for i, (x, y, d) in enumerate(circles):
        circle(sk, "c%d" % i, x, y, d)
    pad = b.newObject("PartDesign::Pad", "Pad_" + name)
    pad.Profile = sk
    pad.setExpression("Length", T)
    b.Tip = pad
    if name == "DialPlateWave":
        rs = xy_sketch(b, "sk_WaveRecess", T)
        for i, s in enumerate(("", "-")):
            circle(rs, "r%d" % i, "%sDesign.dial_x" % s, P("v_cl"), P("ws_d"))
        pk = b.newObject("PartDesign::Pocket", "WaveRecess")
        pk.Profile = rs
        pk.setExpression("Length", P("ws_pk"))
        b.Tip = pk
    doc.recompute()
    if App.GuiUp:
        c = COLOURS[name]
        b.ViewObject.ShapeColor = c
        b.Tip.ViewObject.ShapeColor = c
    bb = b.Shape.BoundBox
    return "%s: %d geo, fully constrained %s, volume %.0f mm3, X %.1f..%.1f Y %.1f..%.1f, valid %s" % (
        name, sk.GeometryCount, sk.FullyConstrained, b.Shape.Volume, bb.XMin, bb.XMax,
        bb.YMin, bb.YMax, b.Shape.isValid())


# rotations taking a flat part (Z = thickness) into its place
ROT_CYCLIC = App.Rotation(App.Matrix(0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1))  # x->y, y->z, z->x
ROT_UP = App.Rotation(V(1, 0, 0), 90)                                                   # y->z, z->-y

# link name, body, parent container, rotation, {placement axis: expression}
LINKS = [
    ("L_Base", "Base", "Stand", App.Rotation(), {}),
    ("L_SideL", "Side", "Stand", ROT_CYCLIC, {"x": "-" + HW}),
    ("L_SideR", "Side", "Stand", ROT_CYCLIC, {"x": HW + " - " + T}),
    ("L_Rear", "Rear", "Stand", ROT_UP, {"y": P("base_d")}),
    ("L_Fascia", "Fascia", "Stack", ROT_UP, {"y": T}),
    ("L_Top", "Top", "Stack", App.Rotation(), {"z": P("stack_h") + " - " + T}),
    ("L_DeckLower", "Deck", "Stack", App.Rotation(), {"z": P("vdl")}),
    ("L_DeckUpper", "Deck", "Stack", App.Rotation(), {"z": P("vdu")}),
    ("L_CheekL", "Cheek", "Stack", ROT_CYCLIC, {"x": "-Design.cheek_x - Design.thk"}),
    ("L_CheekR", "Cheek", "Stack", ROT_CYCLIC, {"x": P("cheek_x")}),
    ("L_DialCrow", "DialPlateCrow", "Stack", ROT_UP, {"y": "2 * Design.thk"}),
    ("L_DialWave", "DialPlateWave", "Stack", ROT_UP, {"y": "2 * Design.thk"}),
]


def build_assembly(doc):
    for n in [l[0] for l in LINKS] + ["Stack", "Stand"]:
        if doc.getObject(n):
            doc.removeObject(n)
    stand = doc.addObject("App::Part", "Stand")
    stack = doc.addObject("App::Part", "Stack")
    stand.addObject(stack)
    stack.Placement = App.Placement(V(0, 0, 5.2), App.Rotation(V(-1, 0, 0), 15))
    stack.setExpression(".Placement.Base.z", T)
    stack.setExpression(".Placement.Rotation.Angle", P("tilt"))
    parents = {"Stand": stand, "Stack": stack}
    for name, body, parent, rot, exprs in LINKS:
        ln = doc.addObject("App::Link", name)
        ln.LinkedObject = doc.getObject(body)
        ln.Placement = App.Placement(V(0, 0, 0), rot)
        for ax, e in exprs.items():
            ln.setExpression(".Placement.Base." + ax, e)
        parents[parent].addObject(ln)
    doc.recompute()
    if App.GuiUp:
        doc.getObject("L_DialWave").ViewObject.Visibility = False   # the two dial plates are alternatives
    return "Stack rotation %s, base %s" % (stack.Placement.Rotation.toEuler(), stack.Placement.Base)


def add_refs(doc):
    """Fit-check references from the double-DIN enclosure exports, in the Stack frame."""
    out = []
    for name, f, col in (("Ref_Bezel", "Godwit_DD_Bezel.step", (0.16, 0.17, 0.19)),
                         ("Ref_Chassis", "Godwit_DD_Chassis.step", (0.55, 0.57, 0.60)),
                         ("Ref_RearPlate", "Godwit_DD_RearPlate.step", (0.22, 0.23, 0.26))):
        if doc.getObject(name):
            doc.removeObject(name)
        shp = Part.read(os.path.join(EXPORTS, f))
        o = doc.addObject("Part::Feature", name)
        o.Shape = shp
        o.setExpression(".Placement.Base.y", "-" + P("enc_face"))
        o.setExpression(".Placement.Base.z", P("v_din"))
        doc.getObject("Stack").addObject(o)
        if App.GuiUp:
            o.ViewObject.ShapeColor = col
        bb = shp.BoundBox
        out.append("%s local X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f" % (
            name, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
    doc.recompute()
    return "\n".join(out)


def tidy(doc):
    for o in doc.Objects:
        if o.TypeId in ("Sketcher::SketchObject", "App::Origin", "App::Plane", "App::Line", "App::Point"):
            o.ViewObject.Visibility = False
    for b in [o for o in doc.Objects if o.TypeId == "PartDesign::Body"]:
        for f in b.Group:
            if f.TypeId.startswith("PartDesign::"):
                f.ViewObject.Visibility = (f is b.Tip)
        b.ViewObject.Visibility = False     # the links show the parts
    for n in ("Stand", "Stack"):
        doc.getObject(n).ViewObject.Visibility = True


def world_shape(doc, name):
    """A link or reference with its containers' placements applied."""
    o = doc.getObject(name)
    s = o.Shape.copy()
    par = o.getParentGeoFeatureGroup()
    while par is not None:
        s.Placement = par.Placement.multiply(s.Placement)
        par = par.getParentGeoFeatureGroup()
    return s


def check_fit(doc):
    """Assembly checks in WORLD coordinates (the Stack tilt applied):
    overlapping pairs, joints that should touch, chassis clearances."""
    import itertools
    names = [l[0] for l in LINKS if l[0] != "L_DialWave"] + ["Ref_Bezel", "Ref_Chassis", "Ref_RearPlate"]
    W = {n: world_shape(doc, n) for n in names}
    over = []
    for a, b in itertools.combinations(names, 2):
        if W[a].BoundBox.intersect(W[b].BoundBox):
            v = W[a].common(W[b]).Volume
            if v > 0.01:
                over.append((a, b, round(v, 2)))
    touch = [("L_Fascia", "L_SideL"), ("L_Fascia", "L_Top"), ("L_Top", "L_SideR"), ("L_SideL", "L_Base"),
             ("L_Rear", "L_SideR"), ("L_Rear", "L_Base"), ("L_DeckLower", "L_SideL"), ("L_DeckUpper", "L_SideR"),
             ("L_CheekL", "L_DeckLower"), ("L_CheekR", "L_DeckUpper"), ("L_DialCrow", "L_Fascia"),
             ("Ref_Chassis", "L_Fascia")]
    gaps = [(a, b, round(W[a].distToShape(W[b])[0], 3)) for a, b in touch]
    clr = [(b, round(W["Ref_Chassis"].distToShape(W[b])[0], 3))
           for b in ("L_CheekL", "L_CheekR", "L_DeckLower", "L_DeckUpper", "L_Top")]
    return dict(overlaps=over, joint_gaps=gaps, chassis_clearance=clr)


def export_step(doc, path=None):
    """The assembled stand (no enclosure) as one STEP, world coordinates."""
    path = path or os.path.join(EXPORTS, "Godwit_CenterStack.step")
    shapes = [world_shape(doc, l[0]) for l in LINKS if l[0] != "L_DialWave"]
    Part.makeCompound(shapes).exportStep(path)
    back = Part.read(path)
    return path, len(back.Solids), round(back.Volume)


def render(doc, path):
    """Isometric picture of the stand with the enclosure fitted (CAM objects hidden)."""
    import FreeCADGui as Gui
    gd = Gui.getDocument(doc.Name)
    v = gd.activeView() or gd.mdiViewsOfType("Gui::View3DInventor")[0]
    for o in doc.Objects:
        if o.Name.startswith(("Job", "Clone", "Stock", "OP_", "Dogbone")):
            o.ViewObject.Visibility = False
    v.viewIsometric()
    v.fitAll()
    v.saveImage(path, 1600, 1200, "White")
    return path


def sizes(doc):
    """Flat size of every part, for the cut list."""
    out = []
    for n in PARTS:
        bb = doc.getObject(n).Shape.BoundBox
        out.append((n, round(bb.XLength, 1), round(bb.YLength, 1)))
    return out
