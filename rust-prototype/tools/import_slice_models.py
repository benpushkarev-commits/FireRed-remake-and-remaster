"""Blender batch importer for Ben's local model archives; no downloads.

Run blender -b --python tools/import_slice_models.py -- <archive-folder>.
Humanoid clips are deliberately simple authored cycles, not original-game clips.
"""
import bpy
import json
import math
import pathlib
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from mathutils import Matrix, Vector, Euler, Quaternion

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = pathlib.Path(sys.argv[sys.argv.index('--') + 1])
TMP = ROOT / 'tmp' / 'slice-models'
OUT = ROOT / 'assets' / 'models'


def material(name, folder, image_name=None):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Roughness'].default_value = 0.9
    shader.inputs['Specular IOR Level'].default_value = 0.15
    candidates = list(folder.rglob('*.png'))
    wanted = pathlib.Path(image_name or name).stem.lower()
    wanted = re.sub(r'\.\d+$', '', wanted)
    image = next((p for p in candidates if p.stem.lower() == wanted), None)
    if image is None:
        image = next((p for p in sorted(candidates) if p.stem.lower().endswith('_' + wanted + '_col')), None)
    if image:
        texture = mat.node_tree.nodes.new('ShaderNodeTexImage')
        texture.image = bpy.data.images.load(str(image))
        if 'fire' in name.lower():
            shader.inputs['Base Color'].default_value = (1., 0.24, 0.01, 1.)
            mat.node_tree.links.new(texture.outputs['Alpha'], shader.inputs['Alpha'])
            mat.surface_render_method = 'DITHERED'
            shader.inputs['Emission Strength'].default_value = 0.7
            shader.inputs['Emission Color'].default_value = (1., 0.3, 0.01, 1.)
        else:
            mat.node_tree.links.new(texture.outputs['Color'], shader.inputs['Base Color'])
    return mat


def import_smd(path):
    lines = [s.strip() for s in path.read_text().splitlines() if s.strip()]
    names, parents, local = {}, {}, {}
    start = lines.index('nodes') + 1
    for line in lines[start:]:
        if line == 'end':
            break
        match = re.fullmatch(r'(\d+) "([^"]+)" (-?\d+)', line)
        i, name, parent = match.groups()
        names[int(i)], parents[int(i)] = name, int(parent)
    start = lines.index('skeleton') + 2
    for line in lines[start:]:
        if line == 'end':
            break
        values = line.split()
        i = int(values[0])
        xyz = tuple(map(float, values[1:4]))
        rot = Euler(tuple(map(float, values[4:7])), 'XYZ')
        local[i] = Matrix.Translation(xyz) @ rot.to_matrix().to_4x4()
    world = {}
    def matrix(i):
        if i not in world:
            world[i] = matrix(parents[i]) @ local[i] if parents[i] >= 0 else local[i]
        return world[i]
    arm = bpy.data.objects.new('Armature', bpy.data.armatures.new('Rig'))
    bpy.context.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for i, name in names.items():
        bone = arm.data.edit_bones.new(name)
        bone.matrix = matrix(i)
        bone.length = 1.0
        if parents[i] >= 0:
            bone.parent = arm.data.edit_bones[names[parents[i]]]
    bpy.ops.object.mode_set(mode='OBJECT')
    batches = {}
    start = lines.index('triangles') + 1
    while lines[start] != 'end':
        mat = lines[start]
        verts = []
        for line in lines[start + 1:start + 4]:
            values = line.split()
            weights = [(int(values[j]), float(values[j + 1])) for j in range(10, len(values), 2)]
            if not weights:
                weights = [(int(values[0]), 1.0)]
            verts.append((tuple(map(float, values[1:4])), tuple(map(float, values[7:9])), weights))
        batches.setdefault(mat, []).extend(verts)
        start += 4
    for name, verts in batches.items():
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([v[0] for v in verts], [], [tuple(range(i, i + 3)) for i in range(0, len(verts), 3)])
        mesh.uv_layers.new()
        for loop in mesh.loops:
            mesh.uv_layers.active.data[loop.index].uv = verts[loop.vertex_index][1]
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        obj.data.materials.append(material(name, path.parent))
        for i, (_, _, weights) in enumerate(verts):
            for joint, weight in weights:
                if weight <= 0:
                    continue
                group = obj.vertex_groups.get(names[joint]) or obj.vertex_groups.new(name=names[joint])
                group.add([i], weight, 'REPLACE')
        obj.parent = arm
        modifier = obj.modifiers.new('Skin', 'ARMATURE')
        modifier.object = arm
    # These exports use Y up; Blender uses Z up.
    arm.rotation_euler.x = math.pi / 2
    return arm


