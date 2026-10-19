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
// The synthetic system constants are 16 vectors; the live knobs follow them
// (ShaderOverrides::kKnobCount).
constexpr uint32_t kSystemConstantVectors = 16;
constexpr uint32_t kKnobCount = 64;

// What ShaderOverrides::BuildContract writes for a pixel shader with color
// target 0, all interpolators, every fetch constant bound as 2D, 3D and cube
// (unsigned and signed) with its default sampler, and every float constant.
std::string Contract(bool post_ao, bool live_knobs) {
  std::string out = "// Synthetic contract (fm4_shader_mod_tests).\n#define XE_PIXEL_SHADER 1\n";
  auto line = [&out](const std::string& text) { out += text + "\n"; };
  if (live_knobs) {
    line("#define XE_SYSTEM_CONSTANT_VECTORS " +
         std::to_string(kSystemConstantVectors + kKnobCount / 4));
    line("#define XE_SYS_MOD_KNOBS " + std::to_string(kSystemConstantVectors * 16) + "u");
  } else {
    line("#define XE_SYSTEM_CONSTANT_VECTORS " + std::to_string(kSystemConstantVectors));
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
               bool live_knobs = false) {
  std::string source = Contract(post_ao, live_knobs) + "#include \"xenos_override.hlsli\"\n";
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
      {"FM4_LOOK_ROAD_SHEEN_GRAIN", "2.0"}, {"FM4_LOOK_COCKPIT_LIGHT", "0.3"}};
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

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
