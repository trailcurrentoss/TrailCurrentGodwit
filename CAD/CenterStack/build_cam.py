"""CAM for the Godwit centre-stack stand: one 2 x 4 ft sheet, one 1/4 in O-flute.

Builds a FreeCAD CAM job in GodwitCenterStack.FCStd and writes its G-code for
a LowRider V4 (GRBL) with a Makita RT0701C router as the spindle.

    import sys; sys.path.insert(0, "<repo>/CAD/CenterStack")
    import build_cam as bc
    bc.build_sheet()        # CAM sheet (feeds, depths, nest), job, clones, operations
    bc.check_all()          # path extents, depth, outline offset, dogbone corners
    bc.post()               # writes cam/<name>__MAKITA-dial2.nc

Everything is an expression:
- Part shapes: every model is a clone of the real Body.
- Nest positions: CAM sheet rows n_<part>_x / n_<part>_y, stacked from the
  part sizes in the Design sheet, so a size change reflows the nest.
- Depths, feeds and speeds: also in the CAM sheet.

Operations, in cutting order:
1. Waveshare knob recesses (2 mm pocket), while the plate is still held by
   the sheet.
2. Holes, slots and openings, with dogbones.
3. Outlines, with dogbones.

There are no holding tabs. Tape or screw the sheet down.
"""

import os
import re

import FreeCAD as App

import build_center_stack as cs

V = App.Vector
OUT_DIR = os.path.join(cs.HERE, "cam")
JOB_LABEL = "CenterStack sheet [2x4 ProWood]"
NC_NAME = "CenterStack_sheet1__1of1_T1-1-4in-O-flute__MAKITA-dial2.nc"

# Makita RT0701C dial speeds, tachometer-measured (forum.v1e.com/t/makita-rt0701c-rpms/40651).
MAKITA_DIAL = {1: 9600, 2: 12000, 3: 16500, 4: 21500, 5: 26500, 6: 29500}

# Part extents in their own sketch coordinates: (xmin, xlen, ymin, ylen)
EXT = {
    "Fascia": ("-Design.stack_w / 2", "Design.stack_w", "Design.v_e", "Design.stack_h - Design.v_e"),
    "Side": ("Design.y_sb0", "Design.base_d - Design.y_sb0", "0 mm",
             "Design.thk + Design.stack_h * cos(Design.tilt) - Design.thk * sin(Design.tilt)"),
    "Base": ("-Design.stack_w / 2", "Design.stack_w", "-Design.base_f", "Design.base_d + Design.base_f"),
    "Top": ("-Design.stack_w / 2", "Design.stack_w", "0 mm", "Design.top_w"),
    "Rear": ("-Design.stack_w / 2", "Design.stack_w", "0 mm", "Design.rear_h"),
    "Deck": ("-Design.stack_w / 2", "Design.stack_w", "Design.thk", "Design.cage_d"),
    "Cheek": ("Design.thk", "Design.cage_d", "Design.vdl", "Design.ch_h + 2 * Design.ch_clr + 2 * Design.thk"),
    "DialPlateCrow": ("-Design.pl_w / 2", "Design.pl_w", "Design.v_cl - Design.pl_h / 2", "Design.pl_h"),
    "DialPlateWave": ("-Design.pl_w / 2", "Design.pl_w", "Design.v_cl - Design.pl_h / 2", "Design.pl_h"),
}


def _len(body, axis):
    return "(%s)" % EXT[body][1 if axis == "x" else 3]


COL_A = "CAM.edge_m"
COL_B = "CAM.edge_m + Design.stack_w + CAM.gap"

# instance, body, nest x, nest y. Two columns along the sheet's long (Y) side.
NEST = [
    ("Fascia", "Fascia", COL_A, "CAM.edge_m"),
    ("Base", "Base", COL_A, "CAM.n_Fascia_y + %s + CAM.gap" % _len("Fascia", "y")),
    ("Top", "Top", COL_A, "CAM.n_Base_y + %s + CAM.gap" % _len("Base", "y")),
    ("DeckA", "Deck", COL_A, "CAM.n_Top_y + %s + CAM.gap" % _len("Top", "y")),
    ("DeckB", "Deck", COL_A, "CAM.n_DeckA_y + %s + CAM.gap" % _len("Deck", "y")),
    ("SideA", "Side", COL_B, "CAM.edge_m"),
    ("SideB", "Side", COL_B, "CAM.n_SideA_y + %s + CAM.gap" % _len("Side", "y")),
    ("Rear", "Rear", COL_B, "CAM.n_SideB_y + %s + CAM.gap" % _len("Side", "y")),
    ("DialCrow", "DialPlateCrow", COL_B, "CAM.n_Rear_y + %s + CAM.gap" % _len("Rear", "y")),
    ("DialWave", "DialPlateWave", COL_B, "CAM.n_DialCrow_y + %s + CAM.gap" % _len("DialPlateCrow", "y")),
    ("CheekA", "Cheek", COL_B, "CAM.n_DialWave_y + %s + CAM.gap" % _len("DialPlateWave", "y")),
    ("CheekB", "Cheek", "CAM.n_CheekA_x + %s + CAM.gap" % _len("Cheek", "x"), "CAM.n_CheekA_y"),
]

