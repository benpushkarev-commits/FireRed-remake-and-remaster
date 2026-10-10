import bpy
import os
source = os.environ['RED_MODEL_SOURCE']
out = os.environ['RED_MODEL_OUTPUT']
bpy.ops.wm.read_factory_settings(use_empty=True)
status = bpy.ops.import_scene.fbx(filepath=source, use_image_search=True)
print('FBX_IMPORT_STATUS', status)
meshes = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
if not meshes:
    raise RuntimeError('FBX import produced no mesh objects')
armatures = [obj for obj in bpy.context.scene.objects if obj.type == 'ARMATURE']
if not armatures:
    raise RuntimeError('FBX import produced no armature')
armature = armatures[0]
# Rotate around each upper arm's local depth axis to lower the arms beside Red.
for name, angle in [('LArm', -1.2), ('RArm', 1.2)]:
    bone = armature.pose.bones.get(name)
    if bone is None:
        raise RuntimeError(f'Missing expected Red arm bone: {name}')
    bone.rotation_mode = 'XYZ'
    bone.rotation_euler[2] = angle
bpy.context.view_layer.update()
for obj in meshes:
    obj.name = 'Red_' + obj.name
    obj.data.name = obj.name + '_Mesh'
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    baked = bpy.data.meshes.new_from_object(evaluated, depsgraph=depsgraph)
    if baked is None:
        raise RuntimeError(f'Could not bake mesh {obj.name}')
    while obj.modifiers:
        obj.modifiers.remove(obj.modifiers[0])
    obj.parent = None
    obj.data = baked
    for poly in obj.data.polygons:
        poly.use_smooth = True
bpy.ops.object.select_all(action='DESELECT')
for obj in meshes:
    obj.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', use_selection=True, export_apply=True)
print('RED_GLB_OUTPUT', out)
