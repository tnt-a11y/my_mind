"""Create the Coral Reach level in an Unreal Python commandlet."""

import math
import os
from pathlib import Path
import random
import sys

import unreal as u

sys.path.insert(0, str(Path(__file__).parent))
from island_geometry import coast, terrain, trail_x, normal
from island_materials import terrain_material, water_material

ROOT = "/Game/Saltwind"
MAP = ROOT + "/Maps/CoralReach"
SOURCE = Path(os.environ.get("SALTWIND_ASSETS", Path(u.Paths.project_dir()).parent / ".saltwind-work" / "assets")).resolve()
asset_lib = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
meshes = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
registry = u.AssetRegistryHelpers.get_asset_registry()
rng = random.Random(22716)


def note(message):
    u.log("SALTWIND | " + str(message))


def set_value(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as error:
        note(f"{obj.get_name()} {name}: {error}")
        return False


def folder(path):
    result = f"{ROOT}/{path}"
    asset_lib.make_directory(result)
    return result


def import_source(file, destination, name=None):
    assert file.is_file(), file
    target = folder(destination)
    task = u.AssetImportTask()
    task.set_editor_properties(dict(filename=str(file), destination_path=target, automated=True,
                                    save=True, replace_existing=False))
    if name:
        existing = asset_lib.load_asset(f"{target}/{name}") if asset_lib.does_asset_exist(f"{target}/{name}") else None
        if existing:
            return [existing]
        task.set_editor_property("destination_name", name)
    assets.import_asset_tasks([task])
    results = task.get_objects()
    if not results:
        results = [record.get_asset() for record in registry.get_assets_by_path(target, recursive=True)]
    note(f"{file.name} -> " + ", ".join(result.get_name() for result in results if result))
    return results


def create_asset(name, destination, cls, factory):
    path = folder(destination)
    full = f"{path}/{name}"
    return asset_lib.load_asset(full) if asset_lib.does_asset_exist(full) else assets.create_asset(name, path, cls, factory)


def import_surface(slug, label):
    directory = SOURCE / slug
    selection = [next(p for p in directory.glob("*.jpg") if n in p.name) for n in ("_diff", "_nor_dx", "_rough")]
    output = []
    for suffix, file in zip(("D", "N", "R"), selection):
        texture, *_ = import_source(file, "Textures", f"T_{label}_{suffix}")
        if suffix == "N":
            texture.set_editor_properties(dict(srgb=False, compression_settings=u.TextureCompressionSettings.TC_NORMALMAP))
        if suffix == "R":
            texture.set_editor_properties(dict(srgb=False, compression_settings=u.TextureCompressionSettings.TC_DEFAULT))
        asset_lib.save_loaded_asset(texture)
        output.append(texture)
    return output


def import_mesh(file, path):
    existing = [record.get_asset() for record in registry.get_assets_by_path(f"{ROOT}/{path}", recursive=True)
                if str(record.asset_class_path.asset_name) == "StaticMesh"]
    if existing:
        return sorted(existing, key=lambda value: value.get_name())
    result = [item for item in import_source(file, path) if isinstance(item, u.StaticMesh)]
    if not result:
        raise RuntimeError(f"{file.name} produced no static meshes")
    return result


def actor(cls, name, x=0, y=0, z=0, pitch=0, yaw=0, roll=0, group=""):
    placed = actors.spawn_actor_from_class(cls, u.Vector(x*100, y*100, z*100), u.Rotator(roll, pitch, yaw))
    if not placed:
        raise RuntimeError(f"Could not place {name}")
    placed.set_actor_label(name)
    if group:
        placed.set_folder_path(group)
    return placed


def static_actor(mesh, name, x=0, y=0, z=0, yaw=0, scale=1, group="Terrain", collision=True):
    obj = actor(u.StaticMeshActor, name, x, y, z, yaw=yaw, group=group)
    comp = obj.get_editor_property("static_mesh_component")
    comp.set_static_mesh(mesh)
    comp.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS if collision else u.CollisionEnabled.NO_COLLISION)
    if isinstance(scale, (tuple, list)):
        obj.set_actor_scale3d(u.Vector(*scale))
    else:
        obj.set_actor_scale3d(u.Vector(scale, scale, scale))
    return obj, comp


