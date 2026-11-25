"""Install collectible stones, the campfire item, and its Coral Reach recipe."""

from pathlib import Path
from random import Random
import sys

import unreal as u

sys.path.insert(0, str(Path(__file__).parent))
from author_campfire_materials import run as build_fire_materials
from build_island import MAP, ROOT, actors, asset_lib, create_asset, import_mesh, import_source, levels, registry
from island_geometry import normal, terrain, trail_x
from refine_collision import conform_surface


ROCK_PATH = "rock_moss_set_01/rock_moss_set_01_2k/StaticMeshes/rock_moss_set_01_rock"
STONES = (
    ("MossStone", "Moss Stone", ROCK_PATH + "01", 25),
    ("SplitRock", "Split Rock", ROCK_PATH + "03", 29),
    ("CoastalStone", "Coastal Stone", ROCK_PATH + "06", 22),
    ("GraniteRock", "Granite Rock", "boulder_01/boulder_01_2k/StaticMeshes/boulder_01_2k", 27),
)


def data_asset(name, folder, asset_class):
    factory = u.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    return create_asset(name, folder, asset_class, factory)


def item(art, folder, key, label, mesh, length, kind, stack=16):
    name = "T_" + key
    texture = next(asset for asset in import_source(art / (name + ".png"), folder + "/Thumbnails", name)
                   if isinstance(asset, u.Texture2D))
    texture.set_editor_properties(dict(compression_settings=u.TextureCompressionSettings.TC_EDITOR_ICON,
                                        mip_gen_settings=u.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                                        lod_group=u.TextureGroup.TEXTUREGROUP_UI))
    assert asset_lib.save_loaded_asset(texture, False)
    result = data_asset("DA_" + key, folder, u.SaltwindItemDefinition)
    result.set_editor_properties(dict(display_name=u.Text(label), mesh=mesh, length=length,
                                      thumbnail=texture, kind=kind, stack_limit=stack))
    assert asset_lib.save_loaded_asset(result, False)
    return result


def install_items(art):
    wood_count = 0
    for record in registry.get_assets_by_path(ROOT + "/Items/Wood"):
        existing = record.get_asset()
        if isinstance(existing, u.SaltwindItemDefinition):
            existing.set_editor_property("kind", u.SaltwindItemKind.WOOD)
            assert asset_lib.save_loaded_asset(existing, False)
            wood_count += 1
    assert wood_count == 6
    stones = []
    for key, label, path, length in STONES:
        mesh = asset_lib.load_asset(f"{ROOT}/Scans/{path}")
        assert mesh, label
        stones.append(item(art, "Items/Stone", key, label, mesh, length, u.SaltwindItemKind.STONE))

    mesh = import_mesh(art / 'CampfireBed.glb', "Items/Campfire/Meshes")[0]
    settings = mesh.get_editor_property("nanite_settings")
    settings.set_editor_property("enabled", False)
    mesh.set_editor_property("nanite_settings", settings)
    conform_surface(mesh)
    campfire = item(art, "Items/Campfire", "Campfire", "Campfire", mesh, 109, u.SaltwindItemKind.CAMPFIRE, 1)
    recipe = data_asset("DA_CampfireRecipe", "Items/Recipes", u.SaltwindCraftRecipe)
    recipe.set_editor_properties(dict(product=campfire, ingredients=[u.SaltwindItemKind.WOOD]*2 + [u.SaltwindItemKind.STONE]*2))
    assert asset_lib.save_loaded_asset(recipe, False)
    sound = next(asset for asset in import_source(art / "SW_Campfire.wav", "Audio", "SW_Campfire")
                 if isinstance(asset, u.SoundWave))
    sound.set_editor_property("looping", True)
    assert asset_lib.save_loaded_asset(sound, False)
    build_fire_materials()
    return stones


def place_stones(stones):
    rng = Random(23514)
    sites = {
        "Landing Beach": [(-43.7,-40.65), (-43.3,-43.6), (-48.7,-41.9), (-48.4,-37.9), (-41.2,-40.2),
                          (-39.5,-40.2), (-36.3,-43.5), (-39.8,-42.6)],
        "West Shore": [(-61.2,-21.9), (-63.9,-18.9), (-65.5,-13.3), (-65.6,-10.4), (-66.7,-8), (-65.3,-5.8)],
        "South Cove": [(18.8,-43), (20.9,-43.9), (22.3,-43.44), (24.6,-41.5), (27.2,-42.5), (29.8,-39.3)],
        "Northern Tide Line": [(-27.8,56.4), (-25,58), (-23,57.0), (-24.4,55.9), (-20.5,55.9), (-18.1,55.5)],
        "East Point": [(71.5,36.6), (70.4,38.1), (70.5,40), (68.8,41.1), (67.5,42.4), (67.3,44.8)],
        "Forest Trail": [(trail_x(y) + (-1.7 if i % 2 else 1.5), y) for i,y in enumerate((-35,-23,-16,-7,3,13))],
    }
    existing = {placed.get_actor_label(): placed for placed in actors.get_all_level_actors()
                if isinstance(placed, u.SaltwindPickup) and placed.actor_has_tag("SaltwindPlacedStone")}
    count = 0
    for area, points in sites.items():
        for index, (x,y) in enumerate(points):
            definition = stones[index % len(stones)]
            label = f"{area} - {definition.get_editor_property('display_name')} {index+1:02d}"
            location = u.Vector(x*100,y*100,terrain(x,y)*100 - 1.4)
            forward = u.MathLibrary.get_forward_vector(u.Rotator(0,0,rng.uniform(-180,180)))
            rotation = u.MathLibrary.make_rot_from_zx(u.Vector(*normal(x,y)), forward)
            pickup = existing.pop(label, None)
            if pickup:
                pickup.set_actor_location(location, False, False)
                pickup.set_actor_rotation(rotation, False)
            else:
                pickup = actors.spawn_actor_from_class(u.SaltwindPickup, location, rotation)
                assert pickup, label
            pickup.set_editor_properties(dict(item=definition, quantity=1, tags=["SaltwindPlacedStone"]))
            pickup.set_actor_label(label)
            pickup.set_folder_path("Gatherable Stone/" + area)
            assert pickup.get_editor_property("pickup_mesh").get_editor_property("static_mesh"), label
            count += 1
    for obsolete in existing.values():
        assert actors.destroy_actor(obsolete)
    return count


def run(art):
    registry.search_all_assets(True)
    stones = install_items(art)
    assert levels.load_level(MAP)
    count = place_stones(stones)
    assert levels.save_current_level()
    u.log(f"SALTWIND_CRAFTING_READY ({len(stones)} stone types, {count} placed stone pickups)")
