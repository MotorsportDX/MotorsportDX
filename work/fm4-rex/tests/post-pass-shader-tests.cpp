// Tests of the post passes (work/fm4-rex/passes) on WARP, the CPU Direct3D 12
// device: no GPU is used. The compute passes of the manifest's scene_depth
// point run as the runtime's post pass framework (post_pass.cpp) runs them:
// compiled with FXC against the library post_pass.hlsli, with its root
// signature and constant layouts (post_pass_layout.h), the textures, params,
// defines, bindings and conditions of passes.toml. Their input is a synthetic
// scene traced into a stored depth texture the way FM4 stores it (reversed Z
// through the viewport), with a stencil of object IDs the way FM4 writes it (0
// for the static scene, 7n for car n, 128 under shadows). The GTAO occlusion
// (the texture in slot 0) is checked where the answer is known: sky and open
// ground without occlusion, the corner of a wall and the ground, and the
// contact of a box, darker; and its W debug view (GTAO_DEBUG 1) against the
// traced W. The motion vectors (the texture in slot 1) are checked against the
// traced view positions reprojected on the CPU, with the camera and the box
// (object slot 1) moving, and zero without a valid previous frame; so are the
// pixel kinds FM4 adds: an interior under a mask at the stored depth 1.0 (car
// 3, its own matrix at MOTION_INTERIOR_W), a mask without an interior (no
// motion known), and ground seen through car 1's glass (car 1's ID on ground
// far from the box: the world's motion); with the motion mask (slot 2), also
// in a frame without the interior, where the whole mask has no motion known.
// The same scene rendered with the runtime's jitter (the content moved by a
// sub-pixel offset, xe_scene_jitter) gives the motion between unjittered
// positions: that of the reprojection with the camera moving, and none with a
// still camera. Their debug views run, and the passes of the other points are
// compiled.
//
// usage: fm4_post_pass_shader_tests <post_pass.hlsli> <passes directory>
//                                   [occlusion.pgm] [NAME=VALUE ...]
// NAME=VALUE sets params and defines over passes.toml, as shader_mod_defines
// does in the runtime.

#include <windows.h>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

#include <rex/graphics/d3d12/post_pass_camera.h>
#include <rex/graphics/d3d12/post_pass_jitter.h>
#include <rex/graphics/d3d12/post_pass_layout.h>

using Microsoft::WRL::ComPtr;
namespace post_pass = rex::graphics::d3d12::post_pass;

namespace {

constexpr uint32_t kWidth = 640, kHeight = 360;
int failures = 0;

void Check(bool condition, const char* what, double value) {
  std::printf("%s %s (%.3f)\n", condition ? "ok  " : "FAIL", what, value);
  if (!condition) ++failures;
}

bool Succeeded(HRESULT result, const char* what) {
  if (FAILED(result)) {
    std::printf("FAIL %s: 0x%08X\n", what, unsigned(result));
    ++failures;
    return false;
  }
  return true;
}

bool ReadFile(const std::filesystem::path& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

// The scene, in view space (left-handed, +Z forward, +Y up): ground at y =
// -1.5, a wall at z = 14 for x > 1, a 1 m box on the ground at x -3..-2,
// z 7..8. Returns the view distance along the ray, or 0 for the sky.
double Trace(double dx, double dy, double dz, double& hit_y_out, int& surface_out) {
  double best = 0.0;
  surface_out = 0;
  auto consider = [&](double t, int surface) {
    if (t > 0.0 && (best == 0.0 || t < best)) {
      best = t;
      surface_out = surface;
    }
  };
  if (dy < 0.0) {
    double t = -1.5 / dy;
    if (t * dz < 400.0) consider(t, 1);  // Ground (to 400 m).
  }
  if (dz > 0.0) {
    double t = 14.0 / dz;
    if (t * dx > 1.0 && t * dy > -1.5 && t * dy < 4.0) consider(t, 2);  // Wall.
  }
  // Box faces: front (z = 7), top (y = -0.5), side (x = -2).
  if (dz > 0.0) {
    double t = 7.0 / dz, x = t * dx, y = t * dy;
    if (x > -3.0 && x < -2.0 && y > -1.5 && y < -0.5) consider(t, 3);
  }
  if (dy < 0.0) {
    double t = -0.5 / dy, x = t * dx, z = t * dz;
    if (x > -3.0 && x < -2.0 && z > 7.0 && z < 8.0) consider(t, 3);
  }
  if (dx < 0.0) {
    double t = -2.0 / dx, y = t * dy, z = t * dz;
    if (y > -1.5 && y < -0.5 && z > 7.0 && z < 8.0) consider(t, 3);
  }
  hit_y_out = best * dy;
  return best;
}

// Serves the library and the files of the passes directory.
class Include final : public ID3DInclude {
 public:
  Include(std::string library, std::filesystem::path directory)
      : library_(std::move(library)), directory_(std::move(directory)) {}
  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data,
                         UINT* bytes) noexcept override {
    std::string text;
    if (!std::strcmp(name, "xe_post_pass.hlsli")) {
      text = library_;
    } else if (!ReadFile(directory_ / name, text)) {
      return E_FAIL;
    }
    std::string& stored = files_.emplace_back(std::move(text));
    *data = stored.data();
    *bytes = UINT(stored.size());
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID data) noexcept override {
    files_.remove_if([data](const std::string& text) { return text.data() == data; });
    return S_OK;
  }

 private:
  std::string library_;
  std::filesystem::path directory_;
  std::list<std::string> files_;
};

struct Param {
  std::string name;
  bool is_uint = false;
  double value = 0.0, min = -1.0e30, max = 1.0e30;
  std::string focal_length_of;
};
struct Texture {
  std::string name;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  uint32_t mips = 1;
  int slot = -1;
  ComPtr<ID3D12Resource> resource;
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  // Heap indices: all mips, each mip (SRV), each mip (UAV).
  uint32_t srv = 0, srv_mips[8] = {}, uavs[8] = {};
};
struct Binding {
  std::string texture;  // or scene_depth, scene_stencil
  int mip = -1;
};
struct Pass {
  std::string name, shader, entry;
  std::vector<std::pair<std::string, std::string>> defines;
  std::vector<Binding> srvs, uavs;
  uint32_t group[2] = {8, 8};
  std::string condition;
  ComPtr<ID3D12PipelineState> pipeline;
};

// A 4x4 transform of a point, in double.
void Transform(const double m[4][4], const double in[4], double out[4]) {
  for (int i = 0; i < 4; ++i) {
    out[i] = m[i][0] * in[0] + m[i][1] * in[1] + m[i][2] * in[2] + m[i][3] * in[3];
  }
}

// A rigid motion of view space: a turn about Y by yaw radians, then a move.
void RigidMotion(double yaw, double tx, double ty, double tz, double out[4][4]) {
  const double c = std::cos(yaw), s = std::sin(yaw);
  const double m[4][4] = {{c, 0.0, s, tx}, {0.0, 1.0, 0.0, ty}, {-s, 0.0, c, tz},
                          {0.0, 0.0, 0.0, 1.0}};
  std::memcpy(out, m, sizeof(m));
}

void MultiplyMatrices(const double a[4][4], const double b[4][4], double out[4][4]) {
  double r[4][4];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
    }
  }
  std::memcpy(out, r, sizeof(r));
}

// The terms of an if (NAME OP NUMBER [&& ...]) on the param and define values.
bool ConditionHolds(const std::string& condition,
                    const std::map<std::string, double>& values, bool& valid) {
  valid = true;
  size_t start = 0;
  bool holds = true;
  while (start <= condition.size()) {
    size_t end = condition.find("&&", start);
    std::string term = condition.substr(start, end == std::string::npos ? end : end - start);
    char name[64] = {}, op[3] = {};
    double number = 0.0;
    if (std::sscanf(term.c_str(), " %63[A-Z_0-9] %2[<>=!] %lf", name, op, &number) != 3 ||
        !values.count(name)) {
      valid = false;
      return false;
    }
    double value = values.at(name);
    std::string o = op;
    holds &= o == "==" ? value == number
             : o == "!=" ? value != number
             : o == "<"  ? value < number
             : o == "<=" ? value <= number
             : o == ">"  ? value > number
                         : value >= number;
    if (end == std::string::npos) break;
    start = end + 2;
  }
  return holds;
}

