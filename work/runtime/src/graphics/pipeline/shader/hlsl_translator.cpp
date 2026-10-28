/**
 * @file        graphics/pipeline/shader/hlsl_translator.cpp
 * @brief       Guest pixel and vertex shader ucode to HLSL shader overrides
 *
 * Mirrors DxbcShaderTranslator (dxbc_translator*.cpp) operation by operation,
 * with the helpers of d3d12/shader_override.hlsli, so that the override renders
 * exactly like the DXBC translation. Keep both in sync.
 */

#include <rex/graphics/pipeline/shader/hlsl_translator.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <sstream>

#include <fmt/format.h>

#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/math.h>
#include <rex/string/buffer.h>

namespace rex::graphics {

using namespace ucode;

namespace {

constexpr char kComponents[] = "xyzw";

std::string Mask(uint32_t mask) {
  std::string letters;
  for (uint32_t i = 0; i < 4; ++i) {
    if (mask & (1u << i)) {
      letters.push_back(kComponents[i]);
    }
  }
  return letters;
}

// An exact float literal.
std::string Float(float value) {
  if (!std::isfinite(value)) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return fmt::format("asfloat(0x{:08X}u)", bits);
  }
  std::string text = fmt::format("{}", value);
  if (text.find_first_of(".e") == std::string::npos) {
    text += ".0";
  }
  return text;
}

constexpr char kFloatMax[] = "asfloat(0x7F7FFFFFu)";
constexpr char kNegativeFloatMax[] = "asfloat(0xFF7FFFFFu)";

const char* Bool(bool value) { return value ? "true" : "false"; }

const char* FilterLetter(xenos::TextureFilter filter) {
  static const char* const kLetters[] = {"P", "L", "B", "F"};
  return kLetters[uint32_t(filter) & 3];
}

}  // namespace

std::string HlslShaderTranslator::TextureSlotName(uint32_t fetch_constant,
                                                  xenos::FetchOpDimension dimension,
                                                  bool is_signed) {
  const char* name = dimension == xenos::FetchOpDimension::k3DOrStacked ? "3D"
                     : dimension == xenos::FetchOpDimension::kCube      ? "CUBE"
                                                                        : "2D";
  return fmt::format("XE_TEXTURE{}_{}_{}", fetch_constant, name, is_signed ? 'S' : 'U');
}

std::string HlslShaderTranslator::SamplerSlotName(const DxbcShader::SamplerBinding& binding) {
  // The names of the DXBC translator's bindful samplers.
  std::string name = fmt::format("XE_SAMPLER{}", binding.fetch_constant);
  if (binding.aniso_filter == xenos::AnisoFilter::kDisabled ||
      binding.aniso_filter == xenos::AnisoFilter::kUseFetchConst) {
    name += fmt::format("_{}{}{}", FilterLetter(binding.mag_filter),
                        FilterLetter(binding.min_filter), FilterLetter(binding.mip_filter));
  }
  if (binding.aniso_filter != xenos::AnisoFilter::kUseFetchConst) {
    name += binding.aniso_filter == xenos::AnisoFilter::kDisabled
                ? std::string("_A0")
                : fmt::format("_A{}", 1u << (uint32_t(binding.aniso_filter) - 1));
  }
  return name;
}

std::string HlslShaderTranslator::Operand::Vector() const {
  if (base.empty()) {
    return "float4(0.0, 0.0, 0.0, 0.0)";
  }
  std::string value = base + "." + std::string(swizzle, 4);
  if (absolute) {
    value = "abs(" + value + ")";
  }
  return negated ? "(-" + value + ")" : value;
}

std::string HlslShaderTranslator::Operand::Component(uint32_t index) const {
  return base.empty() ? std::string("0.0") : Underlying(swizzle[index]);
}

std::string HlslShaderTranslator::Operand::Underlying(char component) const {
  if (base.empty()) {
    return "0.0";
  }
  std::string value = base + "." + component;
  if (absolute) {
    value = "abs(" + value + ")";
  }
  return negated ? "(-" + value + ")" : value;
}

void HlslShaderTranslator::Reset() {
  ShaderTranslator::Reset();
  out_.clear();
  texture_bindings_.clear();
  sampler_bindings_.clear();
  indent_ = 0;
  use_pc_loop_ = false;
  exec_open_ = false;
  label_open_ = false;
}

uint32_t HlslShaderTranslator::GetModificationRegisterCount() const {
  DxbcShaderTranslator::Modification modification(current_translation().modification());
  return is_vertex_shader() ? modification.vertex.dynamic_addressable_register_count
                            : modification.pixel.dynamic_addressable_register_count;
}

// DxbcShaderTranslator::FindOrAddTextureBinding and FindOrAddSamplerBinding
// with bindless resources, and the order of their calls in its
// ProcessTextureFetchInstruction (the sampler before the textures of a fetch):
// keep both in sync. native_shader_contract_check compares the bindings of
// both translators for every pixel shader translated
// (ShaderOverrides::CheckBindings, docs/analysis/shader-contract.md 5.1), and
// the DXBC translator's PostTranslation logs an error when a shader's stored
// bindings differ from its own.
DxbcShader::TextureBinding HlslShaderTranslator::FindOrAddTextureBinding(
    uint32_t fetch_constant, xenos::FetchOpDimension dimension, bool is_signed) {
  // 1D and 2D textures (including stacked ones) are 2D arrays.
  if (dimension == xenos::FetchOpDimension::k1D) {
    dimension = xenos::FetchOpDimension::k2D;
  }
  for (const DxbcShader::TextureBinding& binding : texture_bindings_) {
    if (binding.fetch_constant == fetch_constant && binding.dimension == dimension &&
        binding.is_signed == is_signed) {
      return binding;
    }
  }
  if (texture_bindings_.size() >= DxbcShader::kMaxTextureBindings) {
    EmitTranslationError("too many texture bindings");
    return texture_bindings_.back();
  }
  DxbcShader::TextureBinding binding;
  // As DxbcShader stores it: zeroed for a stable hash.
  std::memset(&binding, 0, sizeof(binding));
  // As the DXBC translator assigns it: the count of the bindings with this one
  // (it adds the binding before it counts), so the first binding has 1.
  binding.bindless_descriptor_index =
      uint32_t(texture_bindings_.size() + sampler_bindings_.size()) + 1;
  binding.fetch_constant = fetch_constant;
  binding.dimension = dimension;
  binding.is_signed = is_signed;
  texture_bindings_.push_back(binding);
  return binding;
}

