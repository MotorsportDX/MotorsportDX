// The windshield and side windows of the race cockpit camera, a post pass of
// the runtime's post pass framework (passes.toml, post_pass_path; see
// docs/analysis/windshield.md and docs/analysis/post-pass-framework.md).
// Original code of this project.
//
// FM4 draws no window glass for the player's car in the cockpit camera, so the
// world outside shows as through open frames. These passes add the glass at
// the final point, before the HUD, on the pixels that see the world through
// the cockpit's windows:
// - the frame: the interior source of the manifest's [motion] drew in it
//   (xe_motion_interior_slot) and its pixels cover at least
//   WINDSHIELD_MIN_INTERIOR of the frame, which only the cockpit camera does;
// - the pixel: not the interior (the player's car ID under the mask at the
//   stored depth 1.0, xe_motion_is_interior) and not another pixel at the mask
//   depth or the near plane; the world, the sky and the other cars behind the
//   windshield, the side windows and the rear window all qualify.
// Every effect scales with the run-time param WINDSHIELD (0 off):
// - a tint: the light through the glass times WINDSHIELD_TINT_COLOR, by
//   WINDSHIELD_TINT;
// - a reflection of the dashboard: the interior below the dashboard's top edge
//   in each column, mirrored about that edge (WINDSHIELD_REFLECTION);
// - glare: the bright light seen through the glass, spread over
//   WINDSHIELD_GLARE_RADIUS of the frame height and scattered by the glass
//   (WINDSHIELD_GLARE) and its dust (WINDSHIELD_DUST, a fixed pattern on the
//   glass that shows only where light falls on it, with the wipers' arcs on
//   the windshield only).
//
// Entry points of passes.toml (final point, screen orientation; final_color is
// display referred, about gamma 2):
//   windshield_reduce   compute, a group per block of 8x8 final pixels, a
//                       thread per pixel, into 1/8 of the final size: per block
//                       the mean linear color of its interior pixels and
//                       their share, and the same of its window pixels;
//                       per final pixel its kind (R window, G interior)
//   windshield_frame    compute, one thread group: the interior's share of
//                       the frame, into one texel
//   windshield_light    compute, 1/8 of the final size: the light the glass
//                       adds, the dashboard's reflection (RGB) and the
//                       glare (A)
//   windshield_compose  compute, the final size: final_color with the glass
//                       over the window pixels (alpha -1 elsewhere, and in
//                       every pixel of a frame that is not the cockpit
//                       camera's), or a debug view (WINDSHIELD_DEBUG)
//   windshield_output   pixel: draws it into final_color, the rest discarded
//                       (those pixels keep their value bit for bit)
//
// Debug views (WINDSHIELD_DEBUG, over the frame before the HUD):
//   1 the pixel kinds: window cyan, interior red, the mask without an interior
//     magenta, each over the image; a bar at the top left green in a cockpit
//     frame, red otherwise
//   2 the added light alone (reflection and glare, dust included), 4 times
//   3 the dust pattern
// Colors are never the clear gray, which e2e counts as missing geometry.

// FM4 draws its scene rotated 180 degrees within the tiles: the scene textures
// are upside down and mirrored against the frame.
static const bool kSceneRotated = true;
// Final pixels per pixel of the 1/8 textures (scale = 0.125 in passes.toml).
static const int kBlock = 8;

// The stored values are gamma 2: squared, they are about linear.
float3 WindshieldLinear(float3 color) { return color * color; }
float3 WindshieldDisplay(float3 color) { return sqrt(max(color, 0.0)); }
float WindshieldLuma(float3 color) { return dot(color, float3(0.299, 0.587, 0.114)); }

// The scene texture pixel of a final pixel.
int2 WindshieldScenePixel(int2 final_pixel) {
  int2 pixel = int2((float2(final_pixel) + 0.5) * float2(xe_scene_size) / float2(xe_final_size));
  int2 last = int2(xe_scene_size) - 1;
  pixel = clamp(pixel, 0, last);
  return kSceneRotated ? last - pixel : pixel;
}

// What a final pixel shows.
static const uint kKindWindow = 0u;    // the world through the glass
static const uint kKindInterior = 1u;  // the interior under the mask
static const uint kKindMask = 2u;      // the mask without an interior, the near plane

