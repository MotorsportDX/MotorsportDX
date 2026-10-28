// Compile tests of the shader override library (work/runtime/src/graphics/
// d3d12/shader_override.hlsli) and the look mods (work/fm4-rex/look) with FXC,
// as shader_override.cpp compiles them (ps_5_1, unbounded descriptor tables,
// IEEE strictness, /O3), without the game: the contract of a DXBC translation
// is synthetic here, a pixel shader with every fetch constant bound as 2D, 3D
// and cube, and every float constant. Each mod is compiled after the library
// and before a synthetic override that fetches and reads constants through the
// hooks, like a generated one. Each mod is compiled at its default knobs and
// with every look knob away from the original look, which must change its
// DXBC. The real contracts of a run define only the fetches and constants their
// shader reads: override_fxc.py --stub compiles the mods against those.
//
// The library's code under #ifdef XE_POST_VIEWS (the textures of the post
// passes) is only compiled when the contract defines it, so both contracts are
// compiled, and the disassembly must show the occlusion texture read (resinfo,
// from xe_post_ao) exactly where it is expected: in an override that calls
// xe_post_ao with the define, never without it, never in a mod at its default
// knobs, and in at least one mod with FM4_LOOK_AO=1 and with
// FM4_LOOK_AO_DEBUG=1.
//
// With live knobs (shader_mod_live_knobs) the runtime puts the knobs after the
// system constants and defines each as xe_mod_knob(<slot>): every mod must also
// compile that way, with every knob of the look files live, and read the knobs
// from the constant buffer.
//
// With the defines of a look (pipeline.py passes the launcher's default one),
// every mod is also compiled with them. The compiles with knobs print an FNV-1a
// fingerprint of their DXBC, so two versions of the library or the mods can be
// compared for the output they give without live knobs.
//
// usage: fm4_shader_mod_tests <shader_override.hlsli> <look directory> [NAME=VALUE;...]

#include <windows.h>

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <set>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

int failures = 0;

void Check(bool condition, const std::string& what) {
  std::printf("%s %s\n", condition ? "ok  " : "FAIL", what.c_str());
  if (!condition) ++failures;
}