DxbcShader::SamplerBinding HlslShaderTranslator::FindOrAddSamplerBinding(
    uint32_t fetch_constant, xenos::TextureFilter mag_filter, xenos::TextureFilter min_filter,
    xenos::TextureFilter mip_filter, xenos::AnisoFilter aniso_filter) {
  // In Direct3D 12, anisotropic filtering implies linear filtering.
  if (aniso_filter != xenos::AnisoFilter::kDisabled &&
      aniso_filter != xenos::AnisoFilter::kUseFetchConst) {
    mag_filter = xenos::TextureFilter::kLinear;
    min_filter = xenos::TextureFilter::kLinear;
    mip_filter = xenos::TextureFilter::kLinear;
    aniso_filter = std::min(aniso_filter, xenos::AnisoFilter::kMax_16_1);
  }
  for (const DxbcShader::SamplerBinding& binding : sampler_bindings_) {
    if (binding.fetch_constant == fetch_constant && binding.mag_filter == mag_filter &&
        binding.min_filter == min_filter && binding.mip_filter == mip_filter &&
        binding.aniso_filter == aniso_filter) {
      return binding;
    }
  }
  if (sampler_bindings_.size() >= DxbcShader::kMaxSamplerBindings) {
    EmitTranslationError("too many sampler bindings");
    return sampler_bindings_.back();
  }
  DxbcShader::SamplerBinding binding;
  // The count with this one, as for the textures.
  binding.bindless_descriptor_index =
      uint32_t(texture_bindings_.size() + sampler_bindings_.size()) + 1;
  binding.fetch_constant = fetch_constant;
  binding.mag_filter = mag_filter;
  binding.min_filter = min_filter;
  binding.mip_filter = mip_filter;
  binding.aniso_filter = aniso_filter;
  sampler_bindings_.push_back(binding);
  return binding;
}

void HlslShaderTranslator::Line(const std::string& text) {
  out_.append(indent_ * 2, ' ');
  out_ += text;
  out_.push_back('\n');
}

void HlslShaderTranslator::Open(const std::string& text) {
  Line(text);
  ++indent_;
}

void HlslShaderTranslator::Close() {
  --indent_;
  Line("}");
}

void HlslShaderTranslator::CloseExec() {
  if (exec_open_) {
    Close();
    exec_open_ = false;
  }
}

void HlslShaderTranslator::EmitDisassembly(const std::string& disassembly) {
  std::istringstream lines(disassembly);
  for (std::string line; std::getline(lines, line);) {
    size_t start = line.find_first_not_of(' ');
    if (start != std::string::npos) {
      Line("// " + line.substr(start));
    }
  }
}

void HlslShaderTranslator::StartTranslation() {
  if (is_vertex_shader()) {
    // Vertex shaders of the host vertex stage only, not the domain shaders of
    // tessellation (docs/analysis/shader-contract.md 9.2.1).
    DxbcShaderTranslator::Modification modification(current_translation().modification());
    if (modification.vertex.host_vertex_shader_type != Shader::HostVertexShaderType::kVertex) {
      EmitTranslationError("HLSL is generated only for vertex shaders of the vertex stage");
      return;
    }
  } else if (!is_pixel_shader()) {
    EmitTranslationError("HLSL overrides are generated only for pixel and vertex shaders");
    return;
  }
  const Shader& shader = current_shader();
  // Labels (jump, loop and call targets) need the pc loop of the translator.
  use_pc_loop_ = !shader.label_addresses().empty();
  Line(fmt::format(
      "// Generated from {} shader {:016X}, modification {:016X}, by HlslShaderTranslator:",
      is_vertex_shader() ? "vertex" : "pixel", shader.ucode_data_hash(),
      current_translation().modification()));
  Line("// the operations of its DXBC translation. Each block follows the guest");
  Line("// instruction in the comment above it.");
  if (is_vertex_shader()) {
    Open("void xe_vertex_shader(inout XeVertex p) {");
    // The byte address of the last vfetch_full's element, for the vfetch_mini
    // after it (system_temp_grad_v_vfetch_address_.w).
    Line("int vfetch_address = 0;");
  } else {
    Open("void xe_pixel_shader(inout XePixel p) {");
  }
  Line("precise float ps = 0.0;");
  Line("bool p0 = false;");
  Line("int a0 = 0;");
  Line("int4 aL = 0;");
  Line("uint4 loop_count = 0u;");
  Line("float4 grad_h_lod = 0.0;");
  Line("float4 grad_v = 0.0;");
  if (use_pc_loop_) {
    Line("uint pc = 0u;");
    Open("[loop] while (true) {");
    Open("if (pc == 0u) {");
    label_open_ = true;
  }
}

std::vector<uint8_t> HlslShaderTranslator::CompleteTranslation() {
  if (!is_pixel_shader() && !is_vertex_shader()) {
    return std::vector<uint8_t>();
  }
  CloseExec();
  if (use_pc_loop_) {
    if (label_open_) {
      Close();
    }
    Line("break;");
    Close();
  }
  Close();
  return std::vector<uint8_t>(out_.begin(), out_.end());
}

void HlslShaderTranslator::ProcessLabel(uint32_t cf_index) {
  if (cf_index == 0 || !use_pc_loop_) {
    return;
  }
  CloseExec();
  // Falls through to the label from the previous one (if-chain on pc).
  Close();
  Open(fmt::format("if (pc <= {}u) {{", cf_index));
}

void HlslShaderTranslator::ProcessExecInstructionBegin(const ParsedExecInstruction& instr) {
  CloseExec();
  string::StringBuffer disassembly;
  instr.Disassemble(&disassembly);
  EmitDisassembly(disassembly.to_string());
  if (instr.type == ParsedExecInstruction::Type::kConditional) {
    Open(fmt::format("if (xe_bool_constant({}u) == {}) {{", instr.bool_constant_index,
                     Bool(instr.condition)));
    exec_open_ = true;
  } else if (instr.type == ParsedExecInstruction::Type::kPredicated) {
    Open(fmt::format("if (p0 == {}) {{", Bool(instr.condition)));
    exec_open_ = true;
  }
}

void HlslShaderTranslator::ProcessExecInstructionEnd(const ParsedExecInstruction& instr) {
  if (instr.is_end) {
    Line("return;");
  }
  CloseExec();
}

void HlslShaderTranslator::ProcessLoopStartInstruction(const ParsedLoopStartInstruction& instr) {
  CloseExec();
  string::StringBuffer disassembly;
  instr.Disassemble(&disassembly);
  EmitDisassembly(disassembly.to_string());
  Open("{");
  Line(fmt::format("uint loop_constant = xe_loop_constant({}u);", instr.loop_constant_index));
  Line("uint new_loop_count = loop_constant & 0xFFu;");
  Open("if (new_loop_count == 0u) {");
  Line(fmt::format("pc = {}u;", instr.loop_skip_address));
  Line("continue;");
  Close();
  Line("loop_count = uint4(new_loop_count, loop_count.xyz);");
  Line(instr.is_repeat ? "aL = aL.xxyz;" : "aL = int4(int((loop_constant >> 8u) & 0xFFu), aL.xyz);");
  Close();
}

