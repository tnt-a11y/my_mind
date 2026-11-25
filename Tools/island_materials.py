"""Author the scanned terrain and animated single-layer water materials."""

import unreal as u


class Graph:
    def __init__(self, material):
        self.material = material
        self.api = u.MaterialEditingLibrary
        for expression in list(self.api.get_material_expressions(material)):
            self.api.delete_material_expression(material, expression)

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

    def scalar(self, name, value, x, y):
        return self.node(u.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)

    def color(self, rgb, x, y):
        return self.node(u.MaterialExpressionConstant3Vector, x, y, constant=u.LinearColor(*rgb, 1.0))

    def finish(self):
        self.api.recompile_material(self.material)
        if not u.EditorAssetLibrary.save_loaded_asset(self.material, only_if_is_dirty=False):
            raise RuntimeError("Could not save " + self.material.get_path_name())


def terrain_material(material, surfaces):
    g = Graph(material)
    position = g.node(u.MaterialExpressionWorldPosition, -1300, -120)
    rain = g.scalar("RainIntensity", 0, -1280, 1550)
    rain_wetness = g.scalar("RainWetness", 0, -1280, 1720)
    time = g.node(u.MaterialExpressionTime, -1280, 1890)
    uv_sand = g.custom("Beach scan coordinates",
                       "float2 p=World.xy/325; return p+float2(sin(p.y*.63),sin(p.x*.54))*.23;",
                       2, -1075, -460, World=position)
    uv_forest = g.custom("Leaf litter scan coordinates",
                         "float2 p=World.xy/232; return p+float2(sin(p.y*.43),cos(p.x*.48))*.38;",
                         2, -1075, -40, World=position)
    uv_rock = g.custom("Coastal stone coordinates",
                       "float2 p=World.xy/500; return p+float2(World.z/1200,sin(p.x*.56)*.16);",
                       2, -1075, 355, World=position)
    uv_shelf = g.custom("Current-shaped seabed sand coordinates",
                        "float2 p = World.xy / 238; return mul(float2x2(.86,-.51,.51,.86),p)"
                        "+float2(sin(p.y*.43),sin(p.x*.57))*.29;",
                        2, -1075, 545, World=position)
    samples = {}
    for row, (name, uv) in enumerate((("Sand", uv_sand), ("Forest", uv_forest), ("Rock", uv_rock),
                                      ("Wet", uv_sand), ("Shelf", uv_shelf))):
        textures = surfaces.get(name, surfaces["Sand"])
        samples[name] = [g.sample(tex, kind, -820 + j * 185, row * 280 - 465, uv)
                         for j, (tex, kind) in enumerate(zip(textures, ("COLOR", "NORMAL", "LINEAR_COLOR")))]

    rock_x = g.custom("Cliff side projection X", "return World.yz/370;", 2, -1080, 1100, World=position)
    rock_y = g.custom("Cliff side projection Y", "return World.xz/370;", 2, -1080, 1320, World=position)
    rock_map = surfaces["Rock"][0]
    rock_side_x = g.sample(rock_map, "COLOR", -815, 1100, rock_x)
    rock_side_y = g.sample(rock_map, "COLOR", -815, 1320, rock_y)

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
float3 face = abs(normalize(cross(ddx(World),ddy(World))));
float slope = 1-face.z;
float outerIslet = smoothstep(91,111,length(p-float2(2,8)));
float forest = smoothstep(2.0,4.5,World.z*.01+blendNoise*1.55) * (1-trail*.82) * (1-outerIslet);
float cliff = smoothstep(.065,.30,slope);
float rock = saturate(cliff*.88 + smoothstep(1.3,6.0,World.z*.01)*outerIslet*.91
                    + smoothstep(5.0,12.5,World.z*.01)*(.15+.22*blendNoise));
float wet = 1-smoothstep(-15,42,World.z+blendNoise*56);
float seabed = 1-smoothstep(-120,-22,World.z+blendNoise*38);
float rainCoat = RainWetness * lerp(.72,1,patch) * (1-.72*seabed);
float depression = .50 + .20*sin(dot(p,float2(2.15,1.71)))
                       + .16*cos(dot(p,float2(-1.83,2.14))) + .12*sin(dot(p,float2(5.1,-3.8)));
