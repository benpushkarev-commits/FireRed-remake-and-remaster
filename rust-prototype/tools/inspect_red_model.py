import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\assets\models\red\source\tr0022_00_red.fbx')
from mathutils import Vector
pts=[]
for o in bpy.context.scene.objects:
 if o.type=='MESH': pts += [o.matrix_world @ Vector(c) for c in o.bound_box]
print('BOUNDS',tuple(min(p[i] for p in pts) for i in range(3)),tuple(max(p[i] for p in pts) for i in range(3)))
print('MESHES',[(o.name,len(o.data.vertices),len(o.data.polygons),o.parent.name if o.parent else '') for o in bpy.context.scene.objects if o.type=='MESH'])
