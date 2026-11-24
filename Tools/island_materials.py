"""Author the scanned terrain and animated single-layer water materials."""

import unreal as u


class Graph:
    def __init__(self, material):
        self.material = material
        self.api = u.MaterialEditingLibrary
        self.api.delete_all_material_expressions(material)

    def node(self, kind, x, y, **properties):
        expression = self.api.create_material_expression(self.material, kind, x, y)
        for name, value in properties.items():
            expression.set_editor_property(name, value)
        return expression

    def wire(self, source, target, name):
        if not self.api.connect_material_expressions(source, "", target, name):
            raise RuntimeError("Cannot connect " + name + " on " + str(target))

    def pin(self, source, name):
        self.api.connect_material_property(source, "", getattr(u.MaterialProperty, "MP_" + name))

    def custom(self, label, code, size, x, y, **inputs):
        slots = []
        for name in inputs:
            slot = u.CustomInput()
            slot.set_editor_property("input_name", name)
            slots.append(slot)
        node = self.node(u.MaterialExpressionCustom, x, y, code=code, description=label,
                         output_type=getattr(u.CustomMaterialOutputType, "CMOT_FLOAT" + str(size)),
                         inputs=slots)
        for name, value in inputs.items():
            self.wire(value, node, name)
        return node

    def sample(self, texture, kind, x, y, uv=None):
        expression = self.node(u.MaterialExpressionTextureSample, x, y, texture=texture,
                               sampler_type=getattr(u.MaterialSamplerType, "SAMPLERTYPE_" + kind),
                               sampler_source=u.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        if uv:
            self.wire(uv, expression, "UVs")
        return expression

    def number(self, value, x, y):
        return self.node(u.MaterialExpressionConstant, x, y, r=value)

    def color(self, rgb, x, y):
        return self.node(u.MaterialExpressionConstant3Vector, x, y, constant=u.LinearColor(*rgb, 1.0))

    def finish(self):
        self.api.recompile_material(self.material)
        u.EditorAssetLibrary.save_loaded_asset(self.material, only_if_is_dirty=False)


def terrain_material(material, surfaces):
    g = Graph(material)
    position = g.node(u.MaterialExpressionWorldPosition, -1300, -120)
    uv_sand = g.custom("Beach scan coordinates",
                       "float2 p=World.xy/325; return p+float2(sin(p.y*.63),sin(p.x*.54))*.23;",
                       2, -1075, -460, World=position)
    uv_forest = g.custom("Leaf litter scan coordinates",
                         "float2 p=World.xy/232; return p+float2(sin(p.y*.43),cos(p.x*.48))*.38;",
                         2, -1075, -40, World=position)
    uv_rock = g.custom("Coastal stone coordinates",
                       "float2 p=World.xy/500; return p+float2(World.z/1200,sin(p.x*.56)*.16);",
                       2, -1075, 355, World=position)
    samples = {}
    for row, (name, uv) in enumerate((("Sand", uv_sand), ("Forest", uv_forest), ("Rock", uv_rock), ("Wet", uv_sand))):
        samples[name] = [g.sample(tex, kind, -820 + j * 185, row * 280 - 465, uv)
                         for j, (tex, kind) in enumerate(zip(surfaces[name], ("COLOR", "NORMAL", "LINEAR_COLOR")))]

    uv_second = g.custom("Rotated larger scan",
                         "return mul(float2x2(.78,-.625,.625,.78),World.xy)/557+float2(.31,.57);",
                         2, -1075, 655, World=position)
    sand_second = g.sample(surfaces["Sand"][0], "COLOR", -820, 690, uv_second)
    forest_second = g.sample(surfaces["Forest"][0], "COLOR", -635, 690, uv_second)
    weights = """
float2 p = World.xy * .01;
float multi = sin(p.x*.108+p.y*.12)*sin(p.y*.142-p.x*.085);
float patch = saturate(.5+multi*.54+sin(p.x*.42-p.y*.34)*.13);
float blendNoise = sin(p.x*.32+p.y*.19)*.23+sin(p.y*.77-p.x*.41)*.10+multi*.12;
float pathX = -39 + .41*(p.y+36) + sin((p.y+38)/9)*4.6;
float trail = (1-smoothstep(.75,2.9,abs(p.x-pathX))) * smoothstep(-45,-26,p.y) * (1-smoothstep(37,57,p.y));
float slope = 1-abs(normalize(cross(ddx(World),ddy(World))).z);
float forest = smoothstep(1.7,4.3,World.z*.01+blendNoise*2) * (1-trail*.82);
float rock = saturate(smoothstep(.13,.36,slope) + smoothstep(5.0,12.5,World.z*.01)*(.15+.22*blendNoise));
float wet = 1-smoothstep(-38,64, World.z+blendNoise*88);
"""
    color = g.custom("Wet sand, coastal sand, leaf litter and exposed rock", weights + """
float3 dry = lerp(Sand,SandLarge,smoothstep(.08,.90,patch));
float3 sand = lerp(dry*float3(1.03,1.01,.95), Wet*.78, wet);
float3 soil = lerp(Forest,ForestLarge,smoothstep(.20,1,patch));
float3 ground = lerp(sand, soil*float3(.86,.89,.80), forest);
ground = lerp(ground, Rock*float3(.78,.81,.75), rock);
return ground * lerp(.84, 1.115, saturate(.5+blendNoise));
""", 3, -10, -350, World=position, SandLarge=sand_second, ForestLarge=forest_second,
                         **{key: value[0] for key, value in samples.items()})
    norms = g.custom("Blended scan normals", weights + """
float3 n = lerp(lerp(Sand, Wet, wet), Forest, forest);
n = lerp(n, Rock, rock);
return normalize(float3(n.xy*.72,max(n.z,.18)));
""", 3, -10, -20, World=position, **{key: value[1] for key, value in samples.items()})
    rough = g.custom("Damp sand roughness", weights + """
return lerp(lerp(lerp(Sand.r, min(Wet.r,.48),wet),Forest.r,forest),Rock.r,rock);
""", 1, -10, 310, World=position, **{key: value[2] for key, value in samples.items()})
    g.pin(color, "BASE_COLOR")
    g.pin(norms, "NORMAL")
    g.pin(rough, "ROUGHNESS")
    g.pin(g.number(.24, 40, 620), "SPECULAR")
    g.finish()


def leaf_material(material, image, dry=False, alpha=None, normal=None, sway=3.1):
    material.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_MASKED,
                                        shading_model=u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE,
                                        two_sided=True, opacity_mask_clip_value=.41))
    g = Graph(material)
    sample = g.sample(image, "COLOR", -710, -130)
    seed = g.node(u.MaterialExpressionPerInstanceRandom, -710, 540)
    base = g.custom("Leaf pigment",
                    "return saturate(Leaf * lerp(float3(.88,.94,.83),float3(1.09,1.16,1.05),Seed));",
                    3, -390, -170, Leaf=sample, Seed=seed)
    transmission = g.custom("Light filtering through leaves",
                            "return Leaf * float3(.78,.90,.46) + float3(.016,.022,.006);",
                            3, -390, 50, Leaf=sample)
    g.pin(base, "BASE_COLOR")
    g.pin(transmission, "SUBSURFACE_COLOR")
    g.pin(g.number(.72 if dry else .55, -330, 270), "ROUGHNESS")
    g.pin(g.number(.22, -330, 390), "SPECULAR")
    mask = g.sample(alpha, "LINEAR_GRAYSCALE", -710, 410) if alpha else sample
    if not g.api.connect_material_property(mask, "R" if alpha else "A", u.MaterialProperty.MP_OPACITY_MASK):
        raise RuntimeError("Unable to connect foliage texture alpha")
    if normal:
        g.pin(g.sample(normal,"NORMAL",-700,260), "NORMAL")
    position = g.node(u.MaterialExpressionWorldPosition, -1050, 470)
    local = g.node(u.MaterialExpressionPreSkinnedPosition, -1050, 710)
    time = g.node(u.MaterialExpressionTime, -1050, 930)
    strength = g.number(sway, -710, 820)
    wind = g.custom("Coastal breeze",
                    """
float swayPhase=dot(World.xy,float2(.0035,.0041));
float gust = sin(Time*.79+swayPhase*.35)*.5+.5;
float flexible = smoothstep(14,115,abs(Local.z));
float wind = sin(Time*1.47+swayPhase+Seed*4)*(.43+gust*.57);
float flutter = sin(Time*6.2+swayPhase*3+Local.y*.036)*.17;
return float3((wind+flutter),wind*.46,flutter*.7)*flexible*Strength;
""", 3, -380, 570, World=position, Local=local, Time=time, Seed=seed, Strength=strength)
    g.pin(wind, "WORLD_POSITION_OFFSET")
    g.finish()


