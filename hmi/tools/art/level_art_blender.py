"""Model and render the Level screen's bull's-eye level and leveling boards.

Run headless, with the output folder after "--":

    blender -b -P level_art_blender.py -- OUTDIR

Writes, each in a light and a dark version, on transparent backgrounds:

    vial_<theme>.png      the bull's-eye level from above: bezel, tinted
                          liquid, printed rings and glass dome, no bubble
    bubble_<theme>.png    the bubble alone, which QML moves over the vial
    board_side_<theme>.png  a wooden leveling board seen from the side
    board_end_<theme>.png   the same board seen end on

The dark theme versions are darker and less glossy so they sit on the black
page without glare.
"""
import math
import os
import sys

import bpy

OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.getcwd()
os.makedirs(OUT, exist_ok=True)
THEMES = {"light": 0.0, "dark": -1.6}   # exposure


def clear():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras,
                 bpy.data.worlds, bpy.data.node_groups, bpy.data.curves):
        for x in list(coll):
            coll.remove(x)


def setup(sc, w, h, samples=128):
    try:
        sc.render.engine = "CYCLES"
    except TypeError as e:
        print("engine:", e)
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.cycles.transparent_max_bounces = 16
    sc.cycles.transmission_bounces = 12
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.look = "None"


def world(sc, colour, strength):
    w = bpy.data.worlds.new("W")
    sc.world = w
    w.use_nodes = True
    bg = next(n for n in w.node_tree.nodes if n.type == "BACKGROUND")
    bg.inputs["Color"].default_value = (*colour, 1)
    bg.inputs["Strength"].default_value = strength


