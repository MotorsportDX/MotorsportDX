// FM4 car material mods beyond the body paint: glass, chrome and rims. Original
// code of this project, like fm4-paint.hlsli: it reads the inputs of the game's
// car shaders (docs/analysis/material-atlas.md) and adds to their output.
//   FM4_LOOK_GLASS             0...1  windows and lamp covers: the environment's Fresnel
//                                     reflection at grazing angles and a sharp GGX highlight
//                                     of the sun (0: the game's glass)
//   FM4_LOOK_GLASS_ROUGHNESS   > 0    roughness of the glass highlight (GGX alpha is its square)
//   FM4_LOOK_CHROME            0...1  chrome trim and chrome rims: an energy normalized GGX
//                                     highlight of the sun and the Fresnel rise of the
//                                     environment's reflection (0: the game's chrome)
//   FM4_LOOK_CHROME_ROUGHNESS  > 0    roughness of the chrome highlight
//   FM4_LOOK_RIMS              0...1  painted and alloy rims: a clear lacquer over the game's
//                                     metal, like the body's clear coat (0: the game's rims)
//   FM4_LOOK_RIMS_ROUGHNESS    > 0    roughness of the rim lacquer
// FM4_LOOK_CLEARCOAT_GLINT (fm4-paint.hlsli) limits these highlights too.
//
// The game lights glass and rims with a Phong highlight, pow(R.L), and chrome
// with no highlight of the sun at all, only the environment cube. These mods add
// what the materials show in a physically based model: a GGX highlight of the
// sun, normalized so that a sharper one is brighter, and the rise of the
// reflection towards grazing angles (Schlick Fresnel) above the head-on share
// that the game's own reflection already shows. The alpha of every output stays
// the game's.
//
// Inputs of these shaders (the car vertex shaders, as for the body paint):
// interpolator 1 the world normal, 2 the world position, 3 the shadow map
// coordinate, 4 XY the track lightmap UV. Constants: c43 the direction to the
// sun, c47 the fog color and amount, c59 the eye position. Chrome and rims also
// have c42, the sun color, and c60, the lightmap shadow range. Which fetch
// holds which environment cube, and which constants a shader reads, differ per
// shader: the shader_<hash>.mod.hlsli files pass them, and a function here
// reads no constant on its own (a translation defines only the constants its
// shader reads).

#ifndef FM4_LOOK_GLASS
#define FM4_LOOK_GLASS 0.0
#endif
#ifndef FM4_LOOK_GLASS_ROUGHNESS
#define FM4_LOOK_GLASS_ROUGHNESS 0.08
#endif
#ifndef FM4_LOOK_CHROME
#define FM4_LOOK_CHROME 0.0
#endif
#ifndef FM4_LOOK_CHROME_ROUGHNESS
#define FM4_LOOK_CHROME_ROUGHNESS 0.1
#endif
#ifndef FM4_LOOK_RIMS
#define FM4_LOOK_RIMS 0.0
#endif
#ifndef FM4_LOOK_RIMS_ROUGHNESS
#define FM4_LOOK_RIMS_ROUGHNESS 0.25
#endif

// The shared helpers of fm4-paint.hlsli (GGX visibility, Schlick, cube
// coordinates, the encoded cube decode, the variance shadow) and its knobs.
#define FM4_PAINT_HELPERS_ONLY
#include "fm4-paint.hlsli"

// Head-on reflectance: glass and the lacquer of rims (dielectric, index 1.5),
// and chromium, which reflects a little over half of the light head-on.
static const float FM4_F0_DIELECTRIC = 0.04;
static const float FM4_F0_CHROME = 0.6;

// The view of a pixel: unit normal n and direction to the eye v, and the cube
// coordinate of the mirror direction at the offset 1.5 that every one of these
// shaders holds in its literals.
struct FM4CarView {
  float3 n;
  float3 v;
  float3 cube;
};

FM4CarView fm4_car_view(XePixel input, float3 eye) {
  FM4CarView view;
  view.n = normalize(input.r[1].xyz);
  view.v = normalize(eye - input.r[2].xyz);
  view.cube = fm4_env_cube_coord(xe_cube(reflect(-view.v, view.n).zzxy), 1.5);
  return view;
}

// An environment cube texel as linear color: squared (stored in gamma 2), or,
// with decode, decoded from the encoded HDR that the garage, car menu and
// HomeSpace shaders (shaders_ui_v16) read when bool 138 (dynamic cube) or 141
// (static cube) is set. All of them hold the same decode in their literals:
// the exponent constants 127, 255 and 0.5, the matrix rows below for red,
// green and blue, and the minimum 0 (fm4_decode_env).
float3 fm4_car_env(float4 texel, bool decode) {
  float3 decoded = fm4_decode_env(texel, float4(127.0, 255.0, 0.5, 0.0),
                                  float3(0.3007, 6.0013, -1.332), float3(-1.088, -2.7, 3.1029),
                                  float3(5.6268, -1.7995, -5.772));
  return decode ? decoded : texel.rgb * texel.rgb;
}

#ifdef XE_TEXTURE10_2D_U
// The track's lightmap (fetch 10) at the lightmap UV as the glass shaders read
// it: the texel as it is, 1 lit, 0 shadowed.
float fm4_car_lightmap(float2 lightmap_uv) {
  return saturate(XE_TFETCH2D(10, lightmap_uv).x);
}

// The lightmap shadow as the chrome and rim shaders read it: the texel squared,
// mapped through the range in X and Y of the lightmap parameters (c60).
float fm4_car_lightmap_shadow(float2 lightmap_uv, float4 lightmap_params) {
  float lightmap = XE_TFETCH2D(10, lightmap_uv).x;
  lightmap *= lightmap;
  return saturate((lightmap - lightmap_params.x) / (lightmap_params.y - lightmap_params.x));
}
#endif

