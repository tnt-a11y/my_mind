"""Match Coral Reach ground and rock collision to the rendered surfaces."""

import unreal as u


ROOT = "/Game/Saltwind"
LIBRARY = u.EditorAssetLibrary
ROCK_PREFIX = ROOT + "/Scans/"


def conform_surface(mesh):
    settings = mesh.get_editor_property("nanite_settings")
    settings.set_editor_properties(dict(
        fallback_target=u.NaniteFallbackTarget.PERCENT_TRIANGLES,
        fallback_percent_triangles=1.0,
        fallback_relative_error=0.0,
    ))
    body = mesh.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag", u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    # Changing mesh build settings also rebuilds its cooked triangle collision.
    mesh.set_editor_property("nanite_settings", settings)
    assert LIBRARY.save_loaded_asset(mesh, False), mesh.get_path_name()


def run():
    registry = u.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    for record in registry.get_assets_by_path(ROOT + "/Scans", recursive=True):
        if str(record.asset_class_path.asset_name) == "StaticMesh":
            conform_surface(record.get_asset())
    terrain = LIBRARY.load_asset(ROOT + "/Geometry/Land/CoralReachTerrain/StaticMeshes/CoralReachTerrain")
    conform_surface(terrain)

    for record in registry.get_assets_by_path(ROOT + "/Foliage"):
        if str(record.asset_name).startswith("FT_moss_stone_"):
            foliage = record.get_asset()
            body = foliage.get_editor_property("body_instance")
            body.set_editor_property("collision_profile_name", "BlockAll")
            foliage.set_editor_property("body_instance", body)
            assert LIBRARY.save_loaded_asset(foliage, False)

    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert levels.load_level(ROOT + "/Maps/CoralReach")
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    instanced_count = 0
    for actor in actors.get_all_level_actors():
        for component in actor.get_components_by_class(u.InstancedStaticMeshComponent):
            mesh = component.get_editor_property("static_mesh")
            if mesh and mesh.get_path_name().startswith(ROCK_PREFIX + "rock_moss_set_01/"):
                component.set_collision_profile_name("BlockAll")
                component.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)
                instanced_count += component.get_instance_count()
    assert instanced_count
    assert levels.save_current_level()
    u.log(f"SALTWIND_COLLISION_READY ({instanced_count} instanced stones)")


if __name__ == "__main__":
    run()
