"""Parametric double-DIN enclosure for the Godwit head unit.

A generic ISO 7736 double-DIN chassis built around the UC-595 7" HDMI
panel, the Godwit 4-channel amplifier board and a single-board computer
(Raspberry Pi 5 by default; Radxa and Rockchip boards fit the same trays).

Every dimension lives in the `Design` spreadsheet. Sketch dimensions, pad
lengths and pattern counts are bound to its aliases, so a change is one
cell followed by a recompute.

Coordinates
    X across the unit, centred on the DIN axis (+X is the panel's
      connector side).
    Z up, centred on the DIN axis, so the chassis runs Z = -din_h/2 .. +din_h/2.
    Y into the dash: Y = 0 is the bezel front face, +Y towards the rear.

Parts (one PartDesign Body each)
    Bezel      front frame, screen window, UC-595 ear bosses, M3 inserts
    Chassis    178 x 100 DIN sleeve with front flange, tray channels,
               M5 side-bracket pads, vents, rear-plate posts
    TrayLower  slotted deck on the floor (amplifier)
    TrayUpper  slotted deck above the amplifier (SBC, USB hub, modules)
    Rail       slotted standoff rail; links of it sit on either tray
    RearPlate  Deutsch DT6 (power), DTM8 (speakers), DTM6 (CAN) on a
               shelf, USB-C x2, 3.5 mm mic jack, SMA x3, vents

Mounting system
    Each tray has rows of X-running slots at row_p pitch. A board whose
    hole spacing along Y is a multiple of row_p (the amplifier's 80 mm is)
    takes standoffs straight in the slots. Anything else sits on two Rails:
    a rail bolts across two rows (it slides along X in the tray slots) and
    its own Y slot takes an M2.5 standoff anywhere along it, held by a nut
    in the rail's underside channel. Two rails give any rectangular
    four-hole pattern up to about 100 x 90 mm.

Run inside FreeCAD, stage by stage (each stage is short so the GUI never
stalls):

    exec(open("<path>/build_double_din.py").read())
    doc = start("<path>/GodwitDoubleDin.FCStd")
    build_bezel(doc); build_chassis_shell(doc); build_chassis_inside(doc)
    build_chassis_vents(doc); build_tray(doc, "TrayLower")
    build_tray(doc, "TrayUpper"); build_rail(doc); build_rear_plate(doc)
    build_rear_housings(doc, plugin_dir)
    add_refs(doc, amp_step, pi5_step, uc595_step); tidy(doc)

Deutsch housings come from the TrailCurrent FreeCAD Tools workbench
(`deutsch_housing_geometry`), so `plugin_dir` is that workbench's folder.
"""

import math

import FreeCAD as App
import Part
import Sketcher

V = App.Vector
DOC_NAME = "GodwitDoubleDin"
SHEET = "Design"

