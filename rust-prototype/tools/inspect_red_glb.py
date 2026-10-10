import bpy
from mathutils import Vector
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\assets\models\red\red.glb')
pts=[]
for o in bpy.context.scene.objects:
 if o.type=='MESH':
  pts += [o.matrix_world @ Vector(c) for c in o.bound_box]
print('ALL_BOUNDS',tuple(round(min(p[i] for p in pts),2) for i in range(3)),tuple(round(max(p[i] for p in pts),2) for i in range(3)))
for o in bpy.context.scene.objects:
 if o.type=='MESH':
  print(o.name,'dim',tuple(round(x,2) for x in o.dimensions),'rot',tuple(round(x,3) for x in o.rotation_euler))
