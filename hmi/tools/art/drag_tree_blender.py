"""Model and render the Track app's drag-racing tree in Blender.

Run headless, with the output folder after "--":

    blender -b -P drag_tree_blender.py -- OUTDIR

Writes tree_{off,on}_{light,dark}_raw.png (transparent, 260x1000) and
lamps_render.json (each lens's box in render pixels), then
drag_tree_post.py trims and scales them into hmi/assets/track/.

The tree, top to bottom: pre-stage and stage (pairs of small bulbs), three
ambers, green, red, each in a black housing with a hood over the lens. The
lenses are LED arrays. "on" lights every lens at once; the HMI shows each
lamp by revealing its own patch of the "on" image over the "off" one.
"""
import json
import math
import os
import sys

import bmesh
import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector

OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.getcwd()
os.makedirs(OUT, exist_ok=True)

# An empty scene, cleared by hand
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras):
    for x in list(coll):
        coll.remove(x)
sc = bpy.context.scene
col = sc.collection


def principled(name, base, rough, metal=0.0, coat=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    b.inputs["Base Color"].default_value = (*base, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    b.inputs["Coat Weight"].default_value = coat
    return m


HOUSING = principled("Housing", (0.012, 0.012, 0.013), 0.42, 0.0, 0.3)
VISOR = principled("Visor", (0.008, 0.008, 0.009), 0.32, 0.0, 0.4)
POLE = principled("Pole", (0.02, 0.02, 0.022), 0.35, 0.6)


def lens_material(name, lit_colour, glass_colour):
    """Tinted glass over a grid of LEDs; LitMix 0 = off, 1 = on."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    N, L = m.node_tree.nodes, m.node_tree.links
    for n in list(N):
        N.remove(n)
    out = N.new("ShaderNodeOutputMaterial")
    tc = N.new("ShaderNodeTexCoord")
    # LED grid on the lens face (object X/Y), ~11 mm pitch
    sc_ = N.new("ShaderNodeVectorMath"); sc_.operation = "SCALE"; sc_.inputs["Scale"].default_value = 90
    fr = N.new("ShaderNodeVectorMath"); fr.operation = "FRACTION"
    sb = N.new("ShaderNodeVectorMath"); sb.operation = "SUBTRACT"; sb.inputs[1].default_value = (0.5, 0.5, 0.5)
    sep = N.new("ShaderNodeSeparateXYZ")
    cmb = N.new("ShaderNodeCombineXYZ")
    ln = N.new("ShaderNodeVectorMath"); ln.operation = "LENGTH"
    led = N.new("ShaderNodeMath"); led.operation = "LESS_THAN"; led.inputs[1].default_value = 0.34
    L.new(tc.outputs["Object"], sc_.inputs[0]); L.new(sc_.outputs[0], fr.inputs[0]); L.new(fr.outputs[0], sb.inputs[0])
    L.new(sb.outputs[0], sep.inputs[0]); L.new(sep.outputs["X"], cmb.inputs["X"]); L.new(sep.outputs["Y"], cmb.inputs["Y"])
    L.new(cmb.outputs[0], ln.inputs[0]); L.new(ln.outputs["Value"], led.inputs[0])
    glass = N.new("ShaderNodeBsdfPrincipled")
    glass.inputs["Roughness"].default_value = 0.08
    glass.inputs["Coat Weight"].default_value = 1.0
    tint = N.new("ShaderNodeMix"); tint.data_type = "RGBA"
    tint.inputs["A"].default_value = (*[c * 0.35 for c in glass_colour], 1)
    tint.inputs["B"].default_value = (*glass_colour, 1)
    L.new(led.outputs[0], tint.inputs["Factor"]); L.new(tint.outputs["Result"], glass.inputs["Base Color"])
    # Lit: the LEDs bright, the gaps between them softer. Kept below clipping
    # so amber stays amber under the Standard view transform.
    em = N.new("ShaderNodeEmission"); em.inputs["Color"].default_value = (*lit_colour, 1)
    st = N.new("ShaderNodeMapRange"); st.inputs["To Min"].default_value = 0.55; st.inputs["To Max"].default_value = 1.6
    L.new(led.outputs[0], st.inputs["Value"]); L.new(st.outputs["Result"], em.inputs["Strength"])
    add = N.new("ShaderNodeAddShader"); L.new(glass.outputs[0], add.inputs[0]); L.new(em.outputs[0], add.inputs[1])
    mix = N.new("ShaderNodeMixShader"); mix.name = "LitMix"; mix.inputs["Fac"].default_value = 0.0
    L.new(glass.outputs[0], mix.inputs[1]); L.new(add.outputs[0], mix.inputs[2]); L.new(mix.outputs[0], out.inputs["Surface"])
    return m


LENS = {
    "stage": lens_material("LensStage", (1.0, 0.86, 0.55), (0.30, 0.26, 0.16)),
    "amber": lens_material("LensAmber", (1.0, 0.36, 0.0), (0.32, 0.14, 0.01)),
    "green": lens_material("LensGreen", (0.1, 1.0, 0.22), (0.04, 0.22, 0.06)),
    "red": lens_material("LensRed", (1.0, 0.04, 0.03), (0.30, 0.02, 0.02)),
}


def smooth(o):
    for p in o.data.polygons:
        p.use_smooth = True


def box(name, size, loc, mat, bevel=0.012):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.active_object; o.name = name; o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    b = o.modifiers.new("Bevel", "BEVEL"); b.width = bevel; b.segments = 4
    o.data.materials.append(mat); smooth(o)
    return o


def lens(name, r, x, z, y, mat):
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=r, depth=0.02, location=(x, y, z), rotation=(math.pi / 2, 0, 0))
    o = bpy.context.active_object; o.name = name
    b = o.modifiers.new("Bevel", "BEVEL"); b.width = 0.004; b.segments = 3
    o.data.materials.append(mat.copy()); smooth(o)
    bpy.ops.mesh.primitive_torus_add(major_radius=r + 0.006, minor_radius=0.007, location=(x, y - 0.006, z),
                                     rotation=(math.pi / 2, 0, 0), major_segments=64, minor_segments=12)
    t = bpy.context.active_object; t.name = name + "_bezel"; t.data.materials.append(HOUSING); smooth(t)
    return o


def hood(name, r, x, z, y0, depth):
    """A visor over the lens, deepest at the top, cut back toward the bottom."""
    me = bpy.data.meshes.new(name); bm = bmesh.new()
    n, a0, a1 = 40, math.radians(-12), math.radians(192)
    front, back = [], []
    for i in range(n + 1):
        a = a0 + (a1 - a0) * i / n
        cx, cz = x + math.cos(a) * r, z + math.sin(a) * r
        d = depth * (0.35 + 0.65 * max(0.0, math.sin(a)))
        back.append(bm.verts.new((cx, y0, cz)))
        front.append(bm.verts.new((cx, y0 - d, cz)))
    for i in range(n):
        bm.faces.new((back[i], back[i + 1], front[i + 1], front[i]))
    bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); col.objects.link(o)
    s = o.modifiers.new("Solid", "SOLIDIFY"); s.thickness = 0.006; s.offset = 1
    bv = o.modifiers.new("Bevel", "BEVEL"); bv.width = 0.002; bv.segments = 2; bv.limit_method = "ANGLE"
    o.data.materials.append(VISOR); smooth(o)
    return o


# The tree, metres, lenses facing -Y
W, D, GAP = 0.30, 0.10, 0.022
ROWS = [("prestage", "small"), ("stage", "small"), ("amber1", "big"), ("amber2", "big"),
        ("amber3", "big"), ("green", "big"), ("red", "big")]
face = -D / 2
z = 0.0
for name, kind in ROWS:
    h = 0.15 if kind == "small" else 0.27
    zc = z - h / 2
    box("H_" + name, (W, D, h), (0, 0, zc), HOUSING)
    mat = LENS["stage" if kind == "small" else "amber" if name.startswith("amber") else name]
    if kind == "small":
        for side, x in (("l", -0.07), ("r", 0.07)):
            lens("L_%s_%s" % (name, side), 0.046, x, zc - 0.005, face - 0.004, mat)
            hood("V_%s_%s" % (name, side), 0.058, x, zc - 0.005, face, 0.06 * 1.35)
    else:
        lens("L_" + name, 0.104, 0, zc - 0.006, face - 0.004, mat)
        hood("V_" + name, 0.119, 0, zc - 0.006, face, 0.11 * 1.35)
    z -= h + GAP
bottom = z
box("Spine", (0.22, 0.06, -bottom + 0.04), (0, 0.065, bottom / 2), POLE, 0.01)
box("Cap", (W + 0.02, D + 0.02, 0.03), (0, 0, 0.015), HOUSING, 0.01)
bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=0.05, depth=0.5, location=(0, 0.065, bottom - 0.25))
p = bpy.context.active_object; p.name = "Pole"; p.data.materials.append(POLE); smooth(p)

# Camera: orthographic, a little left of and above the tree, so the hoods read
target = Vector((0, 0, -0.93))
cam_d = bpy.data.cameras.new("Cam"); cam_d.type = "ORTHO"; cam_d.ortho_scale = 2.02
cam = bpy.data.objects.new("Cam", cam_d); col.objects.link(cam); sc.camera = cam
cam.location = target + Vector((-0.55, -3.0, 0.62))
cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()


def area(name, loc, energy, size, colour=(1, 1, 1)):
    l = bpy.data.lights.new(name, "AREA"); l.energy = energy; l.size = size; l.color = colour
    o = bpy.data.objects.new(name, l); col.objects.link(o); o.location = loc
    o.rotation_euler = (target - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    return o


area("Key", (-2.0, -2.4, 1.2), 260, 1.6)
area("Fill", (2.2, -2.0, -0.6), 70, 2.0, (0.9, 0.95, 1.0))
rim = area("Rim", (1.2, 2.2, 0.8), 220, 1.0)
area("Top", (0, -0.6, 2.2), 120, 1.0)
world = sc.world or bpy.data.worlds.new("World"); sc.world = world; world.use_nodes = True
bg = next(n for n in world.node_tree.nodes if n.type == "BACKGROUND")
bg.inputs[0].default_value = (0.05, 0.05, 0.055, 1); bg.inputs[1].default_value = 0.6

r = sc.render
r.engine = "CYCLES"
r.resolution_x, r.resolution_y, r.resolution_percentage = 260, 1000, 100
r.film_transparent = True
sc.cycles.samples = 192
sc.cycles.use_denoising = True
sc.view_settings.view_transform = "Standard"
sc.view_settings.look = "None"

# Each lens's box (lens and bezel) in render pixels, top-left origin
dg = bpy.context.evaluated_depsgraph_get()
boxes = {}
for o in bpy.data.objects:
    if not o.name.startswith("L_") or o.name.endswith("_bezel"):
        continue
    xs, ys = [], []
    for ob in (o, bpy.data.objects[o.name + "_bezel"]):
        e = ob.evaluated_get(dg)
        for v in e.data.vertices:
            pt = world_to_camera_view(sc, cam, e.matrix_world @ v.co)
            xs.append(pt.x * r.resolution_x); ys.append((1 - pt.y) * r.resolution_y)
    boxes[o.name[2:]] = [min(xs), min(ys), max(xs), max(ys)]
with open(os.path.join(OUT, "lamps_render.json"), "w") as f:
    json.dump({"size": [r.resolution_x, r.resolution_y], "lamps": boxes}, f, indent=1)

# Dark cards get a stronger rim so the black housing does not sink into them
for theme, rim_energy in (("light", 220), ("dark", 520)):
    rim.data.energy = rim_energy
    for state in ("off", "on"):
        for o in bpy.data.objects:
            if o.name.startswith("L_") and not o.name.endswith("_bezel"):
                o.data.materials[0].node_tree.nodes["LitMix"].inputs["Fac"].default_value = 1.0 if state == "on" else 0.0
        r.filepath = os.path.join(OUT, "tree_%s_%s_raw.png" % (state, theme))
        bpy.ops.render.render(write_still=True)
