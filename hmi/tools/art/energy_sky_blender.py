"""Model and render the Energy screen's sky in Blender.

Run headless, with the output folder after "--":

    blender -b -P energy_sky_blender.py -- OUTDIR [layers] [clouds] [moon]

then energy_post.py turns the renders into hmi/assets/energy/ (see the
README there). The Energy screen's sky card is built from layers QML stacks
and blends:

    sky_<phase>_<theme>.png    the sky alone, opaque (no sun disc, no land)
    land_<phase>_<theme>.png   ground, hills and a far range, transparent above
    cloud<N>_<theme>.png       single drifting clouds, transparent
    moon_<k>_<theme>.png       the moon at eight phases, transparent

phase: day (sun 55 deg up), low (1.5 deg), dusk (-3 deg), night (-14 deg)
and overcast (day light under a cloud deck). theme: light, dark. The dark
theme is the same scene 2.6 stops darker and a little less saturated, so it
sits on the black page without glare. The sun disc itself is drawn by QML,
which moves it with the real sun.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.getcwd()
os.makedirs(OUT, exist_ok=True)

W, H = 1096, 440          # the sky card at twice its 548 x 220 size

PHASES = {
    #          sun elevation, sun azimuth (deg), exposure
    "day":      (55.0, 190.0, -1.4),
    "low":      (1.5, 200.0, -0.2),
    "dusk":     (-3.0, 205.0, 2.2),
    "night":    (-14.0, 205.0, 5.0),
    "overcast": (55.0, 190.0, -1.0),
}
THEMES = {"light": (0.0, 1.0), "dark": (-2.6, 0.8)}   # exposure offset, saturation


def clear():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.lights,
                 bpy.data.cameras, bpy.data.worlds, bpy.data.node_groups,
                 bpy.data.volumes, bpy.data.textures):
        for x in list(coll):
            coll.remove(x)


def setup_render(sc):
    try:
        sc.render.engine = "CYCLES"
    except TypeError as e:
        print("engine:", e)
    sc.cycles.device = "CPU"          # the GPU is often busy with ComfyUI
    sc.cycles.samples = 64
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = W, H
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.look = "None"


def world_sky(sc):
    w = bpy.data.worlds.new("Sky")
    sc.world = w
    w.use_nodes = True
    N, L = w.node_tree.nodes, w.node_tree.links
    for n in list(N):
        N.remove(n)
    sky = N.new("ShaderNodeTexSky")
    sky.sky_type = "MULTIPLE_SCATTERING"
    sky.sun_disc = False
    sky.altitude = 1500.0
    sky.air_density = 1.0
    sky.aerosol_density = 1.2
    # Desaturate in the world so the dark theme can mute the sky
    hsv = N.new("ShaderNodeHueSaturation")
    bg = N.new("ShaderNodeBackground")
    out = N.new("ShaderNodeOutputWorld")
    L.new(sky.outputs["Color"], hsv.inputs["Color"])
    L.new(hsv.outputs["Color"], bg.inputs["Color"])
    L.new(bg.outputs["Background"], out.inputs["Surface"])
    return sky, hsv, bg


def principled(name, base, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    b.inputs["Base Color"].default_value = (*base, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular IOR Level"].default_value = 0.0    # no sky glare off the land
    return m


def land(col):
    """Near ground plus a far ridge, both low and wide."""
    import bmesh
    objs = []
    me = bpy.data.meshes.new("Ground")
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=2, y_segments=2, size=2000)
    bm.to_mesh(me)
    g = bpy.data.objects.new("Ground", me)
    g.data.materials.append(principled("Earth", (0.11, 0.095, 0.07), 0.95))
    col.objects.link(g)
    objs.append(g)

    # Two ridges, heights from a few sines so the skyline is the same every
    # run: a tall pale far range, and darker rolling hills in front of it
    def ridge(name, y, span, hfun, base, seg=600):
        me = bpy.data.meshes.new(name)
        bm = bmesh.new()
        xs = [-span + 2 * span * i / seg for i in range(seg + 1)]
        bot = [bm.verts.new((x, y, -1)) for x in xs]
        top = [bm.verts.new((x, y, max(2.0, hfun(x)))) for x in xs]
        for i in range(seg):
            bm.faces.new((bot[i], bot[i + 1], top[i + 1], top[i]))
        bm.to_mesh(me)
        r = bpy.data.objects.new(name, me)
        r.data.materials.append(principled(name, base, 0.9))
        col.objects.link(r)
        objs.append(r)

    def far(x):
        return (250 + 150 * math.sin(x / 260.0 + 0.6) + 80 * math.sin(x / 97.0 + 2.1)
                + 26 * math.sin(x / 37.0 + 0.3) + 8 * math.sin(x / 13.0))

    def near(x):
        return 26 + 14 * math.sin(x / 120.0 + 1.0) + 6 * math.sin(x / 47.0 + 2.6) + 2 * math.sin(x / 17.0)
    ridge("RidgeFar", 2600, 3400, far, (0.42, 0.45, 0.52))
    ridge("RidgeNear", 900, 1300, near, (0.13, 0.13, 0.12))
    return objs


def camera(sc, col):
    cd = bpy.data.cameras.new("Cam")
    cd.lens = 20
    cd.sensor_width = 36
    cd.clip_end = 20000
    c = bpy.data.objects.new("Cam", cd)
    c.location = (0, 0, 1.6)
    # Facing +Y, tipped up so the horizon sits low in the frame
    c.rotation_euler = (math.radians(90 + 13), 0, 0)
    col.objects.link(c)
    sc.camera = c
    return c


def sun_lamp(col):
    ld = bpy.data.lights.new("Sun", "SUN")
    ld.angle = math.radians(1.0)
    s = bpy.data.objects.new("Sun", ld)
    col.objects.link(s)
    return s


def aim_sun(s, elev, azim):
    # Blender's sky: rotation 0 puts the sun towards +Y; we look along +Y
    e, a = math.radians(elev), math.radians(azim - 180)
    d = Vector((math.sin(a) * math.cos(e), math.cos(a) * math.cos(e), math.sin(e)))
    s.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()


def build():
    clear()
    sc = bpy.context.scene
    col = sc.collection
    setup_render(sc)
    sky, hsv, bg = world_sky(sc)
    ground = land(col)
    camera(sc, col)
    sun = sun_lamp(col)
    return sc, sky, hsv, bg, ground, sun


def set_phase(sc, sky, hsv, bg, sun, phase, theme):
    elev, azim, exp = PHASES[phase]
    dexp, sat = THEMES[theme]
    sky.sun_elevation = math.radians(elev)
    sky.sun_rotation = math.radians(azim - 180)
    hsv.inputs["Saturation"].default_value = sat * {"overcast": 0.25, "day": 1.45, "low": 1.3}.get(phase, 1.0)
    bg.inputs["Strength"].default_value = 1.0
    aim_sun(sun, elev, azim)
    # Real sun light only while it is up; the sky lights the land otherwise
    sun.data.energy = 4.0 * max(0.0, math.sin(math.radians(elev))) ** 0.5 if elev > 0 else 0.0
    if phase == "overcast":
        sun.data.energy = 0.0
        bg.inputs["Strength"].default_value = 0.55
    sc.view_settings.exposure = exp + dexp


def render(sc, path):
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def render_layers(phases=None, themes=None):
    sc, sky, hsv, bg, ground, sun = build()
    for phase in phases or PHASES:
        for theme in themes or THEMES:
            set_phase(sc, sky, hsv, bg, sun, phase, theme)
            for o in ground:
                o.hide_render = True
            sc.render.film_transparent = False
            render(sc, os.path.join(OUT, "sky_%s_%s.png" % (phase, theme)))
            for o in ground:
                o.hide_render = False
            # The land takes the sky's light without the sky's extra colour,
            # or blue daylight turns the ground into a lake
            hsv.inputs["Saturation"].default_value = 0.55 * THEMES[theme][1]
            sc.render.film_transparent = True
            render(sc, os.path.join(OUT, "land_%s_%s.png" % (phase, theme)))


# ---------------------------------------------------------------- clouds

# Each cloud: (puffs as x, z, radius in metres), noise seed. Flat-bottomed
# cumulus, wider than tall.
CLOUDS = [
    ([(-3.2, 0.2, 1.5), (-1.4, 0.9, 2.1), (0.6, 1.2, 2.4), (2.6, 0.6, 1.8), (4.0, 0.1, 1.2)], 1.0),
    ([(-2.0, 0.3, 1.6), (0.0, 1.0, 2.2), (2.0, 0.4, 1.7)], 7.0),
    ([(-4.0, 0.0, 1.0), (-2.4, 0.4, 1.4), (-0.6, 0.6, 1.6), (1.2, 0.5, 1.5), (2.8, 0.2, 1.1), (4.0, 0.0, 0.8)], 13.0),
]


def ortho_camera(sc, col, scale, y=-30.0):
    cd = bpy.data.cameras.new("Ortho")
    cd.type = "ORTHO"
    cd.ortho_scale = scale
    c = bpy.data.objects.new("Ortho", cd)
    c.location = (0, y, 0)
    c.rotation_euler = (math.radians(90), 0, 0)
    col.objects.link(c)
    sc.camera = c
    return c


def flat_world(sc, colour, strength):
    w = bpy.data.worlds.new("Flat")
    sc.world = w
    w.use_nodes = True
    bg = next(n for n in w.node_tree.nodes if n.type == "BACKGROUND")
    bg.inputs["Color"].default_value = (*colour, 1)
    bg.inputs["Strength"].default_value = strength


def cloud_object(col, puffs, seed):
    """Puffs merged into a volume, then broken up with noise."""
    import bmesh
    me = bpy.data.meshes.new("Puffs")
    bm = bmesh.new()
    for x, z, r in puffs:
        m = bmesh.ops.create_icosphere(bm, subdivisions=3, radius=r)
        for v in m["verts"]:
            v.co.x += x
            v.co.z = max(v.co.z * 0.8 + z, -0.6)   # squash, flatten the base
            v.co.y *= 0.8
    bm.to_mesh(me)
    puff = bpy.data.objects.new("Puffs", me)
    col.objects.link(puff)
    puff.hide_render = True
    puff.hide_viewport = True

    vol = bpy.data.objects.new("Cloud", bpy.data.volumes.new("Cloud"))
    col.objects.link(vol)
    m2v = vol.modifiers.new("ToVolume", "MESH_TO_VOLUME")
    m2v.object = puff
    m2v.resolution_mode = "VOXEL_SIZE"
    m2v.voxel_size = 0.06
    m2v.interior_band_width = 0.4
    m2v.density = 1.0
    tex = bpy.data.textures.new("CloudNoise%d" % int(seed), "CLOUDS")
    tex.noise_scale = 0.9
    tex.noise_depth = 4
    disp = vol.modifiers.new("Displace", "VOLUME_DISPLACE")
    disp.texture = tex
    disp.strength = 0.9
    disp.texture_mid_level = (0.5, 0.5, 0.5)
    disp.texture_sample_radius = 1.0
    vol.location = (seed * 100, 0, 0)       # a different patch of noise each
    puff.location = vol.location

    m = bpy.data.materials.new("CloudVol")
    m.use_nodes = True
    N, L = m.node_tree.nodes, m.node_tree.links
    for n in list(N):
        N.remove(n)
    pv = N.new("ShaderNodeVolumePrincipled")
    pv.inputs["Color"].default_value = (1, 1, 1, 1)
    pv.inputs["Density"].default_value = 3.0
    pv.inputs["Anisotropy"].default_value = 0.3
    out = N.new("ShaderNodeOutputMaterial")
    L.new(pv.outputs["Volume"], out.inputs["Volume"])
    vol.data.materials.append(m)
    return vol


def render_clouds(themes=None):
    clear()
    sc = bpy.context.scene
    col = sc.collection
    setup_render(sc)
    sc.cycles.samples = 96
    sc.cycles.volume_bounces = 8
    sc.render.film_transparent = True
    sc.render.resolution_x, sc.render.resolution_y = 640, 300
    flat_world(sc, (0.55, 0.66, 0.85), 1.7)          # blue sky fill underneath
    sun = sun_lamp(col)
    sun.data.energy = 7.0
    aim_sun(sun, 50.0, 25.0)        # high, behind the viewer's right shoulder
    cam = ortho_camera(sc, col, 13.0)
    for i, (puffs, seed) in enumerate(CLOUDS):
        for o in list(col.objects):
            if o.name.startswith(("Cloud", "Puffs")):
                bpy.data.objects.remove(o, do_unlink=True)
        v = cloud_object(col, puffs, seed)
        cam.location = (v.location.x, -30, 0.5)
        for theme in themes or THEMES:
            sc.view_settings.exposure = THEMES[theme][0] * 0.8
            render(sc, os.path.join(OUT, "cloud%d_%s.png" % (i + 1, theme)))


# ---------------------------------------------------------------- moon

def render_moon(themes=None):
    clear()
    sc = bpy.context.scene
    col = sc.collection
    setup_render(sc)
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = 160
    flat_world(sc, (0.5, 0.6, 0.8), 0.015)           # earthshine on the dark side
    bpy.ops.mesh.primitive_uv_sphere_add(segments=96, ring_count=48, radius=1.0)
    moon = bpy.context.active_object
    bpy.ops.object.shade_smooth()
    m = bpy.data.materials.new("Moon")
    m.use_nodes = True
    N, L = m.node_tree.nodes, m.node_tree.links
    b = next(n for n in N if n.type == "BSDF_PRINCIPLED")
    b.inputs["Roughness"].default_value = 1.0
    # Maria: large soft dark patches
    nz = N.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 0.9
    nz.inputs["Detail"].default_value = 1.5
    ramp = N.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.42
    ramp.color_ramp.elements[0].color = (0.45, 0.45, 0.46, 1)
    ramp.color_ramp.elements[1].position = 0.66
    ramp.color_ramp.elements[1].color = (0.72, 0.71, 0.69, 1)
    L.new(nz.outputs["Fac"], ramp.inputs["Fac"])
    L.new(ramp.outputs["Color"], b.inputs["Base Color"])
    # Craters: voronoi pits as bump
    vor = N.new("ShaderNodeTexVoronoi")
    vor.inputs["Scale"].default_value = 9.0
    bump = N.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.25
    L.new(vor.outputs["Distance"], bump.inputs["Height"])
    L.new(bump.outputs["Normal"], b.inputs["Normal"])
    moon.data.materials.append(m)
    ortho_camera(sc, col, 2.12, y=-10)
    sun = sun_lamp(col)
    sun.data.energy = 4.0
    for k in range(8):
        phi = math.radians(k * 45.0)
        to_sun = Vector((math.sin(phi), math.cos(phi), 0.0))
        sun.rotation_euler = (-to_sun).to_track_quat("-Z", "Y").to_euler()
        for theme in themes or THEMES:
            sc.view_settings.exposure = THEMES[theme][0] * 0.5
            render(sc, os.path.join(OUT, "moon_%d_%s.png" % (k, theme)))


if __name__ == "__main__" and "--" in sys.argv:
    # Optional parts after the folder: layers, clouds, moon (default: all)
    parts = sys.argv[sys.argv.index("--") + 2:] or ["layers", "clouds", "moon"]
    if "layers" in parts:
        render_layers()
    if "clouds" in parts:
        render_clouds()
    if "moon" in parts:
        render_moon()