def import_dae_mesh(path):
    """Charmander's archive is Collada-only. Preserve indexed UVs/materials.

    This bounded mesh conversion deliberately doesn't claim skeletal conversion.
    It gets the source model into the slice; its initial idle is a gentle sway.
    """
    root = ET.parse(path).getroot()
    ns = {'c': root.tag.split('}')[0][1:]}
    images = {e.attrib['id']: e.findtext('c:init_from', namespaces=ns) for e in root.findall('c:library_images/c:image', ns)}
    mats = {}
    for effect in root.findall('c:library_effects/c:effect', ns):
        image_id = effect.findtext('.//c:surface/c:init_from', namespaces=ns)
        mats[effect.attrib['id']] = material(effect.attrib.get('name', effect.attrib['id']), path.parent, images.get(image_id))
    material_ids = {m.attrib['id']: mats[m.find('c:instance_effect', ns).attrib['url'][1:]] for m in root.findall('c:library_materials/c:material', ns)}
    bindings = {i.attrib['symbol']: i.attrib['target'][1:] for i in root.findall('.//c:instance_material', ns)}
    for geom in root.findall('c:library_geometries/c:geometry', ns):
        # Bulbasaur includes two extended attack-vine meshes. Without their
        # original visibility animation they become tall idle spikes and
        # incorrectly dominate the model's scale. Keep them in the source.
        if 'pm0001' in path.name and '_tuta_' in geom.attrib['id']:
            continue
        sources = {}
        for source in geom.findall('c:mesh/c:source', ns):
            values = list(map(float, source.findtext('c:float_array', namespaces=ns).split()))
            stride = int(source.find('c:technique_common/c:accessor', ns).attrib['stride'])
            sources[source.attrib['id']] = [values[i:i + stride] for i in range(0, len(values), stride)]
        vertices = {v.attrib['id']: v.find('c:input', ns).attrib['source'][1:] for v in geom.findall('c:mesh/c:vertices', ns)}
        for tri in geom.findall('c:mesh/c:triangles', ns):
            inputs = {i.attrib['semantic']: (int(i.attrib['offset']), i.attrib['source'][1:]) for i in tri.findall('c:input', ns)}
            stride = 1 + max(i[0] for i in inputs.values())
            indices = list(map(int, tri.findtext('c:p', namespaces=ns).split()))
            positions, uvs = [], []
            pos_offset, pos_source = inputs['VERTEX']
            uv_offset, uv_source = inputs.get('TEXCOORD', (0, None))
            for i in range(0, len(indices), stride):
                x, y, z = sources[vertices[pos_source]][indices[i + pos_offset]][:3]
                positions.append((x, -z, y))
                uvs.append(sources[uv_source][indices[i + uv_offset]][:2] if uv_source else (0, 0))
            mesh = bpy.data.meshes.new(geom.attrib['id'])
            mesh.from_pydata(positions, [], [tuple(range(i, i + 3)) for i in range(0, len(positions), 3)])
            mesh.uv_layers.new()
            for loop in mesh.loops:
                mesh.uv_layers.active.data[loop.index].uv = uvs[loop.vertex_index]
            obj = bpy.data.objects.new(geom.attrib['id'], mesh)
            bpy.context.collection.objects.link(obj)
            name = tri.attrib['material']
            obj.data.materials.append(material_ids[bindings.get(name, name)])


