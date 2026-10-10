"""blender -b --python render_kit.py -- out.png a.glb b.glb ...   (lines them up 3 m apart)"""
import bpy, sys, math, mathutils
argv = sys.argv[sys.argv.index('--') + 1:]
out, files = argv[0], argv[1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
for i, f in enumerate(files):
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.gltf(filepath=f)
    for o in set(bpy.context.scene.objects) - before:
        if o.parent is None:
            o.location.x += i * 3.0
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
bpy.context.scene.collection.objects.link(cam)
mid = (len(files) - 1) * 1.5
cam.location = (mid, -9, 4)
cam.rotation_euler = (mathutils.Vector((mid, 0, 1)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
bpy.context.scene.camera = cam
sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN')); sun.data.energy = 3
sun.rotation_euler = (math.radians(45), 0, math.radians(25)); bpy.context.scene.collection.objects.link(sun)
w = bpy.data.worlds.new('w'); bpy.context.scene.world = w; w.use_nodes = True
w.node_tree.nodes['Background'].inputs[0].default_value = (0.55, 0.65, 0.75, 1)
sc = bpy.context.scene
for eng in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT'):
    try: sc.render.engine = eng; break
    except Exception: pass
sc.render.resolution_x, sc.render.resolution_y = 1280, 540
sc.render.filepath = out
bpy.ops.render.render(write_still=True)
print('RENDERED', out)
