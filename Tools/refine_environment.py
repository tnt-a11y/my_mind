"""Apply the environment pass to the existing Coral Reach map."""

import json
import math
import random
import struct
import subprocess
from pathlib import Path
import sys
from urllib.request import urlretrieve

import unreal as u

sys.path.insert(0, str(Path(__file__).parent))
from build_island import (ROOT, actors, levels, note, create_asset, folder, import_mesh, import_source,
                          static_actor, actor, foliage, transform, set_value)
from island_geometry import terrain, trail_x, coast, normal
from island_materials import terrain_material, leaf_material, underwater_material, water_material


assets = Path(u.Paths.project_dir()).resolve().parent / ".saltwind-work"
sources = assets / "refinement"
original = assets / "assets"
library = u.EditorAssetLibrary
registry = u.AssetRegistryHelpers.get_asset_registry()
editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem)


def scanned_ground(slug, label):
    patterns = ("*_diff_2k.jpg", "*_nor_dx_2k.jpg", "*_rough_2k.jpg")
    root = sources / slug
    maps = []
    for suffix, pattern in zip(("D","N","R"),patterns):
        texture, *_ = import_source(next(root.glob(pattern)), "Textures/Scans", f"T_{label}_{suffix}")
        if suffix == "N":
            texture.set_editor_properties(dict(srgb=False,compression_settings=u.TextureCompressionSettings.TC_NORMALMAP))
        elif suffix == "R":
            texture.set_editor_property("srgb",False)
        library.save_loaded_asset(texture,False)
        maps.append(texture)
    return maps


def weighted_texture_color():
    sand = scanned_ground("coast_sand_05","FineCoastSand")
    dirt = scanned_ground("forrest_ground_01","JungleSoil")
    existing = {layer:[library.load_asset(f"{ROOT}/Textures/T_{layer}_{suffix}") for suffix in ("D","N","R")]
                for layer in ("Rock","Wet")}
    ground = library.load_asset(ROOT + "/Materials/M_CoralReachTerrain")
    terrain_material(ground, dict(Sand=sand, Forest=dirt, **existing))
    ocean = library.load_asset(ROOT + "/Materials/M_CoralReachOcean")
    water_material(ocean, library.load_asset(ROOT + "/Textures/T_LagoonDepth"))
    underwater = create_asset("M_Underwater","Materials/Underwater",u.Material,u.MaterialFactoryNew())
    underwater_material(underwater)


def foliage_slots(mesh):
    for index, slot in enumerate(mesh.get_editor_property("static_materials")):
        mat = slot.get_editor_property("material_interface")
        yield index, str(slot.get_editor_property("material_slot_name")), mat


def rgba_from_glb(file, name):
    data = file.read_bytes()
    descriptor_length = struct.unpack_from("<I",data,12)[0]
    doc = json.loads(data[20:20+descriptor_length])
    match = next(image for image in doc["images"] if image.get("name","").lower() == name.lower())
    chunk = doc["bufferViews"][match["bufferView"]]
    start = 28 + descriptor_length + chunk.get("byteOffset",0)
    temp = sources / "foliage" / f"{name}_palette.png"
    temp.parent.mkdir(exist_ok=True)
    temp.write_bytes(data[start:start+chunk["byteLength"]])
    output = temp.with_name(name + "_rgba.png")
    subprocess.run(["ffmpeg","-v","error","-y","-i",str(temp),"-pix_fmt","rgba",str(output)], check=True)
    return output


def renew_palms():
    mapping = {
        "banana_plant": {"Banana_Leaf":"bananaLeaf","Banana_Leaf_Dry":"bananaDry"},
        "coconut_palm": {"Coconut_Frond":"coconutLeaf"},
        "cycad": {"Cycad_Frond":"cycadLeaf"},
        "golden_cane_palm": {"Cane_Frond":"caneLeaf"},
        "mangrove_tree": {"Mangrove_Leaves":"mangroveLeaf"},
        "royal_palm": {"Royal_Frond":"royalLeaf"},
        "tree_fern": {"Tree_Fern_Frond":"fernLeaf","Tree_Fern_Dry":"fernDry"},
    }
    for species, slots in mapping.items():
        for slot, tex in slots.items():
            file = rgba_from_glb(original/(species+".glb"),tex)
            texture, *_ = import_source(file, "Textures/LeafAtlases", "T_"+tex)
            texture.set_editor_properties(dict(srgb=True, compression_no_alpha=False,
                                               compression_settings=u.TextureCompressionSettings.TC_DEFAULT))
            library.save_loaded_asset(texture,False)
            material = library.load_asset(f"{ROOT}/Materials/Plants/M_{slot}")
            amplitude = 4.6 if species in ("coconut_palm","royal_palm") else 2.3
            leaf_material(material,texture,dry="Dry" in slot,sway=amplitude)
    note("Cutout foliage now uses RGBA leaf atlases and wind")