# alias, value, note.  Rows that start with "#" are section headings.
PARAMS = [
    ("#", "Double-DIN chassis (ISO 7736)", ""),
    ("din_w", "178 mm", "chassis outside width (fits a standard double-DIN cage)"),
    ("din_h", "100 mm", "chassis outside height"),
    ("wall", "2.5 mm", "chassis wall"),
    ("in_w", "=din_w - 2 * wall", "inside width"),
    ("in_h", "=din_h - 2 * wall", "inside height"),
    ("ch_len", "160 mm", "flange front to rear face (depth behind the bezel)"),
    ("#", "Bezel (overhangs the DIN opening like a trim ring)", ""),
    ("bez_w", "196 mm", "bezel width (room for the chassis screws beside the panel)"),
    ("bez_h", "122 mm", "bezel height (the UC-595 ears span 114 mm)"),
    ("bez_d", "12 mm", "bezel depth, front face to chassis flange"),
    ("bez_wall", "3 mm", "bezel side wall"),
    ("face_t", "3 mm", "face thickness in front of the panel glass"),
    ("fl_t", "3 mm", "chassis flange thickness"),
    ("y_back", "=bez_d + ch_len", "rear face of the chassis (Y)"),
    ("y_in0", "=bez_d + fl_t", "inside front of the chassis (Y)"),
    ("#", "Display UC-595 7in HDMI (mounted 180 deg: connectors on +X)", ""),
    ("disp_w", "165 mm", "panel width"),
    ("disp_h", "100 mm", "panel height"),
    ("disp_t", "5 mm", "panel thickness, glass to back face"),
    ("disp_clr", "0.5 mm", "locating clearance per side"),
    ("disp_dx", "-4 mm", "panel centre X; negative leaves room for right-angle adapters on the +X connectors"),
    ("disp_dz", "0 mm", "panel centre Z"),
    ("win_w", "155 mm", "visible area width"),
    ("win_h", "87 mm", "visible area height"),
    ("win_l", "7 mm", "panel left edge to visible area"),
    ("win_b", "4 mm", "panel bottom edge to visible area"),
    ("loc_t", "1.6 mm", "locating rib thickness round the panel"),
    ("loc_h", "3 mm", "locating rib height behind the face"),
    ("ear_px", "159.05 mm", "ear hole pitch X (vendor STEP)"),
    ("ear_pz", "108.05 mm", "ear hole pitch Z"),
    ("ear_dz", "-0.15 mm", "ear pattern centre offset from panel centre, Z"),
    ("ear_boss_d", "6 mm", "ear boss diameter"),
    ("ear_pilot", "2.2 mm", "M2.5 self-tap pilot (ear holes measure 2.88)"),
    ("ear_skin", "1.2 mm", "face material left in front of the ear pilots"),
    ("#", "Bezel to chassis screws (M3 into heat-set inserts in the bezel)", ""),
    ("bs_x", "92.5 mm", "side screw X (+/-)"),
    ("bs_z", "45 mm", "side screw Z (+/-)"),
    ("bs_zt", "55.5 mm", "top/bottom centre screw Z (+/-)"),
    ("bs_boss_d", "7.5 mm", "insert boss diameter"),
    ("bs_insert", "4.0 mm", "M3 heat-set insert hole"),
    ("bs_insert_l", "6 mm", "insert hole depth"),
    ("bs_clear", "3.4 mm", "M3 clearance in the flange"),
    ("#", "Trays", ""),
    ("tray_t", "3 mm", "tray thickness"),
    ("tray_clr", "0.4 mm", "tray side clearance per side"),
    ("tray_y0", "=y_in0 + 2 mm", "tray front edge (clear of the panel's rear parts)"),
    ("lt_z0", "=-in_h / 2 + 3 mm", "lower tray underside (3 mm under it for screw heads)"),
    ("ut_z0", "0 mm", "upper tray underside"),
    ("ledge_w", "4 mm", "channel strip width from the side wall"),
    ("ledge_h", "4 mm", "upper tray support strip height"),
    ("ret_h", "3 mm", "retaining strip height above a tray"),
    ("strip_y0", "25 mm", "channel strips start here (front zone left free for panel cables)"),
    ("slot_w", "3.4 mm", "tray slot width (M3)"),
    ("slot_in", "8 mm", "slot inner end from the centre line"),
    ("slot_out", "6 mm", "slot outer end to tray edge"),
    ("row_p", "10 mm", "slot row pitch along Y"),
    ("row_m", "4 mm", "tray front edge to first row centre"),
    ("#", "Rear plate", ""),
    ("rp_t", "3 mm", "rear plate thickness"),
    ("rp_clr", "0.3 mm", "rear plate fit clearance per side"),
    ("post", "10 mm", "corner post size"),
    ("post_len", "15 mm", "corner post length"),
    ("post_hole", "2.5 mm", "M3 self-tap pilot in post"),
    ("post_pilot_l", "12 mm", "pilot depth"),
    ("rp_screw", "3.4 mm", "M3 clearance in plate"),
    ("shelf_l", "45 mm", "Deutsch shelf depth (housings are 41-43 mm)"),
    ("shelf_w", "104 mm", "Deutsch shelf width"),
    ("shelf_top", "=lt_z0", "shelf top face Z (housings stand on it)"),
    ("shelf_y0", "=y_back - shelf_l", "shelf front edge Y"),
    ("h_gap", "6 mm", "gap between housings (fingers on the latches)"),
    ("dt6_w", "24.63 mm", "DT 6-pos housing width (workbench BREP)"),
    ("dt6_h", "26.05 mm", "DT 6-pos housing height"),
    ("dtm8_w", "35.42 mm", "DTM 8-pos housing width"),
    ("dtm8_h", "22.11 mm", "DTM 8-pos housing height"),
    ("dtm6_w", "23.0 mm", "DTM 6-pos housing width"),
    ("dtm6_h", "25.69 mm", "DTM 6-pos housing height"),
    ("dtm8_x", "0 mm", "speakers DTM04-08PA centre X (4 speakers, +/-)"),
    ("dt6_x", "=dtm8_x + dtm8_w / 2 + h_gap + dt6_w / 2", "power DT04-6P centre X (BATT x2, GND x2, ACC, spare)"),
    ("dtm6_x", "=dtm8_x - dtm8_w / 2 - h_gap - dtm6_w / 2", "CAN DTM04-6P centre X (H, L, GND per bus)"),
    ("io_z", "30 mm", "row of panel-mount I/O, centre Z"),
    ("usbc_d", "16.2 mm", "USB-C bulkhead hole - PLACEHOLDER, set to the bulkhead you buy"),
    ("usbc1_x", "-62 mm", "dial 1 USB-C X"),
    ("usbc2_x", "-38 mm", "dial 2 USB-C X"),
    ("jack_d", "6.2 mm", "3.5 mm panel jack (M6 thread) - mic in to the USB sound card"),
    ("jack_x", "-18 mm", "mic jack X"),
    ("sma_d", "6.5 mm", "SMA bulkhead hole (1/4-36)"),
    ("sma_x0", "10 mm", "first SMA X"),
    ("sma_p", "22 mm", "SMA pitch (GNSS, Wi-Fi/BT, FM/SDR)"),
    ("sma_n", "3", "SMA count"),
    ("rv_w", "4 mm", "rear vent slot width"),
    ("rv_h", "12 mm", "rear vent slot height"),
    ("rv_z", "-8 mm", "rear vent centre Z (between the housings and the upper tray)"),
    ("rv_p", "8 mm", "rear vent pitch"),
    ("rv_n", "19", "rear vent count"),
    ("#", "Rails (slotted standoff carriers)", ""),
    ("rail_w", "12 mm", "rail width"),
    ("rail_h", "6 mm", "rail height"),
    ("rail_n", "10", "tray rows between the rail's end bolts"),
    ("rail_end", "5 mm", "end bolt centre from rail end"),
    ("rail_l", "=rail_n * row_p + 2 * rail_end", "rail length"),
    ("rail_slot_w", "2.9 mm", "M2.5 slot"),
    ("rail_slot_m", "10 mm", "slot end from rail end"),
    ("nut_w", "5.4 mm", "nut channel width (M2.5 nut is 5.0 A/F)"),
    ("nut_d", "2.5 mm", "nut channel depth from the underside"),
    ("rail_bolt", "3.4 mm", "M3 end bolt clearance"),
    ("rail_cb_d", "6.2 mm", "end bolt counterbore (M3 socket head)"),
    ("rail_cb_h", "3.2 mm", "counterbore depth"),
    ("#", "Side bracket holes (M5 heat-set inserts in internal pads)", ""),
    ("mnt_d", "6.4 mm", "M5 heat-set insert hole"),
    ("mnt_y0", "40 mm", "first column Y"),
    ("mnt_yp", "20 mm", "column pitch"),
    ("mnt_cols", "6", "columns (read when the chassis is built; rebuild to change)"),
    ("mnt_z", "35 mm", "row Z (+/-)"),
    ("mnt_pad", "12 mm", "pad size"),
    ("mnt_pad_t", "5 mm", "pad thickness inside the wall"),
    ("#", "Chassis vents", ""),
    ("cv_w", "4 mm", "vent slot width (X)"),
    ("cv_l", "50 mm", "vent slot length (Y)"),
    ("cv_p", "10 mm", "vent pitch"),
    ("cv_n", "13", "vent count"),
    ("cvt_y", "100 mm", "top vents centre Y"),
    ("cvb_y", "71 mm", "bottom vents centre Y (under the amplifier)"),
    ("#", "Reference placement (fit check only)", ""),
    ("amp_yr", "114 mm", "amplifier speaker-terminal edge Y (board turned 90 deg)"),
    ("amp_xc", "0 mm", "amplifier centre X"),
    ("amp_so", "6 mm", "amplifier standoff height"),
    ("pi_x0", "-80 mm", "Pi 5 board left edge X"),
    ("pi_y0", "26 mm", "Pi 5 board front edge Y (micro-HDMI side)"),
    ("pi_so", "6 mm", "Pi standoff height on the rails"),
    ("rail_y0", "16 mm", "rail front end Y (end bolts land on tray rows)"),
]