def foliage(world, mesh, transforms, name, fade_start, fade_end, shadow=True):
    factory_type = getattr(u, "FoliageType_InstancedStaticMeshFactory", None)
    foliage_type = create_asset("FT_" + name, "Foliage", u.FoliageType_InstancedStaticMesh,
                                factory_type() if factory_type else None)
    if not foliage_type:
        raise RuntimeError("Could not create foliage type " + name)
    foliage_type.set_editor_properties(dict(mesh=mesh, cull_distance=u.Int32Interval(fade_start, fade_end),
                                           cast_shadow=shadow, cast_dynamic_shadow=shadow))
    set_value(foliage_type, "affect_distance_field_lighting", shadow)
    set_value(foliage_type, "enable_density_scaling", False)
    asset_lib.save_loaded_asset(foliage_type)
    u.InstancedFoliageActor.add_instances(world, foliage_type, transforms)
    note(f"{name}: {len(transforms)} foliage instances")


def transform(x, y, scale, yaw, z=None):
    if z is None:
        z = terrain(x, y) - .13
    return u.Transform(u.Vector(x*100, y*100, z*100), u.Rotator(0, 0, yaw), u.Vector(scale,scale,scale))


def add_vegetation(world, collection):
    batches = {name: [] for name in collection}
    for name, x, y, size, yaw in (
        ("shore_palm", -53, -43, .83, 45), ("shore_palm", -42, -35, 1.1, 144),
        ("coconut_palm", -31, -38, .89, 206), ("shore_palm", -19, -43, 1.06, 68),
        ("royal_palm", -52, -27, .94, 270), ("coconut_palm", -40, -22, .86, 35),
        ("coconut_palm", -15, -32, .73, 175), ("mangrove_tree", -4, -32, .94, 10),
        ("mangrove_tree", -57, -30, .88, 107), ("shore_palm", -30, -30, .85, 76),
        ("tree_fern", -32, -23, .49, 125), ("cycad", -36, -28, .66, 65),
        ("banana_plant", -28, -21, .86, 38), ("golden_cane_palm", -26, -26, .67, 90),
    ):
        batches[name].append(transform(x, y, size, yaw))

    # Keep the walking route and foreground beach open while building canopy depth inshore.
    for y_cell in range(-49, 63, 5):
        for x_cell in range(-76, 84, 5):
            x, y = x_cell+rng.uniform(-2,2), y_cell+rng.uniform(-2,2)
            z, shore = terrain(x,y), coast(x,y)
            path = abs(x-trail_x(y))
            if z < 1.55 or shore < 9 or normal(x,y)[2] < .71 or (path < 2.65 and y < 55):
                continue
            if shore < 24 and y < -13:
                choices = ("shore_palm", "coconut_palm", "royal_palm", "mangrove_tree")
                probability = .46
            else:
                choices = ("mangrove_tree", "mangrove_tree", "mangrove_tree", "royal_palm", "coconut_palm")
                probability = .73
            if rng.random() < probability:
                name = rng.choice(choices)
                size = rng.uniform(.62,1.35) * (1.25 if name == "mangrove_tree" and shore > 27 else 1)
                batches[name].append(transform(x, y, size, rng.uniform(0,360), z - .15))

    for _ in range(2050):
        x, y = rng.uniform(-79,82), rng.uniform(-47,69)
        z = terrain(x,y)
        if z < 1.35 or coast(x,y) < 9 or abs(x-trail_x(y)) < 1.75 or normal(x,y)[2] < .72:
            continue
        density = .49 + .28 * math.sin(x*.34+y*.17)*math.sin(y*.36-x*.23)
        if rng.random() > density:
            continue
        name = rng.choice(("tree_fern", "tree_fern", "banana_plant", "cycad", "cycad", "golden_cane_palm"))
        size = rng.uniform(.29,.78)
        batches[name].append(transform(x,y,size,rng.uniform(0,360),z-.12))

    canopy = {"shore_palm", "coconut_palm", "royal_palm", "mangrove_tree"}
    for name, instances in batches.items():
        for index, mesh in enumerate(collection[name]):
            is_canopy = name in canopy
            foliage(world, mesh, instances, f"{name}_{index}", 13500 if is_canopy else 4900,
                    17900 if is_canopy else 7100)


