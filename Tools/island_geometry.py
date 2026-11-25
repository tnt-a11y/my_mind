"""Terrain and water geometry in metres in Unreal's X/Y/Z axes."""

import json
import math
from pathlib import Path
import struct
import sys
import zlib
from array import array


def clamp(value, low=0.0, high=1.0):
    return min(high, max(low, value))


def smooth(low, high, value):
    t = clamp((value - low) / (high - low))
    return t * t * (3 - 2 * t)


def coast(x, y):
    nx, ny = (x - 2) / 83, (y - 8) / 62
    angle = math.atan2(ny, nx)
    outline = 1 + .09 * math.sin(angle * 3 + .45) + .058 * math.cos(angle * 5 - .7)
    outline += .034 * math.sin(angle * 9 + 1.8)
    bay = math.exp(-((x - 24) / 22) ** 2) * smooth(-29, -51, y)
    return (outline - math.hypot(nx, ny) - .12 * bay) * 72


def trail_x(y):
    return -39 + .41 * (y + 36) + math.sin((y + 38) / 9) * 4.6


def terrain(x, y):
    shore = coast(x, y)
    beach = .105 * shore
    sea = -.061 * max(0, -shore - 13) ** 1.34
    ridge = 19.0 * math.exp(-((x - 22) / 31) ** 2 - ((y - 40) / 22) ** 2)
    ridge += 12.5 * math.exp(-((x + 53) / 16) ** 2 - ((y - 17) / 27) ** 2)
    ridge += 7.6 * math.exp(-((x - 54) / 21) ** 2 - ((y + 2) / 21) ** 2)
    inland = smooth(5, 30, shore)
    path = smooth(1.4, 5.1, abs(x - trail_x(y)))
    relief = (.70 * math.sin(.17 * x + .07 * y) + .49 * math.sin(.17 * y - .11 * x)
              + .32 * math.sin(.39 * x - .13 * y) + .16 * math.cos(.7 * y + .41 * x))
    relief *= (.17 + .83 * inland) * (.34 + .66 * path)
    base = beach + sea + ridge * smooth(6, 27, shore) + relief * smooth(-6, 4, shore)
    for cx, cy, rx, ry, peak in [(124, 98, 36, 23, 27), (183, 109, 26, 18, 18), (-149, -107, 30, 17, 16)]:
        dx, dy = (x - cx) / rx, (y - cy) / ry
        angle = math.atan2(dy, dx)
        taper = 1 - math.hypot(dx, dy) + .11 * math.sin(5 * angle)
        if taper > -1.05:
            islet = peak * max(0, taper) ** .80 + 7 * min(taper, 0)
            apron = smooth(-1.05, -.25, taper)
            base += apron * max(0, islet - base)
    return max(-24.0, base)


def normal(x, y):
    dx = (terrain(x + .15, y) - terrain(x - .15, y)) / .3
    dy = (terrain(x, y + .15) - terrain(x, y - .15)) / .3
    length = math.sqrt(dx * dx + dy * dy + 1.0)
    return -dx / length, -dy / length, 1 / length


def grid(path, x_coords, y_coords, height, normals):
    vertices = array("f")
    directions = array("f")
    uv = array("f")
    indices = array("I")
    minimum, maximum = [float("inf")] * 3, [-float("inf")] * 3
    for iy, y in enumerate(y_coords):
        for ix, x in enumerate(x_coords):
            z = height(x, y)
            nx, ny, nz = normals(x, y)
            # Interchange swaps Y and Z and converts metres to centimetres.
            point = (x, z, y)
            vertices.extend(point)
            directions.extend((nx, nz, ny))
            uv.extend((x * .5, 1.0 - y * .5))
            for coordinate, value in enumerate(point):
                minimum[coordinate] = min(minimum[coordinate], value)
                maximum[coordinate] = max(maximum[coordinate], value)
            if ix and iy:
                d = iy * len(x_coords) + ix
                a, b, c = d - len(x_coords) - 1, d - len(x_coords), d - 1
                indices.extend((a, c, b, b, c, d))

    payload = bytearray()
    views, accessors = [], []
    for data, stride, typ, component, target in (
            (vertices, 3, "VEC3", 5126, 34962), (directions, 3, "VEC3", 5126, 34962),
            (uv, 2, "VEC2", 5126, 34962), (indices, 1, "SCALAR", 5125, 34963)):
        raw = data.tobytes()
        views.append(dict(buffer=0, byteOffset=len(payload), byteLength=len(raw), target=target))
        accessors.append(dict(bufferView=len(views)-1, componentType=component, count=len(data)//stride, type=typ))
        payload.extend(raw)
    accessors[0].update(min=minimum, max=maximum)
    accessors[3].update(min=[0], max=[len(vertices)//3-1])
    doc = dict(asset=dict(version="2.0", generator="Saltwind"),
               buffers=[dict(byteLength=len(payload))], bufferViews=views, accessors=accessors,
               meshes=[dict(name=path.stem, primitives=[dict(attributes=dict(POSITION=0, NORMAL=1, TEXCOORD_0=2), indices=3)])],
               nodes=[dict(mesh=0, name=path.stem)], scenes=[dict(nodes=[0])], scene=0)
    descriptor = json.dumps(doc, separators=(",", ":")).encode()
    descriptor += b" " * ((-len(descriptor)) % 4)
    with path.open("wb") as out:
        out.write(struct.pack("<4sII", b"glTF", 2, 28 + len(descriptor) + len(payload)))
        out.write(struct.pack("<I4s", len(descriptor), b"JSON"))
        out.write(descriptor)
        out.write(struct.pack("<I4s", len(payload), b"BIN\x00"))
        out.write(payload)


def depth_texture(path):
    # Lossless linear height samples for depth, wind attenuation and foam.
    size = 512
    raw = bytearray()
    for iy in range(size):
        raw.append(0)
        y = -224 + (iy + .5) * 448 / size
        for ix in range(size):
            x = -224 + (ix + .5) * 448 / size
            depth = clamp((22 - terrain(x, y)) / 46)
            raw.append(round(depth * 255))
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 0, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 7)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    grid(destination / "CoralReachTerrain.glb", range(-208, 217), range(-164, 145), terrain, normal)
    offshore = [-5500, -2500, -1200, -680, -440, -320, -250, -210]
    water_axis = offshore + list(range(-192, 193, 2)) + [-value for value in reversed(offshore)]
    grid(destination / "CoralReachOcean.glb", water_axis, water_axis, lambda x, y: 0, lambda x, y: (0, 0, 1))
    depth_texture(destination / "T_LagoonDepth.png")
