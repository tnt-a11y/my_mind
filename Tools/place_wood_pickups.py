"""Install six collectible wood types and place them around Coral Reach."""

from pathlib import Path
import random
import sys

import unreal as u

sys.path.insert(0, str(Path(__file__).parent))
from build_island import MAP, ROOT, actors, asset_lib, create_asset, import_mesh, import_source, levels, registry
from island_geometry import normal, terrain, trail_x
from refine_collision import conform_surface


WOOD = (
    ("Driftwood", "Driftwood", "dead_tree_trunk", 72, "Log/dead_tree_trunk_1k/StaticMeshes/dead_tree_trunk_1k"),
    ("LichenLog", "Lichen Log", "dead_tree_trunk_02", 62, None),
    ("PaleBranch", "Pale Branch", "dead_quiver_branch_01", 59,
     "Branch/dead_quiver_branch_01_1k/StaticMeshes/dead_quiver_branch_01_1k"),
    ("ForkedBranch", "Forked Branch", "dead_quiver_branch_02", 70, None),
    ("Kindling", "Dry Kindling", "dry_branches_medium_01", 43,
     "Twigs/dry_branches_medium_01_1k/StaticMeshes/dry_branches_medium_01_a"),
    ("QuiverBark", "Quiver Bark", "dead_quiver_trunk", 53, None),
)


def definitions(sources, thumbnails):
    inventory = []
    for key, label, slug, length, shore_path in WOOD:
        if shore_path:
            model = asset_lib.load_asset(f"{ROOT}/ShoreProps/{shore_path}")
        else:
            model = import_mesh(sources / slug / (slug + "_1k.gltf"), f"Items/Wood/Meshes/{key}")[0]
            nanite = model.get_editor_property("nanite_settings")
            nanite.set_editor_property("enabled", False)
            model.set_editor_property("nanite_settings", nanite)
            conform_surface(model)
        assert model, label
        name = "T_Wood_" + key
        icon_path = ROOT + "/Items/Wood/Thumbnails"
        if asset_lib.does_asset_exist(icon_path + "/" + name):
            task = u.AssetImportTask()
            task.set_editor_properties(dict(filename=str(thumbnails / (name + ".png")), destination_path=icon_path,
                                            replace_existing=True, automated=True, save=True))
            u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            imported = task.get_objects() or [asset_lib.load_asset(icon_path + "/" + name)]
        else:
            imported = import_source(thumbnails / (name + ".png"), "Items/Wood/Thumbnails", name)
        icon = next(asset for asset in imported if isinstance(asset, u.Texture2D))
        icon.set_editor_properties(dict(compression_settings=u.TextureCompressionSettings.TC_EDITOR_ICON,
                                        mip_gen_settings=u.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                                        lod_group=u.TextureGroup.TEXTUREGROUP_UI))
        assert asset_lib.save_loaded_asset(icon, False)
        factory = u.DataAssetFactory()
        factory.set_editor_property("data_asset_class", u.SaltwindItemDefinition)
        item = create_asset("DA_Wood_" + key, "Items/Wood", u.SaltwindItemDefinition, factory)
        bounds = model.get_bounding_box()
        size = bounds.max - bounds.min
        rest = u.Rotator(0, 90 if size.z > max(size.x, size.y)*1.08 else 0, 0)
        item.set_editor_properties(dict(display_name=u.Text(label), mesh=model, thumbnail=icon,
                                        length=length, rest_rotation=rest, stack_limit=16, kind=u.SaltwindItemKind.WOOD))
        assert asset_lib.save_loaded_asset(item, False)
        inventory.append(item)
    return inventory


def place(inventory):
    rng = random.Random(84973)
    beaches = {
        "Landing Beach": [(-44.3,-42.05), (-43,-42.1), (-48.2,-41.2), (-43.2,-39.8), (-48.8,-39),
                          (-42,-38.3), (-41.4,-43.1), (-38.5,-41), (-37,-45), (-34,-43)],
        "West Shore": [(-61.6,-21.2), (-64,-17.5), (-65,-14.7), (-65.6,-11.8), (-66,-9.2), (-65.8,-6.7)],
        "South Cove": [(19,-44), (20.3,-43), (22.7,-42.5), (25,-42.1), (27.8,-41.9), (30,-40.2)],
        "Northern Tide Line": [(-27,57), (-25.5,57), (-23.7,58), (-21.5,57), (-19.7,57), (-17.3,56)],
        "East Point": [(72,37), (71.6,39), (70.9,40.8), (69.4,42), (68.7,44.1), (68,46)],
        "Forest Trail": [(trail_x(y) + (1.25 if index % 2 else -1.30), y)
                         for index, y in enumerate((-32, -27, -21, -14, -9, -3, 4, 11))],
    }
    existing = {placed.get_actor_label(): placed for placed in actors.get_all_level_actors()
                if isinstance(placed, u.SaltwindPickup) and placed.actor_has_tag("SaltwindPlacedWood")}
    count = 0
    for area, points in beaches.items():
        for index, (x, y) in enumerate(points):
            variant = index % len(inventory) if area != "Landing Beach" else (0,0,2,3,4,5,1,2,4,1)[index]
            item = inventory[variant]
            label = f"{area} - {item.get_editor_property('display_name')} {index + 1:02d}"
            height = terrain(x, y)
            assert height > 0.15
            location = u.Vector(100*x, 100*y, 100*height - 1.2)
            yaw = rng.uniform(-180, 180)
            rotation = u.MathLibrary.make_rot_from_zx(u.Vector(*normal(x,y)),
                         u.MathLibrary.get_forward_vector(u.Rotator(0,0,yaw)))
            pickup = existing.pop(label, None)
            if pickup:
                pickup.set_actor_location(location, False, False)
                pickup.set_actor_rotation(rotation, False)
                pickup.set_editor_property("item", None)
            else:
                pickup = actors.spawn_actor_from_class(u.SaltwindPickup, location, rotation)
                assert pickup, label
            pickup.set_editor_properties(dict(item=item, quantity=1, tags=["SaltwindPlacedWood"]))
            pickup.set_actor_label(label)
            pickup.set_folder_path("Gatherable Wood/" + area)
            mesh = pickup.get_editor_property("pickup_mesh")
            assert mesh.get_editor_property("static_mesh") == item.get_editor_property("mesh"), label
            assert abs(mesh.get_editor_property("relative_rotation").pitch - item.get_editor_property("rest_rotation").pitch) < 1, label
            count += 1
    for obsolete in existing.values():
        assert actors.destroy_actor(obsolete)
    return count


def run(sources, thumbnails):
    registry.search_all_assets(True)
    inventory = definitions(sources, thumbnails)
    assert levels.load_level(MAP)
    count = place(inventory)
    assert levels.save_current_level()
    u.log(f"SALTWIND_COLLECTION_READY ({len(inventory)} types, {count} placed wood pickups)")


if __name__ == '__main__':
    run(Path(sys.argv[1]), Path(sys.argv[2]))
