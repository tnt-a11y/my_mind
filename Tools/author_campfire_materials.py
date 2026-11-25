"""Create animated fire, embers, and smoke materials for the world campfire."""

import unreal as u

from build_island import create_asset
from island_materials import Graph


NOISE = """
struct FireNoise
{
    float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
    float noise(float2 p)
    {
        float2 i=floor(p), f=frac(p); f=f*f*(3-2*f);
        return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
    }
};
FireNoise N;
float2 uv = Tex;
"""


FIRE = NOISE + """
float h = max(0,(World.z-Actor.z-9)/(61*lerp(.42,1,Strength)));
float x = 2*uv.x-1;
float t = T + Seed;
float bend = (.11+Rain*.16)*sin(h*5.3-t*2.4) + (Rain*.21+.075)*h;
float n = N.noise(float2(x*3.8+Seed, h*3.5-t*4.2));
float detail = N.noise(float2(x*12+Seed*1.6,h*10.8-t*6.4));
float heat = 0;
[unroll] for (int i=0;i<4;++i)
{
    float lane=(i-1.5)*.31;
    float taper=lerp(.34,.062,saturate(h));
    float peak=.46 + .48*N.noise(float2(i*2.24+Seed,t*3.1+i));
    float center=lane*(1-.34*h)+bend*h + (n-.5)*h*.52 + .08*h*sin(t*9+i*2.1);
    float body=1-abs(x-center)/(taper*lerp(.79,1.15,detail));
    float tip = smoothstep(peak-.08,peak+.09,h + .19*(n-.5));
    heat=max(heat,body*(1-tip)*smoothstep(.02,.083,(World.z-Actor.z)*.01));
}
float alpha=smoothstep(.022,.56,heat)*smoothstep(.008,.115,(World.z-Actor.z)*.01)*Strength;
float core=saturate(heat * 1.26 + .22*n - h*.46);
float3 color=lerp(float3(1.42,.083,.006),float3(3.8,.79,.054),smoothstep(.09,.54,core));
color=lerp(color,float3(6.3,3.8,.64),smoothstep(.56,.96,core));
return float4(color*(.87+.27*detail),alpha*.90);
"""


SMOKE = NOISE + """
float h=saturate((World.z-Actor.z-41)/181);
float x=uv.x*2-1;
float t=T*.61;
float wind=h*(.22 + .16*Rain)+.17*h*sin(h*5.0-t*3.3);
float n=N.noise(float2(x*3.7-h*1.5, h*5.7-t*3.9));
float grain=N.noise(float2(x*9.4+2*n,h*11.5-t*4.4));
float crossSection=1-smoothstep(.27,.91,abs((x-wind)/(lerp(.18,.83,h))) + .64*(.5-n));
float envelope=smoothstep(0,.14,h)*(1-smoothstep(.70,1.0,h));
float density=pow(saturate(n*.69+grain*.31),1.8)*crossSection*envelope*Strength;
float3 tone=lerp(float3(.060,.053,.045),float3(.22,.22,.21),saturate(h+Rain*.43));
return float4(tone,density*.57);
"""


EMBERS = NOISE + """
float2 p=float2(uv.x,(World.z-Actor.z-14)/137);
float dots=0;
[unroll] for(int i=0;i<12;++i)
{
    float seed=N.hash(float2(i,12.38));
    float age=frac(T*lerp(.68,1.47,seed)+seed*9.7);
    float height=age*lerp(.52,1.0,seed);
    float x=.5+(seed-.5)*lerp(.22,.70,age)+.12*age+.075*sin(age*8.0+i)*age;
    float d=length((p-float2(x,height))*float2(1,.65));
    float glow=1-smoothstep(.0018,.012,d);
    dots=max(dots, glow*(1-smoothstep(.50,1.0,age))*smoothstep(.02,.11,age));
}
return float4(5.4,1.36,.18,dots*Strength);
"""


def build_material(name, field, fade_cm):
    material = create_asset(name, "Materials/Campfire", u.Material, u.MaterialFactoryNew())
    material.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_TRANSLUCENT, two_sided=True,
                                        shading_model=u.MaterialShadingModel.MSM_UNLIT))
    g = Graph(material)
    picture = g.custom(name, field, 4, -545, -40,
        Tex=g.node(u.MaterialExpressionTextureCoordinate, -1000, -340),
        World=g.node(u.MaterialExpressionWorldPosition, -1000, -190),
        Actor=g.node(u.MaterialExpressionActorPositionWS, -1000, -50),
        T=g.node(u.MaterialExpressionTime, -1000, 90),
        Strength=g.scalar("FireStrength", 1.0, -1000, 225),
        Rain=g.scalar("RainInfluence", 0.0, -1000, 370),
        Seed=g.scalar("FlameSeed", 0.0, -1000, 515))
    color = g.custom("Exposed flame color", "return Field.rgb;", 3, -280, -135, Field=picture)
    exposure = g.node(u.MaterialExpressionEyeAdaptationInverse, 0, -128)
    g.wire(color, exposure, str(g.api.get_material_expression_input_names(exposure)[0]))
    alpha = g.custom("Soft flame edge", "return Field.a;", 1, -275, 85, Field=picture)
    fade = g.node(u.MaterialExpressionDepthFade, 0, 83, fade_distance_default=fade_cm)
    g.wire(alpha, fade, str(g.api.get_material_expression_input_names(fade)[0]))
    g.pin(exposure, "EMISSIVE_COLOR")
    g.pin(fade, "OPACITY")
    g.finish()


def run():
    for name, field, fade in (("M_CampfireFlame", FIRE, 4.0), ("M_CampfireSmoke", SMOKE, 18.0),
                              ("M_CampfireEmbers", EMBERS, 6.0)):
        build_material(name, field, fade)
