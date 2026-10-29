// The GT7 tone mapping operator for the look's tone curve gt7 (fm4-look.hlsli, FM4_LOOK_TONEMAP 4).
// Pure HLSL without the shader override library, so fm4_look_tonemap_tests
// (work/fm4-rex/tests/look-tonemap-tests.cpp) runs it on WARP against the sample's formulas.
#ifndef FM4_GT7_HLSLI
#define FM4_GT7_HLSLI

// GT7 tone mapping (Polyphony Digital, SIGGRAPH 2025 course "Physically Based Shading in
// Theory and Practice"): a port of the SDR path of its sample implementation
// gt7_tone_mapping.cpp 1.0, Copyright (c) 2025 Polyphony Digital Inc., MIT License (the
// notice: outputs/FM4-source-control/third-party-notices/PolyphonyDigital-GT7-tone-mapping-
// LICENSE.txt; docs/sources-and-licensing.md). Frame buffer values: 1 is
// 100 cd/m^2, and SDR white is the paper white of 250 cd/m^2 (2.5), which the output is
// divided by. The sample takes linear Rec. 2020, GT7's working space, where its emissive reds sit
// at the gamut's edge; FM4's working space is Rec. 709, so the operator takes its values as they
// are. Converted to Rec. 2020 first, FM4's reds sat inside that gamut and a bright one turned
// orange through the per-channel curve (a hue of 17 degrees at 8 times middle gray against 5:
// docs/analysis/look-presets-gt7-fm2023.md).
static const float FM4_GT7_PAPER_WHITE = 2.5;

// The GT tone mapping curve V2 with the sample's parameters (alpha 0.25, gray point 0.538,
// linear section 0.444, toe strength 1.28) at the SDR peak: a power toe blended into a
// linear middle, and from the end of the linear section the convergent shoulder
// kA + kB exp(x kC), whose slope there is 1. The output is limited to the peak later.
float3 fm4_gt7_curve(float3 x) {
  const float alpha = 0.25, mid = 0.538, linear_section = 0.444, toe = 1.28;
  const float peak = FM4_GT7_PAPER_WHITE;
  const float k = (linear_section - 1.0) / (alpha - 1.0);
  const float ka = peak * linear_section + peak * k;
  const float kb = -peak * k * exp(linear_section / k);
  const float kc = -1.0 / (k * peak);
  x = max(x, 0.0);
  float3 weight_linear = smoothstep(0.0, mid, x);
  float3 low = (1.0 - weight_linear) * mid * pow(x / mid, toe) + weight_linear * x;
  float3 shoulder = ka + kb * exp(x * kc);
  return x < linear_section * peak ? low : shoulder;
}

// SMPTE ST 2084 (PQ) between frame buffer values and its signal.
float3 fm4_pq_encode(float3 value) {
  const float m1 = 0.1593017578125, m2 = 78.84375;
  const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
  float3 y = pow(max(value, 0.0) * (100.0 / 10000.0), m1);
  return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

float3 fm4_pq_decode(float3 signal) {
  const float m1 = 0.1593017578125, m2 = 78.84375;
  const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
  float3 p = pow(saturate(signal), 1.0 / m2);
  return pow(max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1) * (10000.0 / 100.0);
}

// ICtCp (ITU-R BT.2100) of linear frame buffer values in the working space, and back.
float3 fm4_rgb_to_ictcp(float3 rgb) {
  float3 lms = mul(float3x3(1688.0, 2146.0, 262.0, 683.0, 2951.0, 462.0, 99.0, 309.0, 3688.0),
                   rgb) / 4096.0;
  float3 pq = fm4_pq_encode(lms);
  return float3(0.5 * pq.x + 0.5 * pq.y, dot(float3(6610.0, -13613.0, 7003.0), pq) / 4096.0,
                dot(float3(17933.0, -17390.0, -543.0), pq) / 4096.0);
}

float3 fm4_ictcp_to_rgb(float3 ictcp) {
  float3 pq = mul(float3x3(1.0, 0.00860904, 0.11102963, 1.0, -0.00860904, -0.11102963,
                           1.0, 0.56003134, -0.32062717), ictcp);
  float3 lms = fm4_pq_decode(pq);
  return mul(float3x3(3.43660669, -2.50645212, 0.06984542, -0.79132956, 1.98360045, -0.1922709,
                      -0.0259499, -0.09891371, 1.12486361), lms);
}

// The GT7 operator on a scene value whose middle gray (0.18 out) is 0.1806: per channel the
// curve ("skewed" color); in ICtCp the intensity of that and the chroma of the input, faded out
// from 0.98 to 1.16 times the intensity of the peak; 0.4 of the first and 0.6 of the second,
// limited to the peak and scaled to SDR (paper white 1).
float3 fm4_curve_gt7(float3 x) {
  float3 rgb = x * FM4_GT7_PAPER_WHITE;
  float3 ucs = fm4_rgb_to_ictcp(rgb);
  float3 skewed = fm4_gt7_curve(rgb);
  float3 skewed_ucs = fm4_rgb_to_ictcp(skewed);
  float peak_i = fm4_pq_encode(FM4_GT7_PAPER_WHITE.xxx).x;
  float chroma = 1.0 - smoothstep(0.98, 1.16, ucs.x / peak_i);
  float3 scaled = fm4_ictcp_to_rgb(float3(skewed_ucs.x, ucs.yz * chroma));
  return min(lerp(skewed, scaled, 0.6), FM4_GT7_PAPER_WHITE) / FM4_GT7_PAPER_WHITE;
}

#endif  // FM4_GT7_HLSLI