COLOURS = {
    "Bezel": (0.16, 0.17, 0.19),
    "Chassis": (0.55, 0.57, 0.60),
    "TrayLower": (0.32, 0.55, 0.36),
    "TrayUpper": (0.32, 0.55, 0.36),
    "Rail": (0.85, 0.55, 0.15),
    "RearPlate": (0.22, 0.23, 0.26),
}


# ---------------------------------------------------------------- helpers

def ev(doc, expr):
    """Evaluate a sheet expression to a float in mm (or a plain number)."""
    q = doc.getObject(SHEET).evalExpression(expr)
    return float(getattr(q, "Value", q))


def feature(body, role):
    return {f.Role: f for f in body.Origin.OriginFeatures}[role]


def new_body(doc, name):
    old = doc.getObject(name)
    if old is not None:
        old.removeObjectsFromDocument()
        doc.removeObject(name)
    return doc.addObject("PartDesign::Body", name)


def sketch(body, name, role, offset=None):
    """Sketch on an origin plane.

    XY: local (x, y) = world (X, Y); offset moves +Z.
    XZ: local (x, y) = world (X, Z); offset moves -Y, so pass "-(...)" for +Y.
        On an XZ sketch a Pad grows -Y and a Pocket cuts +Y; Reversed flips both.
    YZ: local (x, y) = world (Y, Z); offset moves +X.
    """
    sk = body.newObject("Sketcher::SketchObject", name)
    sk.AttachmentSupport = [(feature(body, role), "")]
    sk.MapMode = "FlatFace"
    if offset:
        sk.setExpression(".AttachmentOffset.Base.z", offset)
    return sk


def _named(sk, con, name, expr):
    i = sk.addConstraint(con)
    sk.renameConstraint(i, name)
    sk.setExpression("Constraints." + name, expr)


def rect_c(sk, pre, cx, cy, w, h):
    """Fully constrained rectangle centred on (cx, cy); all args are sheet expressions."""
    doc = sk.Document
    cxv, cyv, wv, hv = (ev(doc, e) for e in (cx, cy, w, h))
    x0, y0 = cxv - wv / 2, cyv - hv / 2
    pts = [(x0, y0), (x0 + wv, y0), (x0 + wv, y0 + hv), (x0, y0 + hv)]
    g0 = sk.GeometryCount
    for i in range(4):
        a, b = pts[i], pts[(i + 1) % 4]
        sk.addGeometry(Part.LineSegment(V(a[0], a[1], 0), V(b[0], b[1], 0)))
    sk.addConstraint([Sketcher.Constraint("Coincident", g0 + i, 2, g0 + (i + 1) % 4, 1) for i in range(4)]
                     + [Sketcher.Constraint("Horizontal", g0), Sketcher.Constraint("Horizontal", g0 + 2),
                        Sketcher.Constraint("Vertical", g0 + 1), Sketcher.Constraint("Vertical", g0 + 3)])
    _named(sk, Sketcher.Constraint("DistanceX", g0, 1, g0, 2, wv), pre + "_w", w)
    _named(sk, Sketcher.Constraint("DistanceY", g0 + 1, 1, g0 + 1, 2, hv), pre + "_h", h)
    _named(sk, Sketcher.Constraint("DistanceX", -1, 1, g0, 1, x0), pre + "_x", "(%s) - (%s) / 2" % (cx, w))
    _named(sk, Sketcher.Constraint("DistanceY", -1, 1, g0, 1, y0), pre + "_y", "(%s) - (%s) / 2" % (cy, h))