def mat(name, base, rough, metal=0.0, trans=0.0, ior=1.45, alpha=1.0, coat=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    b.inputs["Base Color"].default_value = (*base, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    b.inputs["Transmission Weight"].default_value = trans
    b.inputs["IOR"].default_value = ior
    b.inputs["Alpha"].default_value = alpha
    b.inputs["Coat Weight"].default_value = coat
    return m


def area(col, name, loc, energy, size, rot=(0, 0, 0)):
    ld = bpy.data.lights.new(name, "AREA")
    ld.energy = energy
    ld.size = size
    o = bpy.data.objects.new(name, ld)
    o.location = loc
    o.rotation_euler = rot
    col.objects.link(o)
    return o


def ortho_cam(sc, col, scale, loc, rot):
    cd = bpy.data.cameras.new("Cam")
    cd.type = "ORTHO"
    cd.ortho_scale = scale
    cd.clip_end = 100
    c = bpy.data.objects.new("Cam", cd)
    c.location = loc
    c.rotation_euler = rot
    col.objects.link(c)
    sc.camera = c
    return c


def add(prim, **kw):
    getattr(bpy.ops.mesh, "primitive_" + prim + "_add")(**kw)
    o = bpy.context.active_object
    return o


def smooth(o):
    for p in o.data.polygons:
        p.use_smooth = True


# ---------------------------------------------------------------- the vial

def render_vial():
    for theme, exp in THEMES.items():
        clear()
        sc = bpy.context.scene
        col = sc.collection
        setup(sc, 480, 480)
        dark = theme == "dark"
        world(sc, (0.8, 0.82, 0.86), 0.25 if dark else 0.6)
        # Bezel: a machined ring, aluminium or black anodised
        bez = add("cylinder", vertices=128, radius=1.0, depth=0.16, location=(0, 0, 0.08))
        inner = add("cylinder", vertices=128, radius=0.86, depth=0.4, location=(0, 0, 0.08))
        mod = bez.modifiers.new("cut", "BOOLEAN")
        mod.object = inner
        mod.operation = "DIFFERENCE"
        bpy.context.view_layer.objects.active = bez
        bpy.ops.object.modifier_apply(modifier="cut")
        bpy.data.objects.remove(inner, do_unlink=True)
        bev = bez.modifiers.new("bevel", "BEVEL")
        bev.width = 0.025
        bev.segments = 4
        smooth(bez)
        bez.data.materials.append(mat("Bezel", (0.05, 0.05, 0.055), 0.45, 0.6) if dark
                                  else mat("Bezel", (0.78, 0.79, 0.81), 0.32, 1.0))
        # Dish: a light face under the liquid, printed rings on it
        dish = add("cylinder", vertices=128, radius=0.87, depth=0.04, location=(0, 0, 0.02))
        dish.data.materials.append(mat("Dish", (0.16, 0.17, 0.18) if dark else (0.93, 0.94, 0.92), 0.6))
        for r, w in ((0.17, 0.012), (0.42, 0.008), (0.66, 0.006)):
            ring = add("torus", major_radius=r, minor_radius=w, major_segments=160, minor_segments=8, location=(0, 0, 0.045))
            ring.scale.z = 0.2
            ring.data.materials.append(mat("Ring", (0.85, 0.86, 0.86) if dark else (0.12, 0.13, 0.13), 0.5))
        for a in range(4):   # cross hairs
            bar = add("cube", size=1, location=(0, 0, 0.045))
            bar.scale = (0.5 if a % 2 == 0 else 0.008, 0.008 if a % 2 == 0 else 0.5, 0.004)
            bar.location = (0.45 * math.cos(a * math.pi / 2) * 1.0, 0.45 * math.sin(a * math.pi / 2), 0.045)
            bar.data.materials.append(mat("Hair", (0.85, 0.86, 0.86) if dark else (0.12, 0.13, 0.13), 0.5))
        # Liquid: a pale green spirit, and a glass dome over everything
        liq = add("cylinder", vertices=128, radius=0.865, depth=0.1, location=(0, 0, 0.1))
        liq.data.materials.append(mat("Spirit", (0.75, 0.92, 0.55) if not dark else (0.45, 0.7, 0.35), 0.05, trans=1.0, ior=1.36))
        dome = add("uv_sphere", segments=96, ring_count=48, radius=0.88, location=(0, 0, 0.0))
        dome.scale.z = 0.24
        smooth(dome)
        dome.data.materials.append(mat("Glass", (1, 1, 1), 0.02, trans=1.0, ior=1.5))
        bpy.ops.object.select_all(action="DESELECT")
        area(col, "Key", (-2.2, 2.4, 4.0), 900 if not dark else 500, 2.5, (math.radians(-30), math.radians(-25), 0))
        area(col, "Fill", (2.5, -1.5, 3.0), 200, 3.0, (math.radians(25), math.radians(30), 0))
        ortho_cam(sc, col, 2.08, (0, 0, 10), (0, 0, 0))
        sc.view_settings.exposure = exp * 0.6
        sc.render.filepath = os.path.join(OUT, "vial_%s.png" % theme)
        bpy.ops.render.render(write_still=True)

        # The bubble alone: a flattened glossy air pocket
        for o in list(bpy.data.objects):     # new objects go to the default collection
            if o.type == "MESH":
                bpy.data.objects.remove(o, do_unlink=True)
        bub = add("uv_sphere", segments=64, ring_count=32, radius=0.15, location=(0, 0, 0.12))
        bub.scale.z = 0.35
        smooth(bub)
        bub.data.materials.append(mat("Bubble", (0.97, 1.0, 0.94) if not dark else (0.8, 0.9, 0.75), 0.05, coat=1.0))
        sc.render.resolution_x = sc.render.resolution_y = 96
        sc.camera.data.ortho_scale = 0.34
        sc.render.filepath = os.path.join(OUT, "bubble_%s.png" % theme)
        bpy.ops.render.render(write_still=True)


# ---------------------------------------------------------------- the board

def wood(dark):
    m = bpy.data.materials.new("Wood")
    m.use_nodes = True
    N, L = m.node_tree.nodes, m.node_tree.links
    b = next(n for n in N if n.type == "BSDF_PRINCIPLED")
    b.inputs["Roughness"].default_value = 0.7
    tc = N.new("ShaderNodeTexCoord")
    mp = N.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (0.6, 6.0, 6.0)    # grain runs along X
    wave = N.new("ShaderNodeTexWave")
    wave.wave_type = "RINGS"
    wave.inputs["Scale"].default_value = 3.0
    wave.inputs["Distortion"].default_value = 6.0
    wave.inputs["Detail"].default_value = 3.0
    ramp = N.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[1].position = 0.75
    lo, hi = ((0.17, 0.085, 0.035), (0.46, 0.27, 0.12)) if not dark else ((0.10, 0.05, 0.02), (0.30, 0.17, 0.08))
    ramp.color_ramp.elements[0].color = (*lo, 1)
    ramp.color_ramp.elements[1].color = (*hi, 1)
    L.new(tc.outputs["Object"], mp.inputs["Vector"])
    L.new(mp.outputs["Vector"], wave.inputs["Vector"])
    L.new(wave.outputs["Fac"], ramp.inputs["Fac"])
    L.new(ramp.outputs["Color"], b.inputs["Base Color"])
    return m


def render_boards():
    for theme, exp in THEMES.items():
        clear()
        sc = bpy.context.scene
        col = sc.collection
        dark = theme == "dark"
        setup(sc, 400, 80, 96)
        world(sc, (0.85, 0.86, 0.9), 0.8)
        # A 2x8 board: 1.5 x 7.25 in, cut 16 in long; metres here, 1 unit = 1 in
        board = add("cube", size=1, location=(0, 0, 0))
        board.scale = (16, 7.25, 1.5)
        bpy.ops.object.transform_apply(scale=True)
        bev = board.modifiers.new("bevel", "BEVEL")
        bev.width = 0.12
        bev.segments = 3
        board.data.materials.append(wood(dark))
        area(col, "Key", (-10, -30, 20), 60000 if not dark else 30000, 20, (math.radians(55), 0, math.radians(-20)))
        sc.view_settings.exposure = exp * 0.6
        # Side: looking along +Y at the long face
        ortho_cam(sc, col, 16.6, (0, -40, 0), (math.radians(90), 0, 0))
        sc.render.filepath = os.path.join(OUT, "board_side_%s.png" % theme)
        bpy.ops.render.render(write_still=True)
        # End on: looking along +X at the end grain
        sc.render.resolution_x, sc.render.resolution_y = 200, 60
        sc.camera.data.ortho_scale = 7.9
        sc.camera.location = (-40, 0, 0)
        sc.camera.rotation_euler = (math.radians(90), 0, math.radians(-90))
        sc.render.filepath = os.path.join(OUT, "board_end_%s.png" % theme)
        bpy.ops.render.render(write_still=True)


# ---------------------------------------------------------------- blocks and ramps

def plastic(name, base, dark):
    return mat(name, tuple(c * (0.7 if dark else 1.0) for c in base), 0.55)


def render_block():
    """A square plastic leveling block, 8.5 x 8.5 x 1 in, ribbed on top."""
    for theme, exp in THEMES.items():
        clear()
        sc = bpy.context.scene
        col = sc.collection
        dark = theme == "dark"
        setup(sc, 360, 60, 96)
        world(sc, (0.85, 0.86, 0.9), 0.8)
        grey = plastic("Block", (0.055, 0.058, 0.062), dark)
        body = add("cube", size=1, location=(0, 0, 0))
        body.scale = (8.5, 8.5, 0.75)
        bpy.ops.object.transform_apply(scale=True)
        b = body.modifiers.new("bevel", "BEVEL")
        b.width = 0.25
        b.segments = 3
        body.data.materials.append(grey)
        for i in range(5):   # ribs across the top
            rib = add("cube", size=1, location=(-3.4 + i * 1.7, 0, 0.5))
            rib.scale = (0.55, 8.2, 0.3)
            rib.data.materials.append(grey)
        area(col, "Key", (-10, -30, 20), 60000 if not dark else 30000, 20, (math.radians(55), 0, math.radians(-20)))
        sc.view_settings.exposure = exp * 0.6
        ortho_cam(sc, col, 8.9, (0, -40, 0.1), (math.radians(90), 0, 0))
        sc.render.filepath = os.path.join(OUT, "block_side_%s.png" % theme)
        bpy.ops.render.render(write_still=True)
        sc.camera.location = (-40, 0, 0.1)
        sc.camera.rotation_euler = (math.radians(90), 0, math.radians(-90))
        sc.render.filepath = os.path.join(OUT, "block_end_%s.png" % theme)
        bpy.ops.render.render(write_still=True)


# The ramp's top: height = RAMP_H * sin(pi/2 * x / RAMP_L) from its low front
# edge, so the screen can mark the point to drive up to for any raise
RAMP_L, RAMP_H, RAMP_W = 18.0, 4.0, 8.0


def render_ramp():
    """A curved leveling ramp seen from the side: driving up it raises the
    wheel smoothly to its full height at the back."""
    import bmesh
    for theme, exp in THEMES.items():
        clear()
        sc = bpy.context.scene
        col = sc.collection
        dark = theme == "dark"
        setup(sc, 540, 140, 96)
        world(sc, (0.85, 0.86, 0.9), 0.8)
        me = bpy.data.meshes.new("Ramp")
        bm = bmesh.new()
        n = 48
        pts = [(RAMP_L * i / n, RAMP_H * math.sin(math.pi / 2 * i / n)) for i in range(n + 1)]
        prof = [(0.0, 0.0)] + pts[1:] + [(RAMP_L, 0.0)]
        front = [bm.verts.new((x, -RAMP_W / 2, z)) for x, z in prof]
        back = [bm.verts.new((x, RAMP_W / 2, z)) for x, z in prof]
        bm.faces.new(front[::-1])
        bm.faces.new(back)
        m = len(prof)
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new((front[i], front[j], back[j], back[i]))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(me)
        ramp = bpy.data.objects.new("Ramp", me)
        col.objects.link(ramp)
        ramp.location = (-RAMP_L / 2, 0, 0)
        b = ramp.modifiers.new("bevel", "BEVEL")
        b.width = 0.12
        b.segments = 2
        body = plastic("Ramp", (0.06, 0.062, 0.066), dark)
        ramp.data.materials.append(body)
        # Treads across the curve
        for i in range(3, n, 4):
            x, z = pts[i]
            ang = math.atan2(RAMP_H * math.pi / 2 / RAMP_L * math.cos(math.pi / 2 * i / n), 1)
            t = add("cube", size=1, location=(x - RAMP_L / 2, 0, z + 0.08))
            t.scale = (0.3, RAMP_W - 0.4, 0.22)
            t.rotation_euler = (0, -ang, 0)
            t.data.materials.append(plastic("Tread", (0.03, 0.031, 0.033), dark))
        area(col, "Key", (-10, -30, 20), 60000 if not dark else 30000, 20, (math.radians(55), 0, math.radians(-20)))
        sc.view_settings.exposure = exp * 0.6
        ortho_cam(sc, col, RAMP_L + 0.6, (0, -40, RAMP_H / 2), (math.radians(90), 0, 0))
        sc.render.filepath = os.path.join(OUT, "ramp_side_%s.png" % theme)
        bpy.ops.render.render(write_still=True)
        sc.render.resolution_x, sc.render.resolution_y = 200, 110
        sc.camera.data.ortho_scale = RAMP_W + 0.6
        sc.camera.location = (-40, 0, RAMP_H / 2)
        sc.camera.rotation_euler = (math.radians(90), 0, math.radians(-90))
        sc.render.filepath = os.path.join(OUT, "ramp_end_%s.png" % theme)
        bpy.ops.render.render(write_still=True)


if __name__ == "__main__" and "--" in sys.argv:
    parts = sys.argv[sys.argv.index("--") + 2:] or ["vial", "boards", "block", "ramp"]
    if "vial" in parts:
        render_vial()
    if "boards" in parts:
        render_boards()
    if "block" in parts:
        render_block()
    if "ramp" in parts:
        render_ramp()
