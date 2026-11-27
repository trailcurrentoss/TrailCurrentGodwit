"""Parametric reference model of the Godwit 4-channel class-D amplifier board.

Every dimension comes from the `Dims` spreadsheet the script creates.
Each sketch dimension and pad length is bound to a sheet alias, so a
caliper correction is a one-cell change followed by a recompute.

Coordinates: X across the 86 mm board width (8-way speaker terminal block
on the X = 0 edge), Y along the board length, Z up. The origin is the
bottom-left corner of the PCB underside, so the PCB top is at Z = pcb_t.

The values were measured photogrammetrically from a 120 fps phone video
with a steel rule in frame (see MEASUREMENTS.md). Check the dimensions
that matter to the enclosure with calipers before cutting material.

Run inside FreeCAD, either all at once:

    exec(open("<path>/build_amplifier_board.py").read()); build_all()

or stage by stage (each stage is short, so FreeCAD's GUI never stalls):

    exec(open(...).read()); doc = start(); build_pcb(doc); build_heatsink(doc); ...
"""

import FreeCAD as App
import Part
import Sketcher

V = App.Vector
DOC_NAME = "AmplifierBoard"
SHEET = "Dims"

# alias, value, note
PARAMS = [
    # PCB
    ("pcb_w", "86.0 mm", "board width (X)"),
    ("pcb_l", "128.6 mm", "board length (Y)"),
    ("pcb_t", "1.6 mm", "board thickness"),
    ("hole_inset", "3.0 mm", "mounting hole centre from each edge (80 x 122.6 centres)"),
    ("hole_d", "3.2 mm", "mounting hole diameter (M3)"),
    ("lead_tail", "1.5 mm", "through-hole lead tails below the PCB (keep-out, not modelled)"),
    # heatsink
    ("hs_x0", "15.3 mm", "heatsink left face"),
    ("hs_x1", "63.9 mm", "heatsink right face"),
    ("hs_y0", "15.3 mm", "heatsink end nearest the toroid"),
    ("hs_y1", "113.0 mm", "heatsink end nearest the inputs"),
    ("hs_h", "26.6 mm", "heatsink fin tops above PCB top"),
    ("hs_gap", "2.0 mm", "PCB top to heatsink underside (amplifier ICs)"),
    ("hs_base_t", "4.0 mm", "heatsink base plate thickness"),
    ("hs_fins", "14", "fin count across X"),
    ("hs_fin_t", "1.2 mm", "fin thickness"),
    ("hs_cuts", "8", "cross-cuts through the fins along Y"),
    ("hs_cut_w", "1.5 mm", "cross-cut width"),
    # 2200 uF bulk capacitors (two rows of four)
    ("cap_d", "12.5 mm", "bulk capacitor diameter"),
    ("cap_h", "25.0 mm", "bulk capacitor body height"),
    ("cap_lift", "0.5 mm", "bulk capacitor standoff above PCB"),
    ("cap_n", "4", "capacitors per row"),
    ("cap_pitch", "13.2 mm", "capacitor pitch along X"),
    ("cap_x0", "19.7 mm", "first capacitor centre X"),
    ("capB_y", "7.0 mm", "bottom (toroid end) row centre Y"),
    ("capT_y", "120.9 mm", "top (input end) row centre Y"),
    # small electrolytics column beside the heatsink
    ("sc_x", "67.6 mm", "small capacitor column centre X"),
    ("sc_d", "5.0 mm", "small capacitor diameter"),
    ("sc_h", "18.4 mm", "small capacitor top above PCB"),
    ("sc_n", "8", "small capacitors in the column"),
    ("sc_y0", "45.3 mm", "first small capacitor centre Y"),
    ("sc_pitch", "5.46 mm", "small capacitor pitch along Y"),
    ("scL_d", "6.3 mm", "larger end-of-column capacitor diameter"),
    ("scL_h", "23.5 mm", "larger end-of-column capacitor top above PCB"),
    ("scL_y1", "39.1 mm", "larger capacitor near the toroid, centre Y"),
    ("scL_y2", "90.6 mm", "larger capacitor near the inputs, centre Y"),
    # barrier terminal blocks, 7.62 mm pitch
    ("tb_pitch", "7.62 mm", "barrier terminal pitch"),
    ("tb_w", "12.7 mm", "terminal block depth (X)"),
    ("tb_end", "1.6 mm", "terminal block length beyond n x pitch"),
    ("tb_body_h", "7.0 mm", "terminal block body top above PCB"),
    ("tb_fence_h", "13.0 mm", "barrier fence top above PCB (estimate)"),
    ("tb_fence_t", "1.0 mm", "barrier fence thickness"),
    ("tb_yc", "64.7 mm", "terminal block centre Y (both blocks)"),
    ("tb_screw_in", "5.7 mm", "screw centre from the board-side face"),
    ("tb_screw_d", "5.5 mm", "screw head diameter"),
    ("tb_screw_h", "8.0 mm", "screw head top above PCB"),
    ("tb_wire_w", "5.0 mm", "wire entry width"),
    ("tb_wire_h", "4.0 mm", "wire entry height"),
    ("tb_wire_z", "1.5 mm", "wire entry bottom above PCB"),
    ("tb8_x0", "0.5 mm", "8-way speaker block outer face X"),
    ("tb3_x0", "73.6 mm", "3-way power block board-side face X"),
    # toroidal inductor, standing on edge, axis along X
    ("ind_x0", "69.5 mm", "toroid face nearest the heatsink"),
    ("ind_t", "9.3 mm", "toroid thickness including windings"),
    ("ind_od", "24.7 mm", "toroid outside diameter including windings"),
    ("ind_id", "11.0 mm", "toroid hole diameter (estimate)"),
    ("ind_y", "20.45 mm", "toroid centre Y"),
    ("ind_lift", "1.0 mm", "toroid bottom above PCB"),
    # JST XH 3-pin headers (B3B-XH-A datasheet body)
    ("xh_w", "5.75 mm", "XH header depth (X)"),
    ("xh_l", "9.9 mm", "XH header length (Y)"),
    ("xh_h", "7.0 mm", "XH header height"),
    ("xh_wall", "0.8 mm", "XH shroud wall"),
    ("xh_x", "83.1 mm", "XH header centre X"),
    ("xh1_y", "86.45 mm", "INPUT1 centre Y"),
    ("xh2_y", "114.5 mm", "INPUT2 centre Y"),
    # DIP switch
    ("dip_x", "80.1 mm", "DIP switch centre X"),
    ("dip_y", "100.5 mm", "DIP switch centre Y"),
    ("dip_l", "9.9 mm", "DIP switch body X"),
    ("dip_w", "6.5 mm", "DIP switch body Y"),
    ("dip_h", "4.5 mm", "DIP switch top above PCB"),
    # axial rectifier diodes (DO-201AD)
    ("d_x1", "77.1 mm", "first diode centre X"),
    ("d_x2", "82.3 mm", "second diode centre X"),
    ("d_y", "42.9 mm", "diode body centre Y"),
    ("d_len", "9.5 mm", "diode body length"),
    ("d_dia", "5.3 mm", "diode body diameter"),
    ("d_lift", "0.3 mm", "diode body standoff above PCB"),
]

