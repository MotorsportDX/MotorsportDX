// fm4_look_tonemap_tests <look directory>: the look's GT7 tone curve (fm4-gt7.hlsli, the HLSL
// port of Polyphony Digital's MIT-licensed gt7_tone_mapping.cpp 1.0) run on WARP (CPU Direct3D
// 11) against a double precision reference written from the sample's formulas: the GT tone
// mapping curve V2 with the sample's parameters, SMPTE ST 2084, ICtCp with the sample's
// constants, the chroma fade and the blend, and the SDR paper white of 250 cd/m^2, on FM4's
// working space as the port feeds it. Checks the toe, the linear section, the shoulder, the
// clip point, middle gray, and that a bright saturated red (a brake light) keeps its hue
// instead of turning orange, as it did with a conversion to Rec. 2020 around the operator.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const std::string& what) {
  std::printf("%s %s\n", condition ? "ok  " : "FAIL", what.c_str());
  if (!condition) ++failures;
}

using Vec = std::array<double, 3>;

Vec Mul(const double m[3][3], const Vec& v) {
  return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
          m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
          m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}

// The sample's SDR operator: frame buffer values (1 is 100 cd/m^2), the peak at the paper white.
constexpr double kPeak = 250.0 / 100.0;

double SmoothStep(double x, double edge0, double edge1) {
  const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// GTToneMappingCurveV2::evaluateCurve with initializeCurve(peak, 0.25, 0.538, 0.444, 1.280).
double Curve(double x) {
  const double alpha = 0.25, mid = 0.538, linear_section = 0.444, toe = 1.280;
  const double k = (linear_section - 1.0) / (alpha - 1.0);
  const double ka = kPeak * linear_section + kPeak * k;
  const double kb = -kPeak * k * std::exp(linear_section / k);
  const double kc = -1.0 / (k * kPeak);
  if (x < 0.0) return 0.0;
  const double weight_linear = SmoothStep(x, 0.0, mid);
  const double weight_toe = 1.0 - weight_linear;
  const double shoulder = ka + kb * std::exp(x * kc);
  if (x < linear_section * kPeak) {
    const double toe_mapped = mid * std::pow(x / mid, toe);
    return weight_toe * toe_mapped + weight_linear * x;
  }
  return shoulder;
}

double PqEncode(double n) {
  const double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625,
               c3 = 18.6875;
  const double y = std::pow(std::max(n, 0.0) * 100.0 / 10000.0, m1);
  return std::pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

double PqDecode(double n) {
  const double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625,
               c3 = 18.6875;
  const double np = std::pow(std::clamp(n, 0.0, 1.0), 1.0 / m2);
  const double l = std::max(np - c1, 0.0) / (c2 - c3 * np);
  return std::pow(l, 1.0 / m1) * 10000.0 / 100.0;
}

Vec RgbToIctcp(const Vec& rgb) {
  const double l = (rgb[0] * 1688.0 + rgb[1] * 2146.0 + rgb[2] * 262.0) / 4096.0;
  const double m = (rgb[0] * 683.0 + rgb[1] * 2951.0 + rgb[2] * 462.0) / 4096.0;
  const double s = (rgb[0] * 99.0 + rgb[1] * 309.0 + rgb[2] * 3688.0) / 4096.0;
  const double lp = PqEncode(l), mp = PqEncode(m), sp = PqEncode(s);
  return {(2048.0 * lp + 2048.0 * mp) / 4096.0, (6610.0 * lp - 13613.0 * mp + 7003.0 * sp) / 4096.0,
          (17933.0 * lp - 17390.0 * mp - 543.0 * sp) / 4096.0};
}

Vec IctcpToRgb(const Vec& ictcp) {
  const double l = PqDecode(ictcp[0] + 0.00860904 * ictcp[1] + 0.11103 * ictcp[2]);
  const double m = PqDecode(ictcp[0] - 0.00860904 * ictcp[1] - 0.11103 * ictcp[2]);
  const double s = PqDecode(ictcp[0] + 0.560031 * ictcp[1] - 0.320627 * ictcp[2]);
  return {3.43661 * l - 2.50645 * m + 0.0698454 * s, -0.79133 * l + 1.9836 * m - 0.192271 * s,
          -0.0259499 * l - 0.0989137 * m + 1.12486 * s};
}

// GT7ToneMapping::applyToneMapping after initializeAsSDR: frame buffer values in, SDR out.
Vec ApplySdr(const Vec& rgb) {
  const double target_i = RgbToIctcp({kPeak, kPeak, kPeak})[0];
  const Vec ucs = RgbToIctcp(rgb);
  const Vec skewed = {Curve(rgb[0]), Curve(rgb[1]), Curve(rgb[2])};
  const Vec skewed_ucs = RgbToIctcp(skewed);
  const double chroma = 1.0 - SmoothStep(ucs[0] / target_i, 0.98, 1.16);
  const Vec scaled = IctcpToRgb({skewed_ucs[0], ucs[1] * chroma, ucs[2] * chroma});
  Vec out;
  for (int i = 0; i < 3; ++i)
    out[i] = (1.0 / kPeak) * std::min(0.4 * skewed[i] + 0.6 * scaled[i], kPeak);
  return out;
}

// The port's integration (fm4_curve_gt7): the exposed scene, whose middle gray the look scales
// to 0.1806, as frame buffer values with that gray at 2.5 times its value, in FM4's working space
// (Rec. 709). With_2020 converts it to Rec. 2020 and back around the operator, the port's first
// version, for the hue check's contrast.
const double k709To2020[3][3] = {{0.6274040, 0.3292820, 0.0433136},
                                 {0.0690970, 0.9195400, 0.0113612},
                                 {0.0163916, 0.0880132, 0.8955950}};
const double k2020To709[3][3] = {{1.6604903, -0.58763911, -0.0728516},
                                 {-0.12454997, 1.13289992, -0.00834796},
                                 {-0.01815112, -0.10057872, 1.11872988}};

Vec Reference(const Vec& x, bool with_2020 = false) {
  Vec rgb = with_2020 ? Mul(k709To2020, x) : x;
  for (double& c : rgb) c *= kPeak;
  const Vec out = ApplySdr(rgb);
  return with_2020 ? Mul(k2020To709, out) : out;
}

double HueDegrees(const Vec& c) {
  const double h = std::atan2(std::sqrt(3.0) * (c[1] - c[2]), 2.0 * c[0] - c[1] - c[2]);
  return h * 180.0 / 3.14159265358979;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

template <typename T>
struct Ref {
  T* p = nullptr;
  ~Ref() {
    if (p) p->Release();
  }
  T** operator&() { return &p; }
  T* operator->() { return p; }
  operator T*() { return p; }
};

// fm4_curve_gt7 and fm4_gt7_curve of the HLSL on WARP: float4 inputs (w 1 picks the per-channel
// curve alone, without the clamp to the peak), float4 outputs.
bool RunHlsl(const std::string& library, const std::vector<std::array<float, 4>>& inputs,
             std::vector<std::array<float, 4>>& outputs) {
  const std::string source = library + R"(
StructuredBuffer<float4> look_in : register(t0);
RWStructuredBuffer<float4> look_out : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  float4 v = look_in[id.x];
  look_out[id.x] = float4(v.w > 0.5 ? fm4_gt7_curve(v.xyz) : fm4_curve_gt7(v.xyz), 1.0);
}
)";
  Ref<ID3DBlob> code, errors;
  if (FAILED(D3DCompile(source.data(), source.size(), "look-tonemap-test.hlsl", nullptr, nullptr,
                        "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
    std::printf("FAIL FXC: %s\n",
                errors.p ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
    return false;
  }
  Ref<ID3D11Device> device;
  Ref<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                               D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("FAIL no WARP device\n");
    return false;
  }
  Ref<ID3D11ComputeShader> shader;
  device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
  const UINT count = static_cast<UINT>(inputs.size());
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = count * 16;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  desc.StructureByteStride = 16;
  D3D11_SUBRESOURCE_DATA data = {inputs.data()};
  Ref<ID3D11Buffer> in_buffer, out_buffer, readback;
  device->CreateBuffer(&desc, &data, &in_buffer);
  desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  device->CreateBuffer(&desc, nullptr, &out_buffer);
  D3D11_BUFFER_DESC staging = {};
  staging.ByteWidth = count * 16;
  staging.Usage = D3D11_USAGE_STAGING;
  staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  device->CreateBuffer(&staging, nullptr, &readback);
  Ref<ID3D11ShaderResourceView> srv;
  Ref<ID3D11UnorderedAccessView> uav;
  device->CreateShaderResourceView(in_buffer, nullptr, &srv);
  device->CreateUnorderedAccessView(out_buffer, nullptr, &uav);
  if (!shader.p || !srv.p || !uav.p || !readback.p) {
    std::printf("FAIL could not create the test's resources\n");
    return false;
  }
  ID3D11ShaderResourceView* srvs[] = {srv};
  ID3D11UnorderedAccessView* uavs[] = {uav};
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetShaderResources(0, 1, srvs);
  context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
  context->Dispatch(count, 1, 1);
  context->CopyResource(readback, out_buffer);
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (FAILED(context->Map(readback, 0, D3D11_MAP_READ, 0, &mapped))) return false;
  outputs.resize(count);
  std::memcpy(outputs.data(), mapped.pData, count * 16);
  context->Unmap(readback, 0);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: fm4_look_tonemap_tests <look directory>\n");
    return 2;
  }
  const std::string library = ReadFile(std::filesystem::path(argv[1]) / "fm4-gt7.hlsli");
  Check(!library.empty(), "fm4-gt7.hlsli read");

  // Neutral grays through the operator (normalized: middle gray 0.1806), from the toe to past
  // the clip point (1.4717), and per channel through the curve alone (frame buffer values).
  const std::vector<double> grays = {0.004, 0.02, 0.05, 0.1, 0.1806, 0.3, 0.44, 0.6, 0.9, 1.2,
                                     1.46, 1.5, 3.0};
  const std::vector<double> curve_points = {0.01, 0.1, 0.4, 0.538, 0.8, 1.11, 1.2, 2.0, 3.6, 5.0};
  // Bright saturated reds (a brake light at 1, 4 and 16 times middle gray's scale) and other
  // saturated colors.
  const std::vector<Vec> colors = {{1.0, 0.02, 0.02}, {4.0, 0.08, 0.04}, {16.0, 0.3, 0.15},
                                   {0.3, 0.6, 1.4},   {0.05, 0.8, 0.1},  {2.0, 1.2, 0.05}};
  std::vector<std::array<float, 4>> inputs;
  for (double g : grays) inputs.push_back({float(g), float(g), float(g), 0.0f});
  for (double x : curve_points) inputs.push_back({float(x), float(x), float(x), 1.0f});
  for (const Vec& c : colors) inputs.push_back({float(c[0]), float(c[1]), float(c[2]), 0.0f});
  std::vector<std::array<float, 4>> outputs;
  if (!RunHlsl(library, inputs, outputs)) return 1;

  size_t i = 0;
  char text[256];
  for (double g : grays) {
    const Vec want = Reference({g, g, g});
    const auto& got = outputs[i++];
    const double error = std::max({std::abs(got[0] - want[0]), std::abs(got[1] - want[1]),
                                   std::abs(got[2] - want[2])});
    std::snprintf(text, sizeof(text), "gray %.4f: HLSL %.5f, reference %.5f (error %.1e)", g,
                  got[0], want[0], error);
    Check(error < 2e-3, text);
  }
  for (double x : curve_points) {
    const double want = Curve(x);
    const auto& got = outputs[i++];
    std::snprintf(text, sizeof(text), "curve at %.3f: HLSL %.5f, reference %.5f", x, got[0], want);
    Check(std::abs(got[0] - want) < 1e-4 * std::max(1.0, want), text);
  }
  // The sections of the curve as the sample's formulas give them.
  Check(std::abs(Reference({0.1806, 0.1806, 0.1806})[0] - 0.18) < 1e-3,
        "middle gray 0.1806 maps to 0.18 (the look's anchor)");
  Check(std::abs(Reference({0.3, 0.3, 0.3})[0] - 0.3) < 1e-6, "the linear section maps 0.3 to 0.3");
  Check(Reference({0.02, 0.02, 0.02})[0] < 0.02 * 0.8, "the toe darkens 0.02 by more than a fifth");
  Check(std::abs(Reference({1.46, 1.46, 1.46})[0] - 1.0) > 1e-3 &&
            std::abs(Reference({1.48, 1.48, 1.48})[0] - 1.0) < 1e-6,
        "the shoulder clips at 1.4717");
  Check(std::abs(Curve(0.444 * kPeak) - 0.444 * kPeak) < 1e-9 &&
            std::abs((Curve(0.444 * kPeak + 1e-6) - Curve(0.444 * kPeak)) / 1e-6 - 1.0) < 1e-3,
        "the shoulder meets the line with slope 1");

  for (const Vec& c : colors) {
    const Vec want = Reference(c);
    const auto& got = outputs[i++];
    const double error = std::max({std::abs(got[0] - want[0]), std::abs(got[1] - want[1]),
                                   std::abs(got[2] - want[2])});
    std::snprintf(text, sizeof(text), "color (%.2f, %.2f, %.2f): HLSL (%.4f, %.4f, %.4f), "
                  "reference (%.4f, %.4f, %.4f)", c[0], c[1], c[2], got[0], got[1], got[2],
                  want[0], want[1], want[2]);
    Check(error < 3e-3, text);
  }
  // The brake light: a saturated red from 1 to 16 times middle gray's scale keeps a red hue
  // (under 10 degrees; orange is about 20 to 40) while the chroma fade takes the brightest toward
  // white. Through Rec. 2020 it turned orange from about 6 times on.
  for (double scale : {1.0, 2.0, 4.0, 8.0, 16.0}) {
    const Vec red = {scale, 0.02 * scale, 0.01 * scale};
    const Vec out = Reference(red);
    const Vec through_2020 = Reference(red, true);
    std::snprintf(text, sizeof(text),
                  "brake light at %2.0f times stays red: (%.3f, %.3f, %.3f), hue %.1f degrees "
                  "(through Rec. 2020: hue %.1f)",
                  scale, out[0], out[1], out[2], HueDegrees(out), HueDegrees(through_2020));
    Check(HueDegrees(out) < 10.0 && out[0] >= out[1] && out[1] >= out[2], text);
  }
  Check(HueDegrees(Reference({8.0, 0.16, 0.08}, true)) > 15.0,
        "the check catches the orange of the conversion to Rec. 2020 at 8 times");

  std::printf("look tone curve tests: %s\n", failures ? "FAILED" : "passed");
  return failures ? 1 : 0;
}