def scanned_foliage(slug, name, alpha_pattern, diffuse_pattern, folder_name="Undergrowth", sway=2.0):
    root = (original if slug == "island_tree_01" else sources) / slug
    mesh_file = next(root.glob("*.gltf"))
    plant_meshes = import_mesh(mesh_file,f"Plants/{folder_name}/{slug}")
    mask_file = next(root.rglob(alpha_pattern))
    diff_file = next(root.rglob(diffuse_pattern))
    image, *_ = import_source(diff_file,"Textures/LeafAtlases","T_"+name+"_D")
    mask, *_ = import_source(mask_file,"Textures/LeafAtlases","T_"+name+"_Mask")
    mask.set_editor_properties(dict(srgb=False,compression_settings=u.TextureCompressionSettings.TC_GRAYSCALE))
    library.save_loaded_asset(mask,False)
    material = create_asset("M_"+name,f"Materials/Plants",u.Material,u.MaterialFactoryNew())
    leaf_material(material,image,alpha=mask,sway=sway)
    for mesh in plant_meshes:
        for index, label, old in foliage_slots(mesh):
            if slug != "island_tree_01" or "leaves" in str(old).lower():
                mesh.set_material(index,material)
        if slug == "island_tree_01":
            nanite = mesh.get_editor_property("nanite_settings")
            nanite.set_editor_property("enabled",True)
            mesh.set_editor_property("nanite_settings",nanite)
        library.save_loaded_asset(mesh,False)
    return plant_meshes


def leaf_litter(world):
    fern = scanned_foliage("fern_02","ForestFern","*alpha_1k.png","*diff_1k.jpg")
    sorrel = scanned_foliage("shrub_sorrel_01","CoastalSorrel","*alpha_1k.png","*diff_1k.jpg",sway=1.2)
    alpha = original / "island_tree_01" / "island_tree_01_leaves_alpha_1k.png"
    if not alpha.is_file():
        urlretrieve("https://dl.polyhaven.com/file/ph-assets/Models/png/1k/island_tree_01/island_tree_01_leaves_alpha_1k.png",alpha)
    tree = scanned_foliage("island_tree_01","BroadleafTree","*leaves_alpha*","*leaves_diff_1k.jpg","Canopy",.85)[0]
    for x,y,scale,yaw in [(-38,-25,.88,93),(-11,-23,.93,26),(-19,9,1.11,145),
                          (-7,17,.83,63),(24,30,.97,184),(49,-8,.80,147),(-61,9,.95,223)]:
        static_actor(tree,"Coastal broadleaf canopy",x,y,terrain(x,y)-.12,yaw,scale,"Refined foliage",False)
    rng = random.Random(22644)
    canopy_meshes = {**{f"ForestFern_{i}":mesh for i,mesh in enumerate(fern)},
                     **{f"CoastalSorrel_{i}":mesh for i,mesh in enumerate(sorrel[:5])}}
    groups = {name: [] for name in canopy_meshes}
    fern_names = [key for key in groups if key.startswith("Forest")]
    sorrel_names = [key for key in groups if key.startswith("Coastal")]
    for i in range(2100):
        x, y = rng.uniform(-76,80),rng.uniform(-43,68)
        z = terrain(x,y)
        edge = abs(x-trail_x(y))
        if z < 1.5 or coast(x,y) < 8 or edge < 1.08 or normal(x,y)[2] < .75:
            continue
        if rng.random() > (.56 if edge < 8 else .38):
            continue
        patch = math.sin(.19*x+.12*y)*math.cos(.16*y-.089*x)+.22
        is_fern = patch > .16 and z > 2.5
        name = rng.choice(fern_names if is_fern else sorrel_names)
        scale = rng.uniform(.66,1.16) if is_fern else rng.uniform(.88,2.20)
        groups[name].append(transform(x,y,scale,rng.uniform(0,360),z-.05))
    for name, transforms in groups.items():
        if transforms:
            foliage(world,canopy_meshes[name],transforms,"Refined_"+name, 3600,5200,True)


def import_audio():
    for wav in sorted((sources/"audio").glob("SW_*.wav")):
        sound, *_ = import_source(wav,"Audio",wav.stem)
        if wav.stem == "SW_Submerged":
            sound.set_editor_property("looping",True)
            sound.set_editor_property("virtualization_mode",u.VirtualizationMode.PLAY_WHEN_SILENT)
        library.save_loaded_asset(sound,False)


