"""Install beach props and game collision for the placed Coral Reach foliage."""

import math
from pathlib import Path
import sys

import unreal as u

sys.path.insert(0, str(Path(__file__).parent))
from build_island import ROOT, MAP, actors, asset_lib, import_mesh, levels, registry, static_actor
from island_geometry import terrain
from island_materials import Graph
from refine_collision import conform_surface


COLLISION = u.CollisionEnabled.QUERY_AND_PHYSICS
SCANS = {"Log": "dead_tree_trunk", "Branch": "dead_quiver_branch_01", "Twigs": "dry_branches_medium_01",
         "Shell": "lambis_shell", "Litter": "plastic_bottle_gallon", "Rusted can": "can_rusted"}


def plant_body(mesh):
    name = mesh.get_name()
    if name in ("Trunk", "Stems", "Stem", "Fruit"):
        # These meshes model the stems and roots in volume.
        conform_surface(mesh)
        return
    bounds = mesh.get_bounding_box()
    center = (bounds.min + bounds.max) * .5
    size = bounds.max - bounds.min
    width = min(size.x, size.y)
    if name == "shore_palm":
        radius, length = 20, 315
        center = u.Vector(0, 0, 176)
    elif name.startswith("island_tree_"):
        radius, length = 28, 238
        center = u.Vector(0, 0, 143)
    elif "/Undergrowth/" in mesh.get_path_name():
        radius = min(width*.32, size.z*.38)
        length = max(0, size.z*.74-radius*2)
        center.z = bounds.min.z + size.z*.43
    else:
        # Foliage quads have masked holes and overhang. Give just the central crown a solid shape.
        radius = min(width*.18, size.z*.36)
        length = max(0, size.z*.53-radius*2)
        center.z = bounds.min.z + size.z*.46
    capsule = u.KSphylElem()
    capsule.set_editor_properties(dict(center=center, radius=radius, length=length))
    aggregate = u.KAggregateGeom()
    aggregate.set_editor_property("sphyl_elems", [capsule])
    body = mesh.get_editor_property("body_setup")
    body.set_editor_properties(dict(agg_geom=aggregate, collision_trace_flag=u.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX))
    mesh.set_editor_property("nanite_settings", mesh.get_editor_property("nanite_settings"))
    assert asset_lib.save_loaded_asset(mesh, False), name


def enable_collision():
    for record in registry.get_assets_by_path(ROOT + "/Plants", recursive=True):
        if str(record.asset_class_path.asset_name) == "StaticMesh":
            plant_body(record.get_asset())
    for record in registry.get_assets_by_path(ROOT + "/Foliage"):
        foliage = record.get_asset()
        if isinstance(foliage, u.FoliageType_InstancedStaticMesh):
            body = foliage.get_editor_property("body_instance")
            body.set_editor_property("collision_profile_name", "BlockAll")
            foliage.set_editor_property("body_instance", body)
            assert asset_lib.save_loaded_asset(foliage, False)
    total = 0
    for actor in actors.get_all_level_actors():
        if actor.actor_has_tag("SaltwindOceanSurface"):
            continue
        for component in actor.get_components_by_class(u.StaticMeshComponent):
            mesh = component.get_editor_property("static_mesh")
            if not mesh or not mesh.get_path_name().startswith(ROOT + "/"):
                continue
            if not isinstance(component, u.InstancedStaticMeshComponent) and "/Plants/" not in mesh.get_path_name():
                continue
            component.set_collision_profile_name("BlockAll")
            component.set_collision_enabled(COLLISION)
            total += component.get_instance_count() if isinstance(component, u.InstancedStaticMeshComponent) else 1
    assert total > 1000, total
    return total


def rusty_iron():
    name = "M_SaltCorrodedIron"
    path = ROOT + "/Materials/Weather/" + name
    found = asset_lib.load_asset(path) if asset_lib.does_asset_exist(path) else None
    if not found:
        found = u.AssetToolsHelpers.get_asset_tools().create_asset(name, ROOT + "/Materials/Weather",
                                                                  u.Material, u.MaterialFactoryNew())
    g = Graph(found)
    world = g.node(u.MaterialExpressionWorldPosition, -850, 0)
    scale = """
float3 p = World*.37;
float n = .53 + sin(p.x*1.42+p.y*.84+p.z*.5)*sin(p.y*1.3-p.z*.72)*.35
               + sin(p.x*2.71+p.y*3.14-p.z*2.3)*.10 + cos(p.z*2.71+p.x*5.7)*.045;
float rust = smoothstep(.20,.65,n);
"""
    color = g.custom("Pitted rust and exposed iron", scale + """
float3 iron = float3(.10,.087,.058);
float3 oxide = lerp(float3(.129,.040,.011),float3(.32,.115,.031),saturate(n));
return lerp(iron,oxide,rust);
""", 3, -500, -140, World=world)
    g.pin(color, "BASE_COLOR")
    g.pin(g.custom("Weathered surface roughness", scale + "return lerp(.49,.88,rust);", 1, -500, 120, World=world),
          "ROUGHNESS")
    g.pin(g.custom("Bare metal fraction", scale + "return lerp(.92,.06,rust);", 1, -500, 340, World=world),
          "METALLIC")
    g.finish()
    return found