def circle(sk, pre, x, y, d):
    doc = sk.Document
    xv, yv, dv = (ev(doc, e) for e in (x, y, d))
    g = sk.addGeometry(Part.Circle(V(xv, yv, 0), V(0, 0, 1), dv / 2))
    _named(sk, Sketcher.Constraint("Diameter", g, dv), pre + "_d", d)
    _named(sk, Sketcher.Constraint("DistanceX", -1, 1, g, 3, xv), pre + "_x", x)
    _named(sk, Sketcher.Constraint("DistanceY", -1, 1, g, 3, yv), pre + "_y", y)


def _extrude(body, kind, name, sk, length, reversed_, side, through):
    f = body.newObject("PartDesign::" + kind, name)
    f.Profile = sk
    if through:
        f.Type = "ThroughAll"
    else:
        f.setExpression("Length", length)
    f.Reversed = reversed_
    if side:
        f.SideType = side
    body.Tip = f
    return f


def pad(body, name, sk, length, reversed_=False, side=None):
    return _extrude(body, "Pad", name, sk, length, reversed_, side, False)


def pocket(body, name, sk, length=None, reversed_=False, side=None, through=False):
    return _extrude(body, "Pocket", name, sk, length, reversed_, side, through)


def pattern(body, name, feats, axis_role, occ, length, reversed_=False):
    p = body.newObject("PartDesign::LinearPattern", name)
    p.Originals = feats
    p.Direction = (feature(body, axis_role), [""])
    p.Reversed = reversed_
    p.setExpression("Occurrences", occ)
    p.setExpression("Length", length)
    body.Tip = p
    return p


def mirror(body, name, feats, plane_role):
    m = body.newObject("PartDesign::Mirrored", name)
    m.Originals = feats
    m.MirrorPlane = (feature(body, plane_role), [""])
    body.Tip = m
    return m


def report(body):
    doc = body.Document
    doc.recompute()
    c = COLOURS.get(body.Name)
    if c and App.GuiUp:
        body.ViewObject.ShapeColor = c
        body.Tip.ViewObject.ShapeColor = c
    bb = body.Shape.BoundBox
    return "%s: volume %.1f mm3, X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f, valid %s" % (
        body.Name, body.Shape.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax,
        body.Shape.isValid())


# ---------------------------------------------------------------- stages

def start(path=None):
    doc = App.getDocument(DOC_NAME) if DOC_NAME in App.listDocuments() else App.newDocument(DOC_NAME)
    sh = doc.getObject(SHEET) or doc.addObject("Spreadsheet::Sheet", SHEET)
    sh.set("A1", "Parameter")
    sh.set("B1", "Value")
    sh.set("C1", "Notes")
    row = 2
    for alias, value, note in PARAMS:
        if alias == "#":
            sh.set("A%d" % row, "'" + value)
            row += 1
            continue
        sh.set("A%d" % row, alias)
        # always a formula: a bare "-4 mm" would be stored as text
        sh.set("B%d" % row, value if value.startswith("=") else "=" + value)
        if sh.getAlias("B%d" % row) != alias:
            sh.setAlias("B%d" % row, alias)
        sh.set("C%d" % row, note)
        row += 1
    doc.recompute()
    if path:
        doc.saveAs(path)
    return doc