uint WindshieldKind(float stored_depth, uint stencil) {
  bool at_mask = xe_motion_interior_depth >= 0.0 &&
                 (abs(stored_depth - xe_motion_interior_depth) <= 1.0e-6 ||
                  xe_scene_w(stored_depth) < xe_scene_w(xe_motion_interior_depth) * 1.01);
  uint kind = at_mask ? kKindMask : kKindWindow;
  if (xe_motion_is_interior(xe_motion_slot(stencil), stored_depth)) {
    kind = kKindInterior;
  }
  return kind;
}

// The frame is the cockpit camera's: the interior drew and covers enough of it
// (its share of the frame from windshield_frame).
bool WindshieldCockpit(float interior_share) {
  return xe_motion_interior_slot != XE_MOTION_NO_INTERIOR &&
         interior_share >= WINDSHIELD_MIN_INTERIOR;
}

// The size of the 1/8 textures: the size of the passes that write them.
uint2 WindshieldLowSize() { return xe_pass_size; }

// ---------------------------------------------------------------------------
// windshield_reduce: one block of 8x8 final pixels per thread.

Texture2D<float4> reduce_final : register(t0);
Texture2D<float> reduce_depth : register(t1);
Texture2D<uint> reduce_stencil : register(t2);
RWTexture2D<float4> reduce_interior_out : register(u0);
RWTexture2D<float4> reduce_window_out : register(u1);
RWTexture2D<unorm float2> reduce_kind_out : register(u2);

// One pixel per thread, one block per group (group = [1, 1] in passes.toml):
// the sums of the 64 threads are added up in groupshared memory.
groupshared float4 reduce_interior_sums[kBlock * kBlock];
groupshared float4 reduce_window_sums[kBlock * kBlock];
groupshared float reduce_inside_sums[kBlock * kBlock];

[numthreads(8, 8, 1)]
void windshield_reduce(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID,
                       uint index : SV_GroupIndex) {
  int2 pixel = int2(group.xy) * kBlock + int2(thread.xy);
  // RGB the color sum, A the pixel count; inside counts the pixels of the
  // frame in the block (edge blocks have fewer).
  float4 interior = 0.0, window = 0.0;
  float inside = 0.0;
  if (all(pixel < int2(xe_final_size))) {
    inside = 1.0;
    int2 scene = WindshieldScenePixel(pixel);
    uint kind = WindshieldKind(reduce_depth.Load(int3(scene, 0)), reduce_stencil.Load(int3(scene, 0)));
    reduce_kind_out[pixel] = float2(kind == kKindWindow ? 1.0 : 0.0, kind == kKindInterior ? 1.0 : 0.0);
    float3 color = WindshieldLinear(reduce_final.Load(int3(pixel, 0)).rgb);
    if (kind == kKindInterior) {
      interior = float4(color, 1.0);
    } else if (kind == kKindWindow) {
      window = float4(color, 1.0);
    }
  }
  reduce_interior_sums[index] = interior;
  reduce_window_sums[index] = window;
  reduce_inside_sums[index] = inside;
  GroupMemoryBarrierWithGroupSync();
  [unroll] for (uint step = kBlock * kBlock / 2u; step > 0u; step >>= 1u) {
    if (index < step) {
      reduce_interior_sums[index] += reduce_interior_sums[index + step];
      reduce_window_sums[index] += reduce_window_sums[index + step];
      reduce_inside_sums[index] += reduce_inside_sums[index + step];
    }
    GroupMemoryBarrierWithGroupSync();
  }
  if (index == 0u && all(group.xy < WindshieldLowSize())) {
    interior = reduce_interior_sums[0];
    window = reduce_window_sums[0];
    float share = 1.0 / max(reduce_inside_sums[0], 1.0);
    reduce_interior_out[group.xy] = float4(interior.rgb / max(interior.a, 1.0), interior.a * share);
    reduce_window_out[group.xy] = float4(window.rgb / max(window.a, 1.0), window.a * share);
  }
}

// ---------------------------------------------------------------------------
// windshield_frame: the mean interior share of the 1/8 texture, one group.

Texture2D<float4> frame_interior : register(t0);
RWTexture2D<float> frame_out : register(u0);

static const uint kFrameThreads = 256u;
groupshared float frame_sums[kFrameThreads];

