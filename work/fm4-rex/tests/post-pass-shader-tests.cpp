// Tests of the post passes (work/fm4-rex/passes) on WARP, the CPU Direct3D 12
// device: no GPU is used. The compute passes of the manifest's scene_depth
// point run as the runtime's post pass framework (post_pass.cpp) runs them:
// compiled with FXC against the library post_pass.hlsli, with its root
// signature and constant layouts (post_pass_layout.h), the textures, params,
// defines, bindings and conditions of passes.toml. Their input is a synthetic
// scene traced into a stored depth texture the way FM4 stores it (reversed Z
// through the viewport). The GTAO occlusion (the texture in slot 0) is checked
// where the answer is known: sky and open ground without occlusion, the corner
// of a wall and the ground, and the contact of a box, darker; and its W debug
// view (GTAO_DEBUG 1) against the traced W.
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
  std::string texture;  // or scene_depth
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
      texture.format = format == "R32_FLOAT"  ? DXGI_FORMAT_R32_FLOAT
                       : format == "R8_UNORM" ? DXGI_FORMAT_R8_UNORM
                       : format == "R16_FLOAT" ? DXGI_FORMAT_R16_FLOAT
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
  std::vector<Pass> passes;
  if (const toml::array* array = manifest["passes"].as_array()) {
    for (const toml::node& node : *array) {
      const toml::table& fields = *node.as_table();
      if (fields["point"].value_or(std::string()) != "scene_depth") continue;
      Pass pass;
      pass.name = fields["name"].value_or(std::string());
      pass.shader = fields["shader"].value_or(std::string());
      pass.entry = fields["entry"].value_or(std::string("main"));
      pass.condition = fields["if"].value_or(std::string());
      if (fields["type"].value_or(std::string("compute")) != "compute") {
        std::printf("FAIL pass %s: only compute passes run here\n", pass.name.c_str());
        return 1;
      }
      if (const toml::table* pass_defines = fields["defines"].as_table()) {
        for (auto&& [key, value] : *pass_defines) {
          pass.defines.emplace_back(std::string(key.str()), DefineText(value));
        }
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
  // screen = (host + 0.5) * 2 - 0.5).
  std::vector<float> depth(kWidth * kHeight);
  std::vector<int> surface(kWidth * kHeight);
  std::vector<double> hit_y(kWidth * kHeight), view_w(kWidth * kHeight);
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      double gx = (x + 0.5) * guest_per_host - 0.5, gy = (y + 0.5) * guest_per_host - 0.5;
      double ndc_x = (gx - viewport.x_offset) / viewport.x_scale;
      double ndc_y = (gy - viewport.y_offset) / viewport.y_scale;
      // View direction with z = 1 (P has no off-center terms here).
      double dx = ndc_x / projection.m[0][0], dy = ndc_y / projection.m[1][1];
      double t = Trace(dx, dy, 1.0, hit_y[y * kWidth + x], surface[y * kWidth + x]);
      double ndc_z = t > 0.0 ? q - q * near_z / t : 1.0;  // Sky at the far plane.
      depth[y * kWidth + x] = float(ndc_z * viewport.z_scale + viewport.z_offset);
      view_w[y * kWidth + x] = t > 0.0 ? t : far_z;
    }
  }

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
  for (Pass& pass : passes) {
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
    ComPtr<ID3DBlob> code, messages;
    HRESULT result = D3DCompile(source.data(), source.size(), pass.name.c_str(), macros.data(),
                                &include, pass.entry.c_str(), "cs_5_1",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
    if (messages) std::printf("%s\n", static_cast<const char*>(messages->GetBufferPointer()));
    if (!Succeeded(result, pass.name.c_str())) return 1;
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
  Texture* occlusion_texture = nullptr;
  for (Texture& texture : textures) {
    if (texture.slot == 0) occlusion_texture = &texture;
  }
  if (!occlusion_texture || occlusion_texture->format != DXGI_FORMAT_R8_UNORM) {
    std::printf("FAIL no R8_UNORM texture in slot 0 (xe_post_ao)\n");
    return 1;
  }

  // Upload and readback buffers (rows aligned to 256 bytes).
  const UINT depth_pitch = (kWidth * 4 + 255) & ~255u, r8_pitch = (kWidth + 255) & ~255u;
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
  ComPtr<ID3D12Resource> upload = buffer(D3D12_HEAP_TYPE_UPLOAD, UINT64(depth_pitch) * kHeight,
                                         D3D12_RESOURCE_STATE_GENERIC_READ);
  ComPtr<ID3D12Resource> constants_buffer =
      buffer(D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_STATE_GENERIC_READ);
  ComPtr<ID3D12Resource> readback = buffer(D3D12_HEAP_TYPE_READBACK, UINT64(r8_pitch) * kHeight * 2,
                                           D3D12_RESOURCE_STATE_COPY_DEST);
  if (failures) return 1;
  {
    uint8_t* mapped = nullptr;
    upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < kHeight; ++y) {
      std::memcpy(mapped + y * depth_pitch, &depth[y * kWidth], kWidth * 4);
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
  {
    void* mapped = nullptr;
    constants_buffer->Map(0, nullptr, &mapped);
    std::memcpy(mapped, &scene, sizeof(scene));
    constants_buffer->Unmap(0, nullptr);
  }

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
  transition(depth_texture.Get(), depth_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap* heaps[] = {heap.Get()};
  list->SetDescriptorHeaps(1, heaps);

  // Runs the point's passes with the params and their debug view, then copies
  // the slot 0 texture to readback row block `block`.
  auto run_point = [&](uint32_t debug, uint32_t block) {
    uint32_t constants[post_pass::kPassConstantCount] = {};
    post_pass::PassHeader header = {{kWidth, kHeight}, {1.0f / kWidth, 1.0f / kHeight}};
    std::memcpy(constants, &header, sizeof(header));
    uint32_t* values = constants + sizeof(header) / 4;
    std::vector<float> floats(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
      double value = params[i].name == "GTAO_DEBUG" ? double(debug) : params[i].value;
      floats[i] = std::clamp(float(value), float(params[i].min), float(params[i].max));
      if (params[i].is_uint) {
        values[i] = uint32_t(floats[i]);
      } else {
        std::memcpy(&values[i], &floats[i], 4);
      }
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
      // NAME OP NUMBER (the manifest's GTAO conditions are single terms).
      if (!pass.condition.empty()) {
        char name[64] = {}, op[3] = {};
        double number = 0.0;
        if (std::sscanf(pass.condition.c_str(), " %63[A-Z_0-9] %2[<>=!] %lf", name, op, &number) !=
            3) {
          std::printf("FAIL pass %s: condition %s\n", pass.name.c_str(), pass.condition.c_str());
          ++failures;
          return;
        }
        double value = 0.0;
        for (const auto& define : defines) {
          if (define.first == name) value = std::atof(define.second.c_str());
        }
        for (size_t i = 0; i < params.size(); ++i) {
          if (params[i].name == name) value = params[i].is_uint ? values[i] : floats[i];
        }
        std::string o = op;
        bool enabled = o == "==" ? value == number
                       : o == "!=" ? value != number
                       : o == "<"  ? value < number
                       : o == "<=" ? value <= number
                       : o == ">"  ? value > number
                                   : value >= number;
        if (!enabled) continue;
      }
      D3D12_GPU_DESCRIPTOR_HANDLE srvs[post_pass::kMaxSrvs], uavs[post_pass::kMaxUavs];
      for (auto& handle : srvs) handle = gpu(depth_srv);
      for (size_t i = 0; i < pass.srvs.size(); ++i) {
        if (pass.srvs[i].texture == "scene_depth") continue;
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
      list->SetComputeRootConstantBufferView(post_pass::kRootSceneConstants,
                                             constants_buffer->GetGPUVirtualAddress());
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
    transition(occlusion_texture->resource.Get(), occlusion_texture->state,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {occlusion_texture->resource.Get(),
                                       D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION dst = {readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint.Offset = UINT64(block) * r8_pitch * kHeight;
    dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8_UNORM, kWidth, kHeight, 1, r8_pitch};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  };
  uint32_t debug = 0;
  for (const Param& param : params) {
    if (param.name == "GTAO_DEBUG") debug = uint32_t(param.value);
  }
  run_point(debug, 0);
  run_point(1, 1);  // The W debug view.
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
  {
    uint8_t* mapped = nullptr;
    D3D12_RANGE range = {0, SIZE_T(r8_pitch) * kHeight * 2};
    readback->Map(0, &range, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < kHeight; ++y) {
      std::memcpy(&occlusion[y * kWidth], mapped + y * r8_pitch, kWidth);
      std::memcpy(&w_debug[y * kWidth], mapped + (kHeight + y) * r8_pitch, kWidth);
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
    if (surface[i] && view_w[i] < 90.0) {
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