def build_bezel(doc):
    b = new_body(doc, "Bezel")
    # outer block, Y 0 .. bez_d
    sk = sketch(b, "sk_BezelOuter", "XZ_Plane")
    rect_c(sk, "bez", "0 mm", "0 mm", "Design.bez_w", "Design.bez_h")
    pad(b, "BezelBlock", sk, "Design.bez_d", reversed_=True)
    # hollow behind the face, Y face_t .. bez_d
    sk = sketch(b, "sk_BezelCavity", "XZ_Plane", "-Design.bez_d")
    rect_c(sk, "cav", "0 mm", "0 mm", "Design.bez_w - 2 * Design.bez_wall",
           "Design.bez_h - 2 * Design.bez_wall")
    pocket(b, "BezelCavity", sk, "Design.bez_d - Design.face_t", reversed_=True)
    # screen window through the face
    sk = sketch(b, "sk_Window", "XZ_Plane")
    rect_c(sk, "win",
           "Design.disp_dx - Design.disp_w / 2 + Design.win_l + Design.win_w / 2",
           "Design.disp_dz - Design.disp_h / 2 + Design.win_b + Design.win_h / 2",
           "Design.win_w", "Design.win_h")
    pocket(b, "Window", sk, through=True, side="Symmetric")
    # locating rib round the panel, on the face back
    sk = sketch(b, "sk_LocRib", "XZ_Plane", "-Design.face_t")
    rect_c(sk, "ribo", "Design.disp_dx", "Design.disp_dz",
           "Design.disp_w + 2 * Design.disp_clr + 2 * Design.loc_t",
           "Design.disp_h + 2 * Design.disp_clr + 2 * Design.loc_t")
    rect_c(sk, "ribi", "Design.disp_dx", "Design.disp_dz",
           "Design.disp_w + 2 * Design.disp_clr", "Design.disp_h + 2 * Design.disp_clr")
    pad(b, "LocRib", sk, "Design.loc_h", reversed_=True)
    # UC-595 ear bosses, face back to the ears' front face
    sk = sketch(b, "sk_EarBoss", "XZ_Plane", "-Design.face_t")
    for i, (sx, sz) in enumerate([(1, 1), (-1, 1), (1, -1), (-1, -1)]):
        circle(sk, "eb%d" % i, "Design.disp_dx %s Design.ear_px / 2" % ("+" if sx > 0 else "-"),
               "Design.disp_dz + Design.ear_dz %s Design.ear_pz / 2" % ("+" if sz > 0 else "-"),
               "Design.ear_boss_d")
    pad(b, "EarBoss", sk, "Design.disp_t", reversed_=True)
    sk = sketch(b, "sk_EarPilot", "XZ_Plane", "-(Design.face_t + Design.disp_t)")
    for i, (sx, sz) in enumerate([(1, 1), (-1, 1), (1, -1), (-1, -1)]):
        circle(sk, "ep%d" % i, "Design.disp_dx %s Design.ear_px / 2" % ("+" if sx > 0 else "-"),
               "Design.disp_dz + Design.ear_dz %s Design.ear_pz / 2" % ("+" if sz > 0 else "-"),
               "Design.ear_pilot")
    pocket(b, "EarPilot", sk, "Design.disp_t + Design.face_t - Design.ear_skin", reversed_=True)
    # chassis screw bosses with M3 inserts: four at the sides, two top/bottom centre
    pts = [("Design.bs_x", "Design.bs_z"), ("-Design.bs_x", "Design.bs_z"),
           ("Design.bs_x", "-Design.bs_z"), ("-Design.bs_x", "-Design.bs_z"),
           ("0 mm", "Design.bs_zt"), ("0 mm", "-Design.bs_zt")]
    sk = sketch(b, "sk_ScrewBoss", "XZ_Plane", "-Design.face_t")
    for i, (x, z) in enumerate(pts):
        circle(sk, "sb%d" % i, x, z, "Design.bs_boss_d")
    pad(b, "ScrewBoss", sk, "Design.bez_d - Design.face_t", reversed_=True)
    sk = sketch(b, "sk_Insert", "XZ_Plane", "-Design.bez_d")
    for i, (x, z) in enumerate(pts):
        circle(sk, "si%d" % i, x, z, "Design.bs_insert")
    pocket(b, "Insert", sk, "Design.bs_insert_l", reversed_=True)
    return report(b)


def build_chassis_shell(doc):
    c = new_body(doc, "Chassis")
    sk = sketch(c, "sk_Sleeve", "XZ_Plane", "-Design.bez_d")
    rect_c(sk, "slv", "0 mm", "0 mm", "Design.din_w", "Design.din_h")
    pad(c, "Sleeve", sk, "Design.ch_len", reversed_=True)
    sk = sketch(c, "sk_Flange", "XZ_Plane", "-Design.bez_d")
    rect_c(sk, "fl", "0 mm", "0 mm", "Design.bez_w", "Design.bez_h")
    pad(c, "Flange", sk, "Design.fl_t", reversed_=True)
    sk = sketch(c, "sk_Bore", "XZ_Plane", "-Design.bez_d")
    rect_c(sk, "bore", "0 mm", "0 mm", "Design.in_w", "Design.in_h")
    pocket(c, "Bore", sk, through=True, side="Symmetric")
    pts = [("Design.bs_x", "Design.bs_z"), ("-Design.bs_x", "Design.bs_z"),
           ("Design.bs_x", "-Design.bs_z"), ("-Design.bs_x", "-Design.bs_z"),
           ("0 mm", "Design.bs_zt"), ("0 mm", "-Design.bs_zt")]
    sk = sketch(c, "sk_FlangeHoles", "XZ_Plane", "-Design.bez_d")
    for i, (x, z) in enumerate(pts):
        circle(sk, "fh%d" % i, x, z, "Design.bs_clear")
    pocket(c, "FlangeHoles", sk, "Design.fl_t")
    return report(c)