Binding ParseBinding(const std::string& text) {
  Binding binding;
  size_t colon = text.find(':');
  binding.texture = text.substr(0, colon);
  if (colon != std::string::npos) binding.mip = std::atoi(text.c_str() + colon + 1);
  return binding;
}

std::string DefineText(const toml::node& node) {
  if (auto value = node.value_exact<int64_t>()) return std::to_string(*value);
  if (auto value = node.value_exact<double>()) return std::to_string(*value);
  if (auto value = node.value_exact<bool>()) return *value ? "1" : "0";
  return node.value_or(std::string());
}

double Number(const toml::node* node, double fallback) {
  if (!node) return fallback;
  if (auto value = node->value_exact<int64_t>()) return double(*value);
  if (auto value = node->value_exact<double>()) return *value;
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: fm4_post_pass_shader_tests <post_pass.hlsli> <passes directory> "
                "[occlusion.pgm] [NAME=VALUE ...]\n");
    return 2;
  }
  std::string library, manifest_text;
  std::filesystem::path directory = argv[2];
  if (!ReadFile(argv[1], library) || !ReadFile(directory / "passes.toml", manifest_text)) {
    std::printf("FAIL cannot read %s or %s\n", argv[1],
                (directory / "passes.toml").string().c_str());
    return 1;
  }
  const char* image_path = nullptr;
  std::map<std::string, std::string> overrides;
  for (int i = 3; i < argc; ++i) {
    std::string argument = argv[i];
    size_t equals = argument.find('=');
    if (equals == std::string::npos) {
      image_path = argv[i];
    } else {
      overrides[argument.substr(0, equals)] = argument.substr(equals + 1);
    }
  }

  // The manifest: defines, params, textures and the scene_depth passes.
  toml::table manifest;
  try {
    manifest = toml::parse(std::string_view(manifest_text), std::string_view("passes.toml"));
  } catch (const toml::parse_error& error) {
    std::printf("FAIL passes.toml line %u: %s\n", unsigned(error.source().begin.line),
                std::string(error.description()).c_str());
    return 1;
  }
  std::vector<std::pair<std::string, std::string>> defines;
  if (const toml::table* table = manifest["defines"].as_table()) {
    for (auto&& [key, node] : *table) {
      std::string name(key.str());
      defines.emplace_back(name, overrides.count(name) ? overrides[name] : DefineText(node));
    }
  }
  std::vector<Param> params;
  if (const toml::table* table = manifest["params"].as_table()) {
    for (auto&& [key, node] : *table) {
      Param param;
      param.name = key.str();
      const toml::node* value = &node;
      if (const toml::table* fields = node.as_table()) {
        value = (*fields)["value"].node();
        param.min = Number((*fields)["min"].node(), param.min);
        param.max = Number((*fields)["max"].node(), param.max);
        param.focal_length_of = (*fields)["focal_length_of"].value_or(std::string());
      }
      param.is_uint = value && value->is_integer();
      param.value = Number(value, 0.0);
      if (overrides.count(param.name)) param.value = std::atof(overrides[param.name].c_str());
      params.push_back(param);
    }
  }
  if (params.size() > post_pass::kMaxParams) {
    std::printf("FAIL more than %u params\n", post_pass::kMaxParams);
    return 1;
  }
  std::vector<Texture> textures;
  if (const toml::table* table = manifest["textures"].as_table()) {
    for (auto&& [key, node] : *table) {
      const toml::table& fields = *node.as_table();
      Texture texture;
      texture.name = key.str();
      std::string format = fields["format"].value_or(std::string());
      texture.format = format == "R32_FLOAT"        ? DXGI_FORMAT_R32_FLOAT
                       : format == "R8_UNORM"       ? DXGI_FORMAT_R8_UNORM
                       : format == "R16_FLOAT"      ? DXGI_FORMAT_R16_FLOAT
                       : format == "R8G8_UNORM"     ? DXGI_FORMAT_R8G8_UNORM
                       : format == "R16G16_FLOAT"   ? DXGI_FORMAT_R16G16_FLOAT
                       : format == "R8G8B8A8_UNORM" ? DXGI_FORMAT_R8G8B8A8_UNORM
                                                    : DXGI_FORMAT_UNKNOWN;
      texture.mips = uint32_t(Number(fields["mips"].node(), 1.0));
      texture.slot = int(Number(fields["slot"].node(), -1.0));
      if (texture.format == DXGI_FORMAT_UNKNOWN) {
        std::printf("FAIL texture %s: format %s is not one this test creates\n",
                    texture.name.c_str(), format.c_str());
        return 1;
      }
      textures.push_back(std::move(texture));
    }
  }
  auto find_texture = [&textures](const std::string& name) -> Texture* {
    for (Texture& texture : textures) {
      if (texture.name == name) return &texture;
    }
    return nullptr;
  };
  // The passes of the other points are only compiled.
  std::vector<Pass> passes, compiled_only;
  std::vector<bool> compiled_only_pixel;
  if (const toml::array* array = manifest["passes"].as_array()) {
    for (const toml::node& node : *array) {
      const toml::table& fields = *node.as_table();
      Pass pass;
      pass.name = fields["name"].value_or(std::string());
      pass.shader = fields["shader"].value_or(std::string());
      pass.entry = fields["entry"].value_or(std::string("main"));
      pass.condition = fields["if"].value_or(std::string());
      if (const toml::table* pass_defines = fields["defines"].as_table()) {
        for (auto&& [key, value] : *pass_defines) {
          pass.defines.emplace_back(std::string(key.str()), DefineText(value));
        }
      }
      bool pixel = fields["type"].value_or(std::string("compute")) == "pixel";
      if (fields["point"].value_or(std::string()) != "scene_depth") {
        compiled_only.push_back(std::move(pass));
        compiled_only_pixel.push_back(pixel);
        continue;
      }
      if (pixel) {
        std::printf("FAIL pass %s: only compute passes run here\n", pass.name.c_str());
        return 1;
      }
      for (auto [list, bindings] : {std::pair{"srv", &pass.srvs}, std::pair{"uav", &pass.uavs}}) {
        if (const toml::array* names = fields[list].as_array()) {
          for (const toml::node& name : *names) {
            bindings->push_back(ParseBinding(name.value_or(std::string())));
          }
        }
      }
      if (const toml::array* group = fields["group"].as_array()) {
        pass.group[0] = uint32_t(Number(group->get(0), 8.0));
        pass.group[1] = uint32_t(Number(group->get(1), 8.0));
      }
      passes.push_back(std::move(pass));
    }
  }
  std::printf("passes.toml: %zu scene_depth passes, %zu textures, %zu params, %zu defines\n",
              passes.size(), textures.size(), params.size(), defines.size());

  // Camera: 60 degrees vertical field of view, near 0.25, far 1000, the guest
  // viewport of 1280x720 with Z reversed (FM4), rendered at half resolution
  // (guest pixels per host pixel 2, like the inverse of a 0.5x scale).
  const double fov_y = 60.0 * 3.14159265358979 / 180.0, aspect = 16.0 / 9.0;
  const double near_z = 0.25, far_z = 1000.0, q = far_z / (far_z - near_z);
  post_pass::Matrix projection = {};
  projection.m[0][0] = float(1.0 / std::tan(fov_y * 0.5) / aspect);
  projection.m[1][1] = float(1.0 / std::tan(fov_y * 0.5));
  projection.m[2][2] = float(q);
  projection.m[2][3] = float(-q * near_z);
  projection.m[3][2] = 1.0f;
  const post_pass::Viewport viewport = {640.0f, 640.0f, -360.0f, 360.0f, -1.0f, 1.0f};
  post_pass::Unpack unpack;
  if (!post_pass::UnpackFromProjection(projection, viewport, unpack)) {
    std::printf("FAIL unpack constants\n");
    return 1;
  }
  const float guest_per_host = 1280.0f / float(kWidth);

  // Stored depth per host pixel center (Direct3D 9 pixel centers: guest
  // screen = (host + 0.5) * 2 - 0.5), or with a jitter at the center less the
  // jitter (the content at host position p is the unjittered scene at p -
  // jitter, as the runtime's jitter moves the scene draws).
  struct Scene {
    std::vector<float> depth;
    std::vector<int> surface;
    std::vector<double> hit_y, view_w, view_x, view_y;
    std::vector<uint32_t> stencil;
  };
  auto trace_scene = [&](double jitter_x, double jitter_y, Scene& scene) {
    scene.depth.assign(kWidth * kHeight, 0.0f);
    scene.surface.assign(kWidth * kHeight, 0);
    for (auto* values : {&scene.hit_y, &scene.view_w, &scene.view_x, &scene.view_y}) {
      values->assign(kWidth * kHeight, 0.0);
    }
    scene.stencil.assign(kWidth * kHeight, 0u);
    std::vector<float>& depth = scene.depth;
    std::vector<int>& surface = scene.surface;
    std::vector<double> &hit_y = scene.hit_y, &view_w = scene.view_w;
    std::vector<double> &view_x = scene.view_x, &view_y = scene.view_y;
    // Object IDs: the box is car 1 (7), the wall car 2 (14), the ground beyond
    // 10 m on the right is under a shadow (128), the rest is the track (0).
    std::vector<uint32_t>& stencil = scene.stencil;
    for (uint32_t y = 0; y < kHeight; ++y) {
      for (uint32_t x = 0; x < kWidth; ++x) {
        double gx = (x + 0.5 - jitter_x) * guest_per_host - 0.5,
               gy = (y + 0.5 - jitter_y) * guest_per_host - 0.5;
        double ndc_x = (gx - viewport.x_offset) / viewport.x_scale;
        double ndc_y = (gy - viewport.y_offset) / viewport.y_scale;
        // View direction with z = 1 (P has no off-center terms here).
        double dx = ndc_x / projection.m[0][0], dy = ndc_y / projection.m[1][1];
        uint32_t i = y * kWidth + x;
        double t = Trace(dx, dy, 1.0, hit_y[i], surface[i]);
        double ndc_z = t > 0.0 ? q - q * near_z / t : 1.0;  // Sky at the far plane.
        depth[i] = float(ndc_z * viewport.z_scale + viewport.z_offset);
        view_w[i] = t > 0.0 ? t : far_z;
        view_x[i] = view_w[i] * dx;
        view_y[i] = view_w[i] * dy;
        stencil[i] = surface[i] == 3   ? 7u
                     : surface[i] == 2 ? 14u
                     : surface[i] == 1 && view_w[i] > 10.0 && view_x[i] > 0.0 ? 128u
                                                                                : 0u;
        // In the sky band at the top: an interior of car 3 (21) under FM4's mask
        // at the stored depth 1.0 on the left, the mask without an interior on
        // the right, half of it at a W 0.5% beyond the near plane (within the 1%
        // the pass takes as a mask, as the mirror panel of the hood and bumper
        // cameras reads).
        if (y >= 8 && y < 48 && ((x >= 20 && x < 140) || (x >= 480 && x < 600))) {
          surface[i] = x < 140 ? 5 : 6;
          stencil[i] = x < 140 ? 21u : 0u;
          depth[i] = x < 540 ? 1.0f : 0.995f;
          view_w[i] = near_z;
          view_x[i] = near_z * dx;
          view_y[i] = near_z * dy;
        }
        // Ground 11.5 to 13 m ahead with car 1's ID: the world through its glass.
        if (surface[i] == 1 && view_w[i] > 11.5 && view_w[i] < 13.0 && view_x[i] > -1.0 &&
            view_x[i] < 0.5) {
          stencil[i] = 7u;
        }
      }
    }
  };
  Scene base;
  trace_scene(0.0, 0.0, base);
  std::vector<float>& depth = base.depth;
  std::vector<int>& surface = base.surface;
  std::vector<double> &hit_y = base.hit_y, &view_w = base.view_w;
  std::vector<double> &view_x = base.view_x, &view_y = base.view_y;
  std::vector<uint32_t>& stencil = base.stencil;
  // The same scene jittered by the third phase of the runtime's sequence (a
  // quarter pixel right, 0.39 up), as the jittered draws render it.
  float jitter[2];
  post_pass::JitterOffset(2, jitter);
  Scene jittered;
  trace_scene(jitter[0], jitter[1], jittered);
  // Car 1's origin, the center of the box.
  const double box_origin[3] = {-2.5, -1.0, 7.5};

  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter> warp;
  ComPtr<ID3D12Device> device;
  if (!Succeeded(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory") ||
      !Succeeded(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter") ||
      !Succeeded(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)),
                 "WARP device")) {
    return 1;
  }

  // Root signature of post_pass.cpp (post_pass_layout.h).
  D3D12_ROOT_PARAMETER parameters[post_pass::kRootParameterCount];
  D3D12_DESCRIPTOR_RANGE ranges[post_pass::kMaxSrvs + post_pass::kMaxUavs];
  parameters[post_pass::kRootSceneConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[post_pass::kRootSceneConstants].Descriptor = {0, 0};
  parameters[post_pass::kRootSceneConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[post_pass::kRootPassConstants].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[post_pass::kRootPassConstants].Constants = {1, 0, post_pass::kPassConstantCount};
  parameters[post_pass::kRootPassConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  for (UINT i = 0; i < post_pass::kMaxSrvs + post_pass::kMaxUavs; ++i) {
    bool srv = i < post_pass::kMaxSrvs;
    ranges[i] = {srv ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1,
                 srv ? i : i - post_pass::kMaxSrvs, 0, 0};
    parameters[post_pass::kRootSrvFirst + i].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[post_pass::kRootSrvFirst + i].DescriptorTable = {1, &ranges[i]};
    parameters[post_pass::kRootSrvFirst + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
  const D3D12_FILTER filters[3] = {D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
                                   D3D12_FILTER_MIN_MAG_MIP_POINT,
                                   D3D12_FILTER_MIN_MAG_MIP_LINEAR};
  for (UINT i = 0; i < 3; ++i) {
    samplers[i].Filter = filters[i];
    samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[i].ShaderRegister = i;
    samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_ROOT_SIGNATURE_DESC root_desc = {post_pass::kRootParameterCount, parameters, 3, samplers,
                                         D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> root_blob, root_error;
  ComPtr<ID3D12RootSignature> root_signature;
  if (!Succeeded(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &root_blob, &root_error),
                 "root signature") ||
      !Succeeded(device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                             root_blob->GetBufferSize(),
                                             IID_PPV_ARGS(&root_signature)),
                 "root signature object")) {
    return 1;
  }

  // Pipelines, compiled as CompileManifest compiles them.
  std::string params_fields;
  for (const Param& param : params) {
    params_fields += std::string(" ") + (param.is_uint ? "uint " : "float ") + param.name + ";";
  }
  auto compile = [&](const Pass& pass, const char* target, ComPtr<ID3DBlob>& code) {
    std::string source = "#define XE_POST_PASS_PARAMS" + params_fields +
                         "\n#include \"xe_post_pass.hlsli\"\n#include \"" + pass.shader + "\"\n";
    std::vector<std::pair<std::string, std::string>> all = defines;
    for (const auto& define : pass.defines) {
      auto same = [&define](const auto& known) { return known.first == define.first; };
      auto existing = std::find_if(all.begin(), all.end(), same);
      if (existing != all.end()) {
        existing->second = define.second;
      } else {
        all.push_back(define);
      }
    }
    std::vector<D3D_SHADER_MACRO> macros;
    for (const auto& define : all) macros.push_back({define.first.c_str(), define.second.c_str()});
    macros.push_back({nullptr, nullptr});
    Include include(library, directory);
    ComPtr<ID3DBlob> messages;
    HRESULT result = D3DCompile(source.data(), source.size(), pass.name.c_str(), macros.data(),
                                &include, pass.entry.c_str(), target,
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
    if (messages) std::printf("%s\n", static_cast<const char*>(messages->GetBufferPointer()));
    return Succeeded(result, pass.name.c_str());
  };
  for (size_t i = 0; i < compiled_only.size(); ++i) {
    ComPtr<ID3DBlob> code;
    if (!compile(compiled_only[i], compiled_only_pixel[i] ? "ps_5_1" : "cs_5_1", code)) return 1;
    std::printf("ok   %s compiled\n", compiled_only[i].name.c_str());
  }
  for (Pass& pass : passes) {
    ComPtr<ID3DBlob> code;
    if (!compile(pass, "cs_5_1", code)) return 1;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature.Get();
    desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    if (!Succeeded(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pass.pipeline)),
                   "pipeline")) {
      return 1;
    }
  }

  // Textures: the scene depth, those of the manifest, and the scene constants.
  D3D12_HEAP_PROPERTIES default_heap = {D3D12_HEAP_TYPE_DEFAULT};
  auto create_texture = [&](DXGI_FORMAT format, uint32_t mips, bool uav,
                            D3D12_RESOURCE_STATES state) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = UINT16(mips);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> resource;
    Succeeded(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc, state,
                                              nullptr, IID_PPV_ARGS(&resource)),
              "texture");
    return resource;
  };
  ComPtr<ID3D12Resource> depth_texture =
      create_texture(DXGI_FORMAT_R32_FLOAT, 1, false, D3D12_RESOURCE_STATE_COPY_DEST);
  // The stencil as the runtime views the depth resolve read as 8_8_8_8: an
  // R8G8B8A8_UINT view with the stencil in R.
  ComPtr<ID3D12Resource> stencil_texture =
      create_texture(DXGI_FORMAT_R8G8B8A8_UINT, 1, false, D3D12_RESOURCE_STATE_COPY_DEST);
  // The jittered scene's.
  ComPtr<ID3D12Resource> jittered_depth_texture =
      create_texture(DXGI_FORMAT_R32_FLOAT, 1, false, D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12Resource> jittered_stencil_texture =
      create_texture(DXGI_FORMAT_R8G8B8A8_UINT, 1, false, D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12DescriptorHeap> heap;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256,
                                          D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  if (!Succeeded(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)), "heap")) return 1;
  UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto cpu = [&](UINT i) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(i) * increment;
    return h;
  };
  auto gpu = [&](UINT i) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(i) * increment;
    return h;
  };
  UINT next_descriptor = 0;
  auto srv = [&](ID3D12Resource* resource, DXGI_FORMAT format, UINT first_mip, UINT mips) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2D.MostDetailedMip = first_mip;
    desc.Texture2D.MipLevels = mips;
    device->CreateShaderResourceView(resource, &desc, cpu(next_descriptor));
    return next_descriptor++;
  };
  auto uav = [&](ID3D12Resource* resource, DXGI_FORMAT format, UINT mip) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = mip;
    device->CreateUnorderedAccessView(resource, nullptr, &desc, cpu(next_descriptor));
    return next_descriptor++;
  };
  const UINT depth_srv = srv(depth_texture.Get(), DXGI_FORMAT_R32_FLOAT, 0, 1);
  const UINT stencil_srv = srv(stencil_texture.Get(), DXGI_FORMAT_R8G8B8A8_UINT, 0, 1);
  const UINT jittered_depth_srv = srv(jittered_depth_texture.Get(), DXGI_FORMAT_R32_FLOAT, 0, 1);
  const UINT jittered_stencil_srv =
      srv(jittered_stencil_texture.Get(), DXGI_FORMAT_R8G8B8A8_UINT, 0, 1);
  for (Texture& texture : textures) {
    // As EnsureTexture: mips while the size allows.
    uint32_t mips = 1;
    while (mips < texture.mips && (std::max(kWidth, kHeight) >> mips)) ++mips;
    texture.mips = mips;
    texture.resource =
        create_texture(texture.format, mips, true, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    texture.srv = srv(texture.resource.Get(), texture.format, 0, mips);
    for (uint32_t mip = 0; mip < mips; ++mip) {
      texture.srv_mips[mip] = mips > 1 ? srv(texture.resource.Get(), texture.format, mip, 1)
                                       : texture.srv;
      texture.uavs[mip] = uav(texture.resource.Get(), texture.format, mip);
    }
  }
  Texture *occlusion_texture = nullptr, *motion_texture = nullptr;
  for (Texture& texture : textures) {
    if (texture.slot == 0) occlusion_texture = &texture;
    if (texture.slot == 1) motion_texture = &texture;
  }
  if (!occlusion_texture || occlusion_texture->format != DXGI_FORMAT_R8_UNORM) {
    std::printf("FAIL no R8_UNORM texture in slot 0 (xe_post_ao)\n");
    return 1;
  }
  if (!motion_texture || motion_texture->format != DXGI_FORMAT_R16G16_FLOAT) {
    std::printf("FAIL no R16G16_FLOAT texture in slot 1 (the motion vectors)\n");
    return 1;
  }
  Texture* motion_debug_texture = find_texture("motion_debug_color");
  Texture* motion_mask_texture = find_texture("motion_mask");
  if (!motion_mask_texture || motion_mask_texture->format != DXGI_FORMAT_R8G8_UNORM) {
    std::printf("FAIL no R8G8_UNORM texture motion_mask\n");
    return 1;
  }

  // Upload and readback buffers (rows aligned to 256 bytes): the occlusion in
  // two R8 blocks, then three blocks of 4 bytes per pixel (the motion with and
  // without a valid previous frame, and the slot debug view).
  const UINT depth_pitch = (kWidth * 4 + 255) & ~255u, r8_pitch = (kWidth + 255) & ~255u;
  const UINT64 r8_block = UINT64(r8_pitch) * kHeight, wide_block = UINT64(depth_pitch) * kHeight;
  auto buffer = [&](D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES properties = {type};
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Succeeded(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state,
                                              nullptr, IID_PPV_ARGS(&resource)),
              "buffer");
    return resource;
  };
  // The depth and stencil of the scene, then of the jittered scene.
  ComPtr<ID3D12Resource> upload =
      buffer(D3D12_HEAP_TYPE_UPLOAD, wide_block * 4, D3D12_RESOURCE_STATE_GENERIC_READ);
  // Four sets of scene constants: with a valid previous frame, without, and
  // with the jitter both with the moving camera and with a still one.
  const UINT64 constants_stride = (sizeof(post_pass::SceneConstants) + 255) & ~UINT64(255);
  ComPtr<ID3D12Resource> constants_buffer =
      buffer(D3D12_HEAP_TYPE_UPLOAD, constants_stride * 4, D3D12_RESOURCE_STATE_GENERIC_READ);
  ComPtr<ID3D12Resource> readback = buffer(
      D3D12_HEAP_TYPE_READBACK, r8_block * 2 + wide_block * 7, D3D12_RESOURCE_STATE_COPY_DEST);
  if (failures) return 1;
  {
    uint8_t* mapped = nullptr;
    upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (const Scene* traced : {&base, &jittered}) {
      for (uint32_t y = 0; y < kHeight; ++y) {
        std::memcpy(mapped + y * depth_pitch, &traced->depth[y * kWidth], kWidth * 4);
        for (uint32_t x = 0; x < kWidth; ++x) {
          uint8_t* texel = mapped + wide_block + y * depth_pitch + x * 4;
          texel[0] = uint8_t(traced->stencil[y * kWidth + x]);
          texel[1] = texel[2] = texel[3] = 0;
        }
      }
      mapped += wide_block * 2;
    }
    upload->Unmap(0, nullptr);
  }
  // Scene constants as FillSceneConstants fills them.
  post_pass::SceneConstants scene = {};
  scene.xy_mul_add[0] = unpack.x_mul;
  scene.xy_mul_add[1] = unpack.x_add;
  scene.xy_mul_add[2] = unpack.y_mul;
  scene.xy_mul_add[3] = unpack.y_add;
  scene.size[0] = kWidth;
  scene.size[1] = kHeight;
  scene.guest_per_host[0] = scene.guest_per_host[1] = guest_per_host;
  scene.guest_origin[0] = scene.guest_origin[1] = -0.5f;
  scene.w_numerator = unpack.w_numerator;
  scene.w_depth_mul = unpack.w_depth_mul;
  scene.w_depth_add = unpack.w_depth_add;
  scene.z_from_w = unpack.z_from_w;
  scene.w_far = unpack.w_far;
  scene.focal[0] = unpack.pixels_per_unit_x / guest_per_host;
  scene.focal[1] = unpack.pixels_per_unit_y / guest_per_host;
  scene.flags = post_pass::kSceneFlagViewKnown;
  scene.motion_stencil_scale = 1.0f / 7.0f;
  scene.motion_stencil_limit = 126;
  scene.motion_slot_count = 19;
  scene.motion_interior_slot = 3;
  scene.motion_interior_depth = 1.0f;
  for (int k = 0; k < 3; ++k) scene.motion_object_origin[1][k] = float(box_origin[k]);
  scene.motion_object_origin[1][3] = 1.0f;
  // Without a previous frame: the identity in every slot, nothing own.
  for (auto& rows : scene.motion_view_to_previous_view) {
    for (int i = 0; i < 3; ++i) rows[i][i] = 1.0f;
  }
  post_pass::SceneConstants scene_no_motion = scene;
  // That frame also has no interior: car 3's interior pixels are a bare mask.
  scene_no_motion.motion_interior_slot = post_pass::kMotionNoInterior;
  // The motion since the previous frame: the camera went 1 m forward, 0.2 m to
  // the side and turned by 1 degree; car 1 (the box) also moved 0.4 m to the
  // side and 0.3 m back; car 2 (the wall) is new in this frame, so its slot
  // holds the static world's. The previous frame's projection was zoomed 2%
  // in.
  double world_motion[4][4], box_offset[4][4], box_motion[4][4], interior_motion[4][4];
  RigidMotion(1.0 * 3.14159265358979 / 180.0, 0.2, 0.05, 1.0, world_motion);
  // The interior: a small turn and move of its own (the cockpit camera against
  // the car).
  RigidMotion(0.3 * 3.14159265358979 / 180.0, 0.01, -0.005, 0.02, interior_motion);
  RigidMotion(0.0, 0.4, 0.0, -0.3, box_offset);
  MultiplyMatrices(world_motion, box_offset, box_motion);
  post_pass::Matrix previous_projection = projection;
  previous_projection.m[0][0] *= 1.02f;
  previous_projection.m[1][1] *= 1.02f;
  post_pass::Unpack previous_unpack;
  if (!post_pass::UnpackFromProjection(previous_projection, viewport, previous_unpack)) {
    std::printf("FAIL previous unpack constants\n");
    return 1;
  }
  scene.flags |= post_pass::kSceneFlagMotionValid;
  scene.previous_xy_mul_add[0] = previous_unpack.x_mul;
  scene.previous_xy_mul_add[1] = previous_unpack.x_add;
  scene.previous_xy_mul_add[2] = previous_unpack.y_mul;
  scene.previous_xy_mul_add[3] = previous_unpack.y_add;
  scene.previous_w_numerator = previous_unpack.w_numerator;
  scene.previous_w_depth_mul = previous_unpack.w_depth_mul;
  scene.previous_w_depth_add = previous_unpack.w_depth_add;
  scene.previous_z_from_w = previous_unpack.z_from_w;
  scene.motion_slots_own = 0b11 | 1u << post_pass::kMotionInteriorSlot;
  for (uint32_t slot = 0; slot < post_pass::kMotionSlots; ++slot) {
    const double(*matrix)[4] = slot == 1                               ? box_motion
                               : slot == post_pass::kMotionInteriorSlot ? interior_motion
                                                                        : world_motion;
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 4; ++j) {
        scene.motion_view_to_previous_view[slot][i][j] = float(matrix[i][j]);
        if (!slot) scene.view_to_previous_view[i][j] = float(matrix[i][j]);
      }
    }
  }
  // The jittered scene with the same motion, and with a still camera and car:
  // the identity in every slot and the same projection in both frames.
  post_pass::SceneConstants scene_jittered = scene;
  scene_jittered.jitter[0] = jitter[0];
  scene_jittered.jitter[1] = jitter[1];
  post_pass::SceneConstants scene_still = scene_jittered;
  std::memset(scene_still.view_to_previous_view, 0, sizeof(scene_still.view_to_previous_view));
  std::memset(scene_still.motion_view_to_previous_view, 0,
              sizeof(scene_still.motion_view_to_previous_view));
  for (int i = 0; i < 3; ++i) {
    scene_still.view_to_previous_view[i][i] = 1.0f;
    for (auto& rows : scene_still.motion_view_to_previous_view) rows[i][i] = 1.0f;
  }
  std::memcpy(scene_still.previous_xy_mul_add, scene_still.xy_mul_add,
              sizeof(scene_still.xy_mul_add));
  scene_still.previous_w_numerator = scene_still.w_numerator;
  scene_still.previous_w_depth_mul = scene_still.w_depth_mul;
  scene_still.previous_w_depth_add = scene_still.w_depth_add;
  scene_still.previous_z_from_w = scene_still.z_from_w;
  {
    uint8_t* mapped = nullptr;
    constants_buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    std::memcpy(mapped, &scene, sizeof(scene));
    std::memcpy(mapped + constants_stride, &scene_no_motion, sizeof(scene_no_motion));
    std::memcpy(mapped + constants_stride * 2, &scene_jittered, sizeof(scene_jittered));
    std::memcpy(mapped + constants_stride * 3, &scene_still, sizeof(scene_still));
    constants_buffer->Unmap(0, nullptr);
  }
  const D3D12_GPU_VIRTUAL_ADDRESS constants_motion = constants_buffer->GetGPUVirtualAddress();
  const D3D12_GPU_VIRTUAL_ADDRESS constants_no_motion = constants_motion + constants_stride;
  const D3D12_GPU_VIRTUAL_ADDRESS constants_jittered = constants_motion + constants_stride * 2;
  const D3D12_GPU_VIRTUAL_ADDRESS constants_still = constants_motion + constants_stride * 3;

  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
  device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
  device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
  device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                            IID_PPV_ARGS(&list));
  D3D12_TEXTURE_COPY_LOCATION depth_dst = {depth_texture.Get(),
                                           D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  D3D12_TEXTURE_COPY_LOCATION depth_src = {upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
  depth_src.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, kWidth, kHeight, 1, depth_pitch};
  list->CopyTextureRegion(&depth_dst, 0, 0, 0, &depth_src, nullptr);
  D3D12_TEXTURE_COPY_LOCATION stencil_dst = {stencil_texture.Get(),
                                             D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  D3D12_TEXTURE_COPY_LOCATION stencil_src = {upload.Get(),
                                             D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
  stencil_src.PlacedFootprint.Offset = wide_block;
  stencil_src.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UINT, kWidth, kHeight, 1,
                                           depth_pitch};
  list->CopyTextureRegion(&stencil_dst, 0, 0, 0, &stencil_src, nullptr);
  depth_dst.pResource = jittered_depth_texture.Get();
  depth_src.PlacedFootprint.Offset = wide_block * 2;
  list->CopyTextureRegion(&depth_dst, 0, 0, 0, &depth_src, nullptr);
  stencil_dst.pResource = jittered_stencil_texture.Get();
  stencil_src.PlacedFootprint.Offset = wide_block * 3;
  list->CopyTextureRegion(&stencil_dst, 0, 0, 0, &stencil_src, nullptr);
  auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES& state,
                        D3D12_RESOURCE_STATES after) {
    if (state == after) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state, after};
    list->ResourceBarrier(1, &barrier);
    state = after;
  };
  D3D12_RESOURCE_STATES depth_state = D3D12_RESOURCE_STATE_COPY_DEST;
  D3D12_RESOURCE_STATES stencil_state = D3D12_RESOURCE_STATE_COPY_DEST;
  transition(depth_texture.Get(), depth_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  transition(stencil_texture.Get(), stencil_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  D3D12_RESOURCE_STATES jittered_depth_state = D3D12_RESOURCE_STATE_COPY_DEST;
  D3D12_RESOURCE_STATES jittered_stencil_state = D3D12_RESOURCE_STATE_COPY_DEST;
  transition(jittered_depth_texture.Get(), jittered_depth_state,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  transition(jittered_stencil_texture.Get(), jittered_stencil_state,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap* heaps[] = {heap.Get()};
  list->SetDescriptorHeaps(1, heaps);

  // Copies a texture's mip 0 to a readback block.
  auto copy_out = [&](Texture& texture, UINT64 offset, DXGI_FORMAT format, UINT pitch) {
    transition(texture.resource.Get(), texture.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {texture.resource.Get(),
                                       D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION dst = {readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint.Offset = offset;
    dst.PlacedFootprint.Footprint = {format, kWidth, kHeight, 1, pitch};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  };
  // Runs the point's passes with the params of the manifest and those of set,
  // on the scene or the jittered scene, then copies the slot 0 texture to
  // occlusion block occlusion_block and the slot 1 texture to wide block
  // motion_block (none if negative).
  auto run_point = [&](const std::map<std::string, double>& set, int occlusion_block,
                       int motion_block, D3D12_GPU_VIRTUAL_ADDRESS scene_constants,
                       bool jittered_scene = false) {
    uint32_t constants[post_pass::kPassConstantCount] = {};
    post_pass::PassHeader header = {{kWidth, kHeight}, {1.0f / kWidth, 1.0f / kHeight}};
    std::memcpy(constants, &header, sizeof(header));
    uint32_t* values = constants + sizeof(header) / 4;
    std::vector<float> floats(params.size());
    std::map<std::string, double> condition_values;
    for (const auto& define : defines) {
      condition_values[define.first] = std::atof(define.second.c_str());
    }
    for (size_t i = 0; i < params.size(); ++i) {
      double value = set.count(params[i].name) ? set.at(params[i].name) : params[i].value;
      floats[i] = std::clamp(float(value), float(params[i].min), float(params[i].max));
      if (params[i].is_uint) {
        values[i] = uint32_t(floats[i]);
      } else {
        std::memcpy(&values[i], &floats[i], 4);
      }
      condition_values[params[i].name] = params[i].is_uint ? values[i] : floats[i];
    }
    for (size_t i = 0; i < params.size(); ++i) {
      for (size_t j = 0; j < params.size(); ++j) {
        if (params[j].name != params[i].focal_length_of || params[i].focal_length_of.empty()) {
          continue;
        }
        float pixels = floats[j] * unpack.pixels_per_unit_x / guest_per_host;
        std::memcpy(&values[i], &pixels, 4);
      }
    }
    for (Pass& pass : passes) {
      if (!pass.condition.empty()) {
        bool valid;
        bool enabled = ConditionHolds(pass.condition, condition_values, valid);
        if (!valid) {
          std::printf("FAIL pass %s: condition %s\n", pass.name.c_str(), pass.condition.c_str());
          ++failures;
          return;
        }
        if (!enabled) continue;
      }
      D3D12_GPU_DESCRIPTOR_HANDLE srvs[post_pass::kMaxSrvs], uavs[post_pass::kMaxUavs];
      for (auto& handle : srvs) handle = gpu(jittered_scene ? jittered_depth_srv : depth_srv);
      for (size_t i = 0; i < pass.srvs.size(); ++i) {
        if (pass.srvs[i].texture == "scene_depth") continue;
        if (pass.srvs[i].texture == "scene_stencil") {
          srvs[i] = gpu(jittered_scene ? jittered_stencil_srv : stencil_srv);
          continue;
        }
        Texture* texture = find_texture(pass.srvs[i].texture);
        if (!texture) {
          std::printf("FAIL pass %s: no texture %s\n", pass.name.c_str(),
                      pass.srvs[i].texture.c_str());
          ++failures;
          return;
        }
        transition(texture->resource.Get(), texture->state,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        srvs[i] = gpu(pass.srvs[i].mip < 0 ? texture->srv : texture->srv_mips[pass.srvs[i].mip]);
      }
      uint32_t width = kWidth, height = kHeight;
      for (size_t i = 0; i < post_pass::kMaxUavs; ++i) {
        const Binding& binding = pass.uavs[std::min(i, pass.uavs.size() - 1)];
        Texture* texture = find_texture(binding.texture);
        if (!texture) {
          std::printf("FAIL pass %s: no texture %s\n", pass.name.c_str(), binding.texture.c_str());
          ++failures;
          return;
        }
        transition(texture->resource.Get(), texture->state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        uavs[i] = gpu(texture->uavs[std::max(binding.mip, 0)]);
        if (i == 0) {
          width = std::max(kWidth >> std::max(binding.mip, 0), 1u);
          height = std::max(kHeight >> std::max(binding.mip, 0), 1u);
        }
      }
      header = {{width, height}, {1.0f / width, 1.0f / height}};
      std::memcpy(constants, &header, sizeof(header));
      list->SetPipelineState(pass.pipeline.Get());
      list->SetComputeRootSignature(root_signature.Get());
      list->SetComputeRootConstantBufferView(post_pass::kRootSceneConstants, scene_constants);
      list->SetComputeRoot32BitConstants(post_pass::kRootPassConstants,
                                         UINT(4 + std::max(params.size(), size_t(1))), constants,
                                         0);
      for (UINT i = 0; i < post_pass::kMaxSrvs; ++i) {
        list->SetComputeRootDescriptorTable(post_pass::kRootSrvFirst + i, srvs[i]);
      }
      for (UINT i = 0; i < post_pass::kMaxUavs; ++i) {
        list->SetComputeRootDescriptorTable(post_pass::kRootUavFirst + i, uavs[i]);
      }
      list->Dispatch((width + pass.group[0] - 1) / pass.group[0],
                     (height + pass.group[1] - 1) / pass.group[1], 1);
    }
    if (occlusion_block >= 0) {
      copy_out(*occlusion_texture, UINT64(occlusion_block) * r8_block, DXGI_FORMAT_R8_UNORM,
               r8_pitch);
    }
    if (motion_block >= 0) {
      copy_out(*motion_texture, r8_block * 2 + UINT64(motion_block) * wide_block,
               DXGI_FORMAT_R16G16_FLOAT, depth_pitch);
    }
  };
  uint32_t debug = 0;
  for (const Param& param : params) {
    if (param.name == "GTAO_DEBUG") debug = uint32_t(param.value);
  }
  run_point({{"GTAO_DEBUG", double(debug)}}, 0, 0, constants_motion);
  copy_out(*motion_mask_texture, r8_block * 2 + wide_block * 3, DXGI_FORMAT_R8G8_UNORM,
           depth_pitch);
  run_point({{"GTAO_DEBUG", 1.0}}, 1, -1, constants_motion);  // The W debug view.
  run_point({}, -1, 1, constants_no_motion);
  copy_out(*motion_mask_texture, r8_block * 2 + wide_block * 4, DXGI_FORMAT_R8G8_UNORM,
           depth_pitch);
  // The jittered scene, with the moving camera and with a still one.
  run_point({}, -1, 5, constants_jittered, true);
  run_point({}, -1, 6, constants_still, true);
  // The motion debug views run; the slot view (3) is read back.
  for (double view : {1.0, 2.0, 4.0, 5.0, 6.0, 3.0}) {
    run_point({{"MOTION_DEBUG", view}}, -1, -1, constants_motion);
  }
  if (motion_debug_texture && motion_debug_texture->format == DXGI_FORMAT_R8G8B8A8_UNORM) {
    copy_out(*motion_debug_texture, r8_block * 2 + wide_block * 2, DXGI_FORMAT_R8G8B8A8_UNORM,
             depth_pitch);
  }
  if (failures) return 1;
  if (!Succeeded(list->Close(), "command list")) return 1;
  ID3D12CommandList* lists[] = {list.Get()};
  queue->ExecuteCommandLists(1, lists);
  ComPtr<ID3D12Fence> fence;
  device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  queue->Signal(fence.Get(), 1);
  fence->SetEventOnCompletion(1, event);
  WaitForSingleObject(event, INFINITE);
  if (!Succeeded(device->GetDeviceRemovedReason(), "device")) return 1;

  std::vector<uint8_t> occlusion(kWidth * kHeight), w_debug(kWidth * kHeight);
  std::vector<uint16_t> motion(kWidth * kHeight * 2), motion_none(kWidth * kHeight * 2);
  std::vector<uint8_t> slot_view(kWidth * kHeight * 4), mask(kWidth * kHeight * 2),
      mask_no_interior(kWidth * kHeight * 2);
  std::vector<uint16_t> motion_jittered(kWidth * kHeight * 2), motion_still(kWidth * kHeight * 2);
  {
    uint8_t* mapped = nullptr;
    D3D12_RANGE range = {0, SIZE_T(r8_block * 2 + wide_block * 7)};
    readback->Map(0, &range, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < kHeight; ++y) {
      std::memcpy(&occlusion[y * kWidth], mapped + y * r8_pitch, kWidth);
      std::memcpy(&w_debug[y * kWidth], mapped + r8_block + y * r8_pitch, kWidth);
      const uint8_t* wide = mapped + r8_block * 2 + y * depth_pitch;
      std::memcpy(&motion[y * kWidth * 2], wide, kWidth * 4);
      std::memcpy(&motion_none[y * kWidth * 2], wide + wide_block, kWidth * 4);
      std::memcpy(&slot_view[y * kWidth * 4], wide + wide_block * 2, kWidth * 4);
      std::memcpy(&mask[y * kWidth * 2], wide + wide_block * 3, kWidth * 2);
      std::memcpy(&mask_no_interior[y * kWidth * 2], wide + wide_block * 4, kWidth * 2);
      std::memcpy(&motion_jittered[y * kWidth * 2], wide + wide_block * 5, kWidth * 4);
      std::memcpy(&motion_still[y * kWidth * 2], wide + wide_block * 6, kWidth * 4);
    }
    readback->Unmap(0, nullptr);
  }
  if (image_path) {
    std::ofstream image(image_path, std::ios::binary);
    image << "P5\n" << kWidth << ' ' << kHeight << "\n255\n";
    image.write(reinterpret_cast<const char*>(occlusion.data()), occlusion.size());
  }
  if (debug) {
    // A debug view was asked for: written to the image, not checked.
    std::printf("post pass shader tests: debug view %u written\n", debug);
    return 0;
  }

  // Motion vectors against the traced view positions reprojected in double:
  // the slot's matrix, the previous projection, the guest viewport, then host
  // pixels (guest = (host + 0.5) * 2 - 0.5 as above).
  auto half = [](uint16_t h) {
    uint32_t sign = uint32_t(h >> 15) << 31, exponent = (h >> 10) & 31, mantissa = h & 1023;
    uint32_t bits;
    if (exponent == 0) {
      if (!mantissa) {
        bits = sign;
      } else {
        float value = std::ldexp(float(mantissa), -24);
        std::memcpy(&bits, &value, 4);
        bits |= sign;
      }
    } else if (exponent == 31) {
      bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
      bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
  };
  // 0 track, 1 track under a shadow (128), 2 the box (car 1), 3 the wall (car
  // 2, not own), 4 sky, 5 the interior of car 3, 6 the mask without an
  // interior, 7 the track through car 1's glass.
  constexpr int kClasses = 8;
  const char* class_names[kClasses] = {"track", "shadowed track", "box (car 1)",
                                       "wall (car 2, new)", "sky", "interior (car 3)",
                                       "mask without an interior", "track through glass"};
  double worst_motion[kClasses] = {}, largest_motion[kClasses] = {};
  int class_count[kClasses] = {};
  double box_object_motion = 0.0;
  uint32_t nonzero_without_previous = 0, wrong_mask = 0, wrong_mask_no_interior = 0;
  // MOTION_INTERIOR_W of the manifest (or the command line).
  double interior_w = 0.6;
  for (const Param& param : params) {
    if (param.name == "MOTION_INTERIOR_W") interior_w = param.value;
  }
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      uint32_t i = y * kWidth + x;
      int motion_class = surface[i] == 0   ? 4
                         : surface[i] == 5 ? 5
                         : surface[i] == 6 ? 6
                         : surface[i] == 3 ? 2
                         : surface[i] == 2 ? 3
                         : stencil[i] == 7 ? 7
                         : stencil[i] == 128 ? 1
                                             : 0;
      // The interior has no depth: its view position at MOTION_INTERIOR_W.
      double scale = motion_class == 5 ? interior_w / view_w[i] : 1.0;
      auto expected = [&](const double matrix[4][4], double out[2]) {
        double view[4] = {view_x[i] * scale, view_y[i] * scale, view_w[i] * scale, 1.0},
               previous[4];
        Transform(matrix, view, previous);
        double ndc_x = (previous_projection.m[0][0] * previous[0] +
                        previous_projection.m[0][2] * previous[2]) / previous[2];
        double ndc_y = (previous_projection.m[1][1] * previous[1] +
                        previous_projection.m[1][2] * previous[2]) / previous[2];
        double gx = ndc_x * viewport.x_scale + viewport.x_offset;
        double gy = ndc_y * viewport.y_scale + viewport.y_offset;
        out[0] = (gx + 0.5) / guest_per_host - (x + 0.5);
        out[1] = (gy + 0.5) / guest_per_host - (y + 0.5);
      };
      double want[2], world_want[2];
      expected(motion_class == 2   ? box_motion
               : motion_class == 5 ? interior_motion
                                   : world_motion,
               want);
      expected(world_motion, world_want);
      if (motion_class == 6) want[0] = want[1] = 0.0;
      // The mask: no motion known on the mask without an interior, reactive
      // through glass.
      int want_mask[2] = {motion_class == 6 ? 255 : 0, motion_class == 7 ? 255 : 0};
      if (mask[i * 2] != want_mask[0] || mask[i * 2 + 1] != want_mask[1]) ++wrong_mask;
      if (mask_no_interior[i * 2] != (motion_class == 5 || motion_class == 6 ? 255 : 0)) {
        ++wrong_mask_no_interior;
      }
      double got[2] = {half(motion[i * 2]), half(motion[i * 2 + 1])};
      double error = std::hypot(got[0] - want[0], got[1] - want[1]);
      double length = std::hypot(want[0], want[1]);
      // RG16F keeps 11 significant bits; the stored depth of the far ground
      // is the least precise input.
      double allowed = 0.02 + 2.0e-3 * length;
      worst_motion[motion_class] = std::fmax(worst_motion[motion_class], error / allowed);
      largest_motion[motion_class] = std::fmax(largest_motion[motion_class], length);
      ++class_count[motion_class];
      if (motion_class == 2) {
        box_object_motion = std::fmax(box_object_motion,
                                      std::hypot(want[0] - world_want[0], want[1] - world_want[1]));
      }
      if (motion_none[i * 2] || motion_none[i * 2 + 1]) ++nonzero_without_previous;
    }
  }
  for (int c = 0; c < kClasses; ++c) {
    std::printf("motion %s: %d pixels, up to %.1f px, worst error %.2f of the allowed\n",
                class_names[c], class_count[c], largest_motion[c], worst_motion[c]);
    char what[96];
    std::snprintf(what, sizeof(what), "motion of the %s reprojected", class_names[c]);
    Check(class_count[c] > 100 && worst_motion[c] <= 1.0, what, worst_motion[c]);
  }
  Check(wrong_mask == 0, "motion mask: no motion known on the bare mask, reactive through glass",
        wrong_mask);
  Check(wrong_mask_no_interior == 0, "motion mask without an interior: the whole mask unknown",
        wrong_mask_no_interior);
  Check(box_object_motion > 2.0, "the box moves apart from the static world", box_object_motion);
  Check(nonzero_without_previous == 0, "no motion without a valid previous frame",
        nonzero_without_previous);

  // The jittered scene: the motion of what each pixel shows, between the
  // unjittered positions (the pixel center less the jitter, and the previous
  // frame's); none with the camera and the car still. Without the jitter in
  // the passes both would be off by the jitter, 0.46 px.
  double worst_jittered = 0.0, worst_still = 0.0;
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      uint32_t i = y * kWidth + x;
      const Scene& s = jittered;
      bool box = s.surface[i] == 3, interior = s.surface[i] == 5, bare_mask = s.surface[i] == 6;
      double scale = interior ? interior_w / s.view_w[i] : 1.0;
      double view[4] = {s.view_x[i] * scale, s.view_y[i] * scale, s.view_w[i] * scale, 1.0},
             previous[4];
      Transform(box ? box_motion : interior ? interior_motion : world_motion, view, previous);
      double ndc_x = (previous_projection.m[0][0] * previous[0] +
                      previous_projection.m[0][2] * previous[2]) / previous[2];
      double ndc_y = (previous_projection.m[1][1] * previous[1] +
                      previous_projection.m[1][2] * previous[2]) / previous[2];
      double gx = ndc_x * viewport.x_scale + viewport.x_offset;
      double gy = ndc_y * viewport.y_scale + viewport.y_offset;
      double want[2] = {(gx + 0.5) / guest_per_host - (x + 0.5 - jitter[0]),
                        (gy + 0.5) / guest_per_host - (y + 0.5 - jitter[1])};
      if (bare_mask) want[0] = want[1] = 0.0;
      double got[2] = {half(motion_jittered[i * 2]), half(motion_jittered[i * 2 + 1])};
      double allowed = 0.02 + 2.0e-3 * std::hypot(want[0], want[1]);
      worst_jittered =
          std::fmax(worst_jittered, std::hypot(got[0] - want[0], got[1] - want[1]) / allowed);
      worst_still = std::fmax(worst_still, std::hypot(half(motion_still[i * 2]),
                                                      half(motion_still[i * 2 + 1])) / 0.02);
    }
  }
  std::printf("jitter %.4f %.4f px: worst motion error %.2f of the allowed, still %.2f\n",
              jitter[0], jitter[1], worst_jittered, worst_still);
  Check(worst_jittered <= 1.0, "motion of the jittered scene between unjittered positions",
        worst_jittered);
  Check(worst_still <= 1.0, "no motion of the jittered scene with a still camera", worst_still);
  if (motion_debug_texture) {
    // Slot view: the track transparent, car 1 its color at alpha 0.65, cars 2
    // and 3 (no own motion) striped at alpha 0.85.
    uint32_t wrong_alpha = 0;
    for (uint32_t i = 0; i < kWidth * kHeight; ++i) {
      int alpha = slot_view[i * 4 + 3];
      int want_alpha = stencil[i] == 7 ? 166 : stencil[i] == 14 || stencil[i] == 21 ? 217 : 0;
      if (std::abs(alpha - want_alpha) > 1) ++wrong_alpha;
    }
    Check(wrong_alpha == 0, "slot debug view: track, own and new object slots", wrong_alpha);
  }

  // Averages of the occlusion (0..1) over pixels of a region.
  auto average = [&](auto&& select) {
    double sum = 0.0;
    int count = 0;
    for (uint32_t y = 2; y + 2 < kHeight; ++y) {
      for (uint32_t x = 2; x + 2 < kWidth; ++x) {
        uint32_t i = y * kWidth + x;
        if (select(x, y, i)) {
          sum += occlusion[i] / 255.0;
          ++count;
        }
      }
    }
    return count ? sum / count : -1.0;
  };
  // Pixel positions on the ground: view x and z.
  auto ground_xz = [&](uint32_t x, uint32_t y, double& gx, double& gz) {
    double w = view_w[y * kWidth + x];
    double sx = (x + 0.5) * guest_per_host - 0.5;
    gx = w * ((sx - viewport.x_offset) / viewport.x_scale) / projection.m[0][0];
    gz = w;
  };
  double sky = average([&](uint32_t, uint32_t, uint32_t i) { return surface[i] == 0; });
  double open_ground = average([&](uint32_t x, uint32_t y, uint32_t i) {
    double gx, gz;
    ground_xz(x, y, gx, gz);
    return surface[i] == 1 && gz > 4.0 && gz < 11.0 && gx > -1.0 && gx < 0.5;
  });
  double wall_corner = average([&](uint32_t x, uint32_t y, uint32_t i) {
    double gx, gz;
    ground_xz(x, y, gx, gz);
    return surface[i] == 1 && gz > 13.6 && gz < 14.0 && gx > 2.0 && gx < 6.0;
  });
  double wall_high = average([&](uint32_t, uint32_t, uint32_t i) {
    return surface[i] == 2 && hit_y[i] > 1.0;
  });
  double box_contact = average([&](uint32_t x, uint32_t y, uint32_t i) {
    double gx, gz;
    ground_xz(x, y, gx, gz);
    return surface[i] == 1 && gz > 6.5 && gz < 7.0 && gx > -3.0 && gx < -2.0;
  });
  // W debug output: saturate(W / 100), against the traced W.
  double worst_w = 0.0;
  for (uint32_t i = 0; i < kWidth * kHeight; ++i) {
    if (surface[i] && surface[i] < 5 && view_w[i] < 90.0) {
      worst_w = std::fmax(worst_w, std::fabs(w_debug[i] / 255.0 - view_w[i] * 0.01));
    }
  }
  std::printf("occlusion: sky %.3f, open ground %.3f, wall corner %.3f, upper wall %.3f, "
              "box contact %.3f\n",
              sky, open_ground, wall_corner, wall_high, box_contact);
  Check(sky == 0.0, "sky has no occlusion", sky);
  Check(open_ground >= 0.0 && open_ground < 0.06, "open ground nearly unoccluded", open_ground);
  Check(wall_high >= 0.0 && wall_high < 0.06, "upper wall nearly unoccluded", wall_high);
  Check(wall_corner > 0.15, "wall and ground corner occluded", wall_corner);
  Check(box_contact > 0.1, "ground at the box occluded", box_contact);
  Check(wall_corner > 4.0 * std::fmax(open_ground, 0.01), "corner darker than open ground",
        wall_corner);
  Check(worst_w < 1.5 / 255.0 + 1.0e-3, "W debug output matches the traced W", worst_w * 255.0);
  std::printf(failures ? "post pass shader tests: %d failed\n" : "post pass shader tests: passed\n",
              failures);
  return failures ? 1 : 0;
}