bool ReadFile(const std::filesystem::path& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

constexpr uint32_t kFetchConstants = 32;
constexpr uint32_t kInterpolators = 16;
// Any index past the system views: only compiled, never bound (3 is the
// runtime's).
constexpr uint32_t kPostViews = 3;
// The synthetic system constants are 16 vectors; the host values (one vector,
// ShaderOverrides::kHostValuesSize) and the live knobs
// (ShaderOverrides::kKnobCount) follow them.
constexpr uint32_t kSystemConstantVectors = 16;
constexpr uint32_t kHostValueVectors = 1;
constexpr uint32_t kKnobCount = 64;

// The native shader contract's part of a contract (ShaderOverrides::BuildContract
// for an entry of shaders.toml, docs/analysis/shader-contract.md): none, the pass
// class, or the pass class with native_shader_pass_debug.
enum class Native { kNone, kPass, kPassDebug };

// What ShaderOverrides::BuildContract writes for a pixel shader with color
// target 0, all interpolators, every fetch constant bound as 2D, 3D and cube
// (unsigned and signed) with its default sampler, and every float constant.
std::string Contract(bool post_ao, bool live_knobs, Native native = Native::kNone) {
  std::string out = "// Synthetic contract (fm4_shader_mod_tests).\n#define XE_PIXEL_SHADER 1\n";
  auto line = [&out](const std::string& text) { out += text + "\n"; };
  if (live_knobs) {
    line("#define XE_SYSTEM_CONSTANT_VECTORS " +
         std::to_string(kSystemConstantVectors + kHostValueVectors + kKnobCount / 4));
    line("#define XE_SYS_MOD_KNOBS " +
         std::to_string((kSystemConstantVectors + kHostValueVectors) * 16) + "u");
  } else {
    line("#define XE_SYSTEM_CONSTANT_VECTORS " +
         std::to_string(kSystemConstantVectors + kHostValueVectors));
  }
  line("#define XE_SYS_GUEST_VBLANK_RATE " + std::to_string(kSystemConstantVectors * 16) + "u");
  line("#define XE_SYS_GUEST_FRAME_INTERVAL " + std::to_string(kSystemConstantVectors * 16 + 4) +
       "u");
  line("#define XE_SYS_GUEST_DESIGN_FRAME_RATE " + std::to_string(kSystemConstantVectors * 16 + 8) +
       "u");
  if (native != Native::kNone) {
    line("#define XE_NATIVE_CONTRACT 1");
    line("#define XE_SYS_NATIVE_PASS " + std::to_string(kSystemConstantVectors * 16 + 12) + "u");
    const char* const kPasses[] = {"OTHER", "POST", "HUD", "SCENE", "MIRROR"};
    for (uint32_t pass = 0; pass < std::size(kPasses); ++pass) {
      line(std::string("#define XE_NATIVE_PASS_") + kPasses[pass] + " " + std::to_string(pass) +
           "u");
    }
    if (native == Native::kPassDebug) {
      line("#define XE_NATIVE_PASS_DEBUG 1");
    }
  }
  line("#define XE_SYS_FLAGS 0u");
  line("#define XE_SYS_TEXTURE_SWIZZLED_SIGNS 16u");
  line("#define XE_SYS_TEXTURES_RESOLUTION_SCALED 48u");
  line("#define XE_SYS_SAMPLE_COUNT_LOG2 52u");
  line("#define XE_SYS_ALPHA_TEST_REFERENCE 60u");
  line("#define XE_SYS_ALPHA_TO_MASK 64u");
  line("#define XE_SYS_COLOR_EXP_BIAS 80u");
  line("#define XE_SYS_TEXTURE_INTEGER_SCALE_BITS 96u");
  line("#define XE_SYSFLAG_PRIMITIVE_POLYGONAL 2u");
  line("#define XE_SYSFLAG_PRIMITIVE_LINE_SHIFT 2u");
  line("#define XE_SYSFLAG_DEPTH_FLOAT24 8u");
  line("#define XE_SYSFLAG_ALPHA_PASS_IF_LESS_SHIFT 4u");
  line("#define XE_SYSFLAG_CONVERT_COLOR0_TO_GAMMA_SHIFT 7u");
  line("#define XE_RESOLUTION_SCALE_X 2");
  line("#define XE_RESOLUTION_SCALE_Y 2");
  line("#define XE_REVERT_RESOLUTION_SCALE_AXES 3u");
  line("#define XE_GAMMA_RENDER_TARGET_AS_UNORM8 1");
  line("#define XE_MSAA_2X_SUPPORTED 1");
  line("#define XE_FUZZY_ALPHA_EPSILON 0");
  if (post_ao) {
    line("#define XE_POST_VIEWS " + std::to_string(kPostViews) + "u");
    line("#define XE_POST_VIEW_COUNT 8u");
  }
  line("#define XE_REGISTER_COUNT " + std::to_string(kInterpolators));
  line("#define XE_INTERPOLATOR_MASK 0xFFFFu");
  line("#define XE_PARAM_GEN_ENABLED 0");
  line("#define XE_PARAM_GEN_INTERPOLATOR 0");
  line("#define XE_PARAM_GEN_POINT 0");
  line("#define XE_COLOR_TARGETS_WRITTEN 0x1");
  line("#define XE_WRITES_DEPTH 0");
  line("#define XE_FORCE_EARLY_DEPTH_STENCIL 0");
  line("#define XE_PS_ATTRIBUTES ");
  line("#define XE_FLOAT_CONSTANT_COUNT 256");
  for (uint32_t i = 0; i < 256; ++i) {
    line("#define XE_FLOAT_CONSTANT_" + std::to_string(i) + " xe_float_constants[" +
         std::to_string(i) + "]");
  }
  uint32_t slot = 0;
  std::string slots;
  for (uint32_t fetch = 0; fetch < kFetchConstants; ++fetch) {
    for (const char* dimension : {"2D", "3D", "CUBE"}) {
      for (char sign : {'U', 'S'}) {
        slots += "#define XE_TEXTURE" + std::to_string(fetch) + "_" + dimension + "_" + sign +
                 " " + std::to_string(slot++) + "u\n";
      }
    }
    slots += "#define XE_SAMPLER" + std::to_string(fetch) + "_FFF " + std::to_string(slot++) +
             "u\n";
  }
  line("#define XE_DESCRIPTOR_INDEX_VECTORS " + std::to_string((slot + 3) / 4));
  out += slots;
  line("struct XePsInput {");
  for (uint32_t i = 0; i < kInterpolators; ++i) {
    line("  float4 xe_interpolator" + std::to_string(i) + " : TEXCOORD" + std::to_string(i) +
         ";");
  }
  line("  float4 xe_position : SV_Position;");
  line("  bool xe_is_front_face : SV_IsFrontFace;");
  line("};");
  line("struct XePsOutput {");
  line("  float4 xe_color0 : SV_Target0;");
  line("  uint xe_coverage : SV_Coverage;");
  line("};");
  out += "#define XE_LOAD_INTERPOLATORS(p, input)";
  for (uint32_t i = 0; i < kInterpolators; ++i) {
    out += " p.r[" + std::to_string(i) + "] = input.xe_interpolator" + std::to_string(i) + ";";
  }
  out += "\n";
  return out;
}

// An override in the form HlslShaderTranslator generates: every texture fetch
// through XE_HOOK_TFETCH (2D, 3D and cube on the fetch constants the look mods
// hook) and constants through XE_CONSTANT.
const char kOverride[] = R"(void xe_pixel_shader(inout XePixel p) {
  float4 r0 = p.r[0], r1 = p.r[1], r2 = p.r[2], r3 = p.r[3];
  float4 sum = 0.0;
  {
    float4 t = XE_HOOK_TFETCH(0u, XE_TFETCH2D(0, r0.xy), r0);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(1u, XE_TFETCH2D(1, r1.zw), r1.zwxy);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(2u, XE_TFETCH2D(2, r0.zw), r0.zwxy);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(4u, XE_TFETCH2D(4, r1.xy), r1);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(13u, XE_TFETCH2D(13, r3.xy), r3);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(7u, XE_TFETCH3D(7, saturate(sum.xyz)), float4(saturate(sum.xyz), 0.0));
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(5u, XE_TFETCHCUBE(5, r2.xyz), r2);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(6u, XE_TFETCHCUBE(6, r2.zyx), r2.zyxw);
    sum += t;
  }
  for (uint i = 0u; i < 3u; ++i) {
    sum += xe_mul(sum, XE_CONSTANT(0));
  }
  sum += XE_CONSTANT(2) + XE_CONSTANT(4) + XE_CONSTANT(5) + XE_CONSTANT(8) + XE_CONSTANT(19) +
         XE_CONSTANT(22) + XE_CONSTANT(59) + XE_CONSTANT(255);
  // The shadow floor constants of the cockpit interior shaders (fm4-cockpit.hlsli).
  sum += XE_CONSTANT(82) + XE_CONSTANT(83) + XE_CONSTANT(84) + XE_CONSTANT(85) + XE_CONSTANT(86) +
         XE_CONSTANT(87) + XE_CONSTANT(88) + XE_CONSTANT(89) + XE_CONSTANT(90) + XE_CONSTANT(91);
  p.color[0] = sum;
}
)";

// An override that only outputs the occlusion.
const char kPostAoOverride[] = R"(void xe_pixel_shader(inout XePixel p) {
  p.color[0] = xe_post_ao(p.r[0].xy).xxxx;
}
)";

