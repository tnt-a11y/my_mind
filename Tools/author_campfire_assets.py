"""Assemble a campfire from CC0 scans and render its item icon and stone icons."""

import math
from pathlib import Path
from random import Random
import sys
import wave

import bpy
from mathutils import Matrix, Vector
import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from render_wood_thumbnails import render


def import_models(path):
    old = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(path))
    return sorted([obj for obj in bpy.data.objects if obj not in old and obj.type == 'MESH'], key=lambda obj: obj.name)


def reframe(obj, origin, dimensions, yaw):
    matrix = obj.matrix_world.copy()
    corners = [matrix @ Vector(corner) for corner in obj.bound_box]
    low = [min(point[i] for point in corners) for i in range(3)]
    high = [max(point[i] for point in corners) for i in range(3)]
    center = Vector(((low[0]+high[0])*.5, (low[1]+high[1])*.5, low[2]))
    matrix.translation -= center
    scale = Matrix.Diagonal(Vector([dimensions[i] / (high[i]-low[i]) for i in range(3)] + [1]))
    obj.parent = None
    obj.matrix_world = Matrix.Translation(origin) @ Matrix.Rotation(yaw, 4, 'Z') @ scale @ matrix


def copy_shape(model):
    obj = model.copy()
    obj.data = model.data.copy()
    bpy.context.collection.objects.link(obj)
    obj.matrix_world = model.matrix_world.copy()
    return obj


def make_bed(stones, wood, output):
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    rocks = import_models(stones)
    log = import_models(wood)[0]
    bpy.context.view_layer.update()
    rng = Random(1921)
    parts = []
    for index in range(10):
        a = index * math.tau/10 + rng.uniform(-.05,.05)
        piece = copy_shape(rocks[index % len(rocks)])
        radius = rng.uniform(.411, .445)
        reframe(piece, (math.cos(a)*radius, math.sin(a)*radius, .005),
                (rng.uniform(.17,.23),rng.uniform(.15,.21),rng.uniform(.095,.14)), a + rng.uniform(.7,1.7))
        parts.append(piece)
    for x, y, z, yaw in ((-.015,-.09,.015,.25), (.03,.065,.027,-.18), (-.035,.01,.099,1.57), (.05,-.01,.046,2.30)):
        piece = copy_shape(log)
        reframe(piece, (x,y,z), (.52,.105,.092), yaw)
        parts.append(piece)
    ash = bpy.data.materials.new("CharcoalAndAsh")
    ash.diffuse_color = (.047,.034,.025,1)
    ash.use_nodes = True
    shader = ash.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (.051,.035,.025,1)
    shader.inputs["Roughness"].default_value = .97
    for _ in range(20):
        a, radius = rng.uniform(0,math.tau), rng.uniform(.03,.26)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,
            location=(math.cos(a)*radius,math.sin(a)*radius,.005))
        coal = bpy.context.object
        coal.scale = (rng.uniform(.022,.061),rng.uniform(.019,.059),rng.uniform(.010,.026))
        coal.data.materials.append(ash)
        parts.append(coal)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in parts:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    combined = bpy.context.object
    combined.name = 'CampfireBed'
    matrix = combined.matrix_world.copy()
    combined.data.transform(matrix)
    combined.matrix_world = Matrix.Identity(4)
    bpy.ops.export_scene.gltf(filepath=str(output), export_format='GLB', use_selection=True, export_apply=True)


def write_crackle(path):
    rng = np.random.default_rng(1921)
    rate, seconds = 44100, 17
    count = rate * seconds
    frequency = np.fft.rfftfreq(count, 1/rate)
    source = rng.standard_normal(count)
    spectrum = np.fft.rfft(source)
    bed = np.fft.irfft(spectrum / np.sqrt(1+(frequency/480)**3) * (frequency/(frequency+90)), n=count)*.14
    for _ in range(186):
        size = int(rng.uniform(.009, .047) * rate)
        position = (rng.integers(count) + np.arange(size)) % count
        t = np.arange(size)/rate
        attack = np.minimum(t*4000, 1)
        grains = np.diff(rng.standard_normal(size+1))*.29 + np.sin(2*np.pi*rng.uniform(280,1400)*t)*.7
        bed[position] += grains*attack*np.exp(-t/rng.uniform(.0014,.009))*rng.uniform(.025,.28)
    with wave.open(str(path), 'wb') as output:
        output.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        output.writeframes(np.asarray(np.clip(bed, -1, 1)*32767, dtype='<i2').tobytes())


if __name__ == '__main__':
    source, wood, output = map(Path, sys.argv[sys.argv.index('--') + 1:])
    output.mkdir(parents=True, exist_ok=True)
    write_crackle(output / 'SW_Campfire.wav')
    rocks = source / 'rock_moss_set_01' / 'rock_moss_set_01_1k.gltf'
    boulder = source / 'boulder_01' / 'boulder_01_1k.gltf'
    driftwood = wood / 'dead_tree_trunk' / 'dead_tree_trunk_1k.gltf'
    campfire = output / 'CampfireBed.glb'
    make_bed(rocks, driftwood, campfire)
    for key, model, index in (('MossStone', rocks, 0), ('SplitRock', rocks, 2), ('CoastalStone', rocks, 5),
                              ('GraniteRock', boulder, None), ('Campfire', campfire, None)):
        render(model, output / ('T_' + key + '.png'), index)