def scene_lighting():
    atmosphere = actor(u.SkyAtmosphere, "Tropical atmosphere", group="Lighting")
    sky = actor(u.SkyLight, "Captured skylight", group="Lighting")
    sun = actor(u.DirectionalLight, "Late afternoon sun", pitch=-42, yaw=-143, group="Lighting")
    sunlight = sun.get_component_by_class(u.DirectionalLightComponent)
    sunlight.set_mobility(u.ComponentMobility.MOVABLE)
    sunlight.set_intensity(70000.0)
    for name, value in dict(atmosphere_sun_light=True, light_source_angle=.535, use_temperature=True,
                            temperature=5670.0, cast_cloud_shadows=False).items():
        set_value(sunlight, name, value)
    sky_component = sky.get_component_by_class(u.SkyLightComponent)
    sky_component.set_mobility(u.ComponentMobility.MOVABLE)
    sky_component.set_intensity(1.0)
    set_value(sky_component, "real_time_capture", True)
    cloud = actor(u.VolumetricCloud, "Maritime cumulus", group="Lighting")
    cloud_comp = cloud.get_component_by_class(u.VolumetricCloudComponent)
    set_value(cloud_comp, "layer_bottom_altitude", 1.37)
    set_value(cloud_comp, "layer_height", 1.85)
    set_value(cloud_comp, "view_sample_count_scale", .62)
    cloud_comp.set_reflection_view_sample_count_scale(.38)
    fog = actor(u.ExponentialHeightFog, "Humid coastal air", z=-2, group="Lighting")
    fog_comp = fog.get_component_by_class(u.ExponentialHeightFogComponent)
    fog_comp.set_fog_density(.013)
    fog_comp.set_fog_height_falloff(.25)
    set_value(fog_comp, "enable_volumetric_fog", True)
    set_value(fog_comp, "volumetric_fog_distance", 7900.0)
    set_value(fog_comp, "volumetric_fog_scattering_distribution", .54)
    post = actor(u.PostProcessVolume, "Exposure and grading", group="Lighting")
    post.set_editor_property("unbound", True)
    pp = post.get_editor_property("settings")
    for name, value in dict(auto_exposure_min_brightness=12.1, auto_exposure_max_brightness=14.3,
                            auto_exposure_bias=.08, bloom_intensity=.24, motion_blur_amount=0.0,
                            vignette_intensity=.13, lens_flare_intensity=0.0,
                            ambient_occlusion_intensity=.68).items():
        if set_value(pp, name, value):
            set_value(pp, "override_" + name, True)
    post.set_editor_property("settings", pp)
    return atmosphere, sun, sky, cloud


def add_audio():
    for name, label, x, y, z, full, falloff, volume in (
        ("Surf", "Rolling surf", -27, -48, .5, 3350, 6200, .64),
        ("Jungle", "Tropical birds and insects", -5, 16, 5, 3500, 4400, .37),
    ):
        sounds = import_source(SOURCE / (name + ".wav"), "Audio", "SW_" + name)
        wave = next(item for item in sounds if isinstance(item,u.SoundWave))
        wave.set_editor_property("looping", True)
        set_value(wave, "virtualization_mode", u.VirtualizationMode.PLAY_WHEN_SILENT)
        asset_lib.save_loaded_asset(wave)
        a = actor(u.AmbientSound, label, x,y,z,group="Ambience")
        component = a.get_editor_property("audio_component")
        component.set_sound(wave)
        component.set_volume_multiplier(volume)
        component.set_editor_property("override_attenuation", True)
        attenuation = component.get_editor_property("attenuation_overrides")
        for key, value in dict(attenuate=True, spatialize=False, attenuation_shape_extents=u.Vector(full,0,0),
                               falloff_distance=falloff).items():
            set_value(attenuation, key, value)
        component.set_editor_property("attenuation_overrides", attenuation)


