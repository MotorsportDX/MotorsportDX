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
// still camera. Their debug views run. The final point's passes run over
// frames as the framework runs them: pixel passes draw into final_color (a
// synthetic final image in screen orientation), and history textures swap at
// the end of each frame; the motion debug view is checked there, drawn rotated
// 180 degrees over the frame. The passes of the scene_color point are
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
  // history = true: previous holds the previous frame's (previous:<name>).
  bool history = false;
  ComPtr<ID3D12Resource> resource;
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  // Heap indices: all mips, each mip (SRV), each mip (UAV); the render target
  // view of mip 0 in the RTV heap.
  uint32_t srv = 0, srv_mips[8] = {}, uavs[8] = {}, rtv = 0;
  Texture* previous = nullptr;
};
struct Binding {
  std::string texture;  // or scene_depth, scene_stencil, final_color, previous_depth
  int mip = -1;
  // previous:<history texture>
  bool previous = false;
};
struct Pass {
  std::string name, shader, entry, point;
  std::vector<std::pair<std::string, std::string>> defines;
  std::vector<Binding> srvs, uavs, rtvs;
  uint32_t group[2] = {8, 8};
  std::string condition, blend;
  bool pixel = false;
  ComPtr<ID3DBlob> code;
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
  if (text.rfind("previous:", 0) == 0) {
    binding.texture = text.substr(9);
    binding.previous = true;
    return binding;
  }
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
      texture.format = format == "R32_FLOAT"            ? DXGI_FORMAT_R32_FLOAT
                       : format == "R8_UNORM"           ? DXGI_FORMAT_R8_UNORM
                       : format == "R16_FLOAT"          ? DXGI_FORMAT_R16_FLOAT
                       : format == "R8G8_UNORM"         ? DXGI_FORMAT_R8G8_UNORM
                       : format == "R16G16_FLOAT"       ? DXGI_FORMAT_R16G16_FLOAT
                       : format == "R8G8B8A8_UNORM"     ? DXGI_FORMAT_R8G8B8A8_UNORM
                       : format == "R10G10B10A2_UNORM"  ? DXGI_FORMAT_R10G10B10A2_UNORM
                       : format == "R16G16B16A16_FLOAT" ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                                        : DXGI_FORMAT_UNKNOWN;
      texture.history = fields["history"].value_or(false);
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
      pass.pixel = pixel;
      pass.point = fields["point"].value_or(std::string());
      pass.blend = fields["blend"].value_or(std::string("none"));
      if (pass.point != "scene_depth") {
        for (auto [list, bindings] : {std::pair{"srv", &pass.srvs}, std::pair{"uav", &pass.uavs},
                                      std::pair{"rtv", &pass.rtvs}}) {
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
    compiled_only[i].code = code;
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
                            D3D12_RESOURCE_STATES state, bool rtv = false) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = UINT16(mips);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    if (rtv) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
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
  // Render target views: the textures' mip 0 and final_color.
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64,
                                              D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
  if (!Succeeded(device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)),
                 "RTV heap")) {
    return 1;
  }
  UINT rtv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  UINT next_rtv = 0;
  auto rtv_cpu = [&](UINT i) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(i) * rtv_increment;
    return h;
  };
  auto make_rtv = [&](ID3D12Resource* resource, DXGI_FORMAT format) {
    D3D12_RENDER_TARGET_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(resource, &desc, rtv_cpu(next_rtv));
    return next_rtv++;
  };
  // The previous frame's copies of the history textures (std::list: stable).
  std::list<Texture> previous_textures;
  for (Texture& texture : textures) {
    if (!texture.history) continue;
    Texture& previous = previous_textures.emplace_back(texture);
    texture.previous = &previous;
  }
  auto create_views = [&](Texture& texture) {
    // As EnsureTexture: mips while the size allows.
    uint32_t mips = 1;
    while (mips < texture.mips && (std::max(kWidth, kHeight) >> mips)) ++mips;
    texture.mips = mips;
    texture.resource = create_texture(texture.format, mips, true,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
    texture.srv = srv(texture.resource.Get(), texture.format, 0, mips);
    for (uint32_t mip = 0; mip < mips; ++mip) {
      texture.srv_mips[mip] = mips > 1 ? srv(texture.resource.Get(), texture.format, mip, 1)
                                       : texture.srv;
      texture.uavs[mip] = uav(texture.resource.Get(), texture.format, mip);
    }
    texture.rtv = make_rtv(texture.resource.Get(), texture.format);
  };
  for (Texture& texture : textures) {
    create_views(texture);
    if (texture.previous) create_views(*texture.previous);
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

  // ---------------------------------------------------------------------------
  // The final point over frames, as the framework runs it. A frame runs the
  // passes of the final point on final_color, a synthetic final image in
  // screen orientation (FM4's final combine shows the scene rotated 180
  // degrees); pixel passes draw one triangle over their render targets with
  // xe_post_pass_vs; previous:<name> reads the previous frame's copy of a
  // history texture, swapped at the end of each frame that wrote it; XE_SCENE
  // is 1, the frame having a scene.
  ComPtr<ID3DBlob> vertex_code;
  {
    const char vertex_source[] = "#include \"xe_post_pass.hlsli\"\n";
    const D3D_SHADER_MACRO vertex_macros[] = {{"XE_POST_PASS_VERTEX_SHADER", "1"},
                                              {nullptr, nullptr}};
    Include include(library, directory);
    ComPtr<ID3DBlob> messages;
    if (!Succeeded(D3DCompile(vertex_source, sizeof(vertex_source) - 1, "xe_post_pass_vs",
                              vertex_macros, &include, "xe_post_pass_vs", "vs_5_1",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertex_code, &messages),
                   "xe_post_pass_vs")) {
      if (messages) std::printf("%s\n", static_cast<const char*>(messages->GetBufferPointer()));
      return 1;
    }
  }
  constexpr DXGI_FORMAT kFinalFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
  // final_color, and the previous frame's scene depth (previous_depth).
  ComPtr<ID3D12Resource> final_texture = create_texture(
      kFinalFormat, 1, false, D3D12_RESOURCE_STATE_COPY_DEST, true);
  D3D12_RESOURCE_STATES final_state = D3D12_RESOURCE_STATE_COPY_DEST;
  const UINT final_srv = srv(final_texture.Get(), kFinalFormat, 0, 1);
  const UINT final_rtv = make_rtv(final_texture.Get(), kFinalFormat);
  ComPtr<ID3D12Resource> previous_depth_texture =
      create_texture(DXGI_FORMAT_R32_FLOAT, 1, false, D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_RESOURCE_STATES previous_depth_state = D3D12_RESOURCE_STATE_COPY_DEST;
  const UINT previous_depth_srv = srv(previous_depth_texture.Get(), DXGI_FORMAT_R32_FLOAT, 0, 1);
  // Per frame: the final image (block 0), the scene and previous depth (blocks
  // 1 and 2, R32F); a readback of two final images.
  ComPtr<ID3D12Resource> frame_upload =
      buffer(D3D12_HEAP_TYPE_UPLOAD, wide_block * 3, D3D12_RESOURCE_STATE_GENERIC_READ);
  ComPtr<ID3D12Resource> final_readback =
      buffer(D3D12_HEAP_TYPE_READBACK, wide_block * 2, D3D12_RESOURCE_STATE_COPY_DEST);
  auto submit = [&]() {
    if (!Succeeded(list->Close(), "command list")) return false;
    ID3D12CommandList* frame_lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, frame_lists);
    static UINT64 fence_value = 1;
    queue->Signal(fence.Get(), ++fence_value);
    fence->SetEventOnCompletion(fence_value, event);
    WaitForSingleObject(event, INFINITE);
    if (!Succeeded(device->GetDeviceRemovedReason(), "device")) return false;
    allocator->Reset();
    list->Reset(allocator.Get(), nullptr);
    list->SetDescriptorHeaps(1, heaps);
    return true;
  };
  allocator->Reset();
  list->Reset(allocator.Get(), nullptr);
  list->SetDescriptorHeaps(1, heaps);
  // The inputs of a frame: depth and stencil of the scene (as uploaded above,
  // the jittered scene or not) and the final image.
  UINT frame_depth_srv = depth_srv, frame_stencil_srv = stencil_srv;
  std::vector<std::string> written_this_frame;
  auto find_previous = [&](const std::string& name) -> Texture* {
    Texture* texture = find_texture(name);
    return texture ? texture->previous : nullptr;
  };
  // Runs the final point's passes with the params of the manifest and set.
  auto run_final = [&](const std::map<std::string, double>& set,
                       D3D12_GPU_VIRTUAL_ADDRESS scene_constants) {
    uint32_t constants[post_pass::kPassConstantCount] = {};
    uint32_t* values = constants + sizeof(post_pass::PassHeader) / 4;
    std::map<std::string, double> condition_values = {{"XE_SCENE", 1.0}};
    for (const auto& define : defines) {
      condition_values[define.first] = std::atof(define.second.c_str());
    }
    for (size_t i = 0; i < params.size(); ++i) {
      double value = set.count(params[i].name) ? set.at(params[i].name) : params[i].value;
      float clamped = std::clamp(float(value), float(params[i].min), float(params[i].max));
      if (params[i].is_uint) {
        values[i] = uint32_t(clamped);
      } else {
        std::memcpy(&values[i], &clamped, 4);
      }
      condition_values[params[i].name] = params[i].is_uint ? values[i] : clamped;
    }
    for (Pass& pass : compiled_only) {
      if (pass.point != "final") continue;
      if (!pass.condition.empty()) {
        bool valid;
        bool enabled = ConditionHolds(pass.condition, condition_values, valid);
        if (!valid) {
          std::printf("FAIL pass %s: condition %s\n", pass.name.c_str(), pass.condition.c_str());
          ++failures;
          return false;
        }
        if (!enabled) continue;
      }
      D3D12_GPU_DESCRIPTOR_HANDLE srvs[post_pass::kMaxSrvs], uavs[post_pass::kMaxUavs];
      for (auto& handle : srvs) handle = gpu(frame_depth_srv);
      for (size_t i = 0; i < pass.srvs.size(); ++i) {
        const std::string& name = pass.srvs[i].texture;
        if (name == "scene_depth") continue;
        if (name == "scene_stencil") {
          srvs[i] = gpu(frame_stencil_srv);
        } else if (name == "final_color") {
          transition(final_texture.Get(), final_state,
                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
          srvs[i] = gpu(final_srv);
        } else if (name == "previous_depth") {
          srvs[i] = gpu(previous_depth_srv);
        } else {
          Texture* texture = pass.srvs[i].previous ? find_previous(name) : find_texture(name);
          if (!texture) {
            std::printf("FAIL pass %s: no texture %s\n", pass.name.c_str(), name.c_str());
            ++failures;
            return false;
          }
          transition(texture->resource.Get(), texture->state,
                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
          srvs[i] = gpu(pass.srvs[i].mip < 0 ? texture->srv : texture->srv_mips[pass.srvs[i].mip]);
        }
      }
      post_pass::PassHeader header = {{kWidth, kHeight}, {1.0f / kWidth, 1.0f / kHeight}};
      std::memcpy(constants, &header, sizeof(header));
      UINT constant_count = UINT(4 + std::max(params.size(), size_t(1)));
      if (!pass.pixel) {
        for (size_t i = 0; i < post_pass::kMaxUavs; ++i) {
          const Binding& binding = pass.uavs[std::min(i, pass.uavs.size() - 1)];
          Texture* texture = find_texture(binding.texture);
          if (!texture) {
            std::printf("FAIL pass %s: no texture %s\n", pass.name.c_str(),
                        binding.texture.c_str());
            ++failures;
            return false;
          }
          transition(texture->resource.Get(), texture->state,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          uavs[i] = gpu(texture->uavs[std::max(binding.mip, 0)]);
          if (texture->history) written_this_frame.push_back(texture->name);
        }
        if (!pass.pipeline) {
          D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
          desc.pRootSignature = root_signature.Get();
          desc.CS = {pass.code->GetBufferPointer(), pass.code->GetBufferSize()};
          if (!Succeeded(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pass.pipeline)),
                         pass.name.c_str())) {
            return false;
          }
        }
        list->SetPipelineState(pass.pipeline.Get());
        list->SetComputeRootSignature(root_signature.Get());
        list->SetComputeRootConstantBufferView(post_pass::kRootSceneConstants, scene_constants);
        list->SetComputeRoot32BitConstants(post_pass::kRootPassConstants, constant_count, constants,
                                           0);
        for (UINT i = 0; i < post_pass::kMaxSrvs; ++i) {
          list->SetComputeRootDescriptorTable(post_pass::kRootSrvFirst + i, srvs[i]);
        }
        for (UINT i = 0; i < post_pass::kMaxUavs; ++i) {
          list->SetComputeRootDescriptorTable(post_pass::kRootUavFirst + i, uavs[i]);
        }
        list->Dispatch((kWidth + pass.group[0] - 1) / pass.group[0],
                       (kHeight + pass.group[1] - 1) / pass.group[1], 1);
        continue;
      }
      // A pixel pass: its render targets, and the pipeline of PixelPipeline.
      D3D12_CPU_DESCRIPTOR_HANDLE rtvs[post_pass::kMaxRenderTargets];
      DXGI_FORMAT formats[post_pass::kMaxRenderTargets] = {};
      for (size_t i = 0; i < pass.rtvs.size(); ++i) {
        if (pass.rtvs[i].texture == "final_color") {
          transition(final_texture.Get(), final_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
          rtvs[i] = rtv_cpu(final_rtv);
          formats[i] = kFinalFormat;
          continue;
        }
        Texture* texture = find_texture(pass.rtvs[i].texture);
        if (!texture) {
          std::printf("FAIL pass %s: no texture %s\n", pass.name.c_str(),
                      pass.rtvs[i].texture.c_str());
          ++failures;
          return false;
        }
        transition(texture->resource.Get(), texture->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        rtvs[i] = rtv_cpu(texture->rtv);
        formats[i] = texture->format;
        if (texture->history) written_this_frame.push_back(texture->name);
      }
      if (!pass.pipeline) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root_signature.Get();
        desc.VS = {vertex_code->GetBufferPointer(), vertex_code->GetBufferSize()};
        desc.PS = {pass.code->GetBufferPointer(), pass.code->GetBufferSize()};
        for (size_t i = 0; i < pass.rtvs.size(); ++i) {
          D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[i];
          blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
          blend.BlendOpAlpha = blend.BlendOp = D3D12_BLEND_OP_ADD;
          blend.BlendEnable = pass.blend != "none";
          blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
          blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
          if (pass.blend == "alpha") {
            blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
          } else if (pass.blend == "add") {
            blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ONE;
          } else if (pass.blend == "multiply") {
            blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.DestBlend = D3D12_BLEND_SRC_COLOR;
            blend.DestBlendAlpha = D3D12_BLEND_SRC_ALPHA;
          }
          desc.RTVFormats[i] = formats[i];
        }
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = UINT(pass.rtvs.size());
        desc.SampleDesc.Count = 1;
        if (!Succeeded(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pass.pipeline)),
                       pass.name.c_str())) {
          return false;
        }
      }
      list->SetPipelineState(pass.pipeline.Get());
      list->SetGraphicsRootSignature(root_signature.Get());
      list->SetGraphicsRootConstantBufferView(post_pass::kRootSceneConstants, scene_constants);
      list->SetGraphicsRoot32BitConstants(post_pass::kRootPassConstants, constant_count, constants,
                                          0);
      for (UINT i = 0; i < post_pass::kMaxSrvs; ++i) {
        list->SetGraphicsRootDescriptorTable(post_pass::kRootSrvFirst + i, srvs[i]);
      }
      for (UINT i = 0; i < post_pass::kMaxUavs; ++i) {
        list->SetGraphicsRootDescriptorTable(post_pass::kRootUavFirst + i,
                                             gpu(textures.front().uavs[0]));
      }
      list->OMSetRenderTargets(UINT(pass.rtvs.size()), rtvs, FALSE, nullptr);
      D3D12_VIEWPORT viewport_rect = {0.0f, 0.0f, float(kWidth), float(kHeight), 0.0f, 1.0f};
      D3D12_RECT scissor = {0, 0, LONG(kWidth), LONG(kHeight)};
      list->RSSetViewports(1, &viewport_rect);
      list->RSSetScissorRects(1, &scissor);
      list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      list->DrawInstanced(3, 1, 0, 0);
    }
    return true;
  };
  // The end of a frame: the history textures written in it become the
  // previous frame's.
  auto end_frame = [&]() {
    for (Texture& texture : textures) {
      if (!texture.history ||
          std::find(written_this_frame.begin(), written_this_frame.end(), texture.name) ==
              written_this_frame.end()) {
        continue;
      }
      Texture* previous = texture.previous;
      std::swap(texture.resource, previous->resource);
      std::swap(texture.state, previous->state);
      std::swap(texture.srv, previous->srv);
      std::swap(texture.srv_mips, previous->srv_mips);
      std::swap(texture.uavs, previous->uavs);
      std::swap(texture.rtv, previous->rtv);
    }
    written_this_frame.clear();
  };
  // Uploads a final image (screen orientation, RGB 0 to 1 per pixel) into
  // final_color, and optionally the scene depth into previous_depth.
  auto upload_final = [&](const std::vector<float>& rgb) {
    uint8_t* mapped = nullptr;
    frame_upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < kHeight; ++y) {
      for (uint32_t x = 0; x < kWidth; ++x) {
        const float* c = &rgb[(y * kWidth + x) * 3];
        uint32_t packed = 3u << 30;
        for (int k = 0; k < 3; ++k) {
          packed |= uint32_t(std::lround(std::clamp(c[k], 0.0f, 1.0f) * 1023.0f)) << (10 * k);
        }
        std::memcpy(mapped + y * depth_pitch + x * 4, &packed, 4);
      }
    }
    frame_upload->Unmap(0, nullptr);
    transition(final_texture.Get(), final_state, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst = {final_texture.Get(),
                                       D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION src = {frame_upload.Get(),
                                       D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    src.PlacedFootprint.Footprint = {kFinalFormat, kWidth, kHeight, 1, depth_pitch};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  };
  // Copies final_color to readback block block; reads it as RGB 0 to 1.
  auto copy_final = [&](int block) {
    transition(final_texture.Get(), final_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {final_texture.Get(),
                                       D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION dst = {final_readback.Get(),
                                       D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint.Offset = UINT64(block) * wide_block;
    dst.PlacedFootprint.Footprint = {kFinalFormat, kWidth, kHeight, 1, depth_pitch};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  };
  auto read_final = [&](int block) {
    std::vector<float> rgb(kWidth * kHeight * 3);
    uint8_t* mapped = nullptr;
    D3D12_RANGE range = {SIZE_T(UINT64(block) * wide_block),
                         SIZE_T(UINT64(block + 1) * wide_block)};
    final_readback->Map(0, &range, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < kHeight; ++y) {
      for (uint32_t x = 0; x < kWidth; ++x) {
        uint32_t packed;
        std::memcpy(&packed, mapped + UINT64(block) * wide_block + y * depth_pitch + x * 4, 4);
        for (int k = 0; k < 3; ++k) {
          rgb[(y * kWidth + x) * 3 + k] = float((packed >> (10 * k)) & 1023u) / 1023.0f;
        }
      }
    }
    final_readback->Unmap(0, nullptr);
    return rgb;
  };

  // The motion debug view over a final image of mid gray-blue: the slot view
  // of the scene above (MOTION_DEBUG 3, left in motion_debug_color by its last
  // run) drawn rotated 180 degrees and blended by its alpha.
  {
    std::vector<float> base_rgb(kWidth * kHeight * 3);
    for (size_t i = 0; i < base_rgb.size(); i += 3) {
      base_rgb[i] = 0.25f;
      base_rgb[i + 1] = 0.5f;
      base_rgb[i + 2] = 0.75f;
    }
    upload_final(base_rgb);
    if (!run_final({{"MOTION_DEBUG", 3.0}}, constants_motion)) return 1;
    copy_final(0);
    end_frame();
    if (!submit()) return 1;
    std::vector<float> shown = read_final(0);
    double worst = 0.0;
    for (uint32_t y = 0; y < kHeight; ++y) {
      for (uint32_t x = 0; x < kWidth; ++x) {
        uint32_t scene_pixel = (kHeight - 1 - y) * kWidth + (kWidth - 1 - x);
        double alpha = slot_view[scene_pixel * 4 + 3] / 255.0;
        for (int k = 0; k < 3; ++k) {
          double want = slot_view[scene_pixel * 4 + k] / 255.0 * alpha +
                        base_rgb[(y * kWidth + x) * 3 + k] * (1.0 - alpha);
          worst = std::fmax(worst, std::fabs(shown[(y * kWidth + x) * 3 + k] - want));
        }
      }
    }
    Check(worst < 2.0 / 1023.0 + 1.0 / 255.0,
          "final point: the slot view drawn over final_color, rotated 180 degrees", worst * 1023.0);
  }

  // TAA (taa.hlsl) over frames on synthetic final images, with the motion
  // vectors, their mask and the scene depth given directly (scene layout,
  // rotated 180 degrees against the final image) and the runtime's jitter:
  // what a final pixel q shows is the scene at q + 0.5 - jitter in final
  // pixels, the scene jitter negated by the rotation.
  if (find_texture("taa_history")) {
    auto to_half = [](float value) {
      uint32_t bits;
      std::memcpy(&bits, &value, 4);
      uint32_t sign = (bits >> 16) & 0x8000u;
      int exponent = int((bits >> 23) & 0xFF) - 127 + 15;
      uint32_t mantissa = bits & 0x7FFFFFu;
      if (exponent <= 0) return uint16_t(sign);
      if (exponent >= 31) return uint16_t(sign | 0x7C00u);
      return uint16_t(sign | uint32_t(exponent) << 10 | ((mantissa + 0x1000u) >> 13));
    };
    Texture* motion_vectors_texture = find_texture("motion_vectors");
    ComPtr<ID3D12Resource> taa_upload =
        buffer(D3D12_HEAP_TYPE_UPLOAD, wide_block * 5, D3D12_RESOURCE_STATE_GENERIC_READ);
    ComPtr<ID3D12Resource> taa_constants =
        buffer(D3D12_HEAP_TYPE_UPLOAD, constants_stride, D3D12_RESOURCE_STATE_GENERIC_READ);
    ComPtr<ID3D12Resource> taa_readback =
        buffer(D3D12_HEAP_TYPE_READBACK, wide_block * 4, D3D12_RESOURCE_STATE_COPY_DEST);
    // A frame: the final image (RGB per final pixel), the stored scene depth
    // and the previous frame's, the motion (2 per scene pixel, previous minus
    // current) and the mask (2 per scene pixel, 0 or 255).
    struct Frame {
      std::vector<float> rgb, depth, previous_depth, motion;
      std::vector<uint8_t> mask;
      float jitter[2] = {};
      bool history_valid = true;
    };
    auto new_frame = [&]() {
      Frame frame;
      frame.rgb.assign(kWidth * kHeight * 3, 0.0f);
      // Flat ground at a stored depth of 0.02 (W about 12 m).
      frame.depth.assign(kWidth * kHeight, 0.02f);
      frame.previous_depth = frame.depth;
      frame.motion.assign(kWidth * kHeight * 2, 0.0f);
      frame.mask.assign(kWidth * kHeight * 2, 0);
      return frame;
    };
    auto scene_index = [](uint32_t x, uint32_t y) {
      return (kHeight - 1 - y) * kWidth + (kWidth - 1 - x);
    };
    // Runs one frame: uploads, the final point's passes, the readback of
    // final_color to block readback_block (none if negative), the swap.
    auto run_taa = [&](const Frame& frame, const std::map<std::string, double>& set,
                       int readback_block) {
      post_pass::SceneConstants constants = scene;
      constants.final_size[0] = kWidth;
      constants.final_size[1] = kHeight;
      constants.final_uv_scale[0] = constants.final_uv_scale[1] = 1.0f;
      constants.jitter[0] = frame.jitter[0];
      constants.jitter[1] = frame.jitter[1];
      constants.flags = post_pass::kSceneFlagViewKnown | post_pass::kSceneFlagMotionValid |
                        (frame.history_valid ? post_pass::kSceneFlagHistoryValid : 0u);
      std::memcpy(constants.previous_xy_mul_add, constants.xy_mul_add,
                  sizeof(constants.xy_mul_add));
      constants.previous_w_numerator = constants.w_numerator;
      constants.previous_w_depth_mul = constants.w_depth_mul;
      constants.previous_w_depth_add = constants.w_depth_add;
      constants.previous_z_from_w = constants.z_from_w;
      void* mapped_constants = nullptr;
      taa_constants->Map(0, nullptr, &mapped_constants);
      std::memcpy(mapped_constants, &constants, sizeof(constants));
      taa_constants->Unmap(0, nullptr);
      uint8_t* mapped = nullptr;
      taa_upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
      for (uint32_t y = 0; y < kHeight; ++y) {
        std::memcpy(mapped + wide_block + y * depth_pitch, &frame.depth[y * kWidth], kWidth * 4);
        std::memcpy(mapped + wide_block * 2 + y * depth_pitch, &frame.previous_depth[y * kWidth],
                    kWidth * 4);
        for (uint32_t x = 0; x < kWidth; ++x) {
          uint16_t motion[2] = {to_half(frame.motion[(y * kWidth + x) * 2]),
                                to_half(frame.motion[(y * kWidth + x) * 2 + 1])};
          std::memcpy(mapped + wide_block * 3 + y * depth_pitch + x * 4, motion, 4);
          std::memcpy(mapped + wide_block * 4 + y * depth_pitch + x * 2,
                      &frame.mask[(y * kWidth + x) * 2], 2);
        }
      }
      taa_upload->Unmap(0, nullptr);
      upload_final(frame.rgb);
      auto copy_in = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES& state,
                         DXGI_FORMAT format, int block) {
        transition(resource, state, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst = {resource, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        D3D12_TEXTURE_COPY_LOCATION src = {taa_upload.Get(),
                                           D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        src.PlacedFootprint.Offset = UINT64(block) * wide_block;
        src.PlacedFootprint.Footprint = {format, kWidth, kHeight, 1, depth_pitch};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        transition(resource, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      };
      copy_in(depth_texture.Get(), depth_state, DXGI_FORMAT_R32_FLOAT, 1);
      copy_in(previous_depth_texture.Get(), previous_depth_state, DXGI_FORMAT_R32_FLOAT, 2);
      copy_in(motion_vectors_texture->resource.Get(), motion_vectors_texture->state,
              DXGI_FORMAT_R16G16_FLOAT, 3);
      copy_in(motion_mask_texture->resource.Get(), motion_mask_texture->state,
              DXGI_FORMAT_R8G8_UNORM, 4);
      frame_depth_srv = depth_srv;
      if (!run_final(set, taa_constants->GetGPUVirtualAddress())) return false;
      if (readback_block >= 0) {
        transition(final_texture.Get(), final_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src = {final_texture.Get(),
                                           D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        D3D12_TEXTURE_COPY_LOCATION dst = {taa_readback.Get(),
                                           D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        dst.PlacedFootprint.Offset = UINT64(readback_block) * wide_block;
        dst.PlacedFootprint.Footprint = {kFinalFormat, kWidth, kHeight, 1, depth_pitch};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
      }
      end_frame();
      return submit();
    };
    auto read_taa = [&](int block) {
      std::vector<float> rgb(kWidth * kHeight * 3);
      uint8_t* mapped = nullptr;
      D3D12_RANGE range = {0, SIZE_T(wide_block * 4)};
      taa_readback->Map(0, &range, reinterpret_cast<void**>(&mapped));
      for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
          uint32_t packed;
          std::memcpy(&packed, mapped + UINT64(block) * wide_block + y * depth_pitch + x * 4, 4);
          for (int k = 0; k < 3; ++k) {
            rgb[(y * kWidth + x) * 3 + k] = float((packed >> (10 * k)) & 1023u) / 1023.0f;
          }
        }
      }
      taa_readback->Unmap(0, nullptr);
      return rgb;
    };
    auto jitter_of = [](uint32_t index, float out[2]) { post_pass::JitterOffset(index % 8, out); };
    // A tilted edge: white where x cos + y sin > c in final pixels.
    const double edge_angle = 0.3;
    const double edge_c =
        0.5 * kWidth * std::cos(edge_angle) + 0.5 * kHeight * std::sin(edge_angle);
    auto edge = [&](double x, double y) {
      return x * std::cos(edge_angle) + y * std::sin(edge_angle) > edge_c ? 1.0f : 0.0f;
    };
    std::map<std::string, double> taa_on = {{"TAA", 1.0}, {"TAA_SHARPEN", 0.0}};

    // 1. A still edge over 40 frames: it converges to the pixel coverage and
    //    holds still, while the input flips between 0 and 1 on the edge.
    std::vector<float> previous_input;
    double input_change = 0.0;
    bool ok = true;
    for (uint32_t index = 0; index < 40 && ok; ++index) {
      Frame frame = new_frame();
      jitter_of(index, frame.jitter);
      frame.history_valid = index > 0;
      for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
          // The scene jitter in final pixels is its negation.
          float value = edge(x + 0.5 + frame.jitter[0], y + 0.5 + frame.jitter[1]);
          for (int k = 0; k < 3; ++k) frame.rgb[(y * kWidth + x) * 3 + k] = value;
        }
      }
      if (!previous_input.empty()) {
        for (size_t i = 0; i < frame.rgb.size(); ++i) {
          input_change = std::fmax(input_change, std::fabs(frame.rgb[i] - previous_input[i]));
        }
      }
      previous_input = frame.rgb;
      ok = run_taa(frame, taa_on, index == 38 ? 0 : index == 39 ? 1 : -1);
    }
    if (!ok) return 1;
    std::vector<float> out38 = read_taa(0), out39 = read_taa(1);
    double coverage_error = 0.0, change = 0.0;
    int edge_pixels = 0;
    for (uint32_t y = 2; y + 2 < kHeight; ++y) {
      for (uint32_t x = 2; x + 2 < kWidth; ++x) {
        double coverage = 0.0;
        for (int sy = 0; sy < 8; ++sy) {
          for (int sx = 0; sx < 8; ++sx) {
            coverage += edge(x + (sx + 0.5) / 8.0, y + (sy + 0.5) / 8.0);
          }
        }
        coverage /= 64.0;
        uint32_t i = (y * kWidth + x) * 3;
        change = std::fmax(change, std::fabs(out39[i] - out38[i]));
        if (coverage <= 0.0 || coverage >= 1.0) continue;
        coverage_error += std::fabs(out39[i] * out39[i] - coverage);
        ++edge_pixels;
      }
    }
    coverage_error /= std::max(edge_pixels, 1);
    std::printf("taa: still edge, %d edge pixels: mean |linear - coverage| %.3f, largest change "
                "between the last two frames %.3f (input %.3f)\n",
                edge_pixels, coverage_error, change, input_change);
    Check(coverage_error < 0.1, "taa: a still edge converges to its pixel coverage",
          coverage_error);
    Check(change < 0.1 && change * 5.0 < input_change, "taa: a still edge holds still", change);

    // 2. A white box 40 pixels wide moving 4 pixels to the right per frame
    //    over a dark ground, nearer than it: no trail behind it, and the box
    //    stays white.
    double trail = 0.0, box_low = 1.0;
    for (uint32_t index = 0; index < 16 && ok; ++index) {
      Frame frame = new_frame();
      jitter_of(index, frame.jitter);
      frame.history_valid = index > 0;
      double left = 100.0 + 4.0 * index;
      for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
          double sx = x + 0.5 + frame.jitter[0], sy = y + 0.5 + frame.jitter[1];
          bool in_box = sx >= left && sx < left + 40.0 && sy >= 120.0 && sy < 220.0;
          for (int k = 0; k < 3; ++k) frame.rgb[(y * kWidth + x) * 3 + k] = in_box ? 1.0f : 0.0f;
          bool box_pixel = x + 0.5 >= left && x + 0.5 < left + 40.0 && y + 0.5 >= 120.0 &&
                           y + 0.5 < 220.0;
          if (box_pixel) {
            uint32_t s = scene_index(x, y);
            frame.depth[s] = 0.2f;
            // Previous minus current, final pixels (-4, 0), negated for the
            // scene layout.
            frame.motion[s * 2] = 4.0f;
          }
        }
      }
      frame.previous_depth = frame.depth;
      ok = run_taa(frame, taa_on, index == 15 ? 2 : -1);
      if (index == 15) {
        if (!ok) return 1;
        std::vector<float> out = read_taa(2);
        for (uint32_t y = 124; y < 216; ++y) {
          for (uint32_t x = 0; x < kWidth; ++x) {
            double linear = out[(y * kWidth + x) * 3] * out[(y * kWidth + x) * 3];
            if (x + 0.5 < left - 2.0 && x + 0.5 > 100.0) trail = std::fmax(trail, linear);
            if (x + 0.5 > left + 2.0 && x + 0.5 < left + 38.0) box_low = std::fmin(box_low, linear);
          }
        }
      }
    }
    if (!ok) return 1;
    std::printf("taa: moving box: trail %.3f, box at least %.3f\n", trail, box_low);
    Check(trail < 0.05, "taa: no trail behind a moving box", trail);
    Check(box_low > 0.9, "taa: the moving box keeps its color", box_low);

    // 3. The history is not used without xe_history_valid(): a white frame
    //    after the box comes out white.
    {
      Frame frame = new_frame();
      frame.history_valid = false;
      std::fill(frame.rgb.begin(), frame.rgb.end(), 1.0f);
      if (!run_taa(frame, taa_on, 3)) return 1;
      std::vector<float> out = read_taa(3);
      float darkest = *std::min_element(out.begin(), out.end());
      Check(darkest > 0.99f, "taa: no history used without xe_history_valid()", darkest);
    }

    // 4. The weight of the current frame (TAA_DEBUG 2, its red channel) after
    //    20 still frames of a flat gray: 1 under the mask's R (no motion
    //    known), at least TAA_REACTIVE under G (reactive), TAA_BLEND elsewhere;
    //    and (TAA_DEBUG 4) yellow where the previous scene depth was nearer,
    //    but not along a thin near line that the jitter moves across pixels.
    for (int view : {2, 4}) {
      std::vector<float> last_depth;
      for (uint32_t index = 0; index < 20 && ok; ++index) {
        Frame frame = new_frame();
        jitter_of(index, frame.jitter);
        frame.history_valid = index > 0;
        std::fill(frame.rgb.begin(), frame.rgb.end(), 0.5f);
        for (uint32_t y = 40; y < 100; ++y) {
          for (uint32_t x = 40; x < 140; ++x) frame.mask[scene_index(x, y) * 2] = 255;
          for (uint32_t x = 240; x < 340; ++x) frame.mask[scene_index(x, y) * 2 + 1] = 255;
          // Something at a stored depth of 0.5 (W about 0.5 m) was there.
          for (uint32_t x = 440; x < 540; ++x) frame.previous_depth[scene_index(x, y)] = 0.5f;
        }
        // A line 0.6 pixels wide at a stored depth of 0.5, as the jittered
        // draws render it.
        for (uint32_t y = 200; y < 300; ++y) {
          for (uint32_t x = 290; x < 310; ++x) {
            double sx = x + 0.5 + frame.jitter[0];
            if (sx >= 300.3 && sx < 300.9) {
              frame.depth[scene_index(x, y)] = 0.5f;
              for (int k = 0; k < 3; ++k) frame.rgb[(y * kWidth + x) * 3 + k] = 1.0f;
            }
          }
        }
        if (!last_depth.empty()) {
          for (uint32_t y = 200; y < 300; ++y) {
            for (uint32_t x = 290; x < 310; ++x) {
              frame.previous_depth[scene_index(x, y)] = last_depth[scene_index(x, y)];
            }
          }
        }
        last_depth = frame.depth;
        std::map<std::string, double> set = taa_on;
        set["TAA_DEBUG"] = view;
        ok = run_taa(frame, set, index == 19 ? 0 : -1);
      }
      if (!ok) return 1;
      std::vector<float> out = read_taa(0);
      auto at = [&](uint32_t x, uint32_t y, int k) { return out[(y * kWidth + x) * 3 + k]; };
      if (view == 2) {
        Check(at(90, 70, 0) > 0.99f, "taa: all current frame under mask R", at(90, 70, 0));
        Check(at(290, 70, 0) > 0.49f, "taa: at least TAA_REACTIVE under mask G", at(290, 70, 0));
        Check(std::fabs(at(320, 250, 0) - 0.1f) < 0.01f, "taa: TAA_BLEND on a settled history",
              at(320, 250, 0));
      } else {
        bool yellow = at(490, 70, 0) > 0.9f && at(490, 70, 1) > 0.8f && at(490, 70, 2) < 0.2f;
        Check(yellow, "taa: a history behind a nearer previous surface is dropped",
              at(490, 70, 1));
        Check(at(320, 250, 0) < 0.5f, "taa: the history kept elsewhere", at(320, 250, 0));
        int line_dropped = 0;
        for (uint32_t x = 296; x < 305; ++x) {
          line_dropped += at(x, 250, 0) > 0.9f && at(x, 250, 1) > 0.8f && at(x, 250, 2) < 0.2f;
        }
        Check(line_dropped == 0, "taa: a jittered thin line keeps its history", line_dropped);
      }
    }
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