// A sharp GGX highlight of the sun (height correlated Smith visibility, Schlick
// Fresnel from f0), energy normalized: a smoother surface gives a smaller and
// brighter highlight. Like the clear coat's, it is scaled by pi to match the
// game's lighting, which leaves out 1/pi, so it is in units of the light that
// stands in for the sun, and soft limited by FM4_LOOK_CLEARCOAT_GLINT: the core
// stays sharp and bloom does not spread it into a blob.
float3 fm4_car_highlight(FM4CarView view, float3 to_sun, float roughness, float3 f0) {
  float3 l = normalize(to_sun);
  float3 h = normalize(l + view.v);
  float n_dot_v = max(dot(view.n, view.v), 1.0e-4);
  float n_dot_l = saturate(dot(view.n, l));
  float n_dot_h = saturate(dot(view.n, h));
  float r = max(roughness, 0.02);
  float a2 = r * r * r * r;
  float denominator = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
  float distribution = a2 / (FM4_PI * denominator * denominator);
  float m = 1.0 - saturate(dot(l, h));
  float3 fresnel = f0 + (1.0 - f0) * ((m * m) * (m * m) * m);
  float3 glint =
      FM4_PI * distribution * fm4_ggx_visibility(n_dot_v, n_dot_l, a2) * fresnel * n_dot_l;
  float limit = max(FM4_LOOK_CLEARCOAT_GLINT, 1.0e-3);
  return glint / (1.0 + glint / limit);
}

// How much more of the environment a surface reflects at the pixel's angle than
// head-on (Schlick): 0 head-on, 1 - f0 at grazing angles. The game's own
// reflection covers the head-on share.
float3 fm4_car_fresnel_rise(FM4CarView view, float3 f0) {
  float m = 1.0 - saturate(dot(view.n, view.v));
  return (1.0 - f0) * ((m * m) * (m * m) * m);
}

// Adds linear light to an output in gamma 2 (the car shaders write the square
// root of their linear color), before the game's fog (fog: its amount).
void fm4_car_add(inout XePixel p, float3 light, float fog) {
  float3 color = p.color[0].rgb;
  p.color[0].rgb = sqrt(max(color * color + light * (1.0 - saturate(fog)), 0.0));
}

// A reflective layer over an opaque car material: the sun's highlight in the
// pixel's shadow and the Fresnel rise of the environment, which keeps a share
// of 0.35 in shadow (the cube holds the sky's light too), as the clear coat
// does. strength 0...1; sun: the sun color; env: the environment in the mirror
// direction (linear); lit: 1 lit, 0 shadowed.
void fm4_car_layer(inout XePixel p, FM4CarView view, float strength, float roughness, float3 f0,
                   float3 to_sun, float3 sun, float3 env, float lit, float fog) {
  float3 highlight = fm4_car_highlight(view, to_sun, roughness, f0) * sun * lit;
  float3 reflection = env * fm4_car_fresnel_rise(view, f0) * lerp(0.35, 1.0, lit);
  fm4_car_add(p, (highlight + reflection) * saturate(strength), fog);
}

// Chrome: chromium's reflectance and the chrome roughness.
void fm4_chrome(inout XePixel p, FM4CarView view, float3 to_sun, float3 sun, float3 env,
                float lit, float fog) {
  fm4_car_layer(p, view, FM4_LOOK_CHROME, FM4_LOOK_CHROME_ROUGHNESS, FM4_F0_CHROME, to_sun, sun,
                env, lit, fog);
}

// Rims: a clear lacquer (dielectric) over the game's metal.
void fm4_rims(inout XePixel p, FM4CarView view, float3 to_sun, float3 sun, float3 env, float lit,
              float fog) {
  fm4_car_layer(p, view, FM4_LOOK_RIMS, FM4_LOOK_RIMS_ROUGHNESS, FM4_F0_DIELECTRIC, to_sun, sun,
                env, lit, fog);
}

// Glass: the windows and the covers of the lamps. Their output is blended over
// what lies behind them with its alpha (source alpha, one minus source alpha;
// REX_GPU_INVENTORY blend control 0x01060706), and the alpha stays the game's.
// The added light is a reflection at the glass surface, which a pane shows
// whatever its tint, but the blend scales the output by the alpha, and it does
// so on the gamma 2 values the shaders write: over a dark background (the cabin
// behind a window) the screen gets alpha squared times the output's linear
// light. So the light is divided by the square of the glass's opacity (the
// alpha without the car's fade, c57.x), counted as at least 1/8, and reaches
// the screen at its full strength there; over a bright background it comes out
// stronger. Seen from inside the car (the normal faces away from the eye), the
// glass stays as it is: the environment cube shows the outside, not the cabin
// that a window reflects from inside. sun: the shader's own specular amount
// and color, which stand in for the sun's light in its highlight (the glass
// shaders have no sun color); lit: fm4_car_lightmap; fade: c57.x.
void fm4_glass(inout XePixel p, FM4CarView view, float3 to_sun, float3 sun, float3 env, float lit,
               float fog, float fade) {
  if (dot(view.n, view.v) <= 0.0) {
    return;
  }
  float3 f0 = FM4_F0_DIELECTRIC;
  float3 highlight = fm4_car_highlight(view, to_sun, FM4_LOOK_GLASS_ROUGHNESS, f0) * sun * lit;
  float3 reflection = env * fm4_car_fresnel_rise(view, f0) * lerp(0.35, 1.0, lit);
  float opacity = clamp(p.color[0].a / max(fade, 1.0e-3), 1.0 / 8.0, 1.0);
  fm4_car_add(p, (highlight + reflection) * (saturate(FM4_LOOK_GLASS) / (opacity * opacity)), fog);
}