CAM_PARAMS = [
    ("#", "Stock: ProWood 1/4 in x 2 ft x 4 ft sande panel (thickness is Design.thk)", ""),
    ("sheet_x", "603 mm", "sheet width along machine X (23.75 in actual; the listing allows 1/4 in under 24)"),
    ("sheet_y", "1213 mm", "sheet length along machine Y (47.75 in actual)"),
    ("edge_m", "10 mm", "parts start this far in from the sheet corner at X0 Y0"),
    ("gap", "12 mm", "space between parts (cutter + a web)"),
    ("#", "Tool: 1/4 in single-flute O-flute, Makita RT0701C", ""),
    ("tool_d", "=Design.tool_d", "cutter diameter"),
    ("tool_r", "=tool_d / 2", "cutter radius"),
    ("flutes", "1", "flutes"),
    ("dial", "2", "Makita dial setting"),
    ("rpm", "12000", "dial 2 = 12,000 rpm (tachometer-measured RT0701C)"),
    ("feed_xy", "3000 mm/min", "cutting feed"),
    ("feed_z", "600 mm/min", "plunge feed"),
    ("chipload", "=feed_xy * 60 s / (rpm * flutes)", "chip load per tooth (0.25 mm = 0.010 in; O-flute in plywood ~0.18-0.30 mm)"),
    ("#", "Depths (Z0 = top of the sheet at its highest point)", ""),
    ("cut_over", "0.5 mm", "cut this far below the sheet"),
    ("surf_var", "0.5 mm", "how far the lowest spot of the sheet sits below Z0. MEASURE ON YOUR BED"),
    ("cut_depth", "=Design.thk + cut_over + surf_var", "through-cut depth"),
    ("passes", "3", "passes for a through cut"),
    ("step_down", "=cut_depth / passes", "depth per pass"),
    ("safe_z", "5 mm", "safe height"),
    ("clear_z", "10 mm", "clearance height"),
    ("#", "Nest (part corner positions on the sheet; they follow the Design sheet)", ""),
]


def doc():
    return App.getDocument(cs.DOC_NAME)


def _q(v):
    return float(getattr(v, "Value", v))


def build_sheet():
    d = doc()
    sh = d.getObject("CAM") or d.addObject("Spreadsheet::Sheet", "CAM")
    sh.clearAll()
    sh.set("A1", "Parameter")
    sh.set("B1", "Value")
    sh.set("C1", "Notes")
    rows = list(CAM_PARAMS)
    for inst, body, x, y in NEST:
        rows.append(("n_%s_x" % inst, "=" + x, "%s (%s) left edge" % (inst, body)))
        rows.append(("n_%s_y" % inst, "=" + y, "%s (%s) front edge" % (inst, body)))
    rows += [
        ("#", "Checks (each must read 1)", ""),
        ("chk_len_a", "=CAM.n_DeckB_y + %s + CAM.tool_r <= CAM.sheet_y" % _len("Deck", "y"), "column A fits the sheet length"),
        ("chk_len_b", "=CAM.n_CheekA_y + %s + CAM.tool_r <= CAM.sheet_y" % _len("Cheek", "y"), "column B fits the sheet length"),
        ("chk_w_side", "=%s + %s + CAM.tool_r <= CAM.sheet_x" % (COL_B, _len("Side", "x")), "sides fit the sheet width"),
        ("chk_w_cheek", "=CAM.n_CheekB_x + %s + CAM.tool_r <= CAM.sheet_x" % _len("Cheek", "x"), "cheeks fit the sheet width"),
        ("chk_slot", "=Design.ws > Design.tool_d + 0.3 mm", "T-joint slots are wider than the cutter"),
    ]
    r = 2
    for alias, value, note in rows:
        if alias == "#":
            sh.set("A%d" % r, "'" + value)
            sh.setStyle("A%d" % r, "bold")
        else:
            sh.set("A%d" % r, alias)
            sh.set("B%d" % r, value if value.startswith("=") else "=" + value)
            sh.setAlias("B%d" % r, alias)
            sh.set("C%d" % r, note)
        r += 1
    sh.setColumnWidth("A", 120)
    sh.setColumnWidth("C", 520)
    d.recompute()
    return [(a, sh.get(a)) for a, _, _ in rows if a.startswith("chk")]