def prop_meshes(directory):
    meshes = {}
    for kind, slug in SCANS.items():
        meshes[kind] = import_mesh(directory / slug / f"{slug}_1k.gltf", f"ShoreProps/{kind.replace(' ', '_')}")
    for kind, name in (("Cog", "ShoreCog"), ("Snail", "BeachSnail")):
        meshes[kind] = import_mesh(directory / (name + ".glb"), f"ShoreProps/{kind}")
    rust = rusty_iron()
    for kind, parts in meshes.items():
        for mesh in parts:
            settings = mesh.get_editor_property("nanite_settings")
            settings.set_editor_property("enabled", False)
            mesh.set_editor_property("nanite_settings", settings)
            if kind == "Cog":
                mesh.set_material(0, rust)
            conform_surface(mesh)
    moss_path = ROOT + "/Scans/rock_moss_set_01/rock_moss_set_01_2k/StaticMeshes/rock_moss_set_01_rock04"
    meshes["Stone"] = [asset_lib.load_asset(moss_path)]
    return meshes


def place_props(meshes):
    placements = {
        "Landing Beach": [
            ("Log", -49.9, -43.2, 126, 1.12), ("Branch", -49.1, -45.0, 92, .67),
            ("Twigs", -50.4, -42.2, 84, .70), ("Shell", -47.0, -45.65, 23, 1.27),
            ("Shell", -44.85, -44.3, 71, .83), ("Cog", -44.9, -45.8, 33, .86),
            ("Litter", -44.05, -45.96, 135, .83), ("Snail", -44.84, -45.05, 71, 1.44),
            ("Snail", -47.9, -44.58, 112, 1.13), ("Stone", -47.20, -44.80, 17, .26),
            ("Stone", -47.45, -44.59, 78, .135), ("Stone", -44.1, -45.17, 15, .24),
            ("Log", -41.5, -45.7, 11, .81), ("Branch", -39.8, -46.1, 128, .49),
            ("Twigs", -40.9, -46.5, 153, .50), ("Litter", -40.6, -45.7, 81, .70),
            ("Rusted can", -45.19, -45.42, 50, .85),
        ],
        "South Cove": [
            ("Log", 20.0, -47.1, 12, .94), ("Branch", 18.9, -47.9, 24, .86),
            ("Shell", 20.48, -46.45, 188, .89), ("Snail", 20.71, -46.39, 200, 1.1),
            ("Stone", 19.4, -47.7, 34, .24), ("Cog", 19.8, -47.8, 69, .70),
            ("Twigs", 21.3, -46.2, 46, .68), ("Litter", 21.2, -46.65, 155, .86),
            ("Branch", 36.6, -43.5, 73, .78), ("Shell", 38.2, -41, 11, 1.10),
            ("Rusted can", 21.5, -47.14, 160, .73),
        ],
        "Northern Tide Line": [
            ("Log", -25.8, 60.0, -16, .93), ("Branch", -24.1, 61, 35, .62),
            ("Stone", -26.3, 60.8, 129, .23), ("Litter", -25.2, 60.7, 13, .79),
            ("Snail", -25, 60.5, 24, .99), ("Shell", -24.9, 60.5, 24, .87),
            ("Rusted can", -25.56, 60.57, 23, .60),
        ],
        "East Point": [
            ("Twigs", 74.5, 39.5, 82, .54), ("Branch", 74.7, 40.3, 130, .51),
            ("Cog", 74.21, 40.7, 169, 1.0), ("Stone", 74.2, 41.2, 87, .33),
            ("Shell", 74.9, 40.1, 217, 1.15), ("Snail", 73.9, 40.7, 4, 1.0),
        ],
    }
    existing = {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()
                if actor.actor_has_tag("SaltwindShoreProp")}
    count = 0
    for beach, props in placements.items():
        for index, (kind, x, y, yaw, scale) in enumerate(props, 1):
            mesh = meshes[kind][index % len(meshes[kind])]
            name = f"{beach} - {kind} {index}"
            bounds = mesh.get_bounding_box()
            center = (bounds.min + bounds.max)*.5
            radians = math.radians(yaw)
            offset_x = (center.x*math.cos(radians)-center.y*math.sin(radians))*scale*.01
            offset_y = (center.x*math.sin(radians)+center.y*math.cos(radians))*scale*.01
            bury = {"Log": .07, "Litter": .012, "Stone": .055, "Cog": .006}.get(kind, .008)
            z = terrain(x,y) - bounds.min.z*scale*.01 - bury
            origin_x, origin_y = x-offset_x, y-offset_y
            if name in existing:
                placed = existing.pop(name)
                placed.set_actor_location(u.Vector(origin_x*100,origin_y*100,z*100), False, False)
                placed.set_actor_rotation(u.Rotator(0,0,yaw), False)
                placed.set_actor_scale3d(u.Vector(scale,scale,scale))
                component = placed.get_editor_property("static_mesh_component")
                component.set_static_mesh(mesh)
            else:
                placed, component = static_actor(mesh, name, origin_x, origin_y, z, yaw, scale,
                                                 group="Shore Props/" + beach)
            placed.set_editor_property("tags", ["SaltwindShoreProp", kind])
            component.set_collision_profile_name("BlockAll")
            component.set_collision_enabled(COLLISION)
            count += 1
    for obsolete in existing.values():
        assert actors.destroy_actor(obsolete)
    return count


def run(directory):
    registry.search_all_assets(True)
    assert levels.load_level(MAP)
    vegetation = enable_collision()
    props = place_props(prop_meshes(directory))
    assert levels.save_current_level()
    u.log(f"SALTWIND_SHORE_READY ({vegetation} vegetation and rock instances, {props} shore prop actors)")


if __name__ == "__main__":
    run(Path(sys.argv[1]))