def water_material(material, depth_texture):
    material.set_editor_property("shading_model", u.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    material.set_editor_property("tangent_space_normal", False)
    material.set_editor_property("two_sided", True)
    g = Graph(material)
    position = g.node(u.MaterialExpressionWorldPosition, -1350, -100)
    time = g.node(u.MaterialExpressionTime, -1350, 120)
    camera = g.node(u.MaterialExpressionCameraPositionWS, -1350, 325)
    uv = g.custom("Baked seabed coordinates", "return saturate(World.xy/44800 + .5);", 2, -1110, -220, World=position)
    depth = g.sample(depth_texture, "LINEAR_GRAYSCALE", -890, -220, uv)
    depth.set_editor_property("sampler_source", u.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
    pattern = """
float2 p = World.xy*.01;
float t = T;
float d = lerp(-22,24,Depth.r);
float2 q = float2(dot(p,float2(.76,.65)),dot(p,float2(-.65,.76)));
float bend = sin(q.y*.34+t*.39)*.6+sin(q.y*.72-t*.22)*.27;
float waveA = q.x*1.57 + bend - t*1.78;
float waveB = dot(p,float2(-.31,.95))*2.24 - t*2.27;
float waveC = dot(p,float2(.94,-.34))*4.19 - t*2.64;
float active = smoothstep(-.10,.83,d);
float phase = d*10.0 + t*2.17 + sin(q.y*.46)*1.27 + sin(q.y*.16-t*.17)*1.16;
float breaker = pow(saturate(.5+.5*sin(phase)),12) * smoothstep(1.35,.15,d);
float2 cloudUV = p*float2(6.4,5.3) + float2(t*.34,-t*.22);
float lacing = sin(cloudUV.x+sin(cloudUV.y*.72)*2.1) * sin(cloudUV.y+sin(cloudUV.x*.7)*1.4);
float grain = sin(cloudUV.x*3.4-cloudUV.y*2.7)*sin(cloudUV.y*2.1+cloudUV.x*.4);
float lace = smoothstep(.38,.88,lacing+.14*grain) * smoothstep(.09,.85,d) * (1-smoothstep(1.0,2.3,d));
float foam = saturate(breaker*(.61+.22*lacing+.12*grain) + lace*.38);
foam *= smoothstep(-.13,.16,d);
float offshore = smoothstep(2,16,d);
"""
    base = g.custom("Turquoise shoals, deep water and whitewater", pattern + """
float3 water = lerp(float3(.008,.080,.075),float3(.002,.013,.019),offshore);
return lerp(water,float3(.70,.77,.70),foam);
""", 3, -410, -395, World=position, T=time, Depth=depth)
    rough = g.custom("Whitewater roughness", pattern + """
return lerp(.085,.57,foam) + .018*sin(waveA)*sin(waveB);
""", 1, -410, -100, World=position, T=time, Depth=depth)
    norm = g.custom("Cross swell and capillary ripples", pattern + """
float cameraDistance = length(Camera.xy - World.xy) * .01;
float detail = 1-smoothstep(18.0,77.0,cameraDistance);
float2 gradient = (cos(waveA)*.115*float2(.76,.65)+cos(waveB)*.069*float2(-.31,.95)
    +cos(waveC)*.041*detail*float2(.94,-.34)) * (.42+.58*active);
float ripple1 = cos(dot(p,float2(13.4,8.9))-t*5.2 + sin(waveA)*.57);
float ripple2 = cos(dot(p,float2(-9.6,15.1))-t*6.3 + sin(waveB)*.35);
gradient += (ripple1*float2(.018,.012)+ripple2*float2(-.012,.018))*(.52+.48*active)*detail;
return normalize(float3(-gradient, 1));
""", 3, -410, 160, World=position, T=time, Depth=depth, Camera=camera)
    offset = g.custom("Shoaling swell displacement", pattern + """
return float3(0,0,(sin(waveA)*10.5+sin(waveB)*4.0+sin(waveC)*1.4) * active);
""", 3, -410, 440, World=position, T=time, Depth=depth)
    opacity = g.custom("Water surface coverage", pattern + """
return lerp(.11+.085*offshore,.91,foam);
""", 1, -410, 700, World=position, T=time, Depth=depth)
    scattering = g.custom("Shoal scattering", pattern + """
return lerp(float3(.00022,.0016,.0018),float3(.00010,.00047,.00065),offshore);
""", 3, -410, 940, World=position, T=time, Depth=depth)
    absorption = g.color((.0092,.0034,.0022), -410, 1200)
    behind = g.custom("Caustics on submerged sand", pattern + """
float c1=sin(dot(p,float2(5.3,7.4))+sin(q.y*3.3+t*1.8)*1.7+t*1.6);
float c2=sin(dot(p,float2(-7.1,4.8))+sin(q.x*3.9-t*1.4)*1.3-t*1.2);
float caustic=pow(saturate(1-abs(c1+c2)),6);
return (1+caustic*.22*(1-smoothstep(1,6,d))).xxx;
""", 3, -410, 1420, World=position, T=time, Depth=depth)
    output = g.node(u.MaterialExpressionSingleLayerWaterMaterialOutput, 40, 1030)
    names = g.api.get_material_expression_input_names(output)
    for name, source in zip(names, (scattering, absorption, g.number(.15, -220, 1230), behind)):
        g.wire(source, output, name)
    for name, source in (("BASE_COLOR",base),("ROUGHNESS",rough),("NORMAL",norm),
                         ("WORLD_POSITION_OFFSET",offset),("OPACITY",opacity)):
        g.pin(source, name)
    g.finish()


def underwater_material(material):
    material.set_editor_properties(dict(material_domain=u.MaterialDomain.MD_POST_PROCESS,
                                        blendable_location=u.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING))
    g = Graph(material)
    scene = g.node(u.MaterialExpressionSceneTexture, -700, -160, scene_texture_id=u.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    depth = g.node(u.MaterialExpressionSceneTexture, -700, 70, scene_texture_id=u.SceneTextureId.PPI_SCENE_DEPTH)
    filter_color = g.custom("Wave length attenuation and suspended sediment", """
float metres = min(max(Depth.r*.01,0),75);
float3 transmission = exp(-metres*float3(.142,.055,.036));
float3 scattering = float3(.038,.204,.236);
return max(0, Scene.rgb*transmission+scattering*(1-transmission));
""", 3, -310, -65, Scene=scene, Depth=depth)
    g.pin(filter_color,"EMISSIVE_COLOR")
    g.finish()