# ----------------------------------------------------------------- job
def _op_view(op, gui_module):
    import Path.Op.Gui.Base as PathOpGui
    if App.GuiUp:
        op.ViewObject.Proxy = PathOpGui.ViewProvider(op.ViewObject, gui_module.Command.res)


def _model_clone(job, body):
    """A job model clone made the way the Job makes its own, without the
    whole-document recompute that freezes the GUI."""
    import Draft
    clone = Draft.clone(body)
    clone.addProperty("App::PropertyString", "PathResource")
    clone.PathResource = "Model"
    if App.GuiUp:
        import Path.Base.Gui.IconViewProvider
        Path.Base.Gui.IconViewProvider.Attach(clone.ViewObject, "BaseGeometry")
        clone.ViewObject.Visibility = False
        clone.ViewObject.Transparency = 80
    job.Model.addObject(clone)
    return clone


def _place(clone, inst, body):
    xmin, _, ymin, _ = EXT[body]
    clone.Placement = App.Placement()
    clone.setExpression("Placement.Base.x", "CAM.n_%s_x - (%s)" % (inst, xmin))
    clone.setExpression("Placement.Base.y", "CAM.n_%s_y - (%s)" % (inst, ymin))
    clone.setExpression("Placement.Base.z", "-Design.thk")      # sheet top at Z0


def bottom_face(clone):
    """The clone's bottom face: its outer wire is the outline and its inner
    wires are the THROUGH holes only (the knob recesses don't reach it)."""
    zb = clone.Shape.BoundBox.ZMin
    faces = [(i, f) for i, f in enumerate(clone.Shape.Faces)
             if abs(f.BoundBox.ZMin - zb) < 1e-4 and abs(f.BoundBox.ZMax - zb) < 1e-4]
    i, _ = max(faces, key=lambda t: t[1].Area)
    return "Face%d" % (i + 1)


def recess_faces(clone):
    """Floors of the Waveshare knob recesses."""
    z = -_q(doc().getObject("Design").get("ws_pk"))
    return tuple("Face%d" % (i + 1) for i, f in enumerate(clone.Shape.Faces)
                 if abs(f.BoundBox.ZMin - z) < 1e-4 and abs(f.BoundBox.ZMax - z) < 1e-4)


def _profile(job, name, label, perimeter, holes):
    from Path.Op import Profile as PProfile
    import Path.Op.Gui.Profile as ProfileGui
    op = PProfile.Create(name, parentJob=job)
    _op_view(op, ProfileGui)
    op.Label = label
    # Every model's face. With no faces at all, Profile cuts only the outer
    # envelope and silently skips every slot and opening.
    op.Base = [(c, (bottom_face(c),)) for c in job.Model.Group]
    op.processPerimeter = perimeter
    op.processHoles = holes
    op.processCircles = holes
    op.Side = "Outside"
    op.Direction = "CCW"
    op.UseComp = True
    op.setExpression("StartDepth", "0 mm")
    op.setExpression("FinalDepth", "-CAM.cut_depth")
    op.setExpression("StepDown", "CAM.step_down")
    op.setExpression("SafeHeight", "CAM.safe_z")
    op.setExpression("ClearanceHeight", "CAM.clear_z")
    return op


def _recess(job, clone):
    from Path.Op import PocketShape as PPocket
    import Path.Op.Gui.PocketShape as PocketGui
    op = PPocket.Create("OP_recess", parentJob=job)
    _op_view(op, PocketGui)
    op.Label = "1. Waveshare knob recesses"
    op.Base = [(clone, recess_faces(clone))]
    op.setExpression("StartDepth", "0 mm")
    op.setExpression("FinalDepth", "-Design.ws_pk")
    op.setExpression("StepDown", "Design.ws_pk")
    op.setExpression("SafeHeight", "CAM.safe_z")
    op.setExpression("ClearanceHeight", "CAM.clear_z")
    op.StepOver = 45
    for prop in ("ClearingPattern", "OffsetPattern"):     # renamed between FreeCAD versions
        if prop in op.PropertiesList and "Offset" in op.getEnumerationsOfProperty(prop):
            setattr(op, prop, "Offset")
    return op


def _dogbone(op, name):
    import Path.Dressup.Gui.DogboneII as DB      # Gui variant: attaches the view provider, no task panel
    db = DB.Create(op, name)
    db.Style = "Dogbone"
    db.Incision = "adaptive"
    db.Side = "Left"
    db.Label = op.Label + " + dogbones"
    return db


def find_job():
    # PathJob.Instances() only looks at the ACTIVE document; search this one
    js = [o for o in doc().Objects
          if type(getattr(o, "Proxy", None)).__name__ == "ObjectJob"]
    return js[0] if js else None