float puddle = smoothstep(.64,.83,depression+(rainCoat-.6)*.32)
             * smoothstep(.43,.92,rainCoat) * (1-smoothstep(.0004,.0045,slope))
             * (1-seabed) * (1-rock) * (1-wet);
"""
    color = g.custom("Wet sand, coastal sand, leaf litter and exposed rock", weights + """
float3 dry = lerp(Sand,SandLarge,smoothstep(.08,.90,patch));
float3 sand = lerp(dry*float3(1.32,1.22,1.03), Wet*float3(.98,.94,.85), wet);
float currentRipple = sin(p.y*19+p.x*5+sin(p.x*.84+p.y*.31)*2.3);
sand = lerp(sand, Shelf*float3(1.06,1.02,.90)*(1+.035*currentRipple), seabed);
float3 soil = lerp(Forest,ForestLarge,smoothstep(.20,1,patch));
float3 ground = lerp(sand, soil*float3(.86,.89,.80), forest);
float3 weights3 = pow(face, 4);
float3 cliffScan = (RockX*weights3.x + RockY*weights3.y + Rock*weights3.z)
                   / max(weights3.x+weights3.y+weights3.z,.001);
ground = lerp(ground, cliffScan*float3(.94,.89,.80), rock);
float3 soaked = lerp(ground*float3(.61,.52,.44),ground*float3(.47,.40,.33),forest);
ground = lerp(ground,soaked,rainCoat*lerp(1,.43,rock));
ground = lerp(ground,ground*float3(.78,.81,.87),puddle*.74);
return ground * lerp(.84, 1.115, saturate(.5+blendNoise));
""", 3, -10, -350, World=position, RainWetness=rain_wetness, SandLarge=sand_second, ForestLarge=forest_second,
                         RockX=rock_side_x, RockY=rock_side_y,
                         **{key: value[0] for key, value in samples.items()})
    norms = g.custom("Blended scan normals", weights + """
float3 n = lerp(lerp(lerp(Sand, Wet, wet), Shelf, seabed), Forest, forest);
n = lerp(n, Rock, rock);
float ripple = cos(p.y*19+p.x*5+sin(p.x*.84+p.y*.31)*2.3);
n.xy += ripple*float2(.032,.09)*seabed*(1-rock);
n.xy = lerp(n.xy,RainGradient * .032,puddle*.90);
return normalize(float3(n.xy*lerp(.72,.59,rainCoat),max(n.z,.18)));
""", 3, -10, -20, World=position, RainWetness=rain_wetness,
                     RainGradient=g.custom("Raindrop rings in shallow puddles", RAIN_RINGS, 2, -325, 1340,
                                           World=position, T=time, Rain=rain),
                     **{key: value[1] for key, value in samples.items()})
    rough = g.custom("Damp sand roughness", weights + """
float sand = lerp(lerp(Sand.r, min(Wet.r,.48),wet), min(Shelf.r,.64), seabed);
float dryRoughness = lerp(lerp(sand,Forest.r,forest),Rock.r,rock);
float wetRoughness = lerp(dryRoughness,min(dryRoughness,.26+rock*.12),rainCoat);
return lerp(wetRoughness,.087,puddle);
""", 1, -10, 310, World=position, RainWetness=rain_wetness, **{key: value[2] for key, value in samples.items()})
    g.pin(color, "BASE_COLOR")
    g.pin(norms, "NORMAL")
    g.pin(rough, "ROUGHNESS")
    g.pin(g.number(.24, 40, 620), "SPECULAR")
    u.MaterialEditingLibrary.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_NANITE)
    g.finish()


def footprint_material(material):
    material.set_editor_properties(dict(material_domain=u.MaterialDomain.MD_DEFERRED_DECAL,
                                        blend_mode=u.BlendMode.BLEND_TRANSLUCENT))
    g = Graph(material)
    uv = g.node(u.MaterialExpressionTextureCoordinate, -730, -120)
    wet = g.node(u.MaterialExpressionScalarParameter, -730, 120, parameter_name="Wetness", default_value=.25)
    lifetime = g.node(u.MaterialExpressionDecalLifetimeOpacity, -730, 290)
    shoe = """