def humanoid_clips(arm):
    bones = arm.pose.bones
    bpy.context.view_layer.update()
    # Lower T/A-pose arms using their actual world directions, rather than
    # assuming one bone basis across FBX and SMD exports.
    for side in ['L', 'R']:
        upper, elbow = bones.get(side + 'Arm'), bones.get(side + 'ForeArm')
        if upper and elbow:
            direction = (arm.matrix_world @ elbow.head) - (arm.matrix_world @ upper.head)
            target = Vector((0.18 if side == 'L' else -0.18, 0, -1)).normalized()
            delta = direction.normalized().rotation_difference(target)
            basis = (arm.matrix_world @ upper.matrix).to_quaternion()
            original = upper.matrix_basis.to_quaternion()
            upper.rotation_mode = 'QUATERNION'
            upper.rotation_quaternion = original @ basis.inverted() @ delta @ basis
            bpy.context.view_layer.update()
    # Key the complete imported base pose, including Red's root correction.
    # Sampling an unkeyed root reverted it to the FBX bind orientation.
    names = [b.name for b in bones]
    base, axes = {}, {}
    for name in names:
        if name in bones:
            bone = bones[name]
            q = bone.matrix_basis.to_quaternion()
            bone.rotation_mode = 'QUATERNION'
            bone.rotation_quaternion = q
            base[name] = bone.rotation_quaternion.copy()
            axes[name] = (arm.matrix_world @ bone.matrix).to_quaternion().inverted() @ Vector((1, 0, 0))
    arm.animation_data_create()
    for clip, duration, amplitude in [('Idle', 2.4, 0), ('Walk', 0.8, 0.42), ('Run', 0.5, 0.72)]:
        action = bpy.data.actions.new(clip)
        arm.animation_data.action = action
        frames = round(duration * 30)
        for frame in range(frames + 1):
            phase = frame / frames * 2 * math.pi
            wave = math.sin(phase)
            angles = {
                'LThigh': amplitude * wave, 'RThigh': -amplitude * wave,
                'LLeg': -amplitude * 1.5 * max(0, -wave), 'RLeg': -amplitude * 1.5 * max(0, wave),
                'LFoot': amplitude * 0.3 * wave, 'RFoot': -amplitude * 0.3 * wave,
                # These Let's Go rigs export the left upper-arm basis with
                # an extra quarter turn. Correct it in every authored clip.
                'LArm': -math.pi / 2 - amplitude * 0.65 * wave, 'RArm': amplitude * 0.65 * wave,
                'LForeArm': -(0.12 + (0.55 if clip == 'Run' else 0.08)),
                'RForeArm': -(0.12 + (0.55 if clip == 'Run' else 0.08)),
                'Spine1': (0.09 if clip == 'Run' else 0) + 0.015 * wave,
                'Head': 0.012 * math.sin(phase),
            }
            for name, q in base.items():
                bones[name].rotation_quaternion = q @ Quaternion(axes[name], angles.get(name, 0.))
                bones[name].keyframe_insert('rotation_quaternion', frame=frame, group=name)
                if frame in (0, frames):
                    bones[name].keyframe_insert('location', frame=frame, group=name)
                    bones[name].keyframe_insert('scale', frame=frame, group=name)
        slot = arm.animation_data.action_slot
        arm.animation_data.action = None
        track = arm.animation_data.nla_tracks.new()
        track.name = clip
        strip = track.strips.new(clip, 0, action)
        strip.action_slot = slot
        track.mute = True
    for name, q in base.items():
        bones[name].rotation_quaternion = q
    bpy.context.scene.frame_set(0)