[numthreads(256, 1, 1)]
void windshield_frame(uint3 id : SV_GroupThreadID) {
  uint2 size;
  frame_interior.GetDimensions(size.x, size.y);
  float sum = 0.0;
  for (uint i = id.x; i < size.x * size.y; i += kFrameThreads) {
    sum += frame_interior.Load(int3(i % size.x, i / size.x, 0)).a;
  }
  frame_sums[id.x] = sum;
  GroupMemoryBarrierWithGroupSync();
  [unroll] for (uint step = kFrameThreads / 2u; step > 0u; step >>= 1u) {
    if (id.x < step) {
      frame_sums[id.x] += frame_sums[id.x + step];
    }
    GroupMemoryBarrierWithGroupSync();
  }
  if (id.x == 0u) {
    frame_out[uint2(0, 0)] = frame_sums[0] / float(max(size.x * size.y, 1u));
  }
}

// ---------------------------------------------------------------------------
// windshield_light: the light the glass adds, per pixel of the 1/8 textures.

// An interior color (linear) that can be the dashboard: not lit from outside
// and not car paint (saturated).
bool WindshieldDashboard(float3 color) {
  float high = max(color.r, max(color.g, color.b));
  float low = min(color.r, min(color.g, color.b));
  return WindshieldLuma(color) <= WINDSHIELD_REFLECTION_HOOD && high - low <= 0.5 * high + 1.0e-5;
}

Texture2D<float4> light_interior : register(t0);
Texture2D<float4> light_window : register(t1);
Texture2D<float> light_frame : register(t2);
RWTexture2D<float4> light_out : register(u0);

[numthreads(8, 8, 1)]
void windshield_light(uint3 id : SV_DispatchThreadID) {
  uint2 low_size = WindshieldLowSize();
  if (any(id.xy >= low_size)) {
    return;
  }
  if (!WindshieldCockpit(light_frame.Load(int3(0, 0, 0)))) {
    light_out[id.xy] = 0.0;
    return;
  }
  float2 inverse_size = 1.0 / float2(low_size);
  // The dashboard's reflection. The interior below the window's lower edge
  // in this column starts with what is seen through the glass and lies under
  // the interior's mask too, such as the hood: car paint, lit from outside
  // (brighter than WINDSHIELD_REFLECTION_HOOD, linear luma) or colored. The
  // dashboard's top edge is the first interior pixel below that is dark and
  // nearly neutral (WindshieldDashboard); the interior as far
  // below that edge as this pixel is above it, squashed by
  // WINDSHIELD_REFLECTION_SQUASH (the dashboard top is seen at a grazing
  // angle), is reflected, fading out over WINDSHIELD_REFLECTION_HEIGHT of the
  // frame height.
  float3 reflection = 0.0;
  float reach = WINDSHIELD_REFLECTION_HEIGHT * float(low_size.y);
  if (light_window.Load(int3(id.xy, 0)).a > 0.0) {
    int edge = -1;
    int last = min(int(id.y) + int(ceil(reach)), int(low_size.y) - 1);
    for (int y = int(id.y) + 1; y <= last; ++y) {
      float4 interior = light_interior.Load(int3(id.x, y, 0));
      if (interior.a > 0.5 && WindshieldDashboard(interior.rgb)) {
        edge = y;
        break;
      }
    }
    if (edge >= 0) {
      float distance = float(edge) - float(id.y);
      float fade = saturate(1.0 - distance / max(reach, 1.0));
      float source = float(edge) + 0.5 + distance * WINDSHIELD_REFLECTION_SQUASH;
      // 3x2 taps: the glass is not a perfect mirror.
      float4 sum = 0.0;
      [unroll] for (int j = 0; j < 2; ++j) {
        [unroll] for (int i = -1; i <= 1; ++i) {
          float2 uv = float2(float(id.x) + 0.5 + float(i), source + float(j)) * inverse_size;
          float4 sample = light_interior.SampleLevel(xe_linear_clamp, uv, 0.0);
          // Only the dashboard: the hood below it never reflects.
          sample.a *= WindshieldDashboard(sample.rgb) ? 1.0 : 0.0;
          sum += float4(sample.rgb * sample.a, sample.a);
        }
      }
      reflection = sum.rgb / 6.0 * fade * fade;
    }
  }
  // Glare: the light above WINDSHIELD_GLARE_THRESHOLD seen through the glass
  // around this pixel (a golden angle spiral over WINDSHIELD_GLARE_RADIUS of
  // the frame height), weighted toward the center.
  static const int kTaps = 32;
  float radius = WINDSHIELD_GLARE_RADIUS * float(low_size.y);
  float glare = 0.0, weight_sum = 0.0;
  [loop] for (int i = 0; i < kTaps; ++i) {
    float t = (float(i) + 0.5) / float(kTaps);
    float angle = float(i) * 2.39996323;
    float2 offset = sqrt(t) * radius * float2(cos(angle), sin(angle));
    float2 uv = (float2(id.xy) + 0.5 + offset) * inverse_size;
    float4 window = light_window.SampleLevel(xe_linear_clamp, uv, 0.0);
    float weight = 1.0 - 0.75 * t;
    glare += max(WindshieldLuma(window.rgb) - WINDSHIELD_GLARE_THRESHOLD, 0.0) * window.a * weight;
    weight_sum += weight;
  }
  glare /= weight_sum * max(1.0 - WINDSHIELD_GLARE_THRESHOLD, 0.01);
  light_out[id.xy] = float4(reflection, glare);
}