void HlslShaderTranslator::ProcessLoopEndInstruction(const ParsedLoopEndInstruction& instr) {
  CloseExec();
  string::StringBuffer disassembly;
  instr.Disassemble(&disassembly);
  EmitDisassembly(disassembly.to_string());
  Open("{");
  Line("loop_count.x = loop_count.x - 1u;");
  if (instr.is_predicated_break) {
    Line(fmt::format("bool loop_break = loop_count.x == 0u || p0 == {};",
                     Bool(instr.predicate_condition)));
  } else {
    Line("bool loop_break = loop_count.x == 0u;");
  }
  Open("if (loop_break) {");
  Line("loop_count = uint4(loop_count.yzw, 0u);");
  Line("aL = int4(aL.yzw, 0);");
  Close();
  Open("else {");
  Line(fmt::format("aL.x = aL.x + xe_ibfe(xe_loop_constant({}u), 8u, 16u);",
                   instr.loop_constant_index));
  Line(fmt::format("pc = {}u;", instr.loop_body_address));
  Line("continue;");
  Close();
  Close();
}

void HlslShaderTranslator::ProcessJumpInstruction(const ParsedJumpInstruction& instr) {
  CloseExec();
  string::StringBuffer disassembly;
  instr.Disassemble(&disassembly);
  EmitDisassembly(disassembly.to_string());
  if (instr.type == ParsedJumpInstruction::Type::kConditional) {
    Open(fmt::format("if (xe_bool_constant({}u) == {}) {{", instr.bool_constant_index,
                     Bool(instr.condition)));
  } else if (instr.type == ParsedJumpInstruction::Type::kPredicated) {
    Open(fmt::format("if (p0 == {}) {{", Bool(instr.condition)));
  } else {
    Open("{");
  }
  Line(fmt::format("pc = {}u;", instr.target_address));
  Line("continue;");
  Close();
}

void HlslShaderTranslator::ProcessAllocInstruction(const ParsedAllocInstruction& instr,
                                                   uint8_t export_eM) {
  if (export_eM || (instr.type == AllocType::kMemory && current_shader().memexport_eM_written())) {
    EmitTranslationError("memexport is not supported in HLSL overrides");
  }
}

// DxbcShaderTranslator::ProcessVertexFetchInstruction: the same address, words,
// endian swap, unpacking and exponent adjust, with the library's shared memory
// loads (xe_shared_memory_word). Keep both in sync.
void HlslShaderTranslator::ProcessVertexFetchInstruction(
    const ParsedVertexFetchInstruction& instr) {
  if (!is_vertex_shader()) {
    EmitTranslationError("vertex fetch in a pixel shader");
    return;
  }
  string::StringBuffer disassembly;
  instr.Disassemble(&disassembly);
  EmitDisassembly(disassembly.to_string());
  if (instr.is_predicated) {
    Open(fmt::format("if (p0 == {}) {{", Bool(instr.predicate_condition)));
  }
  Open("{");
  EmitVertexFetch(instr);
  Close();
  if (instr.is_predicated) {
    Close();
  }
}