def build_chassis_inside(doc):
    c = doc.getObject("Chassis")
    # Tray channels, +X wall (mirrored to -X). Sketch on the XZ plane at the
    # strip start and pad rearwards. Lower tray rests on the floor strip.
    x_strip = "Design.in_w / 2 - Design.ledge_w / 2"
    sk = sketch(c, "sk_Channels", "XZ_Plane", "-Design.strip_y0")
    # lower: floor support under the tray edge, then retaining strip above it
    rect_c(sk, "lsup", x_strip, "(-Design.in_h / 2 + Design.lt_z0) / 2",
           "Design.ledge_w", "Design.lt_z0 + Design.in_h / 2")
    rect_c(sk, "lret", x_strip,
           "Design.lt_z0 + Design.tray_t + Design.tray_clr + Design.ret_h / 2",
           "Design.ledge_w", "Design.ret_h")
    # upper: support strip under the tray, retaining strip above it
    rect_c(sk, "usup", x_strip, "Design.ut_z0 - Design.ledge_h / 2",
           "Design.ledge_w", "Design.ledge_h")
    rect_c(sk, "uret", x_strip,
           "Design.ut_z0 + Design.tray_t + Design.tray_clr + Design.ret_h / 2",
           "Design.ledge_w", "Design.ret_h")
    pad(c, "Channels", sk, "Design.y_back - Design.rp_t - Design.strip_y0", reversed_=True)
    # Side bracket pads (+X, upper row), patterned along Y, mirrored to Z and X
    # Every column is drawn in one sketch (positions bound to the sheet; the
    # count is read at build time) rather than patterned.
    cols = int(round(ev(doc, "Design.mnt_cols")))
    sk = sketch(c, "sk_MntPad", "YZ_Plane", "Design.in_w / 2 - Design.mnt_pad_t")
    for k in range(cols):
        for j, zs in enumerate(("-", "")):
            rect_c(sk, "mp%d%d" % (k, j), "Design.mnt_y0 + %d * Design.mnt_yp" % k,
                   "%sDesign.mnt_z" % zs, "Design.mnt_pad", "Design.mnt_pad")
    pad(c, "MntPad", sk, "Design.mnt_pad_t + 0.01 mm")
    sk = sketch(c, "sk_MntHole", "YZ_Plane", "Design.din_w / 2")
    for k in range(cols):
        for j, zs in enumerate(("-", "")):
            circle(sk, "mh%d%d" % (k, j), "Design.mnt_y0 + %d * Design.mnt_yp" % k,
                   "%sDesign.mnt_z" % zs, "Design.mnt_d")
    pocket(c, "MntHole", sk, "Design.wall + Design.mnt_pad_t")
    # Rear-plate corner posts with M3 pilots
    sk = sketch(c, "sk_Posts", "XZ_Plane", "-(Design.y_back - Design.rp_t - Design.post_len)")
    for i, (sx, sz) in enumerate([(1, 1), (-1, 1), (1, -1), (-1, -1)]):
        rect_c(sk, "po%d" % i,
               "%sDesign.in_w / 2 %s Design.post / 2" % ("" if sx > 0 else "-", "-" if sx > 0 else "+"),
               "%sDesign.in_h / 2 %s Design.post / 2" % ("" if sz > 0 else "-", "-" if sz > 0 else "+"),
               "Design.post", "Design.post")
    pad(c, "Posts", sk, "Design.post_len", reversed_=True)
    sk = sketch(c, "sk_PostPilot", "XZ_Plane", "-(Design.y_back - Design.rp_t)")
    for i, (sx, sz) in enumerate([(1, 1), (-1, 1), (1, -1), (-1, -1)]):
        circle(sk, "pp%d" % i,
               "%sDesign.in_w / 2 %s Design.post / 2" % ("" if sx > 0 else "-", "-" if sx > 0 else "+"),
               "%sDesign.in_h / 2 %s Design.post / 2" % ("" if sz > 0 else "-", "-" if sz > 0 else "+"),
               "Design.post_hole")
    pocket(c, "PostPilot", sk, "Design.post_pilot_l", reversed_=True)
    return report(c)


def mirror_chassis_sides(doc):
    """Mirror the +X channels and the side pads/holes; split from build_chassis_inside
    so each call stays short."""
    c = doc.getObject("Chassis")
    mirror(c, "MirrorSidesX", [doc.getObject("Channels"), doc.getObject("MntPad"),
                               doc.getObject("MntHole")], "YZ_Plane")
    return report(c)


def build_chassis_vents(doc):
    c = doc.getObject("Chassis")
    x0 = "-(Design.cv_n - 1) * Design.cv_p / 2"
    sk = sketch(c, "sk_TopVent", "XY_Plane", "Design.din_h / 2")
    rect_c(sk, "tv", x0, "Design.cvt_y", "Design.cv_w", "Design.cv_l")
    tv = pocket(c, "TopVent", sk, "Design.wall")
    pattern(c, "TopVents", [tv], "X_Axis", "Design.cv_n", "(Design.cv_n - 1) * Design.cv_p")
    sk = sketch(c, "sk_BotVent", "XY_Plane", "-Design.din_h / 2")
    rect_c(sk, "bv", x0, "Design.cvb_y", "Design.cv_w", "Design.cv_l")
    bv = pocket(c, "BotVent", sk, "Design.wall", reversed_=True)
    pattern(c, "BotVents", [bv], "X_Axis", "Design.cv_n", "(Design.cv_n - 1) * Design.cv_p")
    return report(c)


def tray_dims(name):
    """(underside Z, rear edge Y) expressions for a tray."""
    if name == "TrayLower":
        return "Design.lt_z0", "Design.shelf_y0 - 0.5 mm"
    return "Design.ut_z0", "Design.y_back - Design.rp_t - 0.5 mm"


def build_tray(doc, name):
    t = new_body(doc, name)
    z0, y1 = tray_dims(name)
    w = "Design.in_w - 2 * Design.tray_clr"
    sk = sketch(t, "sk_%s_Plate" % name, "XY_Plane", z0)
    rect_c(sk, "tp", "0 mm", "(Design.tray_y0 + %s) / 2" % y1, w, "%s - Design.tray_y0" % y1)
    pad(t, "%s_Plate" % name, sk, "Design.tray_t")
    # the two slots of the first row (either side of the centre web), patterned along Y
    slot_len = "(%s) / 2 - Design.slot_out - Design.slot_in" % w
    sk = sketch(t, "sk_%s_Slot" % name, "XY_Plane", "%s + Design.tray_t" % z0)
    for side, sign in (("R", ""), ("L", "-")):
        rect_c(sk, "sl" + side, "%s(Design.slot_in + (%s) / 2)" % (sign, slot_len),
               "Design.tray_y0 + Design.row_m", slot_len, "Design.slot_w")
    s = pocket(t, "%s_Slot" % name, sk, "Design.tray_t")
    rows = "floor((%s - Design.tray_y0 - 2 * Design.row_m) / Design.row_p) + 1" % y1
    pattern(t, "%s_Rows" % name, [s], "Y_Axis", rows, "(%s - 1) * Design.row_p" % rows)
    return report(t)