float2 p = UV*2-1;
float toe = length((p-float2(.045,.38))/float2(.52,.54));
float heel = length((p-float2(-.045,-.50))/float2(.39,.36));
float arch = length((p-float2(-.09,-.04))/float2(.31,.43));
float outline = min(toe,min(heel,arch));
float edge = 1-smoothstep(.89,1.06,outline);
float bar = smoothstep(.14,.42,abs(frac((p.y+.34*abs(p.x))*5)-.5));
float sand = .88+.12*sin(p.x*81+p.y*43)*sin(p.y*91-p.x*21);
"""
    mask = g.custom("Heel, forefoot and tread", shoe + """
return Fade*edge*sand*(.67+.33*bar)*lerp(.38,.62,Wet);
""", 1, -345, -130, UV=uv, Wet=wet, Fade=lifetime)
    pigment = g.custom("Compressed sand", "return lerp(float3(.087,.064,.037),float3(.037,.028,.018),Wet);",
                       3, -340, 135, Wet=wet)
    g.pin(mask, "OPACITY")
    g.pin(pigment, "BASE_COLOR")
    g.pin(g.number(.78, -120, 245), "ROUGHNESS")
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
    u.MaterialEditingLibrary.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_NANITE)
    u.MaterialEditingLibrary.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    g.finish()


RAIN_RINGS = """
float2 result = 0;
if (Rain > .004)
{
    [unroll] for (int Layer = 0; Layer < 2; ++Layer)
    {
        float2 q = World.xy / (43 + Layer * 23) + Layer * float2(17.43,11.81);
        float2 cell = floor(q);
        float3 seed = frac(sin(float3(dot(cell,float2(127.1,311.7)),
                                      dot(cell,float2(269.5,183.3)),
                                      dot(cell,float2(73.9,121.4)))) * 43758.5453);
        float2 radial = frac(q) - lerp(.34,.66,seed.xy);
        float age = frac(T * (1.7+seed.y*.95) + seed.z);
        float dist = length(radial);
        float wave = (dist - age*.44)*58;
        float envelope = exp(-wave*wave*.20) * (1-age) * smoothstep(0,.08,age);
        result += radial / max(.015,dist) * (sin(wave)*envelope);
    }
}
return result*Rain;
"""


def water_material(material, depth_texture):
    material.set_editor_property("shading_model", u.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    material.set_editor_property("tangent_space_normal", False)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("max_world_position_offset_displacement", 45.0)
    g = Graph(material)
    position = g.node(u.MaterialExpressionWorldPosition, -1350, -100)
    time = g.node(u.MaterialExpressionTime, -1350, 120)
    camera = g.node(u.MaterialExpressionCameraPositionWS, -1350, 325)
    rain = g.scalar("RainIntensity", 0, -1350, 570)
    uv = g.custom("Baked seabed coordinates", "return saturate(World.xy/44800 + .5);", 2, -1110, -220, World=position)
    depth = g.sample(depth_texture, "LINEAR_GRAYSCALE", -890, -220, uv)
    depth.set_editor_property("sampler_source", u.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
    pattern = """
float2 p = World.xy*.01;
float t = T;
float d = lerp(-22,24,Depth.r);
float2 q = float2(dot(p,float2(.76,.65)),dot(p,float2(-.65,.76)));
// Match the three displacement phases and shoaling range in SaltwindWaterMovement.
float bend = sin(q.y*.16+t*.31)*.52+sin(q.y*.41-t*.21)*.21;
float waveA = q.x*.36 + bend - t*1.80;
float waveB = dot(p,float2(-.31,.95))*.55 - t*2.30;
float waveC = dot(p,float2(.94,-.34))*.78 - t*2.73;
float active = smoothstep(-.10,1.40,d);
float offshore = smoothstep(1.6,15.0,d);
"""
    whitewater = """