COLOURS = {
    "PCB": (0.12, 0.37, 0.66),
    "Heatsink": (0.17, 0.71, 0.78),
    "BulkCaps": (0.12, 0.35, 0.20),
    "BulkCapTops": (0.80, 0.80, 0.82),
    "SmallCaps": (0.10, 0.30, 0.17),
    "SpeakerTerminal": (0.18, 0.62, 0.42),
    "PowerTerminal": (0.18, 0.62, 0.42),
    "TerminalScrews": (0.75, 0.75, 0.75),
    "Toroid": (0.78, 0.46, 0.20),
    "InputHeaders": (0.95, 0.95, 0.93),
    "DipSwitch": (0.85, 0.20, 0.24),
    "Diodes": (0.12, 0.12, 0.12),
}


# ---------------------------------------------------------------- helpers

def ev(doc, expr):
    """Evaluate a sheet expression to a float in mm (or a plain number)."""
    q = doc.getObject(SHEET).evalExpression(expr)
    return float(getattr(q, "Value", q))


def plane(body, role):
    return {f.Role: f for f in body.Origin.OriginFeatures}[role]


def axis(body, role):
    return {f.Role: f for f in body.Origin.OriginFeatures}[role]


def new_body(doc, name):
    old = doc.getObject(name)
    if old is not None:
        old.removeObjectsFromDocument()
        doc.removeObject(name)
    return doc.addObject("PartDesign::Body", name)


