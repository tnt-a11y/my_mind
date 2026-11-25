"""Install the scanned seabed, physical sand, and footprint material in Coral Reach."""

from pathlib import Path
import sys

import unreal as u

from island_geometry import depth_texture, grid, normal, terrain
from island_materials import footprint_material, terrain_material
from refine_collision import conform_surface


ROOT = "/Game/Saltwind"
LIBRARY = u.EditorAssetLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()


def create(name, folder, kind, factory):
    path = f"{folder}/{name}"
    if LIBRARY.does_asset_exist(path):
        return LIBRARY.load_asset(path)
    return TOOLS.create_asset(name, folder, kind, factory)


def import_sand(directory):
    textures = []
    for suffix, file_kind in (("D", "diff"), ("N", "nor_dx"), ("R", "rough")):
        name = "T_SeabedSand_" + suffix
        path = ROOT + "/Textures/Scans/" + name
        if not LIBRARY.does_asset_exist(path):
            task = u.AssetImportTask()
            task.set_editor_properties(dict(
                filename=str(directory / f"sand_03_{file_kind}_2k.jpg"),
                destination_path=ROOT + "/Textures/Scans",
                destination_name=name,
                automated=True, save=False,
            ))
            TOOLS.import_asset_tasks([task])
        texture = LIBRARY.load_asset(path)
        assert texture, suffix
        if suffix == "N":
            texture.set_editor_properties(dict(srgb=False, compression_settings=u.TextureCompressionSettings.TC_NORMALMAP))
        elif suffix == "R":
            texture.set_editor_property("srgb", False)
        assert LIBRARY.save_loaded_asset(texture, False)
        textures.append(texture)
    return textures


def rebuild_islet_shelves(directory):
    mesh_root = ROOT + "/Geometry/Land/CoralReachTerrain/StaticMeshes"
    mesh = LIBRARY.load_asset(mesh_root + "/CoralReachTerrain")
    depth = LIBRARY.load_asset(ROOT + "/Textures/T_LagoonDepth")
    terrain_file = directory / "CoralReachTerrain.glb"
    depth_file = directory / "T_LagoonDepth.png"
    grid(terrain_file, range(-208, 217), range(-164, 145), terrain, normal)
    depth_texture(depth_file)

    manager = u.InterchangeManager.get_interchange_manager_scripted()
    for asset, source_file, destination in ((mesh, terrain_file, mesh_root),
                                             (depth, depth_file, ROOT + "/Textures")):
        source = u.InterchangeManager.create_source_data(str(source_file))
        options = u.ImportAssetParameters()
        options.set_editor_properties(dict(reimport_asset=asset, is_automated=True, replace_existing=True))
        imported = manager.import_asset(destination, source, options)
        assert imported and any(obj == asset for obj in imported), (asset.get_name(), imported)

    conform_surface(mesh)
    depth.set_editor_properties(dict(srgb=False, compression_settings=u.TextureCompressionSettings.TC_GRAYSCALE,
                                    mip_gen_settings=u.TextureMipGenSettings.TMGS_NO_MIPMAPS, never_stream=True))
    assert LIBRARY.save_loaded_asset(depth, False)


def run(directory):
    rebuild_islet_shelves(directory)
    sources = {"Sand": "Scans/T_FineCoastSand", "Forest": "Scans/T_JungleSoil", "Rock": "T_Rock", "Wet": "T_Wet"}
    layers = {name: [LIBRARY.load_asset(f"{ROOT}/Textures/{prefix}_{channel}") for channel in ("D", "N", "R")]
              for name, prefix in sources.items()}
    assert all(all(layers[name]) for name in layers)
    layers["Shelf"] = import_sand(directory)
    terrain = LIBRARY.load_asset(ROOT + "/Materials/M_CoralReachTerrain")
    sand = create("PM_CoastalSand", ROOT + "/Materials", u.PhysicalMaterial, u.PhysicalMaterialFactoryNew())
    sand.set_editor_properties(dict(friction=.66, static_friction=.82, restitution=.08, density=1.56))
    assert LIBRARY.save_loaded_asset(sand, False)
    terrain.set_editor_property("phys_material", sand)
    terrain_material(terrain, layers)
    footprint = create("M_SandFootprint", ROOT + "/Materials", u.Material, u.MaterialFactoryNew())
    footprint_material(footprint)

    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert levels.load_level(ROOT + "/Maps/CoralReach")
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    repaired = set()
    for actor in actors.get_all_level_actors():
        if not isinstance(actor, u.StaticMeshActor):
            continue
        component = actor.get_component_by_class(u.StaticMeshComponent)
        mesh = component.get_editor_property("static_mesh")
        if not mesh:
            continue
        mesh_name = mesh.get_name()
        if mesh_name == "CoralReachTerrain":
            tags = list(component.get_editor_property("component_tags"))
            if not any(str(tag) == "SaltwindSandTerrain" for tag in tags):
                component.set_editor_property("component_tags", tags + ["SaltwindSandTerrain"])
            component.set_material(0, terrain)
            mesh.set_material(0, terrain)
            assert LIBRARY.save_loaded_asset(mesh, False)
            repaired.add(mesh_name)
        elif mesh_name == "CoralReachOcean":
            ocean = LIBRARY.load_asset(ROOT + "/Materials/M_CoralReachOcean")
            component.set_material(0, ocean)
            mesh.set_material(0, ocean)
            assert LIBRARY.save_loaded_asset(mesh, False)
            repaired.add(mesh_name)
    assert repaired == {"CoralReachTerrain", "CoralReachOcean"}, repaired
    assert levels.save_current_level()
    u.log("SALTWIND_SHORE_REFINEMENT_DONE")


if __name__ == "__main__":
    run(Path(sys.argv[1]))