def remove_job():
    j = find_job()
    if j is None:
        return
    d = doc()
    names = set()

    def walk(o):
        if o is None or o.Name in names:
            return
        names.add(o.Name)
        for c in getattr(o, "Group", []) or []:
            walk(c)
        for p in ("Stock", "SetupSheet", "Tool", "Model", "Operations", "Tools", "Base"):
            v = getattr(o, p, None)
            if isinstance(v, App.DocumentObject):
                walk(v)
    walk(j)
    for o in j.Operations.Group:
        walk(o)
    for n in list(names):
        o = d.getObject(n)
        if o is not None and o.TypeId == "PartDesign::Body" and n in cs.PARTS:
            names.discard(n)                       # never the design bodies
    for n in sorted(names, key=lambda n: -len(d.getObject(n).InList) if d.getObject(n) else 0):
        if d.getObject(n) is not None:
            if d.getObject(n).TypeId == "PartDesign::Body":
                d.getObject(n).removeObjectsFromDocument()
            d.removeObject(n)


def build_job():
    """Job.Create and Draft.clone work in the ACTIVE document, so make this
    one active for the build and give the previous one back afterwards."""
    prev = App.ActiveDocument.Name if App.ActiveDocument else None
    App.setActiveDocument(doc().Name)
    try:
        return _build_job()
    finally:
        if prev and prev in App.listDocuments():
            App.setActiveDocument(prev)


def _build_job():
    import Path.Main.Gui.Job as PathJobGui
    import Path.Main.Stock as PathStock
    d = doc()
    remove_job()
    first = NEST[0]
    job = PathJobGui.Create([d.getObject(first[1])], None, openTaskPanel=False)   # NOT the App-layer Create
    job.Label = JOB_LABEL
    clones = list(job.Model.Group)
    for inst, body, _, _ in NEST[1:]:
        clones.append(_model_clone(job, d.getObject(body)))
    for c, (inst, body, _, _) in zip(clones, NEST):
        c.Label = "Model-%s" % inst
        _place(c, inst, body)
    d.recompute(clones)

    old = job.Stock
    stock = PathStock.CreateBox(job)
    stock.setExpression("Length", "CAM.sheet_x")
    stock.setExpression("Width", "CAM.sheet_y")
    stock.setExpression("Height", "Design.thk")
    stock.setExpression("Placement.Base.x", "0 mm")      # sheet corner at the machine origin
    stock.setExpression("Placement.Base.y", "0 mm")
    stock.setExpression("Placement.Base.z", "-Design.thk")
    job.Stock = stock                 # assign BEFORE removing the old one
    d.removeObject(old.Name)

    tc = job.Tools.Group[0]           # retune the job's own controller; never share a ToolBit
    tc.Label = "T1 1/4in O-flute"
    tc.Tool.Label = "1/4in O-flute (1 flute)"
    tc.Tool.setExpression("Diameter", "CAM.tool_d")
    if hasattr(tc.Tool, "Flutes"):
        tc.Tool.setExpression("Flutes", "CAM.flutes")
    tc.setExpression("SpindleSpeed", "CAM.rpm")
    tc.setExpression("HorizFeed", "CAM.feed_xy")
    tc.setExpression("VertFeed", "CAM.feed_z")

    job.PostProcessor = "grbl"
    job.PostProcessorArgs = "--precision=3 --no-line-numbers"
    job.PostProcessorOutputFile = "%D/cam/" + NC_NAME

    wave = [c for c in clones if c.Label == "Model-DialWave"][0]
    recess = _recess(job, wave)
    holes = _profile(job, "OP_holes", "2. CUT holes, slots and openings", False, True)
    outline = _profile(job, "OP_outline", "3. CUT outlines", True, False)
    d.recompute()
    db_h = _dogbone(holes, "Dogbone_holes")
    db_o = _dogbone(outline, "Dogbone_outline")
    job.Operations.Group = [recess, db_h, db_o]      # a dressup reorders the group: put it back
    d.recompute()
    return job


# --------------------------------------------------------------- checks
def path_extents(job):
    xs, ys, zs = [], [], []
    for op in job.Operations.Group:
        for cmd in getattr(op, "Path", None).Commands if hasattr(op, "Path") else []:
            p = cmd.Parameters
            if cmd.Name in ("G0", "G00", "G1", "G01", "G2", "G02", "G3", "G03"):
                if "X" in p:
                    xs.append(p["X"])
                if "Y" in p:
                    ys.append(p["Y"])
                if "Z" in p:
                    zs.append(p["Z"])
    return min(xs), max(xs), min(ys), max(ys), min(zs)