void HlslShaderTranslator::EmitVertexFetch(const ParsedVertexFetchInstruction& instr) {
  const auto& attributes = instr.attributes;
  uint32_t used_result_components = instr.result.GetUsedResultComponents();
  uint32_t needed_words =
      xenos::GetVertexFormatNeededWords(attributes.data_format, used_result_components);
  if (!needed_words && instr.is_mini_fetch) {
    StoreResult(instr.result, "float4(0.0, 0.0, 0.0, 0.0)");
    return;
  }
  // The fetch constant: vf<n> is dwords 2n and 2n + 1 of the fetch constants.
  uint32_t fetch_constant = instr.operands[1].storage_index;
  if (!instr.is_mini_fetch) {
    if (attributes.stride) {
      // The index floored, or rounded as floor(index + 0.5), times the stride,
      // plus the buffer's byte address.
      std::string index = LoadOperand(instr.operands[0], 0b0001).Component(0);
      if (attributes.is_index_rounded) {
        Line(fmt::format("precise float vfetch_index = floor({} + 0.5);", index));
      } else {
        Line(fmt::format("precise float vfetch_index = floor({});", index));
      }
      Line(fmt::format("vfetch_address = int(vfetch_index) * {} + int(xe_vfetch_word({}u, 0u) & "
                       "0xFFFFFFFCu);",
                       attributes.stride * uint32_t(sizeof(uint32_t)), fetch_constant));
    } else {
      Line(fmt::format("vfetch_address = int(xe_vfetch_word({}u, 0u) & 0xFFFFFFFCu);",
                       fetch_constant));
    }
  }
  if (!needed_words) {
    StoreResult(instr.result, "float4(0.0, 0.0, 0.0, 0.0)");
    return;
  }
  // The words, endian-swapped by the fetch constant's endianness.
  Line("uint4 w = 0u;");
  for (uint32_t i = 0; i < 4; ++i) {
    if (needed_words & (1u << i)) {
      Line(fmt::format("w.{} = xe_shared_memory_word(vfetch_address + {});", kComponents[i],
                       (attributes.offset + int32_t(i)) * int32_t(sizeof(uint32_t))));
    }
  }
  Line(fmt::format("w = xe_vfetch_swap(w, xe_vfetch_word({}u, 1u) & 3u);", fetch_constant));

  // Unpacking.
  uint32_t used_format_components =
      used_result_components &
      ((1u << xenos::GetVertexFormatComponentCount(attributes.data_format)) - 1);
  uint32_t packed_widths[4] = {}, packed_offsets[4] = {};
  // The word of each component of the packed formats.
  uint32_t packed_words[4] = {};
  switch (attributes.data_format) {
    case xenos::VertexFormat::k_8_8_8_8:
      packed_widths[0] = packed_widths[1] = packed_widths[2] = packed_widths[3] = 8;
      packed_offsets[1] = 8;
      packed_offsets[2] = 16;
      packed_offsets[3] = 24;
      break;
    case xenos::VertexFormat::k_2_10_10_10:
      packed_widths[0] = packed_widths[1] = packed_widths[2] = 10;
      packed_widths[3] = 2;
      packed_offsets[1] = 10;
      packed_offsets[2] = 20;
      packed_offsets[3] = 30;
      break;
    case xenos::VertexFormat::k_10_11_11:
      packed_widths[0] = packed_widths[1] = 11;
      packed_widths[2] = 10;
      packed_offsets[1] = 11;
      packed_offsets[2] = 22;
      break;
    case xenos::VertexFormat::k_11_11_10:
      packed_widths[0] = 10;
      packed_widths[1] = packed_widths[2] = 11;
      packed_offsets[1] = 10;
      packed_offsets[2] = 21;
      break;
    case xenos::VertexFormat::k_16_16:
      packed_widths[0] = packed_widths[1] = 16;
      packed_offsets[1] = 16;
      break;
    case xenos::VertexFormat::k_16_16_16_16:
      packed_widths[0] = packed_widths[1] = packed_widths[2] = packed_widths[3] = 16;
      packed_offsets[1] = packed_offsets[3] = 16;
      packed_words[2] = packed_words[3] = 1;
      break;
    default:
      break;
  }
  // Exact float constants, as the DXBC translator's literals.
  auto exact = [](float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return fmt::format("asfloat(0x{:08X}u)", bits);
  };
  Line("precise float4 v = asfloat(w);");
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(used_format_components & (1u << i))) {
      continue;
    }
    const char c = kComponents[i];
    if (packed_widths[0]) {
      const char word = kComponents[packed_words[i]];
      if (attributes.is_signed) {
        Line(fmt::format("v.{} = float(xe_ibfe(w.{}, {}u, {}u));", c, word, packed_widths[i],
                         packed_offsets[i]));
        if (!attributes.is_integer) {
          switch (attributes.signed_rf_mode) {
            case xenos::SignedRepeatingFractionMode::kZeroClampMinusOne:
              if (packed_widths[i] > 2) {
                Line(fmt::format(
                    "v.{0} = v.{0} * {1};", c,
                    exact(1.0f / float((uint32_t(1) << (packed_widths[i] - 1)) - 1))));
              }
              // Both -(2^(n-1)) and -(2^(n-1)-1) are -1.
              Line(fmt::format("v.{0} = max(v.{0}, -1.0);", c));
              break;
            case xenos::SignedRepeatingFractionMode::kNoZero: {
              float zero = 1.0f / float((uint32_t(1) << packed_widths[i]) - 1);
              Line(fmt::format("v.{0} = mad(v.{0}, {1}, {2});", c, exact(2.0f * zero),
                               exact(zero)));
            } break;
            default:
              EmitTranslationError("unknown signed repeating fraction mode");
              break;
          }
        }
      } else {
        Line(fmt::format("v.{} = float((w.{} >> {}u) & 0x{:X}u);", c, word, packed_offsets[i],
                         (uint32_t(1) << packed_widths[i]) - 1));
        if (!attributes.is_integer && packed_widths[i] > 1) {
          Line(fmt::format("v.{0} = v.{0} * {1};", c,
                           exact(1.0f / float((uint32_t(1) << packed_widths[i]) - 1))));
        }
      }
      continue;
    }
    switch (attributes.data_format) {
      case xenos::VertexFormat::k_16_16_FLOAT:
      case xenos::VertexFormat::k_16_16_16_16_FLOAT:
        // D3D10+ float16 (with NaNs), as the DXBC translator converts it.
        Line(fmt::format("v.{} = f16tof32(w.{} >> {}u);", c, kComponents[i >> 1],
                         (i & 1) ? 16 : 0));
        break;
      case xenos::VertexFormat::k_32:
      case xenos::VertexFormat::k_32_32:
      case xenos::VertexFormat::k_32_32_32_32:
        Line(fmt::format("v.{0} = float({1}(w.{0}));", c, attributes.is_signed ? "asint" : ""));
        if (!attributes.is_integer) {
          if (attributes.is_signed) {
            switch (attributes.signed_rf_mode) {
              case xenos::SignedRepeatingFractionMode::kZeroClampMinusOne:
                Line(fmt::format("v.{0} = v.{0} * {1};", c, exact(1.0f / 2147483647.0f)));
                break;
              case xenos::SignedRepeatingFractionMode::kNoZero:
                Line(fmt::format("v.{0} = mad(v.{0}, {1}, {2});", c,
                                 exact(1.0f / 2147483647.5f), exact(0.5f / 2147483647.5f)));
                break;
              default:
                EmitTranslationError("unknown signed repeating fraction mode");
                break;
            }
          } else {
            Line(fmt::format("v.{0} = v.{0} * {1};", c, exact(1.0f / 4294967295.0f)));
          }
        }
        break;
      case xenos::VertexFormat::k_32_FLOAT:
      case xenos::VertexFormat::k_32_32_FLOAT:
      case xenos::VertexFormat::k_32_32_32_32_FLOAT:
      case xenos::VertexFormat::k_32_32_32_FLOAT:
        // Already in place.
        break;
      default:
        EmitTranslationError("unknown vertex format");
        break;
    }
  }
  // The exponent bias.
  if (attributes.exp_adjust) {
    std::string mask = Mask(used_format_components);
    Line(fmt::format("v.{0} = v.{0} * {1};", mask,
                     exact(std::ldexp(1.0f, attributes.exp_adjust))));
  }
  // Zeros in the components the format does not have.
  std::string missing = Mask(used_result_components & ~used_format_components);
  if (!missing.empty()) {
    Line(fmt::format("v.{} = 0.0;", missing));
  }
  // Through the vertex fetch hook of shader mods.
  Line(fmt::format("v = XE_HOOK_VFETCH({}u, {}, v);", fetch_constant, attributes.offset));
  StoreResult(instr.result, "v");
}

std::string HlslShaderTranslator::RelativeIndex(InstructionStorageAddressingMode mode,
                                                uint32_t index) const {
  switch (mode) {
    case InstructionStorageAddressingMode::kAddressRegisterRelative:
      return fmt::format("a0 + {}", index);
    case InstructionStorageAddressingMode::kLoopRelative:
      return fmt::format("aL.x + {}", index);
    default:
      return fmt::format("{}", index);
  }
}

HlslShaderTranslator::Operand HlslShaderTranslator::LoadOperand(const InstructionOperand& operand,
                                                                uint32_t needed_components) const {
  Operand result;
  std::memset(result.swizzle, 'x', sizeof(result.swizzle));
  uint32_t first_needed_component;
  if (!rex::bit_scan_forward(needed_components, &first_needed_component)) {
    return result;
  }
  switch (operand.storage_source) {
    case InstructionStorageSource::kRegister:
      if (current_shader().uses_register_dynamic_addressing()) {
        result.base =
            "p.r[" + RelativeIndex(operand.storage_addressing_mode, operand.storage_index) + "]";
      } else {
        result.base = fmt::format("p.r[{}]", operand.storage_index);
      }
      break;
    case InstructionStorageSource::kConstantFloat:
      if (operand.storage_addressing_mode == InstructionStorageAddressingMode::kAbsolute) {
        if (current_shader().constant_register_map().GetPackedFloatConstantIndex(
                operand.storage_index) == UINT32_MAX) {
          return result;
        }
        // XE_C through the constant hook of shader mods.
        result.base = fmt::format("XE_CONSTANT({})", operand.storage_index);
      } else {
        // Dynamically indexed constants are not packed.
        result.base = "xe_float_constants[" +
                      RelativeIndex(operand.storage_addressing_mode, operand.storage_index) + "]";
      }
      break;
    default:
      return result;
  }
  for (uint32_t i = 0; i < 4; ++i) {
    SwizzleSource component =
        operand.GetComponent((needed_components & (1u << i)) ? i : first_needed_component);
    result.swizzle[i] = kComponents[uint32_t(component) & 3];
  }
  result.absolute = operand.is_absolute_value;
  result.negated = operand.is_negated;
  return result;
}