def sketch(body, name, role="XY_Plane", offset=None):
    """Sketch on an origin plane. `offset` is an expression for the plane
    offset along its normal: XY -> +Z, XZ -> -Y, YZ -> +X (FreeCAD 1.1)."""
    sk = body.newObject("Sketcher::SketchObject", name)
    sk.AttachmentSupport = [(plane(body, role), "")]
    sk.MapMode = "FlatFace"
    if offset:
        sk.setExpression(".AttachmentOffset.Base.z", offset)
    return sk


def _named(sk, con, name, expr):
    i = sk.addConstraint(con)
    sk.renameConstraint(i, name)
    sk.setExpression("Constraints." + name, expr)


def rect(sk, pre, x, y, w, h):
    """Constrained rectangle; x, y (lower-left) and w, h are sheet expressions."""
    doc = sk.Document
    xv, yv, wv, hv = (ev(doc, e) for e in (x, y, w, h))
    pts = [(xv, yv), (xv + wv, yv), (xv + wv, yv + hv), (xv, yv + hv)]
    g0 = sk.GeometryCount
    for i in range(4):
        a, b = pts[i], pts[(i + 1) % 4]
        sk.addGeometry(Part.LineSegment(V(a[0], a[1], 0), V(b[0], b[1], 0)))
    sk.addConstraint([Sketcher.Constraint("Coincident", g0 + i, 2, g0 + (i + 1) % 4, 1) for i in range(4)]
                     + [Sketcher.Constraint("Horizontal", g0), Sketcher.Constraint("Horizontal", g0 + 2),
                        Sketcher.Constraint("Vertical", g0 + 1), Sketcher.Constraint("Vertical", g0 + 3)])
    _named(sk, Sketcher.Constraint("DistanceX", -1, 1, g0, 1, xv), pre + "_x", x)
    _named(sk, Sketcher.Constraint("DistanceY", -1, 1, g0, 1, yv), pre + "_y", y)
    _named(sk, Sketcher.Constraint("DistanceX", g0, 1, g0, 2, wv), pre + "_w", w)
    _named(sk, Sketcher.Constraint("DistanceY", g0 + 1, 1, g0 + 1, 2, hv), pre + "_h", h)


def circle(sk, pre, x, y, d):
    doc = sk.Document
    xv, yv, dv = (ev(doc, e) for e in (x, y, d))
    g = sk.addGeometry(Part.Circle(V(xv, yv, 0), V(0, 0, 1), dv / 2))
    _named(sk, Sketcher.Constraint("Diameter", g, dv), pre + "_d", d)
    _named(sk, Sketcher.Constraint("DistanceX", -1, 1, g, 3, xv), pre + "_x", x)
    _named(sk, Sketcher.Constraint("DistanceY", -1, 1, g, 3, yv), pre + "_y", y)


def pad(body, name, sk, length, reversed_=False, side=None):
    p = body.newObject("PartDesign::Pad", name)
    p.Profile = sk
    p.setExpression("Length", length)
    p.Reversed = reversed_
    if side:
        p.SideType = side
    body.Tip = p
    return p


def pocket(body, name, sk, length, reversed_=False):
    p = body.newObject("PartDesign::Pocket", name)
    p.Profile = sk
    p.setExpression("Length", length)
    p.Reversed = reversed_
    body.Tip = p
    return p


def pattern(body, name, feats, axis_role, occ, length):
    p = body.newObject("PartDesign::LinearPattern", name)
    p.Originals = feats
    p.Direction = (axis(body, axis_role), [""])
    p.setExpression("Occurrences", occ)
    p.setExpression("Length", length)
    body.Tip = p
    return p


def finish(body):
    doc = body.Document
    doc.recompute()
    c = COLOURS.get(body.Name)
    if c and App.GuiUp:
        body.ViewObject.ShapeColor = c
        body.Tip.ViewObject.ShapeColor = c
    return "%s: volume %.1f mm3, bbox %s, valid %s" % (
        body.Name, body.Shape.Volume, body.Shape.BoundBox, body.Shape.isValid())


# ---------------------------------------------------------------- stages