// What ShaderOverrides::BuildContract adds for a replacement entry of shaders.toml
// (shader = "<file>.hlsli", docs/analysis/shader-contract.md 8) with these inputs:
// scene = "tf0", lut = "tf7", env = "tf5", uv = "i0.xy", white = "i1.z", filmic = "c5",
// vignette = "b133", pixel = "position", face = "front_face".
const char kReplacementContract[] = R"(#define XE_REPLACEMENT 1
struct XeInputs {
  float4 filmic;
  float2 uv;
  float white;
  bool vignette;
  float4 pixel;
  bool face;
};
#define xe_tex_env(coord) xe_tfetch_cube(5u, XE_TEXTURE5_CUBE_U, XE_TEXTURE5_CUBE_S, XE_SAMPLER5_FFF, (coord), 0.0, false, XE_LOD_COMPUTED, 0.0, 0.0)
#define XE_TEX_ENV 5u
#define XE_IN_ENV 1
#define XE_IN_FACE 1
#define XE_IN_FILMIC 1
#define xe_tex_lut(coord) xe_tfetch_3d(7u, XE_TEXTURE7_3D_U, XE_TEXTURE7_3D_S, XE_TEXTURE7_2D_U, XE_TEXTURE7_2D_S, XE_SAMPLER7_FFF, (coord), 0.0, false, XE_LOD_COMPUTED, 0.0, 0.0, XE_FILTER_FETCH_CONSTANT, XE_FILTER_FETCH_CONSTANT)
#define XE_TEX_LUT 7u
#define XE_IN_LUT 1
#define XE_IN_PIXEL 1
#define xe_tex_scene(coord) xe_tfetch_2d(0u, XE_TEXTURE0_2D_U, XE_TEXTURE0_2D_S, XE_SAMPLER0_FFF, (coord), 0.0, false, XE_LOD_COMPUTED, 0.0, 0.0)
#define XE_TEX_SCENE 0u
#define XE_IN_SCENE 1
#define XE_IN_UV 1
#define XE_IN_VIGNETTE 1
#define XE_IN_WHITE 1
#define XE_LOAD_INPUTS(xe_in, p, input) xe_in.face = input.xe_is_front_face; xe_in.filmic = xe_float_constants[5]; xe_in.pixel = input.xe_position; xe_in.uv = p.r[0].xy; xe_in.vignette = xe_bool_constant(133u); xe_in.white = p.r[1].z;
)";

// The replacement part of the contract of the look's final combine entry (shaders.toml,
// fm4-final-combine.ps.hlsli), in the order the parser keeps its inputs.
const char kFinalCombineContract[] = R"(#define XE_REPLACEMENT 1
struct XeInputs {
  float exposure;
  float4 filmic_ab;
  float4 filmic_cdef;
  float lut_offset;
  float lut_scale;
  float4 transform_b;
  float4 transform_g;
  float4 transform_r;
  float2 uv;
  float white;
};
#define xe_tex_bloom(coord) xe_tfetch_2d(2u, XE_TEXTURE2_2D_U, XE_TEXTURE2_2D_S, XE_SAMPLER2_FFF, (coord), 0.0, false, XE_LOD_COMPUTED, 0.0, 0.0)
#define XE_TEX_BLOOM 2u
#define XE_IN_BLOOM 1
#define XE_IN_EXPOSURE 1
#define XE_IN_FILMIC_AB 1
#define XE_IN_FILMIC_CDEF 1
#define xe_tex_lut(coord) xe_tfetch_3d(7u, XE_TEXTURE7_3D_U, XE_TEXTURE7_3D_S, XE_TEXTURE7_2D_U, XE_TEXTURE7_2D_S, XE_SAMPLER7_FFF, (coord), 0.0, false, XE_LOD_COMPUTED, 0.0, 0.0, XE_FILTER_FETCH_CONSTANT, XE_FILTER_FETCH_CONSTANT)
#define XE_TEX_LUT 7u
#define XE_IN_LUT 1
#define XE_IN_LUT_OFFSET 1
#define XE_IN_LUT_SCALE 1
#define xe_tex_scene(coord) xe_tfetch_2d(0u, XE_TEXTURE0_2D_U, XE_TEXTURE0_2D_S, XE_SAMPLER0_FFF, (coord), 0.0, false, XE_LOD_COMPUTED, 0.0, 0.0)
#define XE_TEX_SCENE 0u
#define XE_IN_SCENE 1
#define XE_IN_TRANSFORM_B 1
#define XE_IN_TRANSFORM_G 1
#define XE_IN_TRANSFORM_R 1
#define XE_IN_UV 1
#define XE_IN_WHITE 1
#define XE_SRV_GTAO_OCCLUSION 1
#define xe_srv_gtao_occlusion(uv) xe_post_texture(0u, uv)
#define XE_LOAD_INPUTS(xe_in, p, input) xe_in.exposure = p.r[1].w; xe_in.filmic_ab = xe_float_constants[5]; xe_in.filmic_cdef = xe_float_constants[6]; xe_in.lut_offset = xe_float_constants[1].x; xe_in.lut_scale = xe_float_constants[0].x; xe_in.transform_b = xe_float_constants[4]; xe_in.transform_g = xe_float_constants[3]; xe_in.transform_r = xe_float_constants[2]; xe_in.uv = p.r[0].xy; xe_in.white = p.r[1].z;
)";

// A replacement that reads every kind of input by name.
const char kReplacement[] = R"(void xe_replacement(XeInputs inputs, inout XePixel p) {
  float4 scene = xe_tex_scene(inputs.uv);
  float4 lut = xe_tex_lut(float3(saturate(scene.rg), inputs.white));
  float4 env = xe_tex_env(float3(inputs.uv, 0.0));
  float2 size = xe_texture_size_2d(XE_TEX_SCENE);
  float4 color = scene + lut + env + inputs.filmic * inputs.white;
  color.rgb += inputs.pixel.xyx / size.xyx;
  if (inputs.vignette) {
    color.rgb *= 0.5;
  }
  p.color[0] = inputs.face ? color : -color;
}
)";

// Serves "xenos_override.hlsli" (the library, as the runtime embeds it) and the
// files of the look directory (shader_mod_path).
class Include final : public ID3DInclude {
 public:
  Include(const std::string& library, const std::filesystem::path& mods)
      : library_(library), mods_(mods) {}
  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data,
                         UINT* bytes) noexcept override {
    if (std::string(name) == "xenos_override.hlsli") {
      *data = library_.data();
      *bytes = UINT(library_.size());
      return S_OK;
    }
    std::string text;
    if (mods_.empty() || !ReadFile(mods_ / name, text)) {
      return E_FAIL;
    }
    std::string& file = files_.emplace_back(std::move(text));
    *data = file.data();
    *bytes = UINT(file.size());
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID data) noexcept override {
    files_.remove_if([data](const std::string& text) { return text.data() == data; });
    return S_OK;
  }

 private:
  const std::string& library_;
  std::filesystem::path mods_;
  std::list<std::string> files_;
};