// ---------------------------------------------------------------------------
// windshield_compose: one final pixel per thread.

Texture2D<float4> compose_final : register(t0);
Texture2D<float2> compose_kind : register(t1);
Texture2D<float4> compose_light : register(t2);
Texture2D<float> compose_frame : register(t3);
RWTexture2D<float4> compose_out : register(u0);

float WindshieldHash(float2 p) {
  float3 q = frac(float3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return frac((q.x + q.y) * q.z);
}

float WindshieldNoise(float2 p) {
  float2 cell = floor(p);
  float2 f = frac(p);
  float2 u = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(WindshieldHash(cell), WindshieldHash(cell + float2(1.0, 0.0)), u.x),
              lerp(WindshieldHash(cell + float2(0.0, 1.0)), WindshieldHash(cell + 1.0), u.x), u.y);
}

// Dust on the glass, fixed to the screen (the glass moves with the camera):
// smudges, soft round specks, and on the windshield (windshield, 0 to 1) the
// arcs of the wipers around two pivots below the frame; 0 to about 1.
float WindshieldDust(float2 pixel, float windshield) {
  // In frame heights, so the pattern is the same at every resolution.
  float2 p = pixel / float(xe_final_size.y);
  float aspect = float(xe_final_size.x) / float(xe_final_size.y);
  float arcs = 0.0;
  [unroll] for (int i = 0; i < 2; ++i) {
    float2 offset = p - float2(aspect * (i == 0 ? 0.3 : 0.75), 1.2);
    float radius = length(offset);
    float angle = atan2(offset.x, -offset.y);
    // Streaks along the arc: fine across the radius, long along the angle.
    arcs += WindshieldNoise(float2(radius * 90.0, angle * 4.0 + float(i) * 17.0));
  }
  float smudge = WindshieldNoise(p * 5.0 + 11.0) * WindshieldNoise(p * 13.0 + 3.0);
  // Specks: in one cell of 1/120 frame height out of about eight, a round dot
  // at a random place, a fifth to a third of the cell across.
  float2 cell_position = p * 120.0;
  float2 cell = floor(cell_position);
  float2 center = 0.25 + 0.5 * float2(WindshieldHash(cell + 0.37), WindshieldHash(cell + 7.11));
  float size = lerp(0.1, 0.17, WindshieldHash(cell + 3.73));
  float speck = WindshieldHash(cell) > 0.88
                    ? smoothstep(size, size * 0.4, length(cell_position - cell - center))
                    : 0.0;
  return saturate(arcs * 0.5 * smoothstep(0.05, 0.5, smudge) * windshield + smudge * 0.6 +
                  speck * 0.8);
}

// How much a final pixel looks forward through the windshield rather than
// through a side window: the horizontal angle of its view direction, full up
// to 30 degrees, none beyond 45.
float WindshieldForward(int2 scene_pixel) {
  float3 view = xe_scene_view_position(float2(scene_pixel) + 0.5, 1.0);
  float tangent = abs(view.x) / max(abs(view.z), 1.0e-5);
  return smoothstep(1.0, 0.577, tangent);
}