def _concave_corners(face, which):
    """Concave corners a square-ended mating part must reach: inside corners of
    the outline, or every sharp corner of a hole. Straight-straight only."""
    pts = []
    wires = [face.OuterWire] if which == "outline" else [w for w in face.Wires if not w.isSame(face.OuterWire)]
    for w in wires:
        es = w.OrderedEdges
        vs = [v.Point for v in w.OrderedVertexes]
        n = len(es)
        if n < 3:
            continue
        area = sum(vs[i].x * vs[(i + 1) % n].y - vs[(i + 1) % n].x * vs[i].y for i in range(n)) / 2
        for i in range(n):
            e0, e1 = es[i - 1], es[i]
            if e0.Curve.TypeId != "Part::GeomLine" or e1.Curve.TypeId != "Part::GeomLine":
                continue
            a, b, c = vs[i - 1], vs[i], vs[(i + 1) % n]
            cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x)
            if abs(cross) < 1e-9:
                continue
            if (which == "holes" and cross * area > 0) or (which == "outline" and cross * area < 0):
                pts.append(b)
    return pts


def _segments(op, z_max, arc_step=0.5):
    """Cutting moves at or below z_max as straight segments. Arcs (G2/G3 with
    I/J centre offsets) are split into short chords: treating an arc as one
    chord makes every circular hole look uncut."""
    import math
    segs, last = [], None
    for cmd in op.Path.Commands:
        p = cmd.Parameters
        if cmd.Name not in ("G1", "G01", "G2", "G02", "G3", "G03", "G0", "G00"):
            continue
        cur = V(p.get("X", last.x if last else 0), p.get("Y", last.y if last else 0),
                p.get("Z", last.z if last else 0))
        if last is not None and cmd.Name not in ("G0", "G00") and cur.z <= z_max and last.z <= z_max:
            if cmd.Name in ("G2", "G02", "G3", "G03"):
                c = V(last.x + p.get("I", 0), last.y + p.get("J", 0), last.z)
                rad = math.hypot(last.x - c.x, last.y - c.y)
                a0 = math.atan2(last.y - c.y, last.x - c.x)
                a1 = math.atan2(cur.y - c.y, cur.x - c.x)
                cw = cmd.Name in ("G2", "G02")
                sweep = (a0 - a1) if cw else (a1 - a0)
                sweep %= 2 * math.pi
                if sweep < 1e-9:
                    sweep = 2 * math.pi            # full circle
                n = max(2, int(rad * sweep / arc_step) + 1)
                prev = last
                for i in range(1, n + 1):
                    a = a0 + (-sweep if cw else sweep) * i / n
                    q = V(c.x + rad * math.cos(a), c.y + rad * math.sin(a), last.z + (cur.z - last.z) * i / n)
                    segs.append((prev, q))
                    prev = q
            else:
                segs.append((last, cur))
        last = cur
    return segs


def _dist(p, a, b):
    ab = b - a
    L2 = ab.x ** 2 + ab.y ** 2
    t = 0 if L2 == 0 else max(0, min(1, ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / L2))
    return ((a.x + t * ab.x - p.x) ** 2 + (a.y + t * ab.y - p.y) ** 2) ** 0.5


