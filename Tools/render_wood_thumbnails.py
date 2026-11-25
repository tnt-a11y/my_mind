"""Render transparent inventory thumbnails from the same CC0 meshes used by pickups."""

from pathlib import Path
import math
import sys

import bpy
from mathutils import Matrix, Vector


MODELS = [
    ("Driftwood", "dead_tree_trunk"), ("LichenLog", "dead_tree_trunk_02"),
    ("PaleBranch", "dead_quiver_branch_01"), ("ForkedBranch", "dead_quiver_branch_02"),
    ("Kindling", "dry_branches_medium_01"), ("QuiverBark", "dead_quiver_trunk"),
]


def aim(obj, point):
    obj.rotation_euler = (Vector(point) - obj.location).to_track_quat('-Z', 'Y').to_euler()


def area(name, position, energy, size, color):
    light = bpy.data.lights.new(name, 'AREA')
    light.energy = energy
    light.shape = 'DISK'
    light.size = size
    light.color = color
    obj = bpy.data.objects.new(name, light)
    bpy.context.collection.objects.link(obj)
    obj.location = position
    aim(obj, (0, 0, 0))


def render(source, destination, variant=None):
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(source))
    meshes = sorted((obj for obj in bpy.context.scene.objects if obj.type == 'MESH'), key=lambda obj: obj.name)
    if variant is not None:
        chosen = meshes[variant]
        for mesh in meshes:
            if mesh != chosen:
                bpy.data.objects.remove(mesh, do_unlink=True)
        meshes = [chosen]
    corners = [obj.matrix_world @ Vector(corner) for obj in meshes for corner in obj.bound_box]
    lo = Vector([min(vertex[i] for vertex in corners) for i in range(3)])
    hi = Vector([max(vertex[i] for vertex in corners) for i in range(3)])
    center = (lo + hi) / 2
    scale = 1.9 / max(hi - lo)
    size = hi - lo
    tilt = Matrix.Rotation(math.pi/2 if size.z > max(size.x, size.y) * 1.08 else 0, 4, 'Y')
    for obj in meshes:
        transform = obj.matrix_world.copy()
        transform.translation -= center
        obj.parent = None
        obj.matrix_world = Matrix.Scale(scale, 4) @ tilt @ transform
    scene = bpy.context.scene
    bpy.ops.object.camera_add(location=(2.3, -3.5, 2.65))
    scene.camera = bpy.context.object
    aim(scene.camera, (0, 0, 0))
    scene.camera.data.type = 'ORTHO'
    scene.camera.data.ortho_scale = 2.72
    area("Warm softbox", (1.2, -2.4, 3.6), 360, 2.5, (1.0, .87, .67))
    area("Cool fill", (-2.8, -1.0, 2.3), 205, 2.8, (.74, .88, 1.0))
    area("Bark rim", (.2, 2.8, 1.65), 260, 1.9, (1.0, .94, .78))
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 20
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 272
    scene.render.resolution_y = 196
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.filepath = str(destination)
    bpy.ops.render.render(write_still=True)


if __name__ == '__main__':
    directory, previous, output = map(Path, sys.argv[sys.argv.index('--') + 1:])
    output.mkdir(parents=True, exist_ok=True)
    for name, slug in MODELS:
        source = directory / slug / (slug + '_1k.gltf')
        if not source.is_file():
            source = previous / slug / (slug + '_1k.gltf')
        render(source, output / ('T_Wood_' + name + '.png'), 0 if name == 'Kindling' else None)
