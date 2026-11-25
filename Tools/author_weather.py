"""Create Coral Reach's wet landscape and weather particle materials in Unreal."""

from pathlib import Path
import sys

import unreal as u

sys.path.insert(0, str(Path(__file__).parent))
from build_island import asset_lib, create_asset, import_source, ROOT
from island_materials import Graph, terrain_material, water_material


def particle(name, color, opacity, age=False):
    material = create_asset(name, "Materials/Weather", u.Material, u.MaterialFactoryNew())
    material.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_TRANSLUCENT, two_sided=True,
                                        shading_model=u.MaterialShadingModel.MSM_UNLIT))
    g = Graph(material)
    uv = g.node(u.MaterialExpressionTextureCoordinate, -850, -80)
    if age:
        instance = g.node(u.MaterialExpressionPerInstanceCustomData, -850, 210, data_index=0)
        interpolator = g.node(u.MaterialExpressionVertexInterpolator, -600, 210)
        names = g.api.get_material_expression_input_names(interpolator)
        g.wire(instance, interpolator, str(names[0]))
        parameters = dict(UV=uv, Age=interpolator)
    else:
        parameters = dict(UV=uv, Rain=g.scalar("RainIntensity", 0.0, -850, 210))
    alpha = g.custom(name, opacity, 1, -320, 0, **parameters)
    fade = g.node(u.MaterialExpressionDepthFade, -65, 85, fade_distance_default=9.0 if age else 16.0)
    g.wire(alpha, fade, str(g.api.get_material_expression_input_names(fade)[0]))
    g.pin(fade, "OPACITY")
    exposure = g.node(u.MaterialExpressionEyeAdaptationInverse, -65, -230)
    g.wire(g.color(color, -310, -260), exposure, str(g.api.get_material_expression_input_names(exposure)[0]))
    g.pin(exposure, "EMISSIVE_COLOR")
    g.api.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    g.finish()


def run(directory):
    layers = {"Sand": "Scans/T_FineCoastSand", "Forest": "Scans/T_JungleSoil", "Rock": "T_Rock",
              "Wet": "T_Wet", "Shelf": "Scans/T_SeabedSand"}
    textures = {name: [asset_lib.load_asset(f"{ROOT}/Textures/{stem}_{channel}") for channel in "DNR"]
                for name, stem in layers.items()}
    assert all(all(layer) for layer in textures.values())
    terrain_material(asset_lib.load_asset(ROOT + "/Materials/M_CoralReachTerrain"), textures)
    water_material(asset_lib.load_asset(ROOT + "/Materials/M_CoralReachOcean"),
                   asset_lib.load_asset(ROOT + "/Textures/T_LagoonDepth"))
    particle("M_RainStreak", (.75, .91, 1.14), """
float y = saturate(UV.y);
float width = lerp(.36,.70,y);
float shaft = 1-smoothstep(width*.35,width,abs(UV.x-.5)*2);
float ends = smoothstep(.015,.44,y) * (1-smoothstep(.75,.99,y));
return shaft * ends * (.48+.17*Rain);
""")
    particle("M_RainImpact", (.58, .70, .72), """
float2 p = UV*2-1;
float d = length(p);
float ring = exp(-pow((d-lerp(.20,.82,Age))*24,2));
float spray = exp(-pow(d*7,2))*(1-smoothstep(0,.38,Age));
float breakup = .70+.30*cos(p.x*18+p.y*12);
return saturate((ring*.31 + spray*.50)*breakup*(1-Age)*smoothstep(.0,.07,Age));
""", age=True)
    particle("M_FootDust", (.49, .37, .23), """
float2 p = (UV*2-1)*float2(1,1.15);
float grain = sin(p.x*12.8+p.y*4.1+Age*2)*sin(p.y*11.2-p.x*5.7)
            + .38*sin(p.x*23.1-p.y*19+Age*3);
float h = length(p)+grain*.19;
float soft = 1-smoothstep(.05,.91,h);
return soft*soft*smoothstep(0,.15,Age)*pow(saturate(1-Age),1.4)*.59;
""", age=True)
    sound_path = ROOT + "/Audio/SW_Rain"
    sound = (asset_lib.load_asset(sound_path) if asset_lib.does_asset_exist(sound_path)
             else next(item for item in import_source(directory / "SW_Rain.wav", "Audio", "SW_Rain")
                       if isinstance(item, u.SoundWave)))
    sound.set_editor_property("looping", True)
    assert asset_lib.save_loaded_asset(sound, False)
    u.log("SALTWIND_WEATHER_READY")


if __name__ == "__main__":
    run(Path(sys.argv[1]))
