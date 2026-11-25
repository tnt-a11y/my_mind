"""Retarget CMU subject 126 swimming captures to Saltwind's Manny skeleton in Unreal."""

import math
from pathlib import Path
import re
import sys

import unreal as u


AXES = {
    "x": u.Vector(1, 0, 0),
    "y": u.Vector(0, 1, 0),
    "z": u.Vector(0, 0, 1),
}
MATH = u.MathLibrary
PATH = "/Game/Saltwind/Swimming"


def rotation(channels):
    q = u.Quat(0, 0, 0, 1)
    for name, degrees in channels:
        half = math.radians(degrees) * .5
        axis = AXES[name[-1]] * math.sin(half)
        q = u.Quat(axis.x, axis.y, axis.z, math.cos(half)) * q
    return q


def read_skeleton(path):
    source, hierarchy = path.read_text().split(":hierarchy", 1)
    bones = {}
    for block in re.findall(r"\bbegin\s+(.*?)\s+end\b", source, re.DOTALL):
        fields = {items[0]: items[1:] for line in block.splitlines() if (items := line.split())}
        angles = map(float, fields["axis"][:3])
        basis = rotation(zip(fields["axis"][3].lower(), angles))
        bones[fields["name"][0]] = (basis, fields.get("dof", []), u.Vector(*map(float, fields["direction"])))
    parents = {}
    for line in hierarchy.splitlines():
        names = line.split()
        if len(names) > 1:
            parents.update({child: names[0] for child in names[1:]})
    return bones, parents


def read_motion(path):
    frames = []
    for line in path.read_text().splitlines():
        fields = line.split()
        if len(fields) == 1 and fields[0].isdigit():
            frames.append({})
        elif frames and len(fields) > 1:
            frames[-1][fields[0]] = tuple(map(float, fields[1:]))
    return [frame for frame in frames if frame.keys() == frames[0].keys()
            and all(len(values) == len(frames[0][name]) for name, values in frame.items())]


def choose_loop(frames, minimum, maximum, minimum_swing):
    names = ("lhumerus", "rhumerus", "lradius", "rradius", "lfemur", "rfemur", "ltibia", "rtibia")
    features = [sum((frame[name] for name in names), ()) for frame in frames]

    def error(start, length):
        return sum((a - b)**2 for offset in (-8, 0, 8)
                   for a, b in zip(features[start + offset], features[start + length + offset]))

    first_length = (minimum + 3) // 4 * 4
    def covers_stroke(start, length):
        samples = features[start:start + length:4]
        ranges = [max(values) - min(values) for values in zip(*(sample[:6] for sample in samples))]
        return all(sum(value**2 for value in ranges[index:index + 3]) >= minimum_swing**2 for index in (0, 3))

    candidates = ((start, length) for start in range(48, len(frames) - maximum - 12, 4)
                  for length in range(first_length, maximum, 4) if covers_stroke(start, length))
    return min(candidates, key=lambda pair: error(*pair))


def component_rotations(frame, bones, parents, heading):
    rotations = {"root": rotation(zip(("rx", "ry", "rz"), frame["root"][3:]))}
    for child, parent in parents.items():
        basis, channels, _ = bones[child]
        local = rotation(zip(channels, frame.get(child, ())))
        rotations[child] = rotations[parent] * basis * local * MATH.quat_inversed(basis)
    # ASF is right-handed, with +Y up and +Z forward. Manny faces +Y with +Z up.
    return {name: heading * u.Quat(-q.x, -q.z, -q.y, q.w) for name, q in rotations.items()}


