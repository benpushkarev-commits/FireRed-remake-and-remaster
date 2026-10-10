import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\assets\models\red\source\tr0022_00_red.fbx',use_image_search=True)
for arm in [o for o in bpy.context.scene.objects if o.type=='ARMATURE']:
 print('ARMATURE',arm.name,'bones',len(arm.data.bones))
 for b in arm.data.bones:
  if any(k in b.name.lower() for k in ('arm','shoulder','upper','elbow','wrist','hand')): print(b.name,'parent=',b.parent.name if b.parent else None,'head=',tuple(round(v,2) for v in b.head_local),'tail=',tuple(round(v,2) for v in b.tail_local))