void HlslShaderTranslator::StoreResult(const InstructionResult& result, const std::string& value) {
  uint32_t used_write_mask = result.GetUsedWriteMask();
  if (!used_write_mask) {
    return;
  }
  std::string dest;
  bool clamped = result.is_clamped;
  bool scalar_dest = false;
  switch (result.storage_target) {
    case InstructionStorageTarget::kNone:
      return;
    case InstructionStorageTarget::kRegister:
      if (current_shader().uses_register_dynamic_addressing()) {
        dest = "p.r[" + RelativeIndex(result.storage_addressing_mode, result.storage_index) + "]";
      } else {
        dest = fmt::format("p.r[{}]", result.storage_index);
      }
      break;
    case InstructionStorageTarget::kColor:
      dest = fmt::format("p.color[{}]", result.storage_index);
      break;
    case InstructionStorageTarget::kInterpolator:
      // Every export register: the epilogue outputs those of the modification's
      // interpolator mask, as the DXBC translator stores only those.
      dest = fmt::format("p.o[{}]", result.storage_index);
      break;
    case InstructionStorageTarget::kPosition:
      dest = "p.position";
      break;
    case InstructionStorageTarget::kPointSizeEdgeFlagKillVertex:
      dest = "p.point_size_edge_flag_kill";
      break;
    case InstructionStorageTarget::kDepth:
      // Saturated: depth outside 0...1 is unsafe for float24 and ROV.
      dest = "p.depth";
      clamped = true;
      scalar_dest = true;
      break;
    default:
      EmitTranslationError("unsupported result target in generated HLSL");
      return;
  }
  std::string dest_mask, source_swizzle, constant_mask, constant_values;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(used_write_mask & (1u << i))) {
      continue;
    }
    SwizzleSource component = result.components[i];
    if (component >= SwizzleSource::kX && component <= SwizzleSource::kW) {
      dest_mask.push_back(kComponents[i]);
      source_swizzle.push_back(kComponents[uint32_t(component)]);
    } else {
      constant_mask.push_back(kComponents[i]);
      if (!constant_values.empty()) {
        constant_values += ", ";
      }
      constant_values += component == SwizzleSource::k1 ? "1.0" : "0.0";
    }
  }
  if (!dest_mask.empty()) {
    std::string source = "(" + value + ")." + source_swizzle;
    if (clamped) {
      source = "saturate(" + source + ")";
    }
    Line((scalar_dest ? dest : dest + "." + dest_mask) + " = " + source + ";");
  }
  if (!constant_mask.empty()) {
    std::string constant = constant_mask.size() == 1
                               ? constant_values
                               : fmt::format("float{}({})", constant_mask.size(), constant_values);
    Line((scalar_dest ? dest : dest + "." + constant_mask) + " = " + constant + ";");
  }
  // The point size clamped to the guest's bounds as signed integers, as the
  // DXBC translator clamps it.
  if (result.storage_target == InstructionStorageTarget::kPointSizeEdgeFlagKillVertex &&
      (used_write_mask & 0b0001)) {
    Line("p.point_size_edge_flag_kill.x = xe_clamp_point_size(p.point_size_edge_flag_kill.x);");
  }
}

