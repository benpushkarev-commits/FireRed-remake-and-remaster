"""blender -b --python render_top.py -- in.glb out.png ox oz s W H
Orthographic top-down render of the FireRed tile window x in [-0.5, W-0.5], z in [-0.5, H-0.5]
where FireRed (x, z) = LGPE (X, Z) * s + (ox, oz). 40 px per tile."""
import bpy, sys, math
argv = sys.argv[sys.argv.index('--') + 1:]
src, out = argv[0], argv[1]
ox, oz, s, W, H = map(float, argv[2:7])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
cx, cz = (W - 1) / 2, (H - 1) / 2
X, Z = (cx - ox) / s, (cz - oz) / s     # LGPE glTF coords of window centre
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
bpy.context.scene.collection.objects.link(cam)
cam.data.type = 'ORTHO'
cam.data.ortho_scale = W / s
cam.data.clip_end = 100000
cam.location = (X, -Z, 20000)          # glTF (x,y,z) -> Blender (x,-z,y)
cam.rotation_euler = (0, 0, 0)
bpy.context.scene.camera = cam
sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
sun.data.energy = 3.0
sun.rotation_euler = (math.radians(20), 0, math.radians(30))
bpy.context.scene.collection.objects.link(sun)
world = bpy.data.worlds.new('w'); bpy.context.scene.world = world
world.use_nodes = True
world.node_tree.nodes['Background'].inputs[0].default_value = (0.2, 0.2, 0.25, 1)
sc = bpy.context.scene
for eng in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT', 'BLENDER_WORKBENCH'):
    try:
        sc.render.engine = eng; break
    except Exception:
        pass
sc.render.resolution_x, sc.render.resolution_y = int(W * 40), int(H * 40)
sc.render.filepath = out
bpy.ops.render.render(write_still=True)
print('RENDERED', out)