def start(path=None):
    doc = App.getDocument(DOC_NAME) if DOC_NAME in App.listDocuments() else App.newDocument(DOC_NAME)
    sh = doc.getObject(SHEET) or doc.addObject("Spreadsheet::Sheet", SHEET)
    sh.set("A1", "Parameter")
    sh.set("B1", "Value")
    sh.set("C1", "Notes")
    for i, (alias, value, note) in enumerate(PARAMS, start=2):
        sh.set("A%d" % i, alias)
        sh.set("B%d" % i, value)
        if sh.getAlias("B%d" % i) != alias:
            sh.setAlias("B%d" % i, alias)
        sh.set("C%d" % i, note)
    doc.recompute()
    if path:
        doc.saveAs(path)
    return doc


def build_pcb(doc):
    b = new_body(doc, "PCB")
    sk = sketch(b, "sk_PCB")
    rect(sk, "pcb", "0 mm", "0 mm", "Dims.pcb_w", "Dims.pcb_l")
    for i, (x, y) in enumerate([("Dims.hole_inset", "Dims.hole_inset"),
                                ("Dims.pcb_w - Dims.hole_inset", "Dims.hole_inset"),
                                ("Dims.hole_inset", "Dims.pcb_l - Dims.hole_inset"),
                                ("Dims.pcb_w - Dims.hole_inset", "Dims.pcb_l - Dims.hole_inset")]):
        circle(sk, "hole%d" % i, x, y, "Dims.hole_d")
    pad(b, "pad_PCB", sk, "Dims.pcb_t")
    return finish(b)


def build_heatsink(doc):
    b = new_body(doc, "Heatsink")
    z_base = "Dims.pcb_t + Dims.hs_gap"
    sk = sketch(b, "sk_HsBase", offset=z_base)
    rect(sk, "base", "Dims.hs_x0", "Dims.hs_y0", "Dims.hs_x1 - Dims.hs_x0", "Dims.hs_y1 - Dims.hs_y0")
    pad(b, "pad_HsBase", sk, "Dims.hs_base_t")
    fin_h = "Dims.hs_h - Dims.hs_gap - Dims.hs_base_t"
    sk = sketch(b, "sk_Fin", offset=z_base + " + Dims.hs_base_t")
    rect(sk, "fin", "Dims.hs_x0", "Dims.hs_y0", "Dims.hs_fin_t", "Dims.hs_y1 - Dims.hs_y0")
    fin = pad(b, "pad_Fin", sk, fin_h)
    pattern(b, "Fins", [fin], "X_Axis", "Dims.hs_fins", "Dims.hs_x1 - Dims.hs_x0 - Dims.hs_fin_t")
    span = "(Dims.hs_y1 - Dims.hs_y0)"
    sk = sketch(b, "sk_CrossCut", offset="Dims.pcb_t + Dims.hs_h")
    rect(sk, "cut", "Dims.hs_x0 - 1 mm",
         "Dims.hs_y0 + %s / (Dims.hs_cuts + 1) - Dims.hs_cut_w / 2" % span,
         "Dims.hs_x1 - Dims.hs_x0 + 2 mm", "Dims.hs_cut_w")
    cut = pocket(b, "pocket_CrossCut", sk, fin_h)
    pattern(b, "CrossCuts", [cut], "Y_Axis", "Dims.hs_cuts",
            "%s * (Dims.hs_cuts - 1) / (Dims.hs_cuts + 1)" % span)
    return finish(b)


def build_bulk_caps(doc):
    b = new_body(doc, "BulkCaps")
    sk = sketch(b, "sk_BulkCaps", offset="Dims.pcb_t + Dims.cap_lift")
    circle(sk, "capB", "Dims.cap_x0", "Dims.capB_y", "Dims.cap_d")
    circle(sk, "capT", "Dims.cap_x0", "Dims.capT_y", "Dims.cap_d")
    p = pad(b, "pad_BulkCaps", sk, "Dims.cap_h")
    pattern(b, "BulkCapRows", [p], "X_Axis", "Dims.cap_n", "Dims.cap_pitch * (Dims.cap_n - 1)")
    r1 = finish(b)
    t = new_body(doc, "BulkCapTops")
    sk = sketch(t, "sk_BulkCapTops", offset="Dims.pcb_t + Dims.cap_lift + Dims.cap_h")
    circle(sk, "topB", "Dims.cap_x0", "Dims.capB_y", "Dims.cap_d - 1.2 mm")
    circle(sk, "topT", "Dims.cap_x0", "Dims.capT_y", "Dims.cap_d - 1.2 mm")
    p = pad(t, "pad_BulkCapTops", sk, "0.3 mm")
    pattern(t, "BulkCapTopRows", [p], "X_Axis", "Dims.cap_n", "Dims.cap_pitch * (Dims.cap_n - 1)")
    return r1 + "\n" + finish(t)