[numthreads(8, 8, 1)]
void windshield_compose(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= xe_final_size)) {
    return;
  }
  int2 pixel = int2(id.xy);
  bool cockpit = WindshieldCockpit(compose_frame.Load(int3(0, 0, 0)));
  if (!cockpit && WINDSHIELD_DEBUG != 1u) {
    compose_out[id.xy] = float4(0.0, 0.0, 0.0, -1.0);
    return;
  }
  float4 final = compose_final.Load(int3(pixel, 0));
  // Coverage: the window share of the 3x3 around the pixel weighted 1 2 1
  // (four bilinear taps between the pixels), so the edges of the pillars stay
  // soft against the scene jitter.
  float2 uv = (float2(pixel) + 0.5) / float2(xe_final_size);
  float2 half_pixel = 0.5 / float2(xe_final_size);
  float coverage = 0.25 * (compose_kind.SampleLevel(xe_linear_clamp, uv - half_pixel, 0.0).r +
                           compose_kind.SampleLevel(xe_linear_clamp, uv + half_pixel, 0.0).r +
                           compose_kind.SampleLevel(xe_linear_clamp, uv + float2(half_pixel.x, -half_pixel.y), 0.0).r +
                           compose_kind.SampleLevel(xe_linear_clamp, uv + float2(-half_pixel.x, half_pixel.y), 0.0).r);
  float2 own = compose_kind.Load(int3(pixel, 0));
  uint kind = own.r > 0.5 ? kKindWindow : own.g > 0.5 ? kKindInterior : kKindMask;
  if (WINDSHIELD_DEBUG == 1u) {
    float3 color = final.rgb;
    if (cockpit) {
      float3 mark = kind == kKindWindow ? float3(0.0, 1.0, 1.0)
                    : kind == kKindInterior ? float3(1.0, 0.15, 0.1) : float3(1.0, 0.0, 1.0);
      color = lerp(color, mark, 0.5);
    }
    if (all(id.xy < uint2(96, 6))) {
      color = cockpit ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    }
    compose_out[id.xy] = float4(color, final.a);
    return;
  }
  if (!cockpit || coverage <= 0.0) {
    // Left as it is, or dark behind debug views 2 and 3.
    bool dark = WINDSHIELD_DEBUG > 1u && cockpit;
    compose_out[id.xy] = dark ? float4(0.02, 0.0, 0.04, final.a) : float4(final.rgb, -1.0);
    return;
  }
  // The light of the 1/8 textures, smoothed by four bilinear taps.
  float2 tap = 0.75 * float(kBlock) / float2(xe_final_size);
  float4 light = 0.25 * (compose_light.SampleLevel(xe_linear_clamp, uv + tap * float2(-1.0, -1.0), 0.0) +
                         compose_light.SampleLevel(xe_linear_clamp, uv + tap * float2(1.0, -1.0), 0.0) +
                         compose_light.SampleLevel(xe_linear_clamp, uv + tap * float2(-1.0, 1.0), 0.0) +
                         compose_light.SampleLevel(xe_linear_clamp, uv + tap * float2(1.0, 1.0), 0.0));
  // Dust shows only in the glare.
  float dust = light.a > 0.0 || WINDSHIELD_DEBUG == 3u
                   ? WindshieldDust(float2(pixel) + 0.5, WindshieldForward(WindshieldScenePixel(pixel)))
                   : 0.0;
  float strength = WINDSHIELD;
  float3 linear_color = WindshieldLinear(final.rgb);
  float3 tinted = linear_color * lerp(1.0, WINDSHIELD_TINT_COLOR, saturate(WINDSHIELD_TINT * strength));
  float glare = light.a * (WINDSHIELD_GLARE + WINDSHIELD_DUST * dust);
  float3 added = (light.rgb * WINDSHIELD_REFLECTION + glare * WINDSHIELD_GLARE_COLOR) * strength;
  // Screen-like: added light fades toward white, it never clips harder.
  float3 glass = tinted + added * (1.0 - tinted);
  float3 color = lerp(linear_color, glass, coverage);
  if (WINDSHIELD_DEBUG == 2u) {
    color = saturate(added * 4.0 * coverage) + float3(0.02, 0.0, 0.04);
  } else if (WINDSHIELD_DEBUG == 3u) {
    color = lerp(float3(0.02, 0.0, 0.04), 1.0, dust * coverage);
  }
  compose_out[id.xy] = float4(WindshieldDisplay(color), final.a);
}

// ---------------------------------------------------------------------------
// windshield_output: the composed pixels into final_color; alpha below 0
// marks the pixels left as they are.

Texture2D<float4> output_color : register(t0);

float4 windshield_output(XePostPassVertex input) : SV_Target {
  float4 color = output_color.Load(int3(int2(input.position.xy), 0));
  if (color.a < 0.0) {
    discard;
  }
  return color;
}
