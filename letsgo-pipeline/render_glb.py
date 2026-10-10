"""blender -b --python render_glb.py -- in.glb out.png [yaw_deg pitch_deg dist_scale]"""
import bpy, sys, math, mathutils
argv = sys.argv[sys.argv.index('--') + 1:]
src, out = argv[0], argv[1]
yaw = float(argv[2]) if len(argv) > 2 else 200
pitch = float(argv[3]) if len(argv) > 3 else 50
dscale = float(argv[4]) if len(argv) > 4 else 1.0
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
objs = [o for o in bpy.context.scene.objects if o.type == 'MESH']
mn = mathutils.Vector((1e9,) * 3); mx = -mn
for o in objs:
    for c in o.bound_box:
        w = o.matrix_world @ mathutils.Vector(c)
        mn = mathutils.Vector(map(min, mn, w)); mx = mathutils.Vector(map(max, mx, w))
ctr = (mn + mx) / 2
size = (mx - mn).length
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
bpy.context.scene.collection.objects.link(cam)
d = size * 0.75 * dscale
y, p = math.radians(yaw), math.radians(pitch)
cam.location = ctr + mathutils.Vector((math.cos(p) * math.sin(y), -math.cos(p) * math.cos(y), math.sin(p))) * d
cam.rotation_euler = (ctr - cam.location).to_track_quat('-Z', 'Y').to_euler()
cam.data.clip_end = size * 10
cam.data.lens = 35
bpy.context.scene.camera = cam
sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
sun.data.energy = 3.0
sun.rotation_euler = (math.radians(40), 0, math.radians(30))
bpy.context.scene.collection.objects.link(sun)
world = bpy.data.worlds.new('w'); bpy.context.scene.world = world
world.use_nodes = True
world.node_tree.nodes['Background'].inputs[0].default_value = (0.6, 0.75, 0.95, 1)
world.node_tree.nodes['Background'].inputs[1].default_value = 0.8
sc = bpy.context.scene
for eng in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT', 'BLENDER_WORKBENCH'):
    try:
        sc.render.engine = eng; break
    except Exception:
        pass
sc.render.resolution_x, sc.render.resolution_y = 1280, 720
sc.render.filepath = out
bpy.ops.render.render(write_still=True)
print('RENDERED', out, 'bounds', tuple(round(v) for v in mn), tuple(round(v) for v in mx))
