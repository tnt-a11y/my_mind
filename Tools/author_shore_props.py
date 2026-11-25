"""Build the original cog and live beach snail in a headless Blender session."""

import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector


def material(name, color, roughness, metallic=0.0):
    result = bpy.data.materials.new(name)
    result.diffuse_color = (*color, 1)
    result.use_nodes = True
    shader = result.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Metallic"].default_value = metallic
    return result


def mesh(name, vertices, faces, materials):
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices, [], faces)
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    for finish in materials:
        obj.data.materials.append(finish)
    return obj


def ellipsoid(name, position, scale, surface):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=22, ring_count=12, location=position)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    obj.data.materials.append(surface)
    for face in obj.data.polygons:
        face.use_smooth = True
    return obj


def stalk(name, start, end, root, tip, surface):
    axis = Vector(end) - Vector(start)
    bpy.ops.mesh.primitive_cone_add(vertices=10, radius1=root, radius2=tip,
                                    depth=axis.length, location=Vector(start) + axis * 0.5)
    obj = bpy.context.object
    obj.name = name
    obj.rotation_euler = axis.to_track_quat('Z', 'Y').to_euler()
    obj.data.materials.append(surface)
    return obj


def cog():
    steel = material("SaltStainedIron", (.115, .071, .038), .71, .55)
    vertices = []
    faces = []
    teeth = 15
    segments = teeth * 6
    for z in (0, .045):
        for rim in (True, False):
            for segment in range(segments):
                angle = math.tau * segment / segments
                radius = (0.225, .225, .265, .265, .258, .225)[segment % 6] if rim else .081
                radius *= 1 + .015 * math.sin(angle * 9)
                vertices.append((radius * math.cos(angle), radius * math.sin(angle), z))
    for i in range(segments):
        j = (i + 1) % segments
        lo, hi = segments, segments * 2
        faces += [(i, lo+i, lo+j, j), (hi+i, hi+j, hi+lo+j, hi+lo+i),
                  (i, j, hi+j, hi+i), (lo+j, lo+i, hi+lo+i, hi+lo+j)]
    obj = mesh("ShoreCog", vertices, faces, [steel])
    bevel = obj.modifiers.new("Worn edges", 'BEVEL')
    bevel.width = .0040
    bevel.segments = 3
    obj.modifiers.new("Face normals", 'WEIGHTED_NORMAL')
    return obj


def snail():
    skin = material("DappledSnailSkin", (.115, .095, .047), .57)
    eyes = material("SnailEyes", (.032, .025, .016), .22)
    bands = [material("SpiralShell" + str(i), rgb, .43) for i, rgb in enumerate([
        (.26, .14, .062), (.43, .26, .115), (.23, .107, .047), (.52, .34, .18), (.31, .16, .061)])]
    parts = [ellipsoid("Foot", (-.004, 0, .0125), (.064, .018, .012), skin),
             ellipsoid("Head", (.040, 0, .022), (.021, .015, .013), skin)]
    for side in (-1, 1):
        start, end = (.044, side*.007, .027), (.067, side*.017, .049)
        parts.append(stalk("Upper feeler", start, end, .0031, .0015, skin))
        parts.append(ellipsoid("Eye", end, (.0028, .0025, .0027), eyes))
        parts.append(stalk("Lower feeler", (.046, side*.010, .019), (.066, side*.020, .022), .0021, .0010, skin))
    rings, segments = 118, 18
    vertices, faces = [], []
    for row in range(rings):
        t = row / (rings - 1)
        angle = .72 + t * math.tau * 2.30
        radial = Vector((math.cos(angle), 0, math.sin(angle)))
        radius = .001 + .031 * t**2.05
        tube = (.001 + .020 * t**2.48) * (1 + .036 * math.cos(row * 1.9))
        center = Vector((-.014, .010 * (1 - t), .046)) + radial * radius
        for step in range(segments):
            phase = math.tau * step / segments
            position = center + tube * (radial * math.cos(phase) + Vector((0, .92 * math.sin(phase), 0)))
            position.z = max(.010, position.z)
            vertices.append(position)
    for row in range(rings - 1):
        for step in range(segments):
            i, j = row*segments+step, row*segments+(step+1) % segments
            faces.append((i, j, j+segments, i+segments))
    faces.append(tuple(reversed(range(segments))))
    faces.append(tuple(range((rings-1)*segments, rings*segments)))
    shell = mesh("Spiral", vertices, faces, bands)
    for face in shell.data.polygons:
        row, segment = divmod(face.index, segments)
        groove = math.sin(row * .52 + math.sin(segment*.7)*.62) + .23*math.sin(row*2.36)
        face.material_index = 3 if row >= rings-4 else (1 if groove > .33 else 2 if groove < -.70 else 4 if groove < -.18 else 0)
        face.use_smooth = True
    parts.append(shell)
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = shell
    bpy.ops.object.join()
    shell.name = "BeachSnail"
    return shell


def export(directory, name, build):
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    obj = build()
    bpy.ops.object.select_all(action='DESELECT')
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(directory / (name + ".glb")), export_format='GLB',
                              use_selection=True, export_apply=True)


if __name__ == "__main__":
    output = Path(sys.argv[sys.argv.index("--") + 1])
    output.mkdir(parents=True, exist_ok=True)
    export(output, "ShoreCog", cog)
    export(output, "BeachSnail", snail)