def assemble_level():
    assert levels.load_level(ROOT+"/Maps/CoralReach")
    world = editor_world.get_editor_world()
    for obj in actors.get_all_level_actors():
        label = obj.get_actor_label()
        if label in {"Ocean swimming volume","Underwater light falloff","Rolling surf","Moon","Underwater boulder"}:
            actors.destroy_actor(obj)
        elif label == "Late afternoon sun":
            obj.set_editor_property("tags",["SaltwindSun"])
            obj.set_actor_label("Sun")
        elif label == "Sun":
            obj.set_editor_property("tags",["SaltwindSun"])
        elif label == "Animated ocean surface":
            obj.set_editor_property("tags",["SaltwindOceanSurface"])
            obj.set_actor_enable_collision(False)
            obj.get_component_by_class(u.StaticMeshComponent).set_collision_profile_name("NoCollision")
        elif label == "Beach trailhead":
            x,y=-46,-42
            obj.set_actor_location_and_rotation(u.Vector(x*100,y*100,(terrain(x,y)+1.05)*100),u.Rotator(0,0,-25),False,True)
        elif label == "Captured skylight":
            skylight = obj.get_component_by_class(u.SkyLightComponent)
            set_value(skylight,"real_time_capture",True)
            set_value(skylight,"cubemap_resolution",128)
        elif label == "Humid coastal air":
            fog = obj.get_component_by_class(u.ExponentialHeightFogComponent)
            fog.set_fog_density(.009)
            fog.set_fog_height_falloff(.28)
            fog.set_volumetric_fog_distance(6600)
        elif label == "Exposure and grading":
            pp = obj.get_editor_property("settings")
            for key,value in dict(auto_exposure_min_brightness=-2.0, auto_exposure_max_brightness=16.0,
                                  auto_exposure_bias=-.10, bloom_intensity=.16, vignette_intensity=.065,
                                  color_saturation=u.Vector4(.98,1.012,.99,.99),
                                  color_contrast=u.Vector4(.985,.99,1.0,.98),
                                  auto_exposure_speed_up=2.8,auto_exposure_speed_down=1.7).items():
                if set_value(pp,key,value):
                    set_value(pp,"override_"+key,True)
            obj.set_editor_property("settings",pp)
        elif label == "Sea-worn rock shelf":
            position = obj.get_actor_location()
            if position.x < -3000 and position.y > -7000:
                obj.set_actor_scale3d(u.Vector(.23,.33,.42))
                obj.set_actor_location(u.Vector(-7320,-3050,-180),False,True)
        elif label == "Weathered escarpment":
            position = obj.get_actor_location()
            if -6000 < position.x < -5000 and "SaltwindReframed" not in obj.get_editor_property("tags"):
                obj.set_actor_location(position-u.Vector(650,0,220),False,True)
                obj.set_editor_property("tags", [*obj.get_editor_property("tags"),"SaltwindReframed"])
    moon = actor(u.DirectionalLight,"Moon",pitch=-37,yaw=30,group="Lighting")
    moon.set_editor_property("tags",["SaltwindMoon"])
    moonlight = moon.get_component_by_class(u.DirectionalLightComponent)
    moonlight.set_mobility(u.ComponentMobility.MOVABLE)
    moonlight.set_intensity(0.0)
    moonlight.set_light_color(u.LinearColor(.74,.83,1.0,1.0))
    set_value(moonlight,"atmosphere_sun_light",True)
    set_value(moonlight,"atmosphere_sun_light_index",1)
    set_value(moonlight,"light_source_angle",.52)
    boulder = next(item.get_asset() for item in registry.get_assets_by_path(ROOT+"/Scans/boulder_01",recursive=True)
                   if str(item.asset_class_path.asset_name) == "StaticMesh")
    for x,y,scale,yaw in [(-46,-70,.72,26),(-49,-71,.51,75),(-45,-76,1.45,176),(-38,-82,1.01,32),
                          (-16,-78,1.4,230),(-21,-75,.65,300),(28,-78,1.14,15),(34,-74,.89,35),
                          (54,-71,.61,43),(-78,-71,1.48,85),(-90,-55,1.33,65),(-54,-106,1.64,-91),
                          (29,-111,1.20,84),(26,-112,1.27,114)]:
        static_actor(boulder,"Underwater boulder",x,y,terrain(x,y)-.19,yaw,scale,"Coastal geology")
    assert levels.save_current_level()
    if not any(a.get_actor_label() == "Refined understorey" for a in actors.get_all_level_actors()):
        leaf_litter(world)
        actor(u.Actor,"Refined understorey",group="Refined foliage")
    assert levels.save_all_dirty_levels()
    note("Refined coast, understory, sound library and lighting saved")


registry.scan_paths_synchronous([ROOT],True)
weighted_texture_color()
renew_palms()
import_audio()
assemble_level()
