"""Check embedded textures, real loop motion and world-space skin bounds."""
import json
import pathlib
import struct
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / 'assets/models/slice-models.json').read_text())
for entry in manifest:
    path = ROOT / 'assets' / entry['file']
    blob = path.read_bytes()
    assert blob[:4] == b'glTF' and struct.unpack_from('<I', blob, 8)[0] == len(blob), path
    size = struct.unpack_from('<I', blob, 12)[0]
    gltf = json.loads(blob[20:20 + size])
    binary = blob[28 + size:]
    assert gltf.get('images'), f'Missing textures: {path}'

    def read(index):
        accessor = gltf['accessors'][index]
        view = gltf['bufferViews'][accessor['bufferView']]
        offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
        dtype = {5126: '<f4', 5123: '<u2', 5121: 'u1'}[accessor['componentType']]
        count = {'VEC3': 3, 'VEC4': 4, 'SCALAR': 1, 'MAT4': 16}[accessor['type']]
        assert not view.get('byteStride'), 'Checker expects packed exporter output'
        return np.frombuffer(binary, dtype, count=accessor['count'] * count, offset=offset).reshape(-1, count)

    def matrix(node):
        x, y, z, w = node.get('rotation', [0, 0, 0, 1])
        result = np.eye(4)
        result[:3, :3] = np.array([
            [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)],
        ]) @ np.diag(node.get('scale', [1, 1, 1]))
        result[:3, 3] = node.get('translation', [0, 0, 0])
        return result

    # The active first frame includes imported FBX base-pose corrections.
    if gltf.get('animations'):
        animation = gltf['animations'][0]
        for channel in animation['channels']:
            values = read(animation['samplers'][channel['sampler']]['output'])
            gltf['nodes'][channel['target']['node']][channel['target']['path']] = values[0].tolist()
    world = {}
    def walk(index, parent):
        world[index] = parent @ matrix(gltf['nodes'][index])
        for child in gltf['nodes'][index].get('children', []):
            walk(child, world[index])
    for root in gltf['scenes'][0]['nodes']:
        walk(root, np.eye(4))
    if entry['clips']:
        names = {node.get('name'): i for i, node in enumerate(gltf['nodes'])}
        for side in ['L', 'R']:
            upper, elbow = names[side + 'Arm'], names[side + 'ForeArm']
            direction = world[elbow][:3, 3] - world[upper][:3, 3]
            direction /= np.linalg.norm(direction)
            assert direction[1] < -0.9 and abs(direction[2]) < 0.1, f'Idle arm basis: {entry["key"]} {side} {direction}'
    all_points = []
    for index, node in enumerate(gltf['nodes']):
        if 'mesh' not in node:
            continue
        skin = None
        if 'skin' in node:
            spec = gltf['skins'][node['skin']]
            assert len(spec['joints']) <= 256
            bind = read(spec['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
            skin = np.array([world[i] @ m for i, m in zip(spec['joints'], bind)])
        for primitive in gltf['meshes'][node['mesh']]['primitives']:
            attributes = primitive['attributes']
            points = read(attributes['POSITION'])
            points = np.concatenate([points, np.ones((len(points), 1))], axis=1)
            if skin is not None:
                joints, weights = read(attributes['JOINTS_0']), read(attributes['WEIGHTS_0'])
                assert np.allclose(weights.sum(axis=1), 1., atol=0.001)
                output = np.zeros_like(points)
                for k in range(4):
                    output += np.einsum('nij,nj->ni', skin[joints[:, k]], points) * weights[:, k, None]
            else:
                output = points @ world[index].T
            all_points.append(output[:, :3])
    points = np.concatenate(all_points)
    low, high = points.min(axis=0), points.max(axis=0)
    assert abs(low[1]) < 0.035, f'Not grounded/upright: {entry["key"]} {low} {high}'
    assert abs(high[1] - entry['height']) < 0.05, f'Bad skin height: {entry["key"]} {low} {high}'
    clips = gltf.get('animations', [])
    assert [a['name'] for a in clips] == entry['clips']
    for animation in clips:
        changes = 0
        for sampler in animation['samplers']:
            values = read(sampler['output'])
            # Quaternion signs are equivalent rotations, but exporter loops
            # should still return to the identical encoded pose.
            assert np.allclose(values[0], values[-1], atol=0.0001), f'Loop seam: {path} {animation["name"]}'
            changes += not np.allclose(values[0], values[len(values)//4], atol=0.001)
        assert changes > 0, f'Static clip: {path} {animation["name"]}'
    print(entry['key'], 'textures/skin bounds/loops OK')