def setup_character():
    character = u.load_asset("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter")
    cls = u.BlueprintEditorLibrary.generated_class(character)
    defaults = u.get_default_object(cls)
    body = defaults.get_editor_property("mesh")
    manny = u.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
    assert manny, "Manny's skeletal mesh is missing"
    body.set_editor_property("skeletal_mesh_asset", manny)
    movement = defaults.get_editor_property("character_movement")
    for prop,value in dict(max_walk_speed=390.0,max_swim_speed=255.0,buoyancy=1.12,
                           braking_deceleration_swimming=165.0,max_out_of_water_step_height=68.0).items():
        set_value(movement, prop, value)
    navigation = movement.get_editor_property("nav_agent_props")
    if set_value(navigation, "can_swim", True):
        set_value(movement, "nav_agent_props", navigation)
    camera = defaults.get_component_by_class(u.SpringArmComponent)
    if camera:
        set_value(camera, "target_arm_length", 365.0)
        set_value(camera, "socket_offset", u.Vector(0,43,55))
        set_value(camera, "enable_camera_lag", True)
        set_value(camera, "camera_lag_speed", 13.0)
    asset_lib.save_loaded_asset(character, False)
    note(f"Player mesh: {body.get_editor_property('skeletal_mesh_asset')}, animation: {body.get_editor_property('anim_class')}")