def build_small_caps(doc):
    b = new_body(doc, "SmallCaps")
    sk = sketch(b, "sk_SmallCap", offset="Dims.pcb_t")
    circle(sk, "sc", "Dims.sc_x", "Dims.sc_y0", "Dims.sc_d")
    p = pad(b, "pad_SmallCap", sk, "Dims.sc_h")
    pattern(b, "SmallCapColumn", [p], "Y_Axis", "Dims.sc_n", "Dims.sc_pitch * (Dims.sc_n - 1)")
    sk = sketch(b, "sk_LargerCaps", offset="Dims.pcb_t")
    circle(sk, "scL1", "Dims.sc_x", "Dims.scL_y1", "Dims.scL_d")
    circle(sk, "scL2", "Dims.sc_x", "Dims.scL_y2", "Dims.scL_d")
    pad(b, "pad_LargerCaps", sk, "Dims.scL_h")
    return finish(b)


def _terminal(doc, name, n, x0, screw_x, wire_face_x, wire_reversed):
    """Barrier block: body, n+1 fences, n wire entries on the outer face."""
    b = new_body(doc, name)
    length = "(%d * Dims.tb_pitch + Dims.tb_end)" % n
    y0 = "Dims.tb_yc - %s / 2" % length
    sk = sketch(b, "sk_%sBody" % name, offset="Dims.pcb_t")
    rect(sk, "body", x0, y0, "Dims.tb_w", length)
    pad(b, "pad_%sBody" % name, sk, "Dims.tb_body_h")
    sk = sketch(b, "sk_%sFence" % name, offset="Dims.pcb_t + Dims.tb_body_h")
    rect(sk, "fence", x0, "Dims.tb_yc - %d * Dims.tb_pitch / 2 - Dims.tb_fence_t / 2" % n,
         "Dims.tb_w", "Dims.tb_fence_t")
    f = pad(b, "pad_%sFence" % name, sk, "Dims.tb_fence_h - Dims.tb_body_h")
    pattern(b, "%sFences" % name, [f], "Y_Axis", str(n + 1), "%d * Dims.tb_pitch" % n)
    # wire entry on the outer face: sketch on YZ (local x = world Y, local y = world Z)
    sk = sketch(b, "sk_%sWire" % name, role="YZ_Plane", offset=wire_face_x)
    rect(sk, "wire", "Dims.tb_yc - (%d - 1) * Dims.tb_pitch / 2 - Dims.tb_wire_w / 2" % n,
         "Dims.pcb_t + Dims.tb_wire_z", "Dims.tb_wire_w", "Dims.tb_wire_h")
    w = pocket(b, "pocket_%sWire" % name, sk, "Dims.tb_w - Dims.tb_screw_in", reversed_=wire_reversed)
    pattern(b, "%sWires" % name, [w], "Y_Axis", str(n), "%d * Dims.tb_pitch" % (n - 1))
    # screw heads go in a separate body so they can be coloured as metal
    return finish(b), (screw_x, "Dims.tb_yc - (%d - 1) * Dims.tb_pitch / 2" % n, n)


def build_terminals(doc):
    r8, s8 = _terminal(doc, "SpeakerTerminal", 8, "Dims.tb8_x0",
                       "Dims.tb8_x0 + Dims.tb_w - Dims.tb_screw_in", "Dims.tb8_x0", True)
    r3, s3 = _terminal(doc, "PowerTerminal", 3, "Dims.tb3_x0",
                       "Dims.tb3_x0 + Dims.tb_screw_in", "Dims.tb3_x0 + Dims.tb_w", False)
    b = new_body(doc, "TerminalScrews")
    z = "Dims.pcb_t + Dims.tb_body_h"
    feats = []
    for tag, (sx, sy, n) in (("Spk", s8), ("Pwr", s3)):
        sk = sketch(b, "sk_Screw" + tag, offset=z)
        circle(sk, "screw" + tag, sx, sy, "Dims.tb_screw_d")
        p = pad(b, "pad_Screw" + tag, sk, "Dims.tb_screw_h - Dims.tb_body_h")
        pattern(b, "Screws" + tag, [p], "Y_Axis", str(n), "%d * Dims.tb_pitch" % (n - 1))
    return "\n".join([r8, r3, finish(b)])