def write_capture(path, asset_name, periods, bones, parents, mesh, crawl=False):
    skeleton = mesh.get_editor_property("skeleton")
    ref = u.AnimPoseExtensions.get_reference_pose(skeleton)
    world = u.AnimPoseSpaces.WORLD
    local = u.AnimPoseSpaces.LOCAL
    mapping = {
        "pelvis": "root", "spine_01": "lowerback", "spine_02": "lowerback", "spine_03": "upperback",
        "spine_04": "thorax", "spine_05": "thorax", "neck_01": "lowerneck", "neck_02": "upperneck", "head": "head",
    }
    next_bone = {}
    for side in ("l", "r"):
        for target, source, child in (
            ("clavicle", "clavicle", "upperarm"), ("upperarm", "humerus", "lowerarm"),
            ("lowerarm", "radius", "hand"), ("hand", "hand", "middle_01"),
            ("thigh", "femur", "calf"), ("calf", "tibia", "foot"), ("foot", "foot", "ball"),
            ("ball", "toes", None),
        ):
            name = f"{target}_{side}"
            mapping[name] = side + source
            if child:
                next_bone[name] = f"{child}_{side}"

    target_local = {name: ref.get_bone_pose(name, local) for name in mapping}
    target_world = {name: ref.get_bone_pose(name, world) for name in mapping}
    target_parents = {name: str(mesh.get_bone_parent(name)) for name in mapping}
    bind = {name: t.rotation for name, t in target_world.items()}
    for name, child in next_bone.items():
        _, _, axis = bones[mapping[name]]
        start = ref.get_bone_pose(child, world).translation - target_world[name].translation
        bind[name] = MATH.quat_find_between_vectors(start, u.Vector(axis.x, axis.z, axis.y)) * bind[name]

    frames = read_motion(path)
    first, period = choose_loop(frames, *periods, 125 if crawl else 54)
    # Point the average projected chest axis down Manny's forward axis.
    midpoint = frames[first:first + period]
    directions = [MATH.quat_rotate_vector(rotation(zip(("rx", "ry", "rz"), frame["root"][3:])), AXES["y"])
                  for frame in midpoint]
    heading = rotation((("z", math.degrees(math.atan2(sum(v.x for v in directions),
                                                      sum(v.z for v in directions)))),))
    # The studio captures have the hips bent well below the body line.
    # Preserve the kick timing while aligning the extended legs with the stroke.
    leg_centers = {name: sum(frame[name][0] for frame in midpoint) / len(midpoint)
                   for name in ("lfemur", "rfemur", "lfoot", "rfoot")}
    track = {name: [] for name in mapping}
    surface_pitch = rotation((("x", 23.0),)) if crawl else u.Quat(0, 0, 0, 1)
    for index in range(0, period + 1, 4):
        frame = dict(frames[first + index])
        for side in ("l", "r"):
            thigh, calves, foot = side + "femur", side + "tibia", side + "foot"
            flex, yaw, roll = frame[thigh]
            bend = frame[calves][0]
            ankle, twist = frame[foot]
            if crawl:
                frame[thigh] = ((flex - leg_centers[thigh]) * .73, yaw * .77, roll)
                frame[calves] = (7.0 + bend * .48,)
                frame[foot] = (48.0 + (ankle - leg_centers[foot]) * .52, twist * .62)
            else:
                frame[thigh] = ((flex - leg_centers[thigh]) * .86 - 13.0, yaw, roll)
                frame[calves] = (bend * .77,)
                glide = 1.0 - max(0.0, min(1.0, bend / 110.0))
                frame[foot] = (8.0 + 37.0 * glide + (ankle - leg_centers[foot]) * .48, twist * .83)
        component = component_rotations(frame, bones, parents, heading)
        wanted = {}
        for name, source in mapping.items():
            wanted[name] = surface_pitch * component[source] * bind[name]
            parent = target_parents[name]
            parent_q = wanted.get(parent) or ref.get_bone_pose(parent, world).rotation
            track[name].append(MATH.quat_inversed(parent_q) * wanted[name])

    # Ease the last few samples into the first key so looping never snaps.
    for keys in track.values():
        assert all(abs(key.x**2 + key.y**2 + key.z**2 + key.w**2 - 1.0) < .001 for key in keys)
        for offset in range(1, 5):
            sample = offset / 4.0
            keys[-offset] = MATH.quat_slerp(keys[0], keys[-offset], sample * sample * (3 - 2 * sample))
        keys[-1] = keys[0]
    upper_arm = track["upperarm_l"]
    assert max(abs(upper_arm[0].x - key.x) + abs(upper_arm[0].y - key.y)
               + abs(upper_arm[0].z - key.z) + abs(upper_arm[0].w - key.w)
               for key in upper_arm) > .2

    count = period // 4 + 1
    tracks = {name: ([target_local[name].translation] * count, keys, [target_local[name].scale3d] * count)
              for name, keys in track.items()}
    write_sequence(asset_name, skeleton, tracks)
    u.log(f"Saltwind saved {asset_name} from {path.stem}, captured frames {first + 1}–{first + period + 1}.")


def write_sequence(asset_name, skeleton, tracks):
    asset = u.EditorAssetLibrary.load_asset(f"{PATH}/{asset_name}") if u.EditorAssetLibrary.does_asset_exist(
        f"{PATH}/{asset_name}") else None
    if asset is None:
        factory = u.AnimSequenceFactory()
        factory.set_editor_property("target_skeleton", skeleton)
        asset = u.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, PATH, u.AnimSequence, factory)
    assert asset
    controller = asset.get_editor_property("controller")
    controller.open_bracket("Set Saltwind swimming keys", False)
    controller.set_frame_rate(u.FrameRate(30, 1), False)
    count = len(next(iter(tracks.values()))[0])
    controller.set_number_of_frames(u.FrameNumber(count - 1), False)
    existing = {str(name) for name in u.AnimationLibrary.get_animation_track_names(asset)}
    for name in existing - tracks.keys():
        assert controller.remove_bone_track(name, False), name
    for name, (translations, rotations, scales) in tracks.items():
        if name not in existing:
            assert controller.add_bone_curve(name, False), name
        assert controller.set_bone_track_keys(name, translations, rotations, scales, False), name
    controller.close_bracket(False)
    asset.set_editor_property("enable_root_motion", False)
    assert u.EditorAssetLibrary.save_loaded_asset(asset, False)
    return asset


def run(directory, surface_only=False):
    bones, parents = read_skeleton(directory / "126.asf")
    mesh = u.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
    write_capture(directory / "126_11.amc", "AS_SurfaceCrawl", (196, 284), bones, parents, mesh, crawl=True)
    if not surface_only:
        write_capture(directory / "126_04.amc", "AS_SubmergedBreaststroke", (150, 290), bones, parents, mesh)


if __name__ == "__main__":
    run(Path(sys.argv[1]))
