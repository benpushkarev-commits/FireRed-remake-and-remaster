"""blender -b --python render_char.py -- in.glb out.png clip frame yaw"""
import bpy, sys, math, mathutils
argv = sys.argv[sys.argv.index('--') + 1:]
src, out, clip, frame, yaw = argv[0], argv[1], argv[2], int(argv[3]), float(argv[4])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
arm = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
acts = [a for a in bpy.data.actions if clip.lower() in a.name.lower()]
print('ACTIONS', [a.name for a in bpy.data.actions])
if arm and acts:
    arm[0].animation_data_create()
    arm[0].animation_data.action = acts[0]
    try:
        slot = acts[0].slots[0]
        arm[0].animation_data.action_slot = slot
    except Exception as e:
        print('slot', e)
bpy.context.scene.frame_set(frame)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
bpy.context.scene.collection.objects.link(cam)
y = math.radians(yaw)
cam.location = (math.sin(y) * 4.0, -math.cos(y) * 4.0, 1.0)
cam.rotation_euler = (mathutils.Vector((0, 0, 0.75)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
cam.data.lens = 50
bpy.context.scene.camera = cam
sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
sun.data.energy = 3.0
sun.rotation_euler = (math.radians(50), 0, math.radians(20))
bpy.context.scene.collection.objects.link(sun)
world = bpy.data.worlds.new('w'); bpy.context.scene.world = world
world.use_nodes = True
world.node_tree.nodes['Background'].inputs[0].default_value = (0.5, 0.6, 0.7, 1)
sc = bpy.context.scene
for eng in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT', 'BLENDER_WORKBENCH'):
    try:
        sc.render.engine = eng; break
    except Exception:
        pass
sc.render.resolution_x, sc.render.resolution_y = 640, 720
sc.render.filepath = out
bpy.ops.render.render(write_still=True)
print('RENDERED', out)