def build_rail(doc):
    """Rail modelled in the upper-tray position at X = 0; links place copies."""
    r = new_body(doc, "Rail")
    z0 = "Design.ut_z0 + Design.tray_t"
    yc = "Design.rail_y0 + Design.rail_l / 2"
    sk = sketch(r, "sk_RailBar", "XY_Plane", z0)
    rect_c(sk, "rb", "0 mm", yc, "Design.rail_w", "Design.rail_l")
    pad(r, "RailBar", sk, "Design.rail_h")
    sk = sketch(r, "sk_RailSlot", "XY_Plane", "%s + Design.rail_h" % z0)
    rect_c(sk, "rs", "0 mm", yc, "Design.rail_slot_w", "Design.rail_l - 2 * Design.rail_slot_m")
    pocket(r, "RailSlot", sk, "Design.rail_h")
    sk = sketch(r, "sk_NutChannel", "XY_Plane", z0)
    rect_c(sk, "nc", "0 mm", yc, "Design.nut_w", "Design.rail_l - 2 * Design.rail_slot_m + Design.nut_w")
    pocket(r, "NutChannel", sk, "Design.nut_d", reversed_=True)
    ends = ["Design.rail_y0 + Design.rail_end", "Design.rail_y0 + Design.rail_l - Design.rail_end"]
    sk = sketch(r, "sk_RailBolt", "XY_Plane", "%s + Design.rail_h" % z0)
    for i, y in enumerate(ends):
        circle(sk, "rbh%d" % i, "0 mm", y, "Design.rail_bolt")
    pocket(r, "RailBolt", sk, "Design.rail_h")
    sk = sketch(r, "sk_RailCB", "XY_Plane", "%s + Design.rail_h" % z0)
    for i, y in enumerate(ends):
        circle(sk, "rcb%d" % i, "0 mm", y, "Design.rail_cb_d")
    pocket(r, "RailCB", sk, "Design.rail_cb_h")
    return report(r)


def build_rear_plate(doc):
    p = new_body(doc, "RearPlate")
    rp_w = "Design.in_w - 2 * Design.rp_clr"
    rp_h = "Design.in_h - 2 * Design.rp_clr"
    sk = sketch(p, "sk_Plate", "XZ_Plane", "-(Design.y_back - Design.rp_t)")
    rect_c(sk, "rp", "0 mm", "0 mm", rp_w, rp_h)
    pad(p, "Plate", sk, "Design.rp_t", reversed_=True)
    # shelf for the Deutsch housings, running out to the plate's rear face
    sk = sketch(p, "sk_Shelf", "XY_Plane", "-Design.in_h / 2 + Design.rp_clr")
    rect_c(sk, "sh", "0 mm", "Design.y_back - Design.shelf_l / 2", "Design.shelf_w", "Design.shelf_l")
    pad(p, "Shelf", sk, "Design.shelf_top + Design.in_h / 2 - Design.rp_clr")
    # openings the housings pass through (their walls refill them)
    sk = sketch(p, "sk_HousingCut", "XZ_Plane", "-Design.y_back")
    for key in ("dt6", "dtm8", "dtm6"):
        rect_c(sk, "hc_" + key, "Design.%s_x" % key,
               "Design.shelf_top + Design.%s_h / 2" % key, "Design.%s_w" % key, "Design.%s_h" % key)
    pocket(p, "HousingCut", sk, "Design.rp_t", reversed_=True)
    # corner screws
    sk = sketch(p, "sk_PlateScrews", "XZ_Plane", "-Design.y_back")
    for i, (sx, sz) in enumerate([(1, 1), (-1, 1), (1, -1), (-1, -1)]):
        circle(sk, "ps%d" % i,
               "%sDesign.in_w / 2 %s Design.post / 2" % ("" if sx > 0 else "-", "-" if sx > 0 else "+"),
               "%sDesign.in_h / 2 %s Design.post / 2" % ("" if sz > 0 else "-", "-" if sz > 0 else "+"),
               "Design.rp_screw")
    pocket(p, "PlateScrews", sk, "Design.rp_t", reversed_=True)
    # panel-mount I/O: two USB-C dial ports, mic jack, SMA row
    sk = sketch(p, "sk_IO", "XZ_Plane", "-Design.y_back")
    circle(sk, "usb1", "Design.usbc1_x", "Design.io_z", "Design.usbc_d")
    circle(sk, "usb2", "Design.usbc2_x", "Design.io_z", "Design.usbc_d")
    circle(sk, "jack", "Design.jack_x", "Design.io_z", "Design.jack_d")
    pocket(p, "IO", sk, "Design.rp_t", reversed_=True)
    sk = sketch(p, "sk_SMA", "XZ_Plane", "-Design.y_back")
    circle(sk, "sma", "Design.sma_x0", "Design.io_z", "Design.sma_d")
    s = pocket(p, "SMA", sk, "Design.rp_t", reversed_=True)
    pattern(p, "SMARow", [s], "X_Axis", "Design.sma_n", "(Design.sma_n - 1) * Design.sma_p")
    # vents between the housings and the upper tray
    sk = sketch(p, "sk_RearVent", "XZ_Plane", "-Design.y_back")
    rect_c(sk, "rv", "-(Design.rv_n - 1) * Design.rv_p / 2", "Design.rv_z", "Design.rv_w", "Design.rv_h")
    v = pocket(p, "RearVent", sk, "Design.rp_t", reversed_=True)
    pattern(p, "RearVents", [v], "X_Axis", "Design.rv_n", "(Design.rv_n - 1) * Design.rv_p")
    return report(p)