def convert(key, archive, member, height, human):
    folder = TMP / key
    folder.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name.endswith('/'):
                continue
            target = folder / name
            if not target.resolve().is_relative_to(folder.resolve()):
                raise RuntimeError('Archive path outside extraction folder')
            if name == member or name.lower().endswith('.png'):
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(z.read(name))
    path = folder / member
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    if path.suffix.lower() == '.fbx':
        bpy.ops.import_scene.fbx(filepath=str(path), use_image_search=True)
    elif path.suffix.lower() == '.smd':
        import_smd(path)
    else:
        import_dae_mesh(path)
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    if not meshes:
        raise RuntimeError(f'No meshes: {key}')
    if not human:
        # Pokémon currently use root-level idle motion. Bake their source pose
        # for this preview rather than exporting an unanimated FBX skin that
        # can lose source pose corrections. Source archives retain the rigs.
        bpy.context.view_layer.update()
        deps = bpy.context.evaluated_depsgraph_get()
        for obj in meshes:
            world = obj.matrix_world.copy()
            baked = bpy.data.meshes.new_from_object(obj.evaluated_get(deps), depsgraph=deps)
            obj.modifiers.clear()
            obj.parent = None
            obj.data = baked
            obj.matrix_world = world
        for arm in [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']:
            bpy.data.objects.remove(arm, do_unlink=True)
    # Bake import coordinate/unit transforms into BOTH vertices and rest bones.
    # Leaving FBX's rotated armature object in the skin exported a lying-down
    # Red in the native renderer even though the Blender bounds were upright.
    bpy.context.view_layer.update()
    armatures = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
    mesh_world = {o.name: o.matrix_world.copy() for o in meshes}
    arm_world = {o.name: o.matrix_world.copy() for o in armatures}
    for obj in meshes:
        obj.data.transform(mesh_world[obj.name])
        obj.parent = None
        obj.matrix_world = Matrix.Identity(4)
    for arm in armatures:
        arm.data.transform(arm_world[arm.name])
        arm.parent = None
        arm.matrix_world = Matrix.Identity(4)
    for obj in meshes:
        modifier = next((m for m in obj.modifiers if m.type == 'ARMATURE'), None)
        if modifier:
            obj.parent = modifier.object
            obj.matrix_parent_inverse = Matrix.Identity(4)
        obj.matrix_world = Matrix.Identity(4)
        for poly in obj.data.polygons:
            poly.use_smooth = True
    if human:
        humanoid_clips(armatures[0])
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    points = [o.matrix_world @ Vector(v) for o in meshes for v in o.evaluated_get(deps).bound_box]
    low, high = min(p.z for p in points), max(p.z for p in points)
    scale = height / (high - low)
    wrapper = bpy.data.objects.new(key, None)
    bpy.context.collection.objects.link(wrapper)
    for o in list(bpy.context.scene.objects):
        if o != wrapper and o.parent is None:
            o.parent = wrapper
    wrapper.scale = (scale,) * 3
    wrapper.location.z = -low * scale
    output = OUT / key / f'{key}.glb'
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(output), export_format='GLB', export_animations=human,
                              export_animation_mode='NLA_TRACKS', export_force_sampling=True,
                              export_skins=True, export_all_influences=False)
    print('SLICE_MODEL', key, 'height', height, 'meshes', len(meshes), 'rigs', len(armatures), 'bytes', output.stat().st_size)
    return {'key': key, 'file': f'models/{key}/{key}.glb', 'height': height,
            'clips': ['Idle', 'Walk', 'Run'] if human else [], 'archive': archive.name, 'source': member}


jobs = [
    ('red', ' - Red.zip', None, 1.75, True),
    ('blue', ' - Blue.zip', None, 1.8, True),
    ('brock', ' - Brock.zip', None, 1.85, True),
    ('youngster', ' - Youngster.zip', None, 1.35, True),
    ('camper', ' - Camper.zip', None, 1.4, True),
    ('woman', ' - Miscellaneous NPCs.zip', '/Adults/Woman/', 1.65, True),
    ('fat_man', ' - Miscellaneous NPCs.zip', '/Fat/', 1.65, True),
    ('scientist', ' - Miscellaneous NPCs.zip', '/Scientist/', 1.75, True),
    ('clerk', ' - Miscellaneous NPCs.zip', '/Staff/Man/', 1.7, True),
    ('girl', ' - Miscellaneous NPCs.zip', '/Juveniles/Girl/', 1.4, True),
    ('old_man', ' - Miscellaneous NPCs.zip', '/Elderly/Man/', 1.65, True),
    ('bulbasaur', ' - #0001 Bulbasaur.zip', None, 0.7, False),
    ('charmander', ' - #0004 Charmander.zip', None, 0.85, False),
    ('squirtle', ' - #0007 Squirtle.zip', None, 0.72, False),
    ('pidgey', ' - #0016 Pidgey.zip', None, 0.5, False),
    ('rattata', ' - #0019 Rattata.zip', 'Rattata_M', 0.45, False),
]
manifest = []
for key, suffix, contains, height, human in jobs:
    if '--only' in sys.argv and key not in sys.argv[sys.argv.index('--only') + 1].split(','):
        existing = json.loads((OUT / 'slice-models.json').read_text())
        manifest.append(next(e for e in existing if e['key'] == key))
        continue
    archive = next(p for p in SOURCE.glob('*.zip') if p.name.endswith(suffix))
    with zipfile.ZipFile(archive) as z:
        choices = [n for n in z.namelist() if (not contains or contains in n) and 'lod' not in n.lower()]
        member = next((n for n in choices if n.lower().endswith('.dae')), None) if not human else None
        member = member or next((n for n in choices if n.lower().endswith('.fbx')), None)
        member = member or next((n for n in choices if n.lower().endswith('.smd')), None)
        member = member or next(n for n in choices if n.lower().endswith('.dae'))
    manifest.append(convert(key, archive, member, height, human))
(OUT / 'slice-models.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