def main():
    note("Starting asset imports")
    generated = "Geometry"
    land, *_ = import_mesh(SOURCE / "CoralReachTerrain.glb", generated + "/Land")
    water, *_ = import_mesh(SOURCE / "CoralReachOcean.glb", generated + "/Water")
    water_settings = water.get_editor_property("nanite_settings")
    water_settings.enabled = False
    water.set_editor_property("nanite_settings", water_settings)
    asset_lib.save_loaded_asset(water, False)
    mesh_land = land.get_editor_property("body_setup")
    mesh_land.set_editor_property("collision_trace_flag", u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    asset_lib.save_loaded_asset(land, False)
    wet_name = "damp_beach_sand_02" if len(list((SOURCE / "damp_beach_sand_02").glob("*.jpg"))) >= 3 else "coast_sand_03"
    surface_data = {name: import_surface(slug,name) for name,slug in
                    (("Sand","coast_sand_03"),("Wet",wet_name),("Forest","forest_leaves_02"),("Rock","coast_sand_rocks_02"))}
    material = create_asset("M_CoralReachTerrain", "Materials", u.Material, u.MaterialFactoryNew())
    terrain_material(material, surface_data)
    depth, *_ = import_source(SOURCE / "T_LagoonDepth.png", "Textures", "T_LagoonDepth")
    depth.set_editor_properties(dict(srgb=False, compression_settings=u.TextureCompressionSettings.TC_GRAYSCALE,
                                    mip_gen_settings=u.TextureMipGenSettings.TMGS_NO_MIPMAPS, never_stream=True))
    asset_lib.save_loaded_asset(depth, False)
    ocean_mat = create_asset("M_CoralReachOcean", "Materials", u.Material, u.MaterialFactoryNew())
    water_material(ocean_mat, depth)
    note("Importing coastal scans and vegetation")
    rock_meshes = {}
    for slug in ("boulder_01", "mountainside", "rock_moss_set_01", "coast_rocks_01"):
        source = next((SOURCE / slug).glob("*.gltf"))
        rock_meshes[slug] = import_mesh(source, f"Scans/{slug}")
        for mesh in rock_meshes[slug]:
            nanite = mesh.get_editor_property("nanite_settings")
            nanite.set_editor_property("enabled", True)
            mesh.set_editor_property("nanite_settings", nanite)
            asset_lib.save_loaded_asset(mesh, False)
    vegetation = {}
    for slug in ("shore_palm","coconut_palm","royal_palm","mangrove_tree","tree_fern","banana_plant","cycad","golden_cane_palm"):
        vegetation[slug] = import_mesh(SOURCE / (slug+".glb"), f"Plants/{slug}")

    folder("Maps")
    map_file = Path(u.Paths.project_content_dir()) / "Saltwind" / "Maps" / "CoralReach.umap"
    if not (levels.load_level(MAP) if map_file.is_file() else levels.new_level(MAP)):
        raise RuntimeError("Could not open the island level")
    for a in actors.get_all_level_actors():
        actors.destroy_actor(a)
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    _, ground = static_actor(land,"Coral Reach terrain")
    ground.set_material(0,material)
    surface, sea = static_actor(water,"Animated ocean surface", collision=False)
    surface.set_editor_property("tags",["SaltwindOceanSurface"])
    surface.set_actor_enable_collision(False)
    sea.set_collision_profile_name("NoCollision")
    sea.set_material(0,ocean_mat)
    sea.set_cast_shadow(False)
    set_value(sea, "affect_distance_field_lighting", False)
    note(f"Terrain bounds: {ground.get_local_bounds()}")
    scene_lighting()

    post_water = actor(u.PostProcessVolume,"Underwater light falloff",z=-13.85,group="Gameplay")
    post_water.set_actor_scale3d(u.Vector(199,153,13.8))
    set_value(post_water,"blend_radius",27.0)
    aqua = post_water.get_editor_property("settings")
    for name,value in dict(color_saturation=u.Vector4(.58,.87,.97,1),
                           color_gain=u.Vector4(.46,.82,.93,1),color_contrast=u.Vector4(.84,.91,.94,1)).items():
        if set_value(aqua,name,value):
            set_value(aqua,"override_"+name,True)
    post_water.set_editor_property("settings",aqua)
    # Broad blocking volumes keep swimmers inside the coast and two outer islets.
    for label,x,y,sx,sy in (("West", -197,0,.8,153),("East",197,0,.8,153),
                           ("North",0,151,198,.8),("South",0,-151,198,.8)):
        barrier = actor(u.BlockingVolume, label+" exploration boundary", x,y,7,group="Gameplay")
        barrier.set_actor_scale3d(u.Vector(sx,sy,31))

    setup_character()
    start_x,start_y = -44,-38
    start_z = terrain(start_x,start_y)
    actor(u.PlayerStart,"Beach trailhead", start_x,start_y,start_z+1.05,yaw=12,group="Gameplay")
    note(f"Player start: {start_x}, {start_y}, {start_z}")
    add_audio()

    shoreline = rock_meshes["coast_rocks_01"][0]
    cliff = rock_meshes["mountainside"][0]
    for x,y,scale,yaw in [(-69,-25,(.49,.68,.6),-18),(67,-26,(.32,.68,.59),58),
                          (128,98,(.64,.5,.48),-13),(-148,-112,(.36,.46,.4),39)]:
        static_actor(shoreline, "Sea-worn rock shelf",x,y,terrain(x,y)-.24,yaw,scale,"Coastal geology")
    for x,y,s,yaw in [(-57,25,.56,35),(-56,34,.48,-28),(37,50,.60,121),(64,6,.36,75),(126,94,.60,-7)]:
        static_actor(cliff,"Weathered escarpment",x,y,terrain(x,y)-.62,yaw,s,"Coastal geology")

    boulder = rock_meshes["boulder_01"][0]
    for x,y,scale,yaw in [(-39,-44,.31,20),(-37,-44,.15,91),(-28,-40,.34,36),(-59,-36,1.00,22),
                          (-57,-37,.58,60),(-54,-37,.40,67),(-34,-25,.76,220),(-36,-25,.36,53),
                          (-46,-29,.46,25),(-23,-33,.63,97),(56,-32,.81,11),(-2,-49,.52,120)]:
        placed, prop = static_actor(boulder,"Shore boulder",x,y,terrain(x,y)-.11,yaw,scale,"Coastal geology")
    rocks = rock_meshes["rock_moss_set_01"]
    scattered = [[] for _ in rocks]
    for _ in range(350):
        x,y = rng.uniform(-77,79),rng.uniform(-61,62)
        z = terrain(x,y)
        if z < -.35 or abs(x-trail_x(y)) < 1.3 or normal(x,y)[2] < .66:
            continue
        scattered[rng.randrange(len(rocks))].append(transform(x,y,rng.uniform(.19,.77),rng.uniform(0,360),z-.12))
    for i, mesh in enumerate(rocks):
        if scattered[i]:
            foliage(world,mesh,scattered[i],f"moss_stone_{i}",6800,9000)
    note("Saving the traversable coast before plant placement")
    assert levels.save_current_level()
    add_vegetation(world, vegetation)
    assert levels.save_all_dirty_levels()
    asset_lib.save_directory(ROOT, recursive=True)
    note("Coral Reach build complete")


if __name__ == "__main__":
    main()