struct Result {
  bool compiled = false;
  bool reads_post_ao = false;
  // Reads a live knob: cb0 past the system constants.
  bool reads_knobs = false;
  // FNV-1a of the DXBC.
  uint64_t fingerprint = 0;
  // FXC's "Approximately N instruction slots used".
  int instructions = -1;
  std::string messages;
};

// Every knob of the look files: the names after #ifndef FM4_LOOK_.
std::vector<std::string> Knobs(const std::filesystem::path& look) {
  std::set<std::string> knobs;
  const std::string marker = "#ifndef FM4_LOOK_";
  for (const auto& entry : std::filesystem::directory_iterator(look)) {
    std::string text;
    if (entry.path().extension() != ".hlsli" || !ReadFile(entry.path(), text)) continue;
    for (size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + 1)) {
      size_t start = at + std::strlen("#ifndef "), end = start;
      while (end < text.size() && (std::isalnum(uint8_t(text[end])) || text[end] == '_')) ++end;
      knobs.insert(text.substr(start, end - start));
    }
  }
  return std::vector<std::string>(knobs.begin(), knobs.end());
}

// ShaderOverrides::Compile: contract, library, mod, override, main.
Result Compile(const std::string& library, const std::filesystem::path& mods,
               const std::string& mod, bool post_ao, const char* override_text,
               const std::vector<std::pair<std::string, std::string>>& defines,
               bool live_knobs = false, Native native = Native::kNone,
               const char* replacement_contract = "") {
  std::string source = Contract(post_ao, live_knobs, native) + replacement_contract +
                       "#include \"xenos_override.hlsli\"\n";
  if (!mod.empty()) {
    source += "#include \"" + mod + "\"\n";
  }
  source += "#line 1 \"override.ps.hlsl\"\n";
  source += override_text;
  source += "\n#define XE_OVERRIDE_MAIN\n#include \"xenos_override.hlsli\"\n";
  std::vector<D3D_SHADER_MACRO> macros;
  for (const auto& define : defines) {
    macros.push_back({define.first.c_str(), define.second.c_str()});
  }
  macros.push_back({nullptr, nullptr});
  Include include(library, mods);
  ComPtr<ID3DBlob> code, errors;
  HRESULT hr = D3DCompile(source.data(), source.size(), "main.hlsl", macros.data(), &include,
                          "main", "ps_5_1",
                          D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES |
                              D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                          0, &code, &errors);
  Result result;
  if (errors) {
    result.messages.assign(static_cast<const char*>(errors->GetBufferPointer()),
                           errors->GetBufferSize());
  }
  if (FAILED(hr) || !code) {
    return result;
  }
  result.compiled = true;
  result.fingerprint = 0xCBF29CE484222325ull;
  const uint8_t* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
  for (size_t i = 0; i < code->GetBufferSize(); ++i) {
    result.fingerprint = (result.fingerprint ^ bytes[i]) * 0x100000001B3ull;
  }
  ComPtr<ID3DBlob> disassembly;
  if (SUCCEEDED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr,
                               &disassembly))) {
    std::string text(static_cast<const char*>(disassembly->GetBufferPointer()),
                     disassembly->GetBufferSize());
    // xe_post_ao is the library's only GetDimensions.
    result.reads_post_ao = text.find("resinfo") != std::string::npos;
    const std::string slots = "// Approximately ";
    size_t at = text.find(slots);
    if (at != std::string::npos) {
      result.instructions = std::atoi(text.c_str() + at + slots.size());
    }
    // Shader Model 5.1 operands: CB<id>[<register>][<vector>].
    for (uint32_t vector = kSystemConstantVectors; vector < kSystemConstantVectors + kKnobCount / 4;
         ++vector) {
      result.reads_knobs |=
          text.find("CB0[0][" + std::to_string(vector) + "]") != std::string::npos;
    }
  }
  return result;
}