def check_corners(op_name, which):
    """(corners, not cleared, worst gap mm) on the op's final pass. Arcs in the
    path are checked by their end points only, which is conservative."""
    d = doc()
    job, op = find_job(), d.getObject(op_name)
    r = _q(d.getObject("CAM").get("tool_r"))
    zs = [c.Parameters["Z"] for c in op.Path.Commands if "Z" in c.Parameters]
    segs = _segments(op, min(zs) + 1e-6)
    cell, grid = 20.0, {}
    for s in segs:
        for gx in range(int(min(s[0].x, s[1].x) // cell), int(max(s[0].x, s[1].x) // cell) + 1):
            for gy in range(int(min(s[0].y, s[1].y) // cell), int(max(s[0].y, s[1].y) // cell) + 1):
                grid.setdefault((gx, gy), []).append(s)
    n = bad = 0
    worst = 0.0
    for c in job.Model.Group:
        face = c.Shape.getElement(bottom_face(c))
        for p in _concave_corners(face, which):
            near = [s for gx in (-1, 0, 1) for gy in (-1, 0, 1)
                    for s in grid.get((int(p.x // cell) + gx, int(p.y // cell) + gy), [])]
            dist = min([_dist(p, a, b) for a, b in near] or [1e9])
            n += 1
            if dist > r + 0.01:
                bad += 1
                worst = max(worst, dist - r)
    return n, bad, round(worst, 3)


def check_edges(step=1.0, eps=0.05, tol=0.02):
    """Walk every edge of every part on the final pass. Just outside the
    edge (waste side) must be within one radius of the cutter centre; just
    inside (material side) must not be. Returns (points, missed, gouged, holes
    without a pass)."""
    d = doc()
    job = find_job()
    r = _q(d.getObject("CAM").get("tool_r"))
    segs = []
    for name in ("Dogbone_holes", "Dogbone_outline"):
        op = d.getObject(name)
        zs = [c.Parameters["Z"] for c in op.Path.Commands if "Z" in c.Parameters]
        segs += _segments(op, min(zs) + 1e-6)
    cell, grid = 10.0, {}
    for s in segs:
        for gx in range(int(min(s[0].x, s[1].x) // cell), int(max(s[0].x, s[1].x) // cell) + 1):
            for gy in range(int(min(s[0].y, s[1].y) // cell), int(max(s[0].y, s[1].y) // cell) + 1):
                grid.setdefault((gx, gy), []).append(s)

    def dmin(p):
        near = [s for gx in (-1, 0, 1) for gy in (-1, 0, 1)
                for s in grid.get((int(p.x // cell) + gx, int(p.y // cell) + gy), [])]
        return min([_dist(p, a, b) for a, b in near] or [1e9])

    n = missed = gouged = 0
    unpassed = []
    bone = r * 2 ** 0.5 + 0.1     # a dogbone legitimately cuts this far along each edge of a concave corner
    for c in job.Model.Group:
        face = c.Shape.getElement(bottom_face(c))
        corners = _concave_corners(face, "outline") + _concave_corners(face, "holes")
        for w in face.Wires:
            hit = False
            for e in w.Edges:
                k = max(2, int(e.Length / step))
                for i in range(1, k):
                    par = e.FirstParameter + (e.LastParameter - e.FirstParameter) * i / k
                    p = e.valueAt(par)
                    t = e.tangentAt(par)
                    nrm = V(-t.y, t.x, 0)
                    a, b = p + nrm * eps, p - nrm * eps
                    pa, pb = V(a.x, a.y, face.BoundBox.ZMin), V(b.x, b.y, face.BoundBox.ZMin)
                    ina, inb = face.isInside(pa, 1e-7, True), face.isInside(pb, 1e-7, True)
                    if ina == inb:
                        continue
                    waste, solid = (b, a) if ina else (a, b)
                    n += 1
                    dw, ds = dmin(waste), dmin(solid)
                    if dw > r + tol:
                        missed += 1
                    else:
                        hit = True
                    if ds < r - tol and not any((V(p.x, p.y, 0) - V(q.x, q.y, 0)).Length < bone
                                                for q in corners):
                        gouged += 1
            if not hit:
                unpassed.append(c.Label)
    return n, missed, gouged, unpassed


def check_all():
    d = doc()
    job = find_job()
    cam = d.getObject("CAM")
    r = _q(cam.get("tool_r"))
    out = []
    x0, x1, y0, y1, zmin = path_extents(job)
    out.append("path X %.1f..%.1f Y %.1f..%.1f (sheet %.0f x %.0f), min Z %.3f (cut_depth %.3f)" % (
        x0, x1, y0, y1, _q(cam.get("sheet_x")), _q(cam.get("sheet_y")), zmin, _q(cam.get("cut_depth"))))
    ok = x0 >= 0 and y0 >= 0 and x1 <= _q(cam.get("sheet_x")) and y1 <= _q(cam.get("sheet_y"))
    out.append("inside sheet: %s" % ok)
    # outline toolpath one radius outside every part's bounding box
    bb = None
    for c in job.Model.Group:
        bb = c.Shape.BoundBox if bb is None else bb.united(c.Shape.BoundBox)
    op = d.getObject("OP_outline")
    xs = [k.Parameters["X"] for k in op.Path.Commands if "X" in k.Parameters]
    ys = [k.Parameters["Y"] for k in op.Path.Commands if "Y" in k.Parameters]
    err = max(abs((bb.XMin - r) - min(xs)), abs((bb.XMax + r) - max(xs)),
              abs((bb.YMin - r) - min(ys)), abs((bb.YMax + r) - max(ys)))
    out.append("outline offset error %.3f mm" % err)
    for name, which in (("Dogbone_holes", "holes"), ("Dogbone_outline", "outline")):
        out.append("%s: corners %d, not cleared %d, worst %.3f" % ((name,) + check_corners(name, which)))
    for o in [d.getObject(n) for n in ("OP_recess", "OP_holes", "OP_outline")]:
        for b, subs in o.Base:
            for s in subs:
                if s.startswith("?"):
                    out.append("STALE face reference %s in %s" % (s, o.Name))
    return out


def post():
    """Write the G-code, with the Makita dial in the name and the header."""
    from Path.Post.Processor import PostProcessorFactory
    d = doc()
    job = find_job()
    cam = d.getObject("CAM")
    os.makedirs(OUT_DIR, exist_ok=True)
    args = job.PostProcessorArgs
    job.PostProcessorArgs = args + " --no-show-editor"   # the editor is modal: it would hang the MCP
    prev = App.ActiveDocument.Name if App.ActiveDocument else None
    App.setActiveDocument(d.Name)
    try:
        sections = PostProcessorFactory.get_post_processor(job, job.PostProcessor).export()
    finally:
        job.PostProcessorArgs = args                      # the GUI Post Process button still shows the editor
        if prev and prev in App.listDocuments():
            App.setActiveDocument(prev)
    gcode = "\n".join(g for _, g in sections)
    dial = int(_q(cam.get("dial")))
    rpm = MAKITA_DIAL[dial]
    if abs(rpm - _q(cam.get("rpm"))) > 1:
        raise RuntimeError("CAM.rpm %s is not Makita dial %d's %d rpm" % (cam.get("rpm"), dial, rpm))
    gcode = re.sub(r"\(\s*M6 T\d+\s*\)", "(Tool T1 is installed before this file starts; no M6)", gcode)
    feed_time = _feed_minutes(gcode)
    head = [
        "(%s)" % NC_NAME,
        "(Godwit centre-stack stand: all 12 parts from one ProWood 1/4in x 2ft x 4ft sande panel.)",
        "(Tool: T1 1/4in single-flute O-flute, 1/4in shank. Chip load %.2f mm.)" % _q(cam.get("chipload")),
        "(Set the Makita dial to %d (%d rpm) and switch the router ON before starting. Switch it OFF when the file ends.)" % (dial, rpm),
        "(Sheet: long side along machine Y, corner at X0 Y0. Z0 = top of the sheet at its highest point.)",
        "(Cuts %.2f mm deep: sheet %.2f + cut_over %.2f + surf_var %.2f. No holding tabs: tape or screw the sheet down.)" % (
            _q(cam.get("cut_depth")), _q(d.getObject("Design").get("thk")), _q(cam.get("cut_over")),
            _q(cam.get("surf_var"))),
        "(Order: Waveshare knob recesses, then holes/slots/openings, then outlines.)",
        "(Approx. feed time: %d min)" % round(feed_time),
    ]
    gcode = "\n".join(head) + "\n" + gcode
    path = os.path.join(OUT_DIR, NC_NAME)
    with open(path, "w") as fh:
        fh.write(gcode)
    return verify(path)


def _nest_wires():
    """[(instance, [wire, ...])] of every part's through-cut outline and holes on the sheet."""
    job = find_job()
    return [(c.Label.replace("Model-", ""), c.Shape.getElement(bottom_face(c)).Wires) for c in job.Model.Group]


def _recess_circles():
    job = find_job()
    wave = [c for c in job.Model.Group if c.Label == "Model-DialWave"][0]
    out = []
    for f in recess_faces(wave):
        for e in wave.Shape.getElement(f).OuterWire.Edges:
            if e.Curve.TypeId == "Part::GeomCircle":
                out.append((e.Curve.Center, e.Curve.Radius))
    return out


def nest_dxf(path=None):
    """R12 DXF of the nest, 1:1 mm, sheet corner at 0,0: layer CUT (through),
    RECESS (2 mm pocket), SHEET (stock outline). For other CAM software."""
    path = path or os.path.join(OUT_DIR, "CenterStack_sheet1_nest.dxf")
    cam = doc().getObject("CAM")
    ents = []

    def line(a, b, layer):
        ents.append("0\nLINE\n8\n%s\n10\n%.4f\n20\n%.4f\n30\n0\n11\n%.4f\n21\n%.4f\n31\n0" % (layer, a[0], a[1], b[0], b[1]))

    def circ(c, r, layer):
        ents.append("0\nCIRCLE\n8\n%s\n10\n%.4f\n20\n%.4f\n30\n0\n40\n%.4f" % (layer, c[0], c[1], r))

    for _, wires in _nest_wires():
        for w in wires:
            for e in w.Edges:
                if e.Curve.TypeId == "Part::GeomLine":
                    line(e.Vertexes[0].Point, e.Vertexes[-1].Point, "CUT")
                elif e.Curve.TypeId == "Part::GeomCircle" and e.isClosed():
                    circ(e.Curve.Center, e.Curve.Radius, "CUT")
                else:
                    pts = e.discretize(Deflection=0.02)
                    for a, b in zip(pts, pts[1:]):
                        line(a, b, "CUT")
    for c, r in _recess_circles():
        circ(c, r, "RECESS")
    sx, sy = _q(cam.get("sheet_x")), _q(cam.get("sheet_y"))
    for a, b in (((0, 0), (sx, 0)), ((sx, 0), (sx, sy)), ((sx, sy), (0, sy)), ((0, sy), (0, 0))):
        line(a, b, "SHEET")
    with open(path, "w") as fh:
        fh.write("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n")
        fh.write("\n".join(ents))
        fh.write("\n0\nENDSEC\n0\nEOF\n")
    txt = open(path).read()
    return dict(path=path, lines=txt.count("\nLINE\n"), circles=txt.count("\nCIRCLE\n"))


def nest_svg(path=None):
    """Picture of the nest for the README."""
    path = path or os.path.join(OUT_DIR, "nest.svg")
    cam = doc().getObject("CAM")
    sx, sy = _q(cam.get("sheet_x")), _q(cam.get("sheet_y"))
    k = 0.6                      # px per mm; the sheet's long side runs left to right
    W, H = sy * k + 40, sx * k + 40

    def P(p):                    # true top view turned 90 deg: machine Y to the right, X down
        return 20 + p.y * k, 20 + p.x * k

    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
           'font-family="sans-serif">' % (W, H, W, H),
           '<rect width="100%" height="100%" fill="#ffffff"/>',
           '<rect x="20" y="20" width="%.1f" height="%.1f" fill="#ead9b8" stroke="#8a6d3b"/>' % (sy * k, sx * k)]
    for name, wires in _nest_wires():
        d = []
        for w in wires:
            pts = w.discretize(Deflection=0.05)
            d.append("M " + " L ".join("%.2f %.2f" % P(p) for p in pts) + " Z")
        out.append('<path d="%s" fill="#c69c5d" fill-rule="evenodd" stroke="#4a3415" stroke-width="0.8"/>' % " ".join(d))
        bb = wires[0].BoundBox
        cx, cy = P(V((bb.XMin + bb.XMax) / 2, (bb.YMin + bb.YMax) / 2, 0))
        out.append('<text x="%.1f" y="%.1f" font-size="13" text-anchor="middle" fill="#2a1d0b">%s</text>' % (cx, cy, name))
    for c, r in _recess_circles():
        x, y = P(c)
        out.append('<circle cx="%.2f" cy="%.2f" r="%.2f" fill="none" stroke="#2a6fb0" stroke-dasharray="3 2"/>' % (x, y, r * k))
    ox, oy = P(V(0, 0, 0))
    out.append('<circle cx="%.1f" cy="%.1f" r="4" fill="#c0392b"/>' % (ox, oy))
    out.append('<text x="%.1f" y="%.1f" font-size="12" fill="#c0392b">X0 Y0</text>' % (ox + 6, oy + 14))
    out.append('<text x="%.1f" y="%.1f" font-size="12" fill="#333">machine Y &#8594;</text>' % (W / 2, 14))
    out.append('<text x="4" y="%.1f" font-size="12" fill="#333" transform="rotate(90 4 %.1f)">machine X &#8594;</text>' % (H / 2, H / 2))
    out.append("</svg>")
    with open(path, "w") as fh:
        fh.write("\n".join(out))
    return path


def _feed_minutes(gcode):
    import math
    x = y = z = 0.0
    f = 1.0
    total = 0.0
    for line in gcode.splitlines():
        if line.startswith("("):
            continue
        m = re.match(r"\s*(G0?[0123])\b(.*)", line)
        if not m:
            continue
        vals = dict((k, float(v)) for k, v in re.findall(r"([XYZF])(-?\d+\.?\d*)", m.group(2)))
        nx, ny, nz = vals.get("X", x), vals.get("Y", y), vals.get("Z", z)
        f = vals.get("F", f)
        if m.group(1) not in ("G0", "G00"):
            total += math.dist((x, y, z), (nx, ny, nz)) / f
        x, y, z = nx, ny, nz
    return total


def verify(path):
    """Read the FILE back: units, depth, feeds, extents, operation order."""
    g = open(path).read()
    cam = doc().getObject("CAM")
    xs, ys, zs, fs = [], [], [], set()
    for line in g.splitlines():
        if line.startswith("("):
            continue
        for k, v in re.findall(r"([XYZF])(-?\d+\.?\d*)", line):
            {"X": xs, "Y": ys, "Z": zs}.get(k, []).append(float(v)) if k != "F" else fs.add(float(v))
    return dict(
        path=path, lines=len(g.splitlines()), metric="G21" in g and "G20" not in g,
        spindle=re.findall(r"M3 S\d+", g)[:1], feeds=sorted(fs),
        x=(min(xs), max(xs)), y=(min(ys), max(ys)), z=(min(zs), max(zs)),
        expected_min_z=-_q(cam.get("cut_depth")),
        order=re.findall(r"\(Begin operation: (.*?)\)", g),
        minutes=round(_feed_minutes(g)))