void HlslShaderTranslator::EmitVectorOperation(const ParsedAluInstruction& instr) {
  uint32_t used = instr.vector_and_constant_result.GetUsedResultComponents();
  AluVectorOpcode opcode = instr.vector_opcode;
  if (!used && !GetAluVectorOpcodeInfo(opcode).changed_state) {
    Line("precise float4 v = 0.0;");
    return;
  }
  uint32_t operand_count = opcode == AluVectorOpcode::kCube ? 1 : instr.vector_operand_count;
  Operand operands[3];
  for (uint32_t i = 0; i < operand_count; ++i) {
    uint32_t needed = GetAluVectorOpNeededSourceComponents(opcode, i + 1, used);
    if (opcode == AluVectorOpcode::kCube) {
      needed &= 0b1101;
    }
    operands[i] = LoadOperand(instr.vector_operands[i], needed);
    Line(fmt::format("float4 o{} = {};", i, operands[i].Vector()));
  }
  uint32_t identical = operand_count >= 2 ? instr.vector_operands[0].GetIdenticalComponents(
                                                instr.vector_operands[1])
                                          : 0;
  auto product = [&](uint32_t component) {
    char c = kComponents[component];
    return (identical & (1u << component)) ? fmt::format("(o0.{0} * o1.{0})", c)
                                           : fmt::format("xe_mul(o0.{0}, o1.{0})", c);
  };
  switch (opcode) {
    case AluVectorOpcode::kAdd:
      Line("precise float4 v = o0 + o1;");
      break;
    case AluVectorOpcode::kMul:
    case AluVectorOpcode::kMad: {
      // Shader Model 3 +-0 * x = +0 where the multiplicands differ; mad is a
      // separate add.
      Line("precise float4 m = o0 * o1;");
      std::string different = Mask(used & ~identical);
      if (!different.empty()) {
        Line(fmt::format("m.{0} = (min(abs(o0.{0}), abs(o1.{0})) == 0.0) ? 0.0 : m.{0};",
                         different));
      }
      Line(opcode == AluVectorOpcode::kMad ? "precise float4 v = m + o2;"
                                           : "precise float4 v = m;");
    } break;
    case AluVectorOpcode::kMax:
      Line("precise float4 v = xe_max(o0, o1);");
      break;
    case AluVectorOpcode::kMin:
      Line("precise float4 v = xe_min(o0, o1);");
      break;
    case AluVectorOpcode::kSeq:
      Line("precise float4 v = xe_seq(o0, o1);");
      break;
    case AluVectorOpcode::kSgt:
      Line("precise float4 v = xe_sgt(o0, o1);");
      break;
    case AluVectorOpcode::kSge:
      Line("precise float4 v = xe_sge(o0, o1);");
      break;
    case AluVectorOpcode::kSne:
      Line("precise float4 v = xe_sne(o0, o1);");
      break;
    case AluVectorOpcode::kFrc:
      Line("precise float4 v = frac(o0);");
      break;
    case AluVectorOpcode::kTrunc:
      Line("precise float4 v = trunc(o0);");
      break;
    case AluVectorOpcode::kFloor:
      Line("precise float4 v = floor(o0);");
      break;
    case AluVectorOpcode::kCndEq:
      Line("precise float4 v = xe_cndeq(o0, o1, o2);");
      break;
    case AluVectorOpcode::kCndGe:
      Line("precise float4 v = xe_cndge(o0, o1, o2);");
      break;
    case AluVectorOpcode::kCndGt:
      Line("precise float4 v = xe_cndgt(o0, o1, o2);");
      break;
    case AluVectorOpcode::kDp4:
    case AluVectorOpcode::kDp3:
    case AluVectorOpcode::kDp2Add: {
      // Sequential, unfused: ((p0 + p1) + p2) + p3.
      uint32_t count = opcode == AluVectorOpcode::kDp4   ? 4
                       : opcode == AluVectorOpcode::kDp3 ? 3
                                                         : 2;
      Line("precise float d = " + product(0) + ";");
      for (uint32_t i = 1; i < count; ++i) {
        Line("d = d + " + product(i) + ";");
      }
      if (opcode == AluVectorOpcode::kDp2Add) {
        Line("d = d + o2.x;");
      }
      Line("precise float4 v = d.xxxx;");
    } break;
    case AluVectorOpcode::kCube:
      Line("precise float4 v = xe_cube(o0);");
      break;
    case AluVectorOpcode::kMax4: {
      // SM4 max of the unique components of the register, in component order.
      uint32_t unique = 0;
      for (uint32_t i = 0; i < 4; ++i) {
        unique |= 1u << (std::strchr(kComponents, operands[0].swizzle[i]) - kComponents);
      }
      std::string maximum;
      for (uint32_t i = 0; i < 4; ++i) {
        if (unique & (1u << i)) {
          std::string component = operands[0].Underlying(kComponents[i]);
          maximum = maximum.empty() ? component : "max(" + maximum + ", " + component + ")";
        }
      }
      Line("precise float4 v = (" + maximum + ").xxxx;");
    } break;
    case AluVectorOpcode::kSetpEqPush:
    case AluVectorOpcode::kSetpNePush:
    case AluVectorOpcode::kSetpGtPush:
    case AluVectorOpcode::kSetpGePush: {
      const char* comparison = opcode == AluVectorOpcode::kSetpEqPush   ? "o1.{0} == 0.0"
                               : opcode == AluVectorOpcode::kSetpNePush ? "o1.{0} != 0.0"
                               : opcode == AluVectorOpcode::kSetpGtPush ? "0.0 < o1.{0}"
                                                                        : "o1.{0} >= 0.0";
      Line("p0 = o0.w == 0.0 && " + fmt::format(fmt::runtime(comparison), 'w') + ";");
      Line("precise float4 v = (((o0.x == 0.0 && " +
           fmt::format(fmt::runtime(comparison), 'x') + ") ? -1.0 : o0.x) + 1.0).xxxx;");
    } break;
    case AluVectorOpcode::kKillEq:
    case AluVectorOpcode::kKillGt:
    case AluVectorOpcode::kKillGe:
    case AluVectorOpcode::kKillNe: {
      const char* comparison = opcode == AluVectorOpcode::kKillEq   ? "o0 == o1"
                               : opcode == AluVectorOpcode::kKillGt ? "o1 < o0"
                               : opcode == AluVectorOpcode::kKillGe ? "o0 >= o1"
                                                                    : "o0 != o1";
      Line(fmt::format("bool kill = any({});", comparison));
      if (is_vertex_shader()) {
        EmitTranslationError("kill in a vertex shader");
      }
      Line("if (kill) { discard; }");
      Line("precise float4 v = (kill ? 1.0 : 0.0).xxxx;");
    } break;
    case AluVectorOpcode::kDst:
      Line("precise float4 v = 0.0;");
      Line("v.x = 1.0;");
      Line("v.y = " + product(1) + ";");
      Line("v.z = o0.z;");
      // As the translator: the loaded component 2 of the second operand.
      Line("v.w = o1.z;");
      break;
    case AluVectorOpcode::kMaxA:
      Line("a0 = int(min(max(floor(o0.w + 0.5), -256.0), 255.0));");
      Line("precise float4 v = xe_max(o0, o1);");
      break;
    default:
      EmitTranslationError("unknown ALU vector operation");
      Line("precise float4 v = 0.0;");
      break;
  }
}