// What ShaderOverrides::BuildContract writes for a vertex shader of the host vertex stage
// (docs/analysis/shader-contract.md 9): interpolators 0 to 3 of the mask, no texture, every
// float constant; with distances, two user clip planes as clip distances, the vertex kill's
// AND rule as a cull distance and the point size.
std::string VertexContract(bool distances, bool live_knobs, Native native) {
  std::string out =
      "// Synthetic vertex contract (fm4_shader_mod_tests).\n#define XE_PIXEL_SHADER 0\n"
      "#define XE_VERTEX_SHADER 1\n";
  auto line = [&out](const std::string& text) { out += text + "\n"; };
  if (live_knobs) {
    line("#define XE_SYSTEM_CONSTANT_VECTORS " +
         std::to_string(kSystemConstantVectors + kHostValueVectors + kKnobCount / 4));
    line("#define XE_SYS_MOD_KNOBS " +
         std::to_string((kSystemConstantVectors + kHostValueVectors) * 16) + "u");
  } else {
    line("#define XE_SYSTEM_CONSTANT_VECTORS " +
         std::to_string(kSystemConstantVectors + kHostValueVectors));
  }
  line("#define XE_SYS_GUEST_VBLANK_RATE " + std::to_string(kSystemConstantVectors * 16) + "u");
  line("#define XE_SYS_GUEST_FRAME_INTERVAL " + std::to_string(kSystemConstantVectors * 16 + 4) +
       "u");
  line("#define XE_SYS_GUEST_DESIGN_FRAME_RATE " + std::to_string(kSystemConstantVectors * 16 + 8) +
       "u");
  if (native != Native::kNone) {
    line("#define XE_NATIVE_CONTRACT 1");
    line("#define XE_SYS_NATIVE_PASS " + std::to_string(kSystemConstantVectors * 16 + 12) + "u");
    const char* const kPasses[] = {"OTHER", "POST", "HUD", "SCENE", "MIRROR"};
    for (uint32_t pass = 0; pass < std::size(kPasses); ++pass) {
      line(std::string("#define XE_NATIVE_PASS_") + kPasses[pass] + " " + std::to_string(pass) +
           "u");
    }
  }
  line("#define XE_SYS_FLAGS 0u");
  line("#define XE_SYS_TEXTURE_SWIZZLED_SIGNS 16u");
  line("#define XE_SYS_TEXTURES_RESOLUTION_SCALED 48u");
  line("#define XE_SYS_SAMPLE_COUNT_LOG2 52u");
  line("#define XE_SYS_ALPHA_TEST_REFERENCE 60u");
  line("#define XE_SYS_ALPHA_TO_MASK 64u");
  line("#define XE_SYS_COLOR_EXP_BIAS 80u");
  line("#define XE_SYS_TEXTURE_INTEGER_SCALE_BITS 96u");
  line("#define XE_SYSFLAG_PRIMITIVE_POLYGONAL 2u");
  line("#define XE_SYSFLAG_PRIMITIVE_LINE_SHIFT 2u");
  line("#define XE_SYSFLAG_DEPTH_FLOAT24 8u");
  line("#define XE_SYSFLAG_ALPHA_PASS_IF_LESS_SHIFT 4u");
  line("#define XE_SYSFLAG_CONVERT_COLOR0_TO_GAMMA_SHIFT 7u");
  line("#define XE_SYS_LINE_LOOP_CLOSING_INDEX 12u");
  line("#define XE_SYS_VERTEX_INDEX_ENDIAN 16u");
  line("#define XE_SYS_VERTEX_INDEX_OFFSET 20u");
  line("#define XE_SYS_VERTEX_INDEX_MIN 24u");
  line("#define XE_SYS_VERTEX_INDEX_MAX 28u");
  line("#define XE_SYS_USER_CLIP_PLANES 32u");
  line("#define XE_SYS_NDC_SCALE 128u");
  line("#define XE_SYS_NDC_OFFSET 144u");
  line("#define XE_SYS_POINT_VERTEX_DIAMETER_MIN 140u");
  line("#define XE_SYS_POINT_VERTEX_DIAMETER_MAX 156u");
  line("#define XE_SYSFLAG_SHARED_MEMORY_IS_UAV 1024u");
  line("#define XE_SYSFLAG_W_NOT_RECIPROCAL 2048u");
  line("#define XE_SYSFLAG_XY_DIVIDED_BY_W 4096u");
  line("#define XE_SYSFLAG_Z_DIVIDED_BY_W 8192u");
  line("#define XE_RESOLUTION_SCALE_X 2");
  line("#define XE_RESOLUTION_SCALE_Y 2");
  line("#define XE_REVERT_RESOLUTION_SCALE_AXES 3u");
  line("#define XE_GAMMA_RENDER_TARGET_AS_UNORM8 1");
  line("#define XE_MSAA_2X_SUPPORTED 1");
  line("#define XE_FUZZY_ALPHA_EPSILON 0");
  line("#define XE_POST_VIEWS " + std::to_string(kPostViews) + "u");
  line("#define XE_POST_VIEW_COUNT 8u");
  line("#define XE_REGISTER_COUNT 8");
  line("#define XE_INTERPOLATOR_MASK 0x000Fu");
  line(std::string("#define XE_USER_CLIP_PLANE_COUNT ") + (distances ? "2u" : "0u"));
  line("#define XE_USER_CLIP_PLANE_FIRST 0u");
  line(std::string("#define XE_WRITES_VERTEX_KILL ") + (distances ? "1" : "0"));
  line(std::string("#define XE_VERTEX_KILL_AND ") + (distances ? "1" : "0"));
  line(std::string("#define XE_VERTEX_KILL_DISTANCE ") + (distances ? "2u" : "0u"));
  line("#define XE_FLOAT_CONSTANT_COUNT 256");
  for (uint32_t i = 0; i < 256; ++i) {
    line("#define XE_FLOAT_CONSTANT_" + std::to_string(i) + " xe_float_constants[" +
         std::to_string(i) + "]");
  }
  line("#define XE_DESCRIPTOR_INDEX_VECTORS 0");
  line("struct XeVsInput {");
  line("  uint xe_vertex_id : SV_VertexID;");
  line("};");
  line("struct XeVsOutput {");
  std::string store = "#define XE_STORE_OUTPUTS(output, p, distances)";
  for (uint32_t i = 0; i < 4; ++i) {
    line("  float4 xe_interpolator" + std::to_string(i) + " : TEXCOORD" + std::to_string(i) + ";");
    store += " output.xe_interpolator" + std::to_string(i) + " = p.o[" + std::to_string(i) + "];";
  }
  line("  float4 xe_position : SV_Position;");
  if (distances) {
    line("  float2 xe_clip0 : SV_ClipDistance0;");
    line("  float xe_cull0 : SV_CullDistance0;");
    line("  float xe_point_size : XEPSIZE;");
    store += " output.xe_clip0.x = distances[0]; output.xe_clip0.y = distances[1];"
             " output.xe_cull0 = distances[2]; output.xe_point_size = p.point_size_edge_flag_kill.x;";
  }
  line("};");
  out += store + "\n";
  return out;
}