float shorePhase = d*5.4 + t*1.72 + sin(q.y*.37)*1.05 + sin(q.y*.12-t*.27)*.76;
float pulse = pow(saturate(.5+.5*sin(shorePhase)),9);
float breaker = pulse * smoothstep(.02,.25,d) * (1-smoothstep(.70,1.68,d));
float2 cell = p*float2(6.4,5.3) + float2(t*.38,-t*.27);
float lacing = sin(cell.x+sin(cell.y*.72)*2.1) * sin(cell.y+sin(cell.x*.7)*1.4);
float grain = sin(cell.x*3.4-cell.y*2.7)*sin(cell.y*2.1+cell.x*.4);
float lace = smoothstep(.16,.83,lacing+.24*grain);
float wash = smoothstep(.02,.26,d) * (1-smoothstep(.45,1.37,d));
float foam = saturate(breaker*(.62+.36*lace) + wash*lace*(.14+.16*pulse));
"""
    base = g.custom("Turquoise shoals, deep water and whitewater", pattern + whitewater + """
float3 water = lerp(float3(.004,.029,.026),float3(.001,.006,.012),offshore);
return lerp(water,float3(.61,.70,.66),foam);
""", 3, -410, -395, World=position, T=time, Depth=depth)
    rough = g.custom("Whitewater roughness", pattern + whitewater + """
return lerp(.068,.52,foam) + .026*(.5+.5*sin(waveA)*sin(waveB)) + Rain*(.043-.026*foam);
""", 1, -410, -100, World=position, T=time, Depth=depth, Rain=rain)
    norm = g.custom("Cross swell and capillary ripples", pattern + """
float cameraDistance = length(Camera.xy - World.xy) * .01;
float detail = 1-smoothstep(12.0,52.0,cameraDistance);
float2 gradient = ((cos(waveA)+.24*sin(2*waveA))*.083*float2(.76,.65)
    +cos(waveB)*.047*float2(-.31,.95)+cos(waveC)*.027*float2(.94,-.34)) * (.26+.74*active);
float ripple1 = cos(dot(p,float2(8.7,4.8))-t*4.8 + sin(waveA)*.84);
float ripple2 = cos(dot(p,float2(-6.9,11.1))-t*6.3 + sin(waveB)*.62);
float ripple3 = cos(dot(p,float2(19.7,-8.4))-t*7.2 + sin(waveC)*.73);
gradient += (ripple1*float2(.041,.024)+ripple2*float2(-.021,.036)+ripple3*float2(.014,-.009)) * detail;
gradient += RainGradient*.19*detail*(1-smoothstep(2.0,18.0,cameraDistance));
return normalize(float3(-gradient, 1));
""", 3, -410, 160, World=position, T=time, Depth=depth, Camera=camera,
                     RainGradient=g.custom("Overlapping raindrop rings", RAIN_RINGS, 2, -810, 170,
                                           World=position, T=time, Rain=rain))
    offset = g.custom("Shoaling swell displacement", pattern + """
return float3(0,0,((sin(waveA)-.12*cos(2*waveA))*23.0+sin(waveB)*8.5+sin(waveC)*3.5) * active);
""", 3, -410, 440, World=position, T=time, Depth=depth)
    opacity = g.custom("Water surface coverage", pattern + whitewater + """
return lerp(.052+.10*offshore,.90,foam);
""", 1, -410, 700, World=position, T=time, Depth=depth)
    scattering = g.custom("Shoal scattering", pattern + """
return lerp(float3(.00030,.0020,.0019),float3(.00012,.00052,.00078),offshore);
""", 3, -410, 940, World=position, T=time, Depth=depth)
    absorption = g.color((.0078,.0029,.0019), -410, 1200)
    behind = g.custom("Caustics on submerged sand", pattern + """
float c1=sin(dot(p,float2(5.3,7.4))+sin(q.y*3.3+t*1.8)*1.7+t*1.6);
float c2=sin(dot(p,float2(-7.1,4.8))+sin(q.x*3.9-t*1.4)*1.3-t*1.2);
float caustic=pow(saturate(1-abs(c1+c2)),6);
return (1+caustic*.43*(1-smoothstep(1,5.5,d))*active).xxx;
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
