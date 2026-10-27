/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstring>

#include <fmt/format.h>

#include <rex/graphics/pipeline/shader/dxbc.h>

namespace rex::graphics {

DxbcShader::DxbcShader(xenos::ShaderType shader_type, uint64_t ucode_data_hash,
                       const uint32_t* ucode_dwords, size_t ucode_dword_count,
                       std::endian ucode_source_endian)
    : Shader(shader_type, ucode_data_hash, ucode_dwords, ucode_dword_count, ucode_source_endian) {}

Shader::Translation* DxbcShader::CreateTranslationInstance(uint64_t modification) {
  return new DxbcTranslation(*this, modification);
}

bool DxbcShader::SetBindingsOnce(const std::vector<TextureBinding>& texture_bindings,
                                 const std::vector<SamplerBinding>& sampler_bindings) {
  if (bindings_setup_entered_.test_and_set(std::memory_order_relaxed)) {
    return HasBindings(texture_bindings, sampler_bindings);
  }
  texture_bindings_ = texture_bindings;
  sampler_bindings_ = sampler_bindings;
  used_texture_mask_ = 0;
  for (const TextureBinding& binding : texture_bindings_) {
    used_texture_mask_ |= 1u << binding.fetch_constant;
  }
  return true;
}

bool DxbcShader::HasBindings(const std::vector<TextureBinding>& texture_bindings,
                             const std::vector<SamplerBinding>& sampler_bindings) const {
  if (texture_bindings.size() != texture_bindings_.size() ||
      sampler_bindings.size() != sampler_bindings_.size()) {
    return false;
  }
  for (size_t i = 0; i < texture_bindings.size(); ++i) {
    const TextureBinding& a = texture_bindings[i];
    const TextureBinding& b = texture_bindings_[i];
    if (a.bindless_descriptor_index != b.bindless_descriptor_index ||
        a.fetch_constant != b.fetch_constant || a.dimension != b.dimension ||
        a.is_signed != b.is_signed) {
      return false;
    }
  }
  for (size_t i = 0; i < sampler_bindings.size(); ++i) {
    const SamplerBinding& a = sampler_bindings[i];
    const SamplerBinding& b = sampler_bindings_[i];
    if (a.bindless_descriptor_index != b.bindless_descriptor_index ||
        a.fetch_constant != b.fetch_constant || a.mag_filter != b.mag_filter ||
        a.min_filter != b.min_filter || a.mip_filter != b.mip_filter ||
        a.aniso_filter != b.aniso_filter) {
      return false;
    }
  }
  return true;
}

std::string DxbcShader::DescribeBindingDifference(
    const std::vector<TextureBinding>& texture_bindings,
    const std::vector<SamplerBinding>& sampler_bindings) const {
  if (texture_bindings.size() != texture_bindings_.size() ||
      sampler_bindings.size() != sampler_bindings_.size()) {
    return fmt::format("{} textures and {} samplers, stored {} and {}", texture_bindings.size(),
                       sampler_bindings.size(), texture_bindings_.size(), sampler_bindings_.size());
  }
  for (size_t i = 0; i < texture_bindings.size(); ++i) {
    const TextureBinding& a = texture_bindings[i];
    const TextureBinding& b = texture_bindings_[i];
    if (a.bindless_descriptor_index != b.bindless_descriptor_index ||
        a.fetch_constant != b.fetch_constant || a.dimension != b.dimension ||
        a.is_signed != b.is_signed) {
      return fmt::format(
          "texture {}: descriptor {} fetch {} dimension {} signed {}, stored {} {} {} {}", i,
          a.bindless_descriptor_index, a.fetch_constant, uint32_t(a.dimension), a.is_signed,
          b.bindless_descriptor_index, b.fetch_constant, uint32_t(b.dimension), b.is_signed);
    }
  }
  for (size_t i = 0; i < sampler_bindings.size(); ++i) {
    const SamplerBinding& a = sampler_bindings[i];
    const SamplerBinding& b = sampler_bindings_[i];
    if (a.bindless_descriptor_index != b.bindless_descriptor_index ||
        a.fetch_constant != b.fetch_constant || a.mag_filter != b.mag_filter ||
        a.min_filter != b.min_filter || a.mip_filter != b.mip_filter ||
        a.aniso_filter != b.aniso_filter) {
      return fmt::format(
          "sampler {}: descriptor {} fetch {} filters {} {} {} aniso {}, stored {} {} {} {} {} {}",
          i, a.bindless_descriptor_index, a.fetch_constant, uint32_t(a.mag_filter),
          uint32_t(a.min_filter), uint32_t(a.mip_filter), uint32_t(a.aniso_filter),
          b.bindless_descriptor_index, b.fetch_constant, uint32_t(b.mag_filter),
          uint32_t(b.min_filter), uint32_t(b.mip_filter), uint32_t(b.aniso_filter));
    }
  }
  return std::string();
}

}  // namespace rex::graphics