void HlslShaderTranslator::EmitScalarOperation(const ParsedAluInstruction& instr) {
  AluScalarOpcode opcode = instr.scalar_opcode;
  if (opcode == AluScalarOpcode::kRetainPrev) {
    return;
  }
  Operand operands[2];
  for (uint32_t i = 0; i < instr.scalar_operand_count; ++i) {
    operands[i] = LoadOperand(instr.scalar_operands[i],
                              (1u << instr.scalar_operands[i].component_count) - 1);
  }
  std::string a = operands[0].Component(0);
  std::string b = operands[0].Component(1);
  std::string c = operands[1].Component(0);
  bool same_components =
      instr.scalar_operands[0].components[0] == instr.scalar_operands[0].components[1];
  auto set_predicate = [&](const std::string& condition) {
    Line("p0 = " + condition + ";");
    Line("ps = p0 ? 0.0 : 1.0;");
  };
  auto kill = [&](const std::string& condition) {
    Line("bool kill_s = " + condition + ";");
    if (is_vertex_shader()) {
      EmitTranslationError("kill in a vertex shader");
    }
    Line("if (kill_s) { discard; }");
    Line("ps = kill_s ? 1.0 : 0.0;");
  };
  auto clamp_a0 = [&](const std::string& value) {
    Line("a0 = int(min(max(" + value + ", -256.0), 255.0));");
  };
  switch (opcode) {
    case AluScalarOpcode::kAdds:
      Line("ps = " + a + " + " + b + ";");
      break;
    case AluScalarOpcode::kAddsPrev:
      Line("ps = " + a + " + ps;");
      break;
    case AluScalarOpcode::kMuls:
      Line(same_components ? "ps = " + a + " * " + b + ";"
                           : "ps = xe_mul(" + a + ", " + b + ");");
      break;
    case AluScalarOpcode::kMulsPrev:
      Line("ps = xe_mul(" + a + ", ps);");
      break;
    case AluScalarOpcode::kMulsPrev2:
      Line(fmt::format("ps = (ps != {0} && -abs(ps) >= {0} && -abs({1}) >= {0} && 0.0 < {1}) ? "
                       "xe_mul({2}, ps) : {0};",
                       kNegativeFloatMax, b, a));
      break;
    case AluScalarOpcode::kMaxs:
      Line(same_components ? "ps = " + a + ";"
                           : "ps = (" + a + " >= " + b + ") ? " + a + " : " + b + ";");
      break;
    case AluScalarOpcode::kMins:
      Line(same_components ? "ps = " + a + ";"
                           : "ps = (" + a + " < " + b + ") ? " + a + " : " + b + ";");
      break;
    case AluScalarOpcode::kSeqs:
      Line("ps = (" + a + " == 0.0) ? 1.0 : 0.0;");
      break;
    case AluScalarOpcode::kSgts:
      Line("ps = (0.0 < " + a + ") ? 1.0 : 0.0;");
      break;
    case AluScalarOpcode::kSges:
      Line("ps = (" + a + " >= 0.0) ? 1.0 : 0.0;");
      break;
    case AluScalarOpcode::kSnes:
      Line("ps = (" + a + " != 0.0) ? 1.0 : 0.0;");
      break;
    case AluScalarOpcode::kFrcs:
      Line("ps = frac(" + a + ");");
      break;
    case AluScalarOpcode::kTruncs:
      Line("ps = trunc(" + a + ");");
      break;
    case AluScalarOpcode::kFloors:
      Line("ps = floor(" + a + ");");
      break;
    case AluScalarOpcode::kExp:
      Line("ps = exp2(" + a + ");");
      break;
    case AluScalarOpcode::kLogc:
      Line("ps = xe_logc(" + a + ");");
      break;
    case AluScalarOpcode::kLog:
      Line("ps = log2(" + a + ");");
      break;
    case AluScalarOpcode::kRcpc:
      Line("ps = xe_rcpc(" + a + ");");
      break;
    case AluScalarOpcode::kRcpf:
      Line("ps = xe_rcpf(" + a + ");");
      break;
    case AluScalarOpcode::kRcp:
      Line("ps = rcp(" + a + ");");
      break;
    case AluScalarOpcode::kRsqc:
      Line("ps = xe_rsqc(" + a + ");");
      break;
    case AluScalarOpcode::kRsqf:
      Line("ps = xe_rsqf(" + a + ");");
      break;
    case AluScalarOpcode::kRsq:
      Line("ps = rsqrt(" + a + ");");
      break;
    case AluScalarOpcode::kMaxAs:
    case AluScalarOpcode::kMaxAsf:
      clamp_a0(opcode == AluScalarOpcode::kMaxAsf ? "floor(" + a + ")"
                                                  : "floor(" + a + " + 0.5)");
      Line(same_components ? "ps = " + a + ";"
                           : "ps = (" + a + " >= " + b + ") ? " + a + " : " + b + ";");
      break;
    case AluScalarOpcode::kSubs:
      Line("ps = " + a + " - " + b + ";");
      break;
    case AluScalarOpcode::kSubsPrev:
      Line("ps = " + a + " - ps;");
      break;
    case AluScalarOpcode::kSetpEq:
      set_predicate(a + " == 0.0");
      break;
    case AluScalarOpcode::kSetpNe:
      set_predicate(a + " != 0.0");
      break;
    case AluScalarOpcode::kSetpGt:
      set_predicate("0.0 < " + a);
      break;
    case AluScalarOpcode::kSetpGe:
      set_predicate(a + " >= 0.0");
      break;
    case AluScalarOpcode::kSetpInv:
      Line("ps = (" + a + " == 0.0) ? 1.0 : " + a + ";");
      Line("p0 = " + a + " == 1.0;");
      Line("ps = p0 ? 0.0 : ps;");
      break;
    case AluScalarOpcode::kSetpPop:
      Line("ps = " + a + " + -1.0;");
      Line("p0 = 0.0 >= ps;");
      Line("ps = p0 ? 0.0 : ps;");
      break;
    case AluScalarOpcode::kSetpClr:
      Line(fmt::format("ps = {};", kFloatMax));
      Line("p0 = false;");
      break;
    case AluScalarOpcode::kSetpRstr:
      Line("p0 = " + a + " == 0.0;");
      Line("ps = p0 ? 0.0 : " + a + ";");
      break;
    case AluScalarOpcode::kKillsEq:
      kill(a + " == 0.0");
      break;
    case AluScalarOpcode::kKillsGt:
      kill("0.0 < " + a);
      break;
    case AluScalarOpcode::kKillsGe:
      kill(a + " >= 0.0");
      break;
    case AluScalarOpcode::kKillsNe:
      kill(a + " != 0.0");
      break;
    case AluScalarOpcode::kKillsOne:
      kill(a + " == 1.0");
      break;
    case AluScalarOpcode::kSqrt:
      Line("ps = sqrt(" + a + ");");
      break;
    case AluScalarOpcode::kMulsc0:
    case AluScalarOpcode::kMulsc1:
      Line((instr.scalar_operands[0].GetIdenticalComponents(instr.scalar_operands[1]) & 1)
               ? "ps = " + a + " * " + c + ";"
               : "ps = xe_mul(" + a + ", " + c + ");");
      break;
    case AluScalarOpcode::kAddsc0:
    case AluScalarOpcode::kAddsc1:
      Line("ps = " + a + " + " + c + ";");
      break;
    case AluScalarOpcode::kSubsc0:
    case AluScalarOpcode::kSubsc1:
      Line("ps = " + a + " - " + c + ";");
      break;
    case AluScalarOpcode::kSin:
      Line("ps = sin(" + a + ");");
      break;
    case AluScalarOpcode::kCos:
      Line("ps = cos(" + a + ");");
      break;
    default:
      EmitTranslationError("unknown ALU scalar operation");
      Line("ps = 0.0;");
      break;
  }
}

void HlslShaderTranslator::ProcessAluInstruction(const ParsedAluInstruction& instr,
                                                 uint8_t /*memexport_eM_written_before*/) {
  if (instr.IsNop()) {
    return;
  }
  string::StringBuffer disassembly;
  instr.Disassemble(&disassembly);
  EmitDisassembly(disassembly.to_string());
  if (instr.is_predicated) {
    Open(fmt::format("if (p0 == {}) {{", Bool(instr.predicate_condition)));
  }
  // Both operations read their operands before either result is stored.
  Open("{");
  EmitVectorOperation(instr);
  EmitScalarOperation(instr);
  uint32_t used = instr.vector_and_constant_result.GetUsedResultComponents();
  StoreResult(instr.vector_and_constant_result, used ? "v" : "float4(0.0, 0.0, 0.0, 0.0)");
  StoreResult(instr.scalar_result, "ps.xxxx");
  Close();
  if (instr.is_predicated) {
    Close();
  }
}