// A vertex shader in the form HlslShaderTranslator generates: a 32-bit float position and a
// signed normalized 8_8_8_8 normal from shared memory (vertex fetch through XE_HOOK_VFETCH),
// constants through XE_CONSTANT, the position and three interpolators exported.
const char kVertexShader[] = R"(void xe_vertex_shader(inout XeVertex p) {
  precise float ps = 0.0;
  bool p0 = false;
  int a0 = 0;
  int4 aL = 0;
  uint4 loop_count = 0u;
  float4 grad_h_lod = 0.0;
  float4 grad_v = 0.0;
  int vfetch_address = 0;
  {
    precise float vfetch_index = floor(p.r[0].x);
    vfetch_address = int(vfetch_index) * 16 + int(xe_vfetch_word(95u, 0u) & 0xFFFFFFFCu);
    uint4 w = 0u;
    w.x = xe_shared_memory_word(vfetch_address + 0);
    w.y = xe_shared_memory_word(vfetch_address + 4);
    w.z = xe_shared_memory_word(vfetch_address + 8);
    w = xe_vfetch_swap(w, xe_vfetch_word(95u, 1u) & 3u);
    precise float4 v = asfloat(w);
    v.w = 0.0;
    v = XE_HOOK_VFETCH(95u, 0, v);
    p.r[1].xyz = (v).xyz;
    p.r[1].w = 1.0;
  }
  {
    uint4 w = 0u;
    w.x = xe_shared_memory_word(vfetch_address + 12);
    w = xe_vfetch_swap(w, xe_vfetch_word(95u, 1u) & 3u);
    precise float4 v = asfloat(w);
    v.x = float(xe_ibfe(w.x, 8u, 0u));
    v.x = v.x * asfloat(0x3C010204u);
    v.x = max(v.x, -1.0);
    v.y = float(xe_ibfe(w.x, 8u, 8u));
    v.y = v.y * asfloat(0x3C010204u);
    v.y = max(v.y, -1.0);
    v.z = float(xe_ibfe(w.x, 8u, 16u));
    v.z = v.z * asfloat(0x3C010204u);
    v.z = max(v.z, -1.0);
    v = XE_HOOK_VFETCH(95u, 3, v);
    p.r[2].xyz = (v).xyz;
  }
  {
    precise float4 v = float4(dot(p.r[1], XE_CONSTANT(0)), dot(p.r[1], XE_CONSTANT(1)),
                              dot(p.r[1], XE_CONSTANT(2)), dot(p.r[1], XE_CONSTANT(3)));
    p.position = v;
    p.point_size_edge_flag_kill.z = XE_CONSTANT(4).x;
  }
  p.o[0] = p.r[2];
  p.o[1] = p.r[1];
  p.o[2].xy = XE_CONSTANT(5).xy;
  return;
}
)";

// A vertex entry's hooks: none, the identity (writes back what it reads), or a move of the
// position by a fixed offset in clip space with one export zeroed.
enum class VertexHook { kNone, kIdentity, kMove };

Result CompileVertex(const std::string& library, VertexHook hook, bool distances,
                     bool live_knobs = false, Native native = Native::kNone) {
  std::string source = VertexContract(distances, live_knobs, native) +
                       "#include \"xenos_override.hlsli\"\n";
  if (hook == VertexHook::kIdentity) {
    source += "#undef XE_HOOK_VERTEX_OUTPUT\n#define XE_HOOK_VERTEX_OUTPUT(p, input) "
              "{ p.position = p.position; p.o[0] = p.o[0]; }\n";
  } else if (hook == VertexHook::kMove) {
    source += "#undef XE_HOOK_VERTEX_OUTPUT\n#define XE_HOOK_VERTEX_OUTPUT(p, input) "
              "{ p.position.xy += 0.0125 * p.position.w; p.o[1] = 0.0; }\n";
  }
  source += "#line 1 \"vertex.vs.hlsl\"\n";
  source += kVertexShader;
  source += "\n#define XE_OVERRIDE_MAIN\n#include \"xenos_override.hlsli\"\n";
  Include include(library, {});
  ComPtr<ID3DBlob> code, errors;
  HRESULT hr = D3DCompile(source.data(), source.size(), "main.hlsl", nullptr, &include, "main",
                          "vs_5_1",
                          D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES |
                              D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                          0, &code, &errors);
  Result result;
  if (errors) {
    result.messages.assign(static_cast<const char*>(errors->GetBufferPointer()),
                           errors->GetBufferSize());
  }
  if (FAILED(hr) || !code) {
    return result;
  }
  result.compiled = true;
  result.fingerprint = 0xCBF29CE484222325ull;
  const uint8_t* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
  for (size_t i = 0; i < code->GetBufferSize(); ++i) {
    result.fingerprint = (result.fingerprint ^ bytes[i]) * 0x100000001B3ull;
  }
  return result;
}