def build_rear_housings(doc, plugin_dir):
    """Deutsch termination housings on the shelf, fronts flush with the plate's rear face.

    Uses the TrailCurrent FreeCAD Tools workbench's own feature class, so each
    housing is a real feature that double-clicks into the workbench's editor.
    The frame anchor X is bound to the sheet, so moving a connector is one cell.
    """
    import sys
    if plugin_dir not in sys.path:
        sys.path.append(plugin_dir)
    import deutsch_housing_command as dhc
    p = doc.getObject("RearPlate")
    out = []
    for key, var, label in (("dt6", "DT6P", "Power_DT04_6P"),
                            ("dtm8", "DTM8P", "Speakers_DTM04_08PA"),
                            ("dtm6", "DTM6P", "CAN_DTM04_6P")):
        old = doc.getObject(label)
        if old is not None:
            doc.removeObject(old.Name)
        h = doc.addObject("PartDesign::FeatureAdditivePython", label)
        dhc._DeutschHousingProxy(h)
        if App.GuiUp:
            dhc._DeutschHousingViewProvider(h.ViewObject)
        dhc._add_properties(h, var)
        x = ev(doc, "Design.%s_x" % key)
        frame = (V(x, ev(doc, "Design.y_back"), ev(doc, "Design.shelf_top")),
                 V(-1, 0, 0), V(0, -1, 0), V(0, 0, 1))
        dhc._store(h, var, frame, True, 0.0, 0.0, 0.0, "shelf top", "rear edge")
        h.Housing_OriginalBody = p.Name
        p.addObject(h)
        h.setExpression("Housing_FrameAnchor.x", "Design.%s_x" % key)
        h.setExpression("Housing_FrameAnchor.y", "Design.y_back")
        h.setExpression("Housing_FrameAnchor.z", "Design.shelf_top")
        p.Tip = h
        doc.recompute()
        out.append("%s %s volume now %.1f" % (label, var, p.Shape.Volume))
    out.append(report(p))
    return "\n".join(out)


def add_refs(doc, amp_step=None, pi5_step=None, uc595_step=None):
    """Fit-check references, positioned from the sheet. Not printed parts."""
    out = []

    def ref(name, path, colour):
        old = doc.getObject(name)
        if old is not None:
            doc.removeObject(old.Name)
        f = doc.addObject("Part::Feature", name)
        f.Shape = Part.read(path)
        if App.GuiUp:
            f.ViewObject.ShapeColor = colour
            f.ViewObject.Transparency = 30
        return f

    if amp_step:
        a = ref("Ref_Amplifier", amp_step, (0.12, 0.37, 0.66))
        # board turned -90 deg about Z: board X (86) runs to -Y, board Y (128.6) runs to +X
        a.setExpression(".Placement.Rotation.Angle", "-90 deg")
        a.setExpression(".Placement.Rotation.Axis.z", "1")
        a.setExpression(".Placement.Base.x", "Design.amp_xc - 64.3 mm")
        a.setExpression(".Placement.Base.y", "Design.amp_yr")
        a.setExpression(".Placement.Base.z", "Design.lt_z0 + Design.tray_t + Design.amp_so")
        out.append("amp")
    if pi5_step:
        p = ref("Ref_Pi5", pi5_step, (0.10, 0.45, 0.20))
        p.setExpression(".Placement.Base.x", "Design.pi_x0")
        p.setExpression(".Placement.Base.y", "Design.pi_y0")
        p.setExpression(".Placement.Base.z",
                        "Design.ut_z0 + Design.tray_t + Design.rail_h + Design.pi_so")
        out.append("pi5")
    if uc595_step:
        d = ref("Ref_UC595", uc595_step, (0.08, 0.08, 0.10))
        d.setExpression(".Placement.Base.x", "Design.disp_dx")
        d.setExpression(".Placement.Base.z", "Design.disp_dz")
        out.append("uc595")
    # rails under the Pi's two hole columns (Pi holes 3.5 mm in from its left edge, 58 mm pitch)
    for i, x in enumerate(("Design.pi_x0 + 3.5 mm", "Design.pi_x0 + 61.5 mm")):
        name = "RailLink%d" % (i + 1)
        old = doc.getObject(name)
        if old is not None:
            doc.removeObject(old.Name)
        l = doc.addObject("App::Link", name)
        l.LinkedObject = doc.getObject("Rail")
        l.setExpression(".Placement.Base.x", x)
        out.append(name)
    doc.recompute()
    return ", ".join(out)


def tidy(doc):
    """Hide sketches/origins, show only each Body's Tip, hide the master Rail."""
    if not App.GuiUp:
        return "no GUI"
    for o in doc.Objects:
        if o.TypeId in ("Sketcher::SketchObject", "App::Origin", "App::Plane",
                        "App::Line", "App::Point"):
            o.ViewObject.Visibility = False
    for b in [o for o in doc.Objects if o.TypeId == "PartDesign::Body"]:
        for f in b.Group:
            if f.TypeId.startswith("PartDesign::"):
                f.ViewObject.Visibility = (f is b.Tip)
        b.ViewObject.Visibility = True
    return "tidy"
