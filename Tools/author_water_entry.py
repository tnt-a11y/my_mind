"""Author a last step, reach and push-off between Manny's walk and surface crawl."""

import unreal as u

from import_swim_capture import MATH, PATH, write_sequence


LIMBS = {
    f"{a}_{side}": f"{b}_{side}" for side in ("l", "r") for a, b in (
        ("upperarm", "lowerarm"), ("lowerarm", "hand"),
        ("thigh", "calf"), ("calf", "foot"),
    )
}


def blend(a, b, weight):
    return a + (b - a) * weight


def author_entry():
    mesh = u.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
    skeleton = mesh.get_editor_property("skeleton")
    stand = u.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd")
    crawl = u.load_asset(PATH + "/AS_SurfaceCrawl")
    options = u.AnimPoseEvaluationOptions()
    pose = u.AnimPoseExtensions
    start = pose.get_anim_pose_at_time(stand, .12, options)
    end = pose.get_anim_pose_at_frame(crawl, 0, options)
    reference = pose.get_reference_pose(skeleton)
    body = {"root", "pelvis", "neck_01", "neck_02", "head"}
    body.update(f"spine_{index:02}" for index in range(1, 6))
    body.update(f"{bone}_{side}" for bone in ("clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "ball")
                for side in ("l", "r"))
    names = [str(name) for name in reference.get_bone_names() if str(name) in body]
    parents = {name: str(mesh.get_bone_parent(name)) for name in names}
    space = u.AnimPoseSpaces.LOCAL
    begin = {name: start.get_bone_pose(name, space) for name in names}
    finish = {name: end.get_bone_pose(name, space) for name in names}
    identity = u.Quat(0, 0, 0, 1)

    # Direction keys are in Manny component space, with +Y forward and +Z up.
    # The left hand reaches as the right side pushes off, then the right arm sculls.
    beats = [
        (0, 0.0, 0.0, {}),
        (6, .18, -9.0, {
            "upperarm_l": (.20,.76,-.59), "lowerarm_l": (.04,.98,.14),
            "upperarm_r": (-.22,-.12,-.96), "lowerarm_r": (-.08,.62,-.77),
            "thigh_l": (.12,.35,-.93), "calf_l": (.07,-.25,-.96),
            "thigh_r": (-.12,-.38,-.92), "calf_r": (-.08,-.50,-.86),
        }),
        (13, .60, -12.0, {
            "upperarm_l": (.11,.98,-.18), "lowerarm_l": (-.08,.99,-.11),
            "upperarm_r": (-.48,.62,-.62), "lowerarm_r": (-.09,.67,-.73),
            "thigh_l": (.12,-.45,-.89), "calf_l": (.04,-.79,-.61),
            "thigh_r": (-.11,-.73,-.67), "calf_r": (-.04,-.79,-.60),
        }),
        (19, .87, -4.0, {
            "upperarm_l": (.14,.96,-.24), "lowerarm_l": (-.05,.95,-.30),
            "upperarm_r": (-.62,.03,-.78), "lowerarm_r": (-.08,-.63,-.77),
            "thigh_l": (.10,-.93,-.35), "calf_l": (.03,-.98,.17),
            "thigh_r": (-.09,-.95,-.31), "calf_r": (-.04,-.98,-.14),
        }),
        (25, 1.0, 0.0, {}),
    ]

    def make_pose(weight, dip, aims):
        result = {}
        world = {}
        for name in names:
            a, b = begin[name], finish[name]
            position = blend(a.translation, b.translation, weight)
            if name == "pelvis":
                position.z += dip
            rotation = MATH.quat_slerp(a.rotation, b.rotation, weight)
            parent = world.get(parents[name], identity)
            orientation = parent * rotation
            if name in aims:
                aim = u.Vector(*aims[name])
                child = LIMBS[name]
                axis = blend(begin[child].translation, finish[child].translation, weight)
                swing = MATH.quat_find_between_vectors(MATH.quat_rotate_vector(orientation, axis), aim)
                orientation = swing * orientation
                rotation = MATH.quat_inversed(parent) * orientation
            world[name] = orientation
            result[name] = position, rotation, blend(a.scale3d, b.scale3d, weight)
        return result

    keys = [(frame, make_pose(weight, dip, aims)) for frame, weight, dip, aims in beats]
    tracks = {name: ([], [], []) for name in names}
    segment = 0
    for frame in range(keys[-1][0] + 1):
        if frame > keys[segment + 1][0]:
            segment += 1
        first, before = keys[segment]
        last, after = keys[segment + 1]
        progress = (frame - first) / (last - first)
        ease = .45 * progress + .55 * progress**2 * (3.0 - 2.0 * progress)
        for name, (positions, rotations, scales) in tracks.items():
            a, b = before[name], after[name]
            positions.append(blend(a[0], b[0], ease))
            rotations.append(MATH.quat_slerp(a[1], b[1], ease))
            scales.append(blend(a[2], b[2], ease))

    entry = write_sequence("AS_WadeToSwim", skeleton, tracks)
    u.log(f"SALTWIND_WATER_ENTRY_READY ({entry.get_play_length():.2f} seconds)")


if __name__ == "__main__":
    author_entry()