void HlslShaderTranslator::ProcessTextureFetchInstruction(
    const ParsedTextureFetchInstruction& instr) {
  string::StringBuffer disassembly;
  instr.Disassemble(&disassembly);
  EmitDisassembly(disassembly.to_string());
  if (instr.is_predicated) {
    Open(fmt::format("if (p0 == {}) {{", Bool(instr.predicate_condition)));
  }
  Open("{");
  const auto& attributes = instr.attributes;
  switch (instr.opcode) {
    case FetchOpcode::kSetTextureLod:
      Line("grad_h_lod.w = " + LoadOperand(instr.operands[0], 0b0001).Component(0) + ";");
      break;
    case FetchOpcode::kSetTextureGradientsHorz:
      Line("grad_h_lod.xyz = (" + LoadOperand(instr.operands[0], 0b0111).Vector() + ").xyz;");
      break;
    case FetchOpcode::kSetTextureGradientsVert:
      Line("grad_v.xyz = (" + LoadOperand(instr.operands[0], 0b0111).Vector() + ").xyz;");
      break;
    case FetchOpcode::kGetTextureGradients: {
      uint32_t non_zero = instr.GetNonZeroResultComponents();
      if (!non_zero) {
        StoreResult(instr.result, "float4(0.0, 0.0, 0.0, 0.0)");
        break;
      }
      if (is_vertex_shader()) {
        EmitTranslationError("getGradients in a vertex shader");
      }
      Operand operand = LoadOperand(
          instr.operands[0], ((non_zero & 0b0011) ? 0b0001 : 0) | ((non_zero & 0b1100) ? 0b0010 : 0));
      std::string s = operand.Component(0), t = operand.Component(1);
      Line(fmt::format(
          "float4 t = float4(ddx_coarse({0}), ddy_coarse({0}), ddx_coarse({1}), ddy_coarse({1}));",
          s, t));
      StoreResult(instr.result, "t");
    } break;
    case FetchOpcode::kTextureFetch: {
      uint32_t non_zero = instr.GetNonZeroResultComponents();
      if (!non_zero) {
        StoreResult(instr.result, "float4(0.0, 0.0, 0.0, 0.0)");
        break;
      }
      uint32_t fetch = instr.operands[1].storage_index;
      bool computed_lod = attributes.use_computed_lod &&
                          (is_pixel_shader() || attributes.use_register_gradients);
      // The bindings in the DXBC translator's order: the sampler, then the
      // unsigned and signed textures, and for 3D those of the stacked 2D array.
      DxbcShader::SamplerBinding sampler = FindOrAddSamplerBinding(
          fetch, attributes.mag_filter, attributes.min_filter, attributes.mip_filter,
          computed_lod ? attributes.aniso_filter : xenos::AnisoFilter::kDisabled);
      xenos::FetchOpDimension dimension = instr.dimension == xenos::FetchOpDimension::k1D
                                               ? xenos::FetchOpDimension::k2D
                                               : instr.dimension;
      for (uint32_t is_stacked = 0;
           is_stacked < (instr.dimension == xenos::FetchOpDimension::k3DOrStacked ? 2u : 1u);
           ++is_stacked) {
        xenos::FetchOpDimension srv_dimension =
            is_stacked ? xenos::FetchOpDimension::k2D : instr.dimension;
        FindOrAddTextureBinding(fetch, srv_dimension, false);
        FindOrAddTextureBinding(fetch, srv_dimension, true);
      }
      std::string lod_flags;
      auto add_flag = [&lod_flags](const char* flag) {
        lod_flags += lod_flags.empty() ? flag : std::string(" | ") + flag;
      };
      if (computed_lod) {
        add_flag("XE_LOD_COMPUTED");
        if (attributes.use_register_gradients) {
          add_flag("XE_LOD_REGISTER_GRADIENTS");
        }
      }
      if (attributes.use_register_lod) {
        add_flag("XE_LOD_REGISTER");
      }
      if (attributes.mip_filter == xenos::TextureFilter::kBaseMap) {
        add_flag("XE_LOD_BASEMAP");
      }
      if (lod_flags.empty()) {
        lod_flags = "0u";
      }
      std::string lod = fmt::format("{}, {}, {}, grad_h_lod.w", attributes.unnormalized_coordinates
                                                                    ? "true"
                                                                    : "false",
                                    lod_flags, Float(attributes.lod_bias));
      std::string gradients =
          (computed_lod && attributes.use_register_gradients) ? ", grad_h_lod.xyz, grad_v.xyz" : "";
      uint32_t coordinate_count = uint32_t(xenos::GetFetchOpDimensionComponentCount(instr.dimension));
      Operand coord = LoadOperand(instr.operands[0], (1u << coordinate_count) - 1);
      std::string sampler_slot = SamplerSlotName(sampler);
      std::string call;
      switch (instr.dimension) {
        case xenos::FetchOpDimension::k1D:
          call = fmt::format("xe_tfetch_1d({}u, {}, {}, {}, {}, {}, {}{})", fetch,
                             TextureSlotName(fetch, dimension, false),
                             TextureSlotName(fetch, dimension, true), sampler_slot,
                             coord.Component(0), Float(attributes.offset_x), lod, gradients);
          break;
        case xenos::FetchOpDimension::k2D:
          call = fmt::format("xe_tfetch_2d({}u, {}, {}, {}, ({}).xy, float2({}, {}), {}{})", fetch,
                             TextureSlotName(fetch, dimension, false),
                             TextureSlotName(fetch, dimension, true), sampler_slot, coord.Vector(),
                             Float(attributes.offset_x), Float(attributes.offset_y), lod,
                             gradients);
          break;
        case xenos::FetchOpDimension::k3DOrStacked:
          call = fmt::format(
              "xe_tfetch_3d({}u, {}, {}, {}, {}, {}, ({}).xyz, float3({}, {}, {}), {}, {}u, {}u{})",
              fetch, TextureSlotName(fetch, dimension, false),
              TextureSlotName(fetch, dimension, true),
              TextureSlotName(fetch, xenos::FetchOpDimension::k2D, false),
              TextureSlotName(fetch, xenos::FetchOpDimension::k2D, true), sampler_slot,
              coord.Vector(), Float(attributes.offset_x), Float(attributes.offset_y),
              Float(attributes.offset_z), lod, uint32_t(attributes.vol_mag_filter),
              uint32_t(attributes.vol_min_filter), gradients);
          break;
        case xenos::FetchOpDimension::kCube:
          call = fmt::format("xe_tfetch_cube({}u, {}, {}, {}, ({}).xyz, float3({}, {}, {}), {}{})",
                             fetch, TextureSlotName(fetch, dimension, false),
                             TextureSlotName(fetch, dimension, true), sampler_slot, coord.Vector(),
                             Float(attributes.offset_x), Float(attributes.offset_y),
                             Float(attributes.offset_z), lod, gradients);
          break;
      }
      // Through the texture fetch hook of shader mods.
      Line(fmt::format("float4 t = XE_HOOK_TFETCH({}u, {}, {});", fetch, call, coord.Vector()));
      std::string zero = Mask(instr.result.GetUsedResultComponents() & ~non_zero);
      if (!zero.empty()) {
        Line("t." + zero + " = 0.0;");
      }
      StoreResult(instr.result, "t");
    } break;
    default:
      EmitTranslationError("getWeights, getCompTexLOD and getBCF are not supported in overrides");
      break;
  }
  Close();
  if (instr.is_predicated) {
    Close();
  }
}

}  // namespace rex::graphics
