"""Import and retarget the CC0 swimming clips onto the playable skeleton."""

import json
from pathlib import Path
import struct
import sys

import unreal as u

sys.path.insert(0,str(Path(__file__).parent))
from build_island import ROOT, create_asset, folder, import_source, note


library = u.EditorAssetLibrary
registry = u.AssetRegistryHelpers.get_asset_registry()
source_assets = ROOT+"/SwimmingSource"
animations = {"Swim_Fwd_Loop":"AS_SwimStroke", "Swim_Idle_Loop":"AS_TreadWater"}


def swim_only():
    assets = Path(u.Paths.project_dir()).resolve().parent / ".saltwind-work/refinement"
    original = (assets/"UAL1.glb").read_bytes()
    length = struct.unpack_from("<I",original,12)[0]
    data = json.loads(original[20:20+length])
    data["animations"] = [anim for anim in data["animations"] if anim["name"] in animations]
    assert len(data["animations"]) == 2
    desc = json.dumps(data,separators=(",",":")).encode()
    desc += b" " * ((-len(desc))%4)
    rest = original[20+length:]
    path = assets/"QuaterniusSwimming.glb"
    path.write_bytes(struct.pack("<4sIII4s",b"glTF",2,20+len(desc)+len(rest),len(desc),b"JSON")+desc+rest)
    return path


def rig(name, mesh, is_target):
    result = create_asset(name,"SwimmingSource",u.IKRigDefinition,u.IKRigDefinitionFactory())
    controller = u.IKRigController.get_controller(result)
    assert controller.set_skeletal_mesh(mesh)
    controller.set_retarget_root("pelvis")
    chains = {"Root":("root","root"), "Spine":("spine_01","spine_05" if is_target else "spine_03"),
              "Head":("neck_01","head")}
    for side in ("l","r"):
        chains[f"Clavicle_{side}"] = (f"clavicle_{side}",f"clavicle_{side}")
        chains[f"Arm_{side}"] = (f"upperarm_{side}",f"hand_{side}")
        chains[f"Leg_{side}"] = (f"thigh_{side}",f"ball_{side}")
        for finger in ("index","middle","ring","pinky","thumb"):
            chains[f"{finger}_{side}"]=(f"{finger}_01_{side}",f"{finger}_03_{side}")
    for chain,(first,last) in chains.items():
        if any(str(item.chain_name)==chain for item in controller.get_retarget_chains()):
            continue
        controller.add_retarget_chain(chain,first,last,"None")
    library.save_loaded_asset(result,False)
    return result


def run():
    if all(library.does_asset_exist(ROOT+"/Swimming/"+name) for name in animations.values()):
        return
    registry.scan_paths_synchronous([ROOT],True)
    records = registry.get_assets_by_path(source_assets,recursive=True)
    if not any(str(item.asset_class_path.asset_name)=="SkeletalMesh" for item in records):
        import_source(swim_only(),"SwimmingSource")
        registry.scan_paths_synchronous([source_assets],True)
        records = registry.get_assets_by_path(source_assets,recursive=True)
    source = next(item.get_asset() for item in records if str(item.asset_class_path.asset_name)=="SkeletalMesh")
    target = u.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
    source_rig, target_rig = rig("IK_Quaternius",source,False), rig("IK_SaltwindManny",target,True)
    asset = create_asset("RTG_Swimming","SwimmingSource", u.IKRetargeter,u.IKRetargetFactory())
    controller = u.IKRetargeterController.get_controller(asset)
    for side,ik,mesh in ((u.RetargetSourceOrTarget.SOURCE,source_rig,source),(u.RetargetSourceOrTarget.TARGET,target_rig,target)):
        controller.set_ik_rig(side,ik)
        controller.set_preview_mesh(side,mesh)
    controller.add_default_ops()
    controller.assign_ik_rig_to_all_ops(u.RetargetSourceOrTarget.SOURCE,source_rig)
    controller.assign_ik_rig_to_all_ops(u.RetargetSourceOrTarget.TARGET,target_rig)
    controller.auto_map_chains(u.AutoMapChainType.EXACT,True)
    library.save_loaded_asset(asset,False)
    destination = folder("Swimming")
    for label,name in animations.items():
        src = next(item for item in records if str(item.asset_class_path.asset_name)=="AnimSequence" and label.lower() in str(item.asset_name).lower())
        if library.does_asset_exist(destination+"/"+name):
            continue
        task = u.IKRetargetBatchOperationInputs()
        task.set_editor_properties(dict(assets_to_retarget=[src],source_mesh=source,target_mesh=target,
                                        ik_retarget_asset=asset,target_path=destination,
                                        include_referenced_assets=False,overwrite_existing_files=True))
        result = u.IKRetargetBatchOperation.run_batch_retarget(task)
        if not result:
            raise RuntimeError("Retarget failed for "+label)
        anim = result[0].get_asset()
        assert anim.get_editor_property("skeleton")==target.get_editor_property("skeleton")
        anim.set_editor_property("enable_root_motion",False)
        library.save_loaded_asset(anim,False)
        assert library.rename_asset(anim.get_path_name(),destination+"/"+name)
        note(f"{label} retargeted to {name}")


run()