void Report(const Result& result, const std::string& what) {
  Check(result.compiled, what + " compiles");
  if (!result.compiled || result.messages.find("warning") != std::string::npos) {
    std::printf("%s\n", result.messages.c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf(
        "usage: fm4_shader_mod_tests <shader_override.hlsli> <look directory> "
        "[NAME=VALUE;...]\n");
    return 2;
  }
  std::string library;
  if (!ReadFile(argv[1], library)) {
    std::printf("FAIL cannot read %s\n", argv[1]);
    return 1;
  }
  std::filesystem::path look = argv[2];

  // The library alone: the occlusion read exists with XE_POST_VIEWS only.
  for (bool post_ao : {true, false}) {
    std::string contract = post_ao ? "with XE_POST_VIEWS" : "without XE_POST_VIEWS";
    Result plain = Compile(library, {}, {}, post_ao, kOverride, {});
    Report(plain, "library, override with hooks, " + contract);
    Check(plain.compiled && !plain.reads_post_ao,
          "library, override with hooks, " + contract + ": no occlusion read");
    Result ao = Compile(library, {}, {}, post_ao, kPostAoOverride, {});
    Report(ao, "library, override calling xe_post_ao, " + contract);
    Check(ao.compiled && ao.reads_post_ao == post_ao,
          "library, override calling xe_post_ao, " + contract +
              (post_ao ? ": reads the occlusion" : ": returns 0 without a read"));
  }

  // Every look mod, at its default knobs and with each knob that reads the
  // occlusion.
  const char* const kAoKnobs[] = {"FM4_LOOK_AO", "FM4_LOOK_AO_DEBUG"};
  // Every other knob away from the original look, so the code each mod skips at
  // its defaults is compiled too.
  const std::vector<std::pair<std::string, std::string>> kLookKnobs = {
      {"FM4_LOOK_SHARPEN", "0.5"},     {"FM4_LOOK_BLOOM", "0.7"},
      {"FM4_LOOK_EXPOSURE", "0.5"},    {"FM4_LOOK_CONTRAST", "0.3"},
      {"FM4_LOOK_SATURATION", "1.1"},  {"FM4_LOOK_VIGNETTE", "0.5"},
      {"FM4_LOOK_MOTION_BLUR", "0.5"}, {"FM4_LOOK_TONEMAP", "2.0"},
      {"FM4_LOOK_CLEARCOAT", "1.0"},   {"FM4_LOOK_FLAKES", "1.0"},
      {"FM4_LOOK_GLASS", "1.0"},       {"FM4_LOOK_CHROME", "1.0"},
      {"FM4_LOOK_RIMS", "1.0"},        {"FM4_LOOK_SKY_SATURATION", "1.2"},
      {"FM4_LOOK_SKY_CONTRAST", "0.3"}, {"FM4_LOOK_SKY_WARMTH", "0.3"},
      {"FM4_LOOK_SKY_HORIZON", "0.5"}, {"FM4_LOOK_FOLIAGE_HUE", "-30.0"},
      {"FM4_LOOK_FOLIAGE_SATURATION", "1.2"}, {"FM4_LOOK_FOLIAGE_BRIGHTNESS", "1.1"},
      {"FM4_LOOK_ROAD_BRIGHTNESS", "0.9"}, {"FM4_LOOK_ROAD_CONTRAST", "0.3"},
      {"FM4_LOOK_ROAD_SPECULAR", "0.5"}, {"FM4_LOOK_ROAD_SHEEN", "1.0"},
      {"FM4_LOOK_ROAD_SHEEN_GRAIN", "2.0"}, {"FM4_LOOK_COCKPIT_LIGHT", "0.3"},
      {"FM4_LOOK_ADAPTATION", "1.0"}};
  std::vector<std::filesystem::path> mods;
  for (const auto& entry : std::filesystem::directory_iterator(look)) {
    std::string name = entry.path().filename().string();
    if (name.starts_with("shader_") && name.ends_with(".mod.hlsli")) {
      mods.push_back(entry.path());
    }
  }
  std::sort(mods.begin(), mods.end());
  Check(!mods.empty(), "look directory has shader_<hash>.mod.hlsli files");
  int readers[std::size(kAoKnobs)] = {};
  for (const auto& path : mods) {
    std::string name = path.filename().string();
    for (bool post_ao : {true, false}) {
      std::string contract = post_ao ? "with XE_POST_VIEWS" : "without XE_POST_VIEWS";
      Result plain = Compile(library, look, name, post_ao, kOverride, {});
      Report(plain, name + ", default knobs, " + contract);
      Check(plain.compiled && !plain.reads_post_ao,
            name + ", default knobs, " + contract + ": no occlusion read");
      Result knobs = Compile(library, look, name, post_ao, kOverride, kLookKnobs);
      Report(knobs, name + ", look knobs set, " + contract);
      // A mod whose code its own knobs do not reach (a wrong #if, a hook left
      // undefined) compiles to the shader without the mod.
      Check(knobs.compiled && plain.compiled && knobs.fingerprint != plain.fingerprint,
            name + ", look knobs set, " + contract + ": the mod changes the shader");
      for (size_t k = 0; k < std::size(kAoKnobs); ++k) {
        std::string knob = std::string(kAoKnobs[k]) + "=1";
        Result result = Compile(library, look, name, post_ao, kOverride, {{kAoKnobs[k], "1.0"}});
        Report(result, name + ", " + knob + ", " + contract);
        if (post_ao) {
          std::printf("     %s, %s: %s, DXBC %016llX\n", name.c_str(), knob.c_str(),
                      result.reads_post_ao ? "reads the occlusion" : "no occlusion read",
                      static_cast<unsigned long long>(result.fingerprint));
          readers[k] += result.reads_post_ao ? 1 : 0;
        } else {
          Check(!result.reads_post_ao, name + ", " + knob + ", " + contract + ": no occlusion read");
        }
      }
    }
  }
  for (size_t k = 0; k < std::size(kAoKnobs); ++k) {
    Check(readers[k] > 0, std::string("a look mod reads the occlusion with ") + kAoKnobs[k] +
                              "=1 (" + std::to_string(readers[k]) + ")");
  }

  // The defines of a look (the launcher's default one from pipeline.py).
  if (argc > 3) {
    std::vector<std::pair<std::string, std::string>> defines;
    std::string text = argv[3];
    for (size_t start = 0; start < text.size();) {
      size_t end = std::min(text.find(';', start), text.size());
      std::string define = text.substr(start, end - start);
      size_t equals = define.find('=');
      if (!define.empty()) {
        defines.emplace_back(define.substr(0, equals),
                             equals == std::string::npos ? "1" : define.substr(equals + 1));
      }
      start = end + 1;
    }
    for (const auto& path : mods) {
      std::string name = path.filename().string();
      Result result = Compile(library, look, name, true, kOverride, defines);
      Report(result, name + ", " + text);
      std::printf("     %s, %s: DXBC %016llX, %d instruction slots\n", name.c_str(), text.c_str(),
                  static_cast<unsigned long long>(result.fingerprint), result.instructions);
    }
  }

  // Live knobs: every knob of the files as xe_mod_knob(<slot>), as the runtime
  // defines them with shader_mod_live_knobs.
  std::vector<std::string> knobs = Knobs(look);
  Check(!knobs.empty() && knobs.size() <= kKnobCount,
        "the look knobs fit the live knob slots (" + std::to_string(knobs.size()) + " of " +
            std::to_string(kKnobCount) + ")");
  std::vector<std::pair<std::string, std::string>> live;
  for (size_t i = 0; i < knobs.size(); ++i) {
    live.emplace_back(knobs[i], "xe_mod_knob(" + std::to_string(i) + "u)");
  }
  Result library_live = Compile(library, {}, {}, true, kOverride, {}, true);
  Report(library_live, "library with live knobs, override with hooks");
  Check(library_live.compiled && !library_live.reads_knobs,
        "library with live knobs, override with hooks: no knob read without a mod");
  for (const auto& path : mods) {
    std::string name = path.filename().string();
    for (bool post_ao : {true, false}) {
      std::string what = name + ", live knobs, " +
                         (post_ao ? "with XE_POST_VIEWS" : "without XE_POST_VIEWS");
      Result result = Compile(library, look, name, post_ao, kOverride, live, true);
      Report(result, what);
      Check(result.compiled && result.reads_knobs, what + ": reads the knobs at run time");
      if (post_ao) {
        std::printf("     %s, live knobs: %d instruction slots\n", name.c_str(),
                    result.instructions);
      }
    }
  }

  // The native shader contract: the library's pass class and its debug view
  // change nothing until a shader reads them, and every mod compiles as an entry
  // of shaders.toml, its knobs as params and its draws tinted by pass.
  Result library_native = Compile(library, {}, {}, true, kOverride, {}, false, Native::kPass);
  Report(library_native, "library with the native contract, override with hooks");
  Result library_plain = Compile(library, {}, {}, true, kOverride, {});
  Check(library_native.compiled && library_native.fingerprint == library_plain.fingerprint,
        "library with the native contract: the same DXBC without a pass read");
  Result library_debug = Compile(library, {}, {}, true, kOverride, {}, false, Native::kPassDebug);
  Report(library_debug, "library with the native contract's pass debug view");
  Check(library_debug.compiled && library_debug.fingerprint != library_plain.fingerprint,
        "library with the native contract's pass debug view: the DXBC changes");
  for (const auto& path : mods) {
    std::string name = path.filename().string();
    Result result = Compile(library, look, name, true, kOverride, live, true, Native::kPassDebug);
    Report(result, name + ", native contract entry, live knobs, pass debug view");
  }

  // A replacement entry: its inputs by name, the library's main without hooks, with and
  // without the pass debug view, with live knobs.
  for (Native native : {Native::kPass, Native::kPassDebug}) {
    std::string what = std::string("a replacement with inputs by name") +
                       (native == Native::kPassDebug ? ", pass debug view" : "");
    Result result =
        Compile(library, {}, {}, true, kReplacement, {}, true, native, kReplacementContract);
    Report(result, what);
    Check(result.compiled && result.fingerprint != library_native.fingerprint,
          what + ": not the hooks' shader");
  }

  // The look's final combine as a replacement (fm4-final-combine.ps.hlsli), included as the
  // runtime includes it: at its default knobs, with the look knobs away from the original look,
  // with the occlusion, and with live knobs.
  const char kFinalCombine[] = "fm4-final-combine.ps.hlsli";
  Result combine_plain =
      Compile(library, look, kFinalCombine, true, "", {}, false, Native::kPass, kFinalCombineContract);
  Report(combine_plain, std::string(kFinalCombine) + ", default knobs");
  Result combine_knobs = Compile(library, look, kFinalCombine, true, "", kLookKnobs, false,
                                 Native::kPass, kFinalCombineContract);
  Report(combine_knobs, std::string(kFinalCombine) + ", look knobs set");
  Check(combine_plain.compiled && combine_knobs.compiled &&
            combine_plain.fingerprint != combine_knobs.fingerprint && !combine_plain.reads_post_ao,
        std::string(kFinalCombine) + ": the knobs change it, no occlusion read at its defaults");
  Result combine_ao = Compile(library, look, kFinalCombine, true, "", {{"FM4_LOOK_AO", "1.0"}},
                              false, Native::kPass, kFinalCombineContract);
  Report(combine_ao, std::string(kFinalCombine) + ", FM4_LOOK_AO=1");
  Check(combine_ao.compiled && combine_ao.reads_post_ao,
        std::string(kFinalCombine) + ", FM4_LOOK_AO=1: reads the occlusion");
  Result combine_live = Compile(library, look, kFinalCombine, true, "", live, true,
                                Native::kPassDebug, kFinalCombineContract);
  Report(combine_live, std::string(kFinalCombine) + ", live knobs, pass debug view");
  Check(combine_live.compiled && combine_live.reads_knobs,
        std::string(kFinalCombine) + ", live knobs: reads the knobs at run time");

  // Vertex shaders (docs/analysis/shader-contract.md 9): the library's vertex main with a
  // shader in the generator's form, with no hooks, the identity hooks (the same DXBC), and
  // hooks that move the position (another DXBC); with the distances and the point size; with
  // live knobs and the native contract's pass class.
  Result vertex_plain = CompileVertex(library, VertexHook::kNone, false);
  Report(vertex_plain, "a vertex shader");
  std::printf("     vertex shader: fingerprint %016llX\n",
              static_cast<unsigned long long>(vertex_plain.fingerprint));
  Result vertex_identity = CompileVertex(library, VertexHook::kIdentity, false);
  Report(vertex_identity, "a vertex shader with identity hooks");
  Check(vertex_identity.compiled && vertex_identity.fingerprint == vertex_plain.fingerprint,
        "a vertex shader with identity hooks: the same DXBC");
  Result vertex_move = CompileVertex(library, VertexHook::kMove, false);
  Report(vertex_move, "a vertex shader with hooks that move the position");
  Check(vertex_move.compiled && vertex_move.fingerprint != vertex_plain.fingerprint,
        "a vertex shader with hooks that move the position: the DXBC changes");
  Result vertex_distances = CompileVertex(library, VertexHook::kNone, true);
  Report(vertex_distances, "a vertex shader with clip and cull distances and the point size");
  std::printf("     vertex shader with distances: fingerprint %016llX\n",
              static_cast<unsigned long long>(vertex_distances.fingerprint));
  Result vertex_native = CompileVertex(library, VertexHook::kMove, false, false, Native::kPass);
  Report(vertex_native, "a vertex shader with the native contract's pass class");
  Check(vertex_native.compiled && vertex_native.fingerprint == vertex_move.fingerprint,
        "a vertex shader with the native contract: the same DXBC without a pass read");
  Result vertex_live = CompileVertex(library, VertexHook::kMove, false, true, Native::kPass);
  Report(vertex_live, "a vertex shader with live knobs and the native contract's pass class");

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
