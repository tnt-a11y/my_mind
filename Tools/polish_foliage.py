"""Install explicit leaf cutouts, tune the sun and reframe the beach spawn."""

from pathlib import Path
import sys

import unreal as u

sys.path.insert(0, str(Path(__file__).parent))
from island_materials import leaf_material
from island_geometry import terrain

ROOT = "/Game/Saltwind"
registry = u.AssetRegistryHelpers.get_asset_registry()
library = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)


def replace_leaf_materials():
    patterns = {
        "banana_plant": {"Banana_Leaf": "bananaLeaf", "Banana_Leaf_Dry": "bananaDry"},
        "coconut_palm": {"Coconut_Frond": "coconutLeaf"},
        "cycad": {"Cycad_Frond": "cycadLeaf"},
        "golden_cane_palm": {"Cane_Frond": "caneLeaf"},
        "mangrove_tree": {"Mangrove_Leaves": "mangroveLeaf"},
        "royal_palm": {"Royal_Frond": "royalLeaf"},
        "tree_fern": {"Tree_Fern_Frond": "fernLeaf", "Tree_Fern_Dry": "fernDry"},
    }
    target = ROOT + "/Materials/Plants"
    library.make_directory(target)
    for plant, mapping in patterns.items():
        source = ROOT + "/Plants/" + plant
        records = registry.get_assets_by_path(source, recursive=True)
        images = {str(item.asset_name):item.get_asset() for item in records
                  if str(item.asset_class_path.asset_name) == "Texture2D"}
        replacements = {}
        for slot, file in mapping.items():
            texture = images[file]
            texture.set_editor_properties(dict(srgb=True, compression_no_alpha=False,
                                               compression_settings=u.TextureCompressionSettings.TC_DEFAULT))
            library.save_loaded_asset(texture, False)
            name = "M_" + slot
            path = f"{target}/{name}"
            material = (library.load_asset(path) if library.does_asset_exist(path)
                        else assets.create_asset(name,target,u.Material,u.MaterialFactoryNew()))
            leaf_material(material, texture, dry="Dry" in slot)
            replacements[slot.lower()] = material
            replacements[name.lower()] = material
        attached = 0
        for item in records:
            if str(item.asset_class_path.asset_name) != "StaticMesh":
                continue
            mesh = item.get_asset()
            for index, slot in enumerate(mesh.get_editor_property("static_materials")):
                original = slot.get_editor_property("material_interface")
                label = str(slot.get_editor_property("material_slot_name")).lower()
                if original:
                    label = original.get_name().lower() if label not in replacements else label
                if label in replacements:
                    mesh.set_material(index, replacements[label])
                    attached += 1
            library.save_loaded_asset(mesh, False)
        if attached != len(mapping):
            raise RuntimeError(f"{plant}: expected {len(mapping)} cutout slots, assigned {attached}")
        u.log(f"SALTWIND | {plant}: {attached} custom foliage slots")


registry.scan_paths_synchronous([ROOT], True)
assert levels.load_level(ROOT + "/Maps/CoralReach")
replace_leaf_materials()
for obj in actors.get_all_level_actors():
    label = obj.get_actor_label()
    if label == "Captured skylight":
        comp = obj.get_component_by_class(u.SkyLightComponent)
        comp.set_intensity(1.55)
    elif label == "Late afternoon sun":
        comp = obj.get_component_by_class(u.DirectionalLightComponent)
        comp.set_intensity(56000.0)
    elif label == "Beach trailhead":
        x, y = -45.0, -41.0
        obj.set_actor_location_and_rotation(u.Vector(x*100,y*100,(terrain(x,y)+1.09)*100),
                                            u.Rotator(0,0,-19),False,True)
assert levels.save_current_level()
u.log("SALTWIND | foliage and coastline framing saved")
