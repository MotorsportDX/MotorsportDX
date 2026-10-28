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

#pragma once

#include <atomic>

#include <rex/graphics/pipeline/shader/dxbc.h>
#include <rex/ui/d3d12/d3d12_provider.h>

namespace rex::graphics::d3d12 {

class D3D12Shader : public DxbcShader {
 public:
  class D3D12Translation : public DxbcTranslation {
   public:
    D3D12Translation(D3D12Shader& shader, uint64_t modification)
        : DxbcTranslation(shader, modification) {}

    void DisassembleDxbcAndDxil(const ui::d3d12::D3D12Provider& provider, bool disassemble_dxbc,
                                IDxbcConverter* dxbc_converter = nullptr,
                                IDxcUtils* dxc_utils = nullptr,
                                IDxcCompiler* dxc_compiler = nullptr);

    // A predicted translation of the start (docs/analysis/d3d-hle-n6.md
    // 19.3.10) that no draw has used yet: its first draw opens the window of
    // early pipeline creation, as a translation for a draw does. Set before
    // the title runs, cleared by the render thread.
    bool awaiting_first_draw() const { return awaiting_first_draw_; }
    void set_awaiting_first_draw(bool awaiting) { awaiting_first_draw_ = awaiting; }

    // The DXIL world (native_shader_dxil, docs/analysis/d3d-hle-n6.md 19.3.9):
    // the translation compiled by DXC beside its DXBC, empty until then. Set
    // where the DXBC binary is replaced: at the translation, and by the swap of
    // a worker's compile on the thread that creates pipelines.
    const std::vector<uint8_t>& dxil_binary() const { return dxil_binary_; }
    void SetDxilBinary(std::vector<uint8_t> binary) { dxil_binary_ = std::move(binary); }

   private:
    bool awaiting_first_draw_ = false;
    std::vector<uint8_t> dxil_binary_;
  };

  D3D12Shader(xenos::ShaderType shader_type, uint64_t ucode_data_hash, const uint32_t* ucode_dwords,
              size_t ucode_dword_count, std::endian ucode_source_endian = std::endian::big);

  // For owning subsystem like the pipeline cache, accessors for unique
  // identifiers (used instead of hashes to make sure collisions can't happen)
  // of binding layouts used by the shader, for invalidation if a shader with an
  // incompatible layout was bound.
  size_t GetTextureBindingLayoutUserUID() const { return texture_binding_layout_user_uid_; }
  size_t GetSamplerBindingLayoutUserUID() const { return sampler_binding_layout_user_uid_; }
  // Modifications of the same shader can be translated on different threads.
  // The "set" function must only be called if "enter" returned true - these are
  // set up only once.
  bool EnterBindingLayoutUserUIDSetup() { return !binding_layout_user_uids_set_up_.test_and_set(); }
  void SetTextureBindingLayoutUserUID(size_t uid) { texture_binding_layout_user_uid_ = uid; }
  void SetSamplerBindingLayoutUserUID(size_t uid) { sampler_binding_layout_user_uid_ = uid; }

  // The draw back end (d3d12_draw_back_end, draw_back_end.cpp): the fetch
  // constant slots the shader reads, set by the thread that runs the draws at
  // the shader's first capture after its translation, and read by the replay.
  bool back_end_fetch_slots_known() const { return back_end_fetch_slots_known_; }
  uint64_t back_end_fetch_slots() const { return back_end_fetch_slots_; }
  void SetBackEndFetchSlots(uint64_t slots) {
    back_end_fetch_slots_ = slots;
    back_end_fetch_slots_known_ = true;
  }

 protected:
  Translation* CreateTranslationInstance(uint64_t modification) override;

 private:
  std::atomic_flag binding_layout_user_uids_set_up_ = ATOMIC_FLAG_INIT;
  size_t texture_binding_layout_user_uid_ = 0;
  size_t sampler_binding_layout_user_uid_ = 0;
  uint64_t back_end_fetch_slots_ = 0;
  bool back_end_fetch_slots_known_ = false;
};

}  // namespace rex::graphics::d3d12