def build_toroid(doc):
    b = new_body(doc, "Toroid")
    # profile in the plane Y = ind_y; XZ sketch: local x = X, local y = Z, offset -> -Y
    sk = sketch(b, "sk_Toroid", role="XZ_Plane", offset="-Dims.ind_y")
    zc = "(Dims.pcb_t + Dims.ind_lift + Dims.ind_od / 2)"
    rect(sk, "ring", "Dims.ind_x0", zc + " + Dims.ind_id / 2", "Dims.ind_t", "(Dims.ind_od - Dims.ind_id) / 2")
    zcv = ev(doc, zc)
    g = sk.addGeometry(Part.LineSegment(V(0, zcv, 0), V(10, zcv, 0)), True)
    sk.addConstraint(Sketcher.Constraint("Horizontal", g))
    _named(sk, Sketcher.Constraint("DistanceY", -1, 1, g, 1, zcv), "axis_z", zc)
    doc.recompute()
    r = b.newObject("PartDesign::Revolution", "rev_Toroid")
    r.Profile = sk
    r.ReferenceAxis = (sk, ["Axis0"])
    r.Angle = 360
    b.Tip = r
    return finish(b)


def build_inputs(doc):
    b = new_body(doc, "InputHeaders")
    sk = sketch(b, "sk_XH", offset="Dims.pcb_t")
    rect(sk, "xh", "Dims.xh_x - Dims.xh_w / 2", "Dims.xh1_y - Dims.xh_l / 2", "Dims.xh_w", "Dims.xh_l")
    p = pad(b, "pad_XH", sk, "Dims.xh_h")
    sk = sketch(b, "sk_XHCavity", offset="Dims.pcb_t + Dims.xh_h")
    rect(sk, "cav", "Dims.xh_x - Dims.xh_w / 2 + Dims.xh_wall", "Dims.xh1_y - Dims.xh_l / 2 + Dims.xh_wall",
         "Dims.xh_w - 2 * Dims.xh_wall", "Dims.xh_l - 2 * Dims.xh_wall")
    c = pocket(b, "pocket_XHCavity", sk, "Dims.xh_h - 1.5 mm")
    pattern(b, "XHPair", [p, c], "Y_Axis", "2", "Dims.xh2_y - Dims.xh1_y")
    r1 = finish(b)
    d = new_body(doc, "DipSwitch")
    sk = sketch(d, "sk_Dip", offset="Dims.pcb_t")
    rect(sk, "dip", "Dims.dip_x - Dims.dip_l / 2", "Dims.dip_y - Dims.dip_w / 2", "Dims.dip_l", "Dims.dip_w")
    pad(d, "pad_Dip", sk, "Dims.dip_h")
    return r1 + "\n" + finish(d)


def build_diodes(doc):
    b = new_body(doc, "Diodes")
    # XZ sketch at Y = d_y, padded symmetrically along Y
    sk = sketch(b, "sk_Diodes", role="XZ_Plane", offset="-Dims.d_y")
    z = "Dims.pcb_t + Dims.d_lift + Dims.d_dia / 2"
    circle(sk, "d1", "Dims.d_x1", z, "Dims.d_dia")
    circle(sk, "d2", "Dims.d_x2", z, "Dims.d_dia")
    pad(b, "pad_Diodes", sk, "Dims.d_len", side="Symmetric")
    return finish(b)


def tidy(doc):
    """Show only each Body's Tip; hide sketches and origin geometry."""
    if not App.GuiUp:
        return
    for o in doc.Objects:
        if o.TypeId in ("Sketcher::SketchObject", "App::Origin", "App::Plane", "App::Line", "App::Point"):
            o.ViewObject.Visibility = False
    for b in [o for o in doc.Objects if o.TypeId == "PartDesign::Body"]:
        b.ViewObject.Visibility = True
        for f in b.Group:
            if f.TypeId.startswith("PartDesign::"):
                f.ViewObject.Visibility = (f is b.Tip)


STAGES = [build_pcb, build_heatsink, build_bulk_caps, build_small_caps,
          build_terminals, build_toroid, build_inputs, build_diodes]


def build_all(path=None):
    doc = start(path)
    out = [stage(doc) for stage in STAGES]
    tidy(doc)
    doc.recompute()
    return "\n".join(out)
