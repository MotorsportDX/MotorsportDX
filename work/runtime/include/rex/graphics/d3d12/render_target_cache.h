/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rex/assert.h>
#include <rex/graphics/d3d12/shared_memory.h>
#include <rex/graphics/d3d12/texture_cache.h>
#include <rex/graphics/flags.h>
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/memory.h>
#include <rex/ui/d3d12/d3d12_cpu_descriptor_pool.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/d3d12_upload_buffer_pool.h>
#include <rex/ui/d3d12/d3d12_util.h>
namespace rex::graphics::d3d12 {

class D3D12CommandProcessor;

class D3D12RenderTargetCache final : public RenderTargetCache {
 public:
  D3D12RenderTargetCache(const RegisterFile& register_file, const memory::Memory& memory,
                         uint32_t draw_resolution_scale_x, uint32_t draw_resolution_scale_y,
                         D3D12CommandProcessor& command_processor, bool bindless_resources_used)
      : RenderTargetCache(register_file, memory, draw_resolution_scale_x, draw_resolution_scale_y),
        command_processor_(command_processor),
        bindless_resources_used_(bindless_resources_used) {}
  ~D3D12RenderTargetCache() override;

  bool Initialize();
  void Shutdown(bool from_destructor = false);

  void CompletedSubmissionUpdated();
  void BeginSubmission();

  Path GetPath() const override { return path_; }

  bool Update(bool is_rasterization_done, reg::RB_DEPTHCONTROL normalized_depth_control,
              uint32_t normalized_color_mask, const Shader& vertex_shader) override;

  void InvalidateCommandListRenderTargets() {
    are_current_command_list_render_targets_valid_ = false;
  }
  // render_target_update_guard: the last Update can stand for the next draw's
  // when the registers it reads and update_epoch() are the same - it left
  // nothing for the next one, the command list has its render targets bound,
  // and nothing waits in the pending transfers or over the memory cap.
  bool CanRepeatLastUpdate() const {
    return are_current_command_list_render_targets_valid_ && !pending_transfers_.slots &&
           IsLastUpdateRepeatable() && !IsOverMemoryCap();
  }

  bool msaa_2x_supported() const { return msaa_2x_supported_; }

  // Post passes (post_pass.cpp): the single-sampled host color render target
  // that the last draw bound as color target `index`, after the pending
  // transfers, transitioned to `state` (the next draw binding it transitions
  // it back). Its draw RTV and format; nullptr without one, with MSAA, or with
  // ROV.
  struct LastColorRenderTarget {
    ID3D12Resource* resource;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    DXGI_FORMAT format;
    uint32_t width, height;
    xenos::ColorRenderTargetFormat guest_format;
    // Without one: 1 nothing bound in the slot, 2 a multisampled target, whose
    // key's EDRAM base, sample count (log2) and resource format these are.
    uint32_t miss;
    uint32_t miss_base_tiles, miss_samples_log2, miss_format;
    // The target's sample count.
    uint32_t samples;
  };
  // With entry0, entry 0's private multisampled pair is taken too (the
  // pre-race screens' final combine, docs/analysis/d3d-hle-n5.md 18.29).
  ID3D12Resource* TransitionLastColorRenderTarget(uint32_t index, D3D12_RESOURCE_STATES state,
                                                  LastColorRenderTarget& out,
                                                  bool entry0 = false);

  // Performs the transfers kept for a clear split into several rectangles (see
  // PendingTransfers), if any. Called before anything else uses the render
  // targets, and at the end of a submission.
  void PerformPendingTransfers();
  // native_rt_surfaces bit 4: copies the deferred transfers of a resolve's
  // spans, or of every tile one render target owns, into their owners.
  void FlushLazyTransfers(const draw_util::ResolveInfo* resolve_info,
                          const RenderTarget* owner);

  void WriteEdramRawSRVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void WriteEdramRawUAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void WriteEdramUintPow2SRVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                       uint32_t element_size_bytes_pow2);
  void WriteEdramUintPow2UAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                       uint32_t element_size_bytes_pow2);

  // Performs the resolve to a shared memory area according to the current
  // register values, and also clears the render targets if needed. Must be in a
  // frame for calling.
  bool Resolve(const memory::Memory& memory, D3D12SharedMemory& shared_memory,
               D3D12TextureCache& texture_cache, uint32_t& written_address_out,
               uint32_t& written_length_out);

  // For host render targets.

  bool gamma_render_target_as_unorm16() const { return gamma_render_target_as_unorm16_; }

  // Using R16G16[B16A16]_SNORM, which are -1...1, not the needed -32...32.
  // Persistent data doesn't depend on this, so can be overriden by per-game
  // configuration.
  bool IsFixed16TruncatedToMinus1To1() const {
    return GetPath() == Path::kHostRenderTargets && !REXCVAR_GET(snorm16_render_target_full_range);
  }

  bool depth_float24_round() const { return depth_float24_round_; }
  bool depth_float24_convert_in_pixel_shader() const {
    return depth_float24_convert_in_pixel_shader_;
  }

  DXGI_FORMAT GetColorResourceDXGIFormat(xenos::ColorRenderTargetFormat format) const;
  DXGI_FORMAT GetColorDrawDXGIFormat(xenos::ColorRenderTargetFormat format) const;
  DXGI_FORMAT GetColorOwnershipTransferDXGIFormat(xenos::ColorRenderTargetFormat format,
                                                  bool* is_integer_out = nullptr) const;
  static DXGI_FORMAT GetDepthResourceDXGIFormat(xenos::DepthRenderTargetFormat format);
  static DXGI_FORMAT GetDepthDSVDXGIFormat(xenos::DepthRenderTargetFormat format);
  static DXGI_FORMAT GetDepthSRVDepthDXGIFormat(xenos::DepthRenderTargetFormat format);
  static DXGI_FORMAT GetDepthSRVStencilDXGIFormat(xenos::DepthRenderTargetFormat format);

 protected:
  uint32_t GetMaxRenderTargetWidth() const override { return D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION; }
  uint32_t GetMaxRenderTargetHeight() const override {
    return D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  }

  RenderTarget* CreateRenderTarget(RenderTargetKey key, uint32_t tile_rows) override;
  RenderTarget* ResizeRenderTarget(RenderTarget* render_target, uint32_t tile_rows) override;
  void RetireRenderTarget(RenderTarget* render_target) override;
  void FlushLazyHolder(RenderTarget* holder) override;

  bool IsHostDepthEncodingDifferent(xenos::DepthRenderTargetFormat format) const override;

  bool IsGammaFormatHostStorageSeparate() const override;

  void RequestPixelShaderInterlockBarrier() override;

 private:
  enum class EdramBufferModificationStatus {
    // The values are ordered by how strong the barrier conditions are.
    // No uncommitted ROV/UAV writes.
    kUnmodified,
    // Need to commit before the next ROV usage with overlap.
    kAsROV,
    // Need to commit before any next ROV usage.
    kAsUAV,
  };
  void TransitionEdramBuffer(D3D12_RESOURCE_STATES new_state);
  void MarkEdramBufferModified(
      EdramBufferModificationStatus modification_status = EdramBufferModificationStatus::kAsUAV);
  void CommitEdramBufferUAVWrites(
      EdramBufferModificationStatus commit_status = EdramBufferModificationStatus::kAsROV);

  D3D12CommandProcessor& command_processor_;
  bool bindless_resources_used_;

  Path path_ = Path::kHostRenderTargets;

  // For host render targets, an EDRAM-sized scratch buffer for:
  // - Guest render target data copied from host render targets during copying
  //   in resolves.
  // - Host float32 depth in ownership transfers when the host depth texture and
  //   the destination are the same.
  // For rasterizer-ordered view, the buffer containing the EDRAM data.
  // (Note that if a hybrid RTV / DSV + ROV approach to color render targets is
  //  added, which is, however, unlikely as it would have very complicated
  //  interaction with depth / stencil testing, host depth will need to be
  //  copied to a different buffer - the same range may have ROV-owned color and
  //  host float32 depth at the same time).
  ID3D12Resource* edram_buffer_ = nullptr;
  D3D12_RESOURCE_STATES edram_buffer_state_;
  EdramBufferModificationStatus edram_buffer_modification_status_ =
      EdramBufferModificationStatus::kUnmodified;

  // Non-shader-visible descriptor heap containing pre-created SRV and UAV
  // descriptors of the EDRAM buffer, for faster binding (by copying rather
  // than creation).
  enum class EdramBufferDescriptorIndex : uint32_t {
    kRawSRV,
    kR32UintSRV,
    kR32G32UintSRV,
    kR32G32B32A32UintSRV,
    kRawUAV,
    kR32UintUAV,
    kR32G32UintUAV,
    kR32G32B32A32UintUAV,

    kCount,
  };
  ID3D12DescriptorHeap* edram_buffer_descriptor_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE edram_buffer_descriptor_heap_start_;

  // Resolve copying root signature and pipelines.
  // Parameter 0 - draw_util::ResolveCopyShaderConstants or its ::DestRelative.
  // Parameter 1 - destination (shared memory or a part of it).
  // Parameter 2 - source (EDRAM).
  ID3D12RootSignature* resolve_copy_root_signature_ = nullptr;
  struct ResolveCopyShaderCode {
    const void* unscaled;
    size_t unscaled_size;
    const void* scaled;
    size_t scaled_size;
  };
  static const ResolveCopyShaderCode
      kResolveCopyShaders[size_t(draw_util::ResolveCopyShaderIndex::kCount)];
  ID3D12PipelineState* resolve_copy_pipelines_[size_t(draw_util::ResolveCopyShaderIndex::kCount)] =
      {};
  // native_rt_resolve (docs/analysis/d3d-hle.md 16.7): resolve_texture_32bpp,
  // with the parameters of the resolve copy but one more constant (the
  // texture's endianness and host format, and whether the copy is bitwise),
  // and the scratch textures of native_rt_resolve_check by destination.
  // native_rt_surfaces bit 2 (docs/analysis/d3d-hle.md 16.9): resolves from the
  // host render target, resolve_host_32bpp by source kind (float color, uint
  // color, depth) and multisampling, with the parameters of the resolve copy,
  // two more constants and the stencil in a table of its own; and the byte check
  // of native_rt_surfaces_check (scratch for the shader's bytes, readbacks of
  // both, compared when the submission completes).
  ID3D12RootSignature* host_resolve_root_signature_ = nullptr;
  ID3D12PipelineState* host_resolve_pipelines_[3][2] = {};
  Microsoft::WRL::ComPtr<ID3D12Resource> host_resolve_scratch_;
  uint64_t host_resolve_scratch_size_ = 0;
  D3D12_RESOURCE_STATES host_resolve_scratch_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  struct HostResolveCheck {
    Microsoft::WRL::ComPtr<ID3D12Resource> readback[2];
    uint64_t length = 0;
    uint64_t submission = 0;
    uint32_t dest = 0, format = 0;
    bool is_depth = false;
    uint32_t msaa = 0, sample_select = 0, host_offset = 0, width = 0, height = 0;
    std::string where;
    // native_rt_surfaces_depth_dump: the file readback[0] is written to,
    // instead of a comparison.
    std::string dump_file;
  };
  std::vector<HostResolveCheck> host_resolve_checks_;
  ID3D12RootSignature* resolve_texture_root_signature_ = nullptr;
  ID3D12PipelineState* resolve_texture_pipeline_ = nullptr;
  std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>> resolve_texture_scratch_;
  // native_rt_surfaces bit 32 (docs/analysis/d3d-hle-n5.md 18.12): resolves
  // from a private surface into the host textures of the destination, from
  // the host render target, per source kind and multisampling; the host
  // resolve's root signature with one more constant.
  ID3D12RootSignature* host_texture_root_signature_ = nullptr;
  ID3D12PipelineState* host_texture_pipelines_[3][2] = {};

  // For host render targets.

  class D3D12RenderTarget final : public RenderTarget {
   public:
    // descriptor_load_separate is present when the DXGI formats are different
    // for drawing and bit-exact loading (for NaN pattern preservation across
    // EDRAM tile ownership transfers in floating-point formats, and to
    // distinguish between two -1 representations in snorm formats).
    D3D12RenderTarget(RenderTargetKey key, ID3D12Resource* resource,
                      ui::d3d12::D3D12CpuDescriptorPool::Descriptor&& descriptor_draw,
                      ui::d3d12::D3D12CpuDescriptorPool::Descriptor&& descriptor_load_separate,
                      ui::d3d12::D3D12CpuDescriptorPool::Descriptor&& descriptor_srv,
                      ui::d3d12::D3D12CpuDescriptorPool::Descriptor&& descriptor_srv_stencil,
                      D3D12_RESOURCE_STATES resource_state)
        : RenderTarget(key),
          resource_(resource),
          descriptor_draw_(std::move(descriptor_draw)),
          descriptor_load_separate_(std::move(descriptor_load_separate)),
          descriptor_srv_(std::move(descriptor_srv)),
          descriptor_srv_stencil_(std::move(descriptor_srv_stencil)),
          resource_state_(resource_state) {}

    ID3D12Resource* resource() const { return resource_.Get(); }
    const ui::d3d12::D3D12CpuDescriptorPool::Descriptor& descriptor_draw() const {
      return descriptor_draw_;
    }
    const ui::d3d12::D3D12CpuDescriptorPool::Descriptor& descriptor_srv() const {
      return descriptor_srv_;
    }
    const ui::d3d12::D3D12CpuDescriptorPool::Descriptor& descriptor_srv_stencil() const {
      return descriptor_srv_stencil_;
    }
    const ui::d3d12::D3D12CpuDescriptorPool::Descriptor& descriptor_load_separate() const {
      return descriptor_load_separate_;
    }

    D3D12_RESOURCE_STATES SetResourceState(D3D12_RESOURCE_STATES new_state) {
      D3D12_RESOURCE_STATES old_state = resource_state_;
      resource_state_ = new_state;
      if (new_state != old_state) {
        // A draw that skips Update would not set the state back.
        BumpUpdateEpoch();
      }
      return old_state;
    }

    uint32_t temporary_srv_descriptor_index() const { return temporary_srv_descriptor_index_; }
    void SetTemporarySRVDescriptorIndex(uint32_t index) { temporary_srv_descriptor_index_ = index; }
    uint32_t temporary_srv_descriptor_index_stencil() const {
      return temporary_srv_descriptor_index_stencil_;
    }
    void SetTemporarySRVDescriptorIndexStencil(uint32_t index) {
      temporary_srv_descriptor_index_stencil_ = index;
    }
    uint32_t temporary_sort_index() const { return temporary_sort_index_; }
    void SetTemporarySortIndex(uint32_t index) { temporary_sort_index_ = index; }

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    ui::d3d12::D3D12CpuDescriptorPool::Descriptor descriptor_draw_;
    ui::d3d12::D3D12CpuDescriptorPool::Descriptor descriptor_load_separate_;
    // Texture SRV non-shader-visible descriptors, to prepare shader-visible
    // descriptors faster, by copying rather than by creating every time.
    // TODO(Triang3l): With bindless resources, persistently store them in the
    // heap.
    ui::d3d12::D3D12CpuDescriptorPool::Descriptor descriptor_srv_;
    ui::d3d12::D3D12CpuDescriptorPool::Descriptor descriptor_srv_stencil_;
    D3D12_RESOURCE_STATES resource_state_;
    // Temporary storage for indices in operations like transfers and dumps.
    uint32_t temporary_srv_descriptor_index_ = UINT32_MAX;
    uint32_t temporary_srv_descriptor_index_stencil_ = UINT32_MAX;
    uint32_t temporary_sort_index_ = 0;
  };

  enum TransferCBVRegister : uint32_t {
    kTransferCBVRegisterStencilMask,
    kTransferCBVRegisterAddress,
    kTransferCBVRegisterHostDepthAddress,
  };
  enum TransferSRVRegister : uint32_t {
    kTransferSRVRegisterColor,
    kTransferSRVRegisterDepth,
    kTransferSRVRegisterStencil,
    kTransferSRVRegisterHostDepth,
    kTransferSRVRegisterCount,
  };
  enum TransferUsedRootParameter : uint32_t {
    // Changed 8 times per transfer.
    kTransferUsedRootParameterStencilMaskConstant,
    kTransferUsedRootParameterColorSRV,
    // Mutually exclusive with ColorSRV.
    kTransferUsedRootParameterDepthSRV,
    // Mutually exclusive with ColorSRV.
    kTransferUsedRootParameterStencilSRV,
    // May happen to be the same for different sources.
    kTransferUsedRootParameterAddressConstant,
    kTransferUsedRootParameterHostDepthSRV,
    kTransferUsedRootParameterHostDepthAddressConstant,
    kTransferUsedRootParameterCount,

    kTransferUsedRootParameterStencilMaskConstantBit =
        uint32_t(1) << kTransferUsedRootParameterStencilMaskConstant,
    kTransferUsedRootParameterColorSRVBit = uint32_t(1) << kTransferUsedRootParameterColorSRV,
    kTransferUsedRootParameterDepthSRVBit = uint32_t(1) << kTransferUsedRootParameterDepthSRV,
    kTransferUsedRootParameterStencilSRVBit = uint32_t(1) << kTransferUsedRootParameterStencilSRV,
    kTransferUsedRootParameterAddressConstantBit = uint32_t(1)
                                                   << kTransferUsedRootParameterAddressConstant,
    kTransferUsedRootParameterHostDepthSRVBit = uint32_t(1)
                                                << kTransferUsedRootParameterHostDepthSRV,
    kTransferUsedRootParameterHostDepthAddressConstantBit =
        uint32_t(1) << kTransferUsedRootParameterHostDepthAddressConstant,

    kTransferUsedRootParametersDescriptorMask =
        kTransferUsedRootParameterColorSRVBit | kTransferUsedRootParameterDepthSRVBit |
        kTransferUsedRootParameterStencilSRVBit | kTransferUsedRootParameterHostDepthSRVBit,
  };
  enum class TransferRootSignatureIndex {
    kColor,
    kDepth,
    kDepthStencil,
    kColorToStencilBit,
    kStencilToStencilBit,
    kColorAndHostDepth,
    kDepthAndHostDepth,
    kDepthStencilAndHostDepth,
    kCount,
  };
  static const uint32_t kTransferUsedRootParameters[size_t(TransferRootSignatureIndex::kCount)];
  enum class TransferMode : uint32_t {
    // 1 SRV (color texture), source constant.
    kColorToDepth,
    // 1 SRV (color texture), source constant.
    kColorToColor,

    // 1 or 2 SRVs (depth texture, stencil texture if SV_StencilRef is
    // supported), source constant.
    kDepthToDepth,
    // 2 SRVs (depth texture, stencil texture), source constant.
    kDepthToColor,

    // 1 SRV (color texture), mask constant (most frequently changed, 8 times
    // per transfer), source constant.
    kColorToStencilBit,
    // 1 SRV (stencil texture), mask constant, source constant.
    kDepthToStencilBit,

    // Two-source modes, using the host depth if it, when converted to the guest
    // format, matches what's in the owner source (not modified, keep host
    // precision), or the guest data otherwise (significantly modified, possibly
    // cleared). Stencil for SV_StencilRef is always taken from the guest
    // source.

    // 2 SRVs (color texture, host depth texture or buffer), source constant,
    // host depth source constant.
    kColorAndHostDepthToDepth,
    // When using different source and destination depth formats. 2 or 3 SRVs
    // (depth texture, stencil texture if SV_StencilRef is supported, host depth
    // texture or buffer), source constant, host depth source constant.
    kDepthAndHostDepthToDepth,

    kCount,
  };
  enum class TransferOutput {
    kColor,
    kDepth,
    // With this output, kTransferCBVRegisterStencilMask is used.
    kStencilBit,
  };
  struct TransferModeInfo {
    TransferOutput output;
    TransferRootSignatureIndex root_signature_no_stencil_ref;
    TransferRootSignatureIndex root_signature_with_stencil_ref;
  };
  static const TransferModeInfo kTransferModes[size_t(TransferMode::kCount)];

  union TransferShaderKey {
    uint32_t key;
    struct {
      xenos::MsaaSamples dest_msaa_samples : xenos::kMsaaSamplesBits;
      uint32_t dest_resource_format : xenos::kRenderTargetFormatBits;
      xenos::MsaaSamples source_msaa_samples : xenos::kMsaaSamplesBits;
      // Always 1x when host_depth_source_is_copy is true not to create the same
      // pipeline for different MSAA sample counts as it doesn't matter in this
      // case.
      xenos::MsaaSamples host_depth_source_msaa_samples : xenos::kMsaaSamplesBits;
      uint32_t source_resource_format : xenos::kRenderTargetFormatBits;
      // If host depth is also fetched, whether it's pre-copied to the EDRAM
      // buffer (but since it's just a scratch buffer, with tiles laid out
      // linearly with the same pitch as in the original render target; also no
      // swapping of 40-sample columns as opposed to the host render target -
      // this is done only for the color source).
      uint32_t host_depth_source_is_copy : 1;

      // Last bits because this affects the root signature - after sorting, only
      // change it as fewer times as possible. Depth buffers have an additional
      // stencil SRV.
      static_assert(size_t(TransferMode::kCount) <= (size_t(1) << 3));
      TransferMode mode : 3;
    };

    TransferShaderKey() : key(0) { static_assert_size(*this, sizeof(key)); }

    struct Hasher {
      size_t operator()(const TransferShaderKey& key) const {
        return std::hash<uint32_t>{}(key.key);
      }
    };
    bool operator==(const TransferShaderKey& other_key) const { return key == other_key.key; }
    bool operator!=(const TransferShaderKey& other_key) const { return !(*this == other_key); }
    bool operator<(const TransferShaderKey& other_key) const { return key < other_key.key; }
  };

  union TransferAddressConstant {
    uint32_t constant;
    struct {
      // All in tiles.
      uint32_t dest_pitch : xenos::kEdramPitchTilesBits;
      uint32_t source_pitch : xenos::kEdramPitchTilesBits;
      // Destination base in tiles minus source base in tiles (not vice versa
      // because this is a transform of the coordinate system, not addresses
      // themselves).
      // + 1 bit because this is a signed difference between two EDRAM bases.
      // 0 for host_depth_source_is_copy (ignored in this case anyway as
      // destination == source anyway).
      int32_t source_to_dest : xenos::kEdramBaseTilesBits + 1;
    };
    TransferAddressConstant() : constant(0) { static_assert_size(*this, sizeof(constant)); }
    bool operator==(const TransferAddressConstant& other_constant) const {
      return constant == other_constant.constant;
    }
    bool operator!=(const TransferAddressConstant& other_constant) const {
      return !(*this == other_constant);
    }
  };

  struct TransferInvocation {
    Transfer transfer;
    TransferShaderKey shader_key;
    TransferInvocation(const Transfer& transfer, const TransferShaderKey& shader_key)
        : transfer(transfer), shader_key(shader_key) {}
    bool operator<(const TransferInvocation& other_invocation) const {
      // TODO(Triang3l): See if it may be better to sort by the source in the
      // first place, especially when reading the same data multiple times (like
      // to write the stencil bits after depth) for better read locality.
      // Sort by the shader key primarily to reduce pipeline state (context)
      // switches.
      if (shader_key != other_invocation.shader_key) {
        return shader_key < other_invocation.shader_key;
      }
      // Host depth render targets are changed rarely if they exist, won't save
      // many binding changes, ignore them for simplicity (their existence is
      // caught by the shader key change).
      assert_not_null(transfer.source);
      assert_not_null(other_invocation.transfer.source);
      uint32_t source_index =
          static_cast<const D3D12RenderTarget*>(transfer.source)->temporary_sort_index();
      uint32_t other_source_index =
          static_cast<const D3D12RenderTarget*>(other_invocation.transfer.source)
              ->temporary_sort_index();
      if (source_index != other_source_index) {
        return source_index < other_source_index;
      }
      return transfer.start_tiles < other_invocation.transfer.start_tiles;
    }
    bool CanBeMergedIntoOneDraw(const TransferInvocation& other_invocation) const {
      return shader_key == other_invocation.shader_key &&
             transfer.AreSourcesSame(other_invocation.transfer);
    }
  };

  enum {
    kHostDepthStoreRootParameterConstants,
    kHostDepthStoreRootParameterSource,
    kHostDepthStoreRootParameterDest,
    kHostDepthStoreRootParameterCount,
  };

  union DumpPipelineKey {
    uint32_t key;
    struct {
      xenos::MsaaSamples msaa_samples : 2;
      uint32_t resource_format : 4;
      // Last bit because this affects the root signature - after sorting, only
      // change it at most once. Depth buffers have an additional stencil SRV.
      uint32_t is_depth : 1;
    };

    DumpPipelineKey() : key(0) { static_assert_size(*this, sizeof(key)); }

    struct Hasher {
      size_t operator()(const DumpPipelineKey& key) const { return std::hash<uint32_t>{}(key.key); }
    };
    bool operator==(const DumpPipelineKey& other_key) const { return key == other_key.key; }
    bool operator!=(const DumpPipelineKey& other_key) const { return !(*this == other_key); }
    bool operator<(const DumpPipelineKey& other_key) const { return key < other_key.key; }

    xenos::ColorRenderTargetFormat GetColorFormat() const {
      assert_false(is_depth);
      return xenos::ColorRenderTargetFormat(resource_format);
    }
    xenos::DepthRenderTargetFormat GetDepthFormat() const {
      assert_true(is_depth);
      return xenos::DepthRenderTargetFormat(resource_format);
    }
  };

  union DumpOffsets {
    uint32_t offsets;
    struct {
      // May be beyond the EDRAM tile count in case of EDRAM addressing
      // wrapping, thus + 1 bit.
      uint32_t dispatch_first_tile : xenos::kEdramBaseTilesBits + 1;
      uint32_t source_base_tiles : xenos::kEdramBaseTilesBits;
    };
    DumpOffsets() : offsets(0) { static_assert_size(*this, sizeof(offsets)); }
    bool operator==(const DumpOffsets& other_offsets) const {
      return offsets == other_offsets.offsets;
    }
    bool operator!=(const DumpOffsets& other_offsets) const { return !(*this == other_offsets); }
  };

  union DumpPitches {
    uint32_t pitches;
    struct {
      // Both in tiles.
      uint32_t dest_pitch : xenos::kEdramPitchTilesBits;
      uint32_t source_pitch : xenos::kEdramPitchTilesBits;
    };
    DumpPitches() : pitches(0) { static_assert_size(*this, sizeof(pitches)); }
    bool operator==(const DumpPitches& other_pitches) const {
      return pitches == other_pitches.pitches;
    }
    bool operator!=(const DumpPitches& other_pitches) const { return !(*this == other_pitches); }
  };

  enum DumpCbuffer : uint32_t {
    kDumpCbufferOffsets,
    kDumpCbufferPitches,
    kDumpCbufferCount,
  };

  enum DumpRootParameter : uint32_t {
    // May be changed multiple times for the same source.
    kDumpRootParameterOffsets,
    // One resolve may need multiple sources.
    kDumpRootParameterSource,

    // May be different for different sources.
    kDumpRootParameterColorPitches = kDumpRootParameterSource + 1,
    // Only changed between 32bpp and 64bpp.
    kDumpRootParameterColorEdram,

    kDumpRootParameterColorCount,

    // Same change frequency than the source (though currently the command
    // processor can't contiguously allocate multiple descriptors with bindless,
    // when such functionality is added, switch to one root signature).
    kDumpRootParameterDepthStencil = kDumpRootParameterSource + 1,
    kDumpRootParameterDepthPitches,
    kDumpRootParameterDepthEdram,

    kDumpRootParameterDepthCount,
  };

  struct DumpInvocation {
    ResolveCopyDumpRectangle rectangle;
    DumpPipelineKey pipeline_key;
    DumpInvocation(const ResolveCopyDumpRectangle& rectangle, const DumpPipelineKey& pipeline_key)
        : rectangle(rectangle), pipeline_key(pipeline_key) {}
    bool operator<(const DumpInvocation& other_invocation) const {
      // Sort by the pipeline key primarily to reduce pipeline state (context)
      // switches.
      if (pipeline_key != other_invocation.pipeline_key) {
        return pipeline_key < other_invocation.pipeline_key;
      }
      assert_not_null(rectangle.render_target);
      uint32_t render_target_index =
          static_cast<const D3D12RenderTarget*>(rectangle.render_target)->temporary_sort_index();
      const ResolveCopyDumpRectangle& other_rectangle = other_invocation.rectangle;
      uint32_t other_render_target_index =
          static_cast<const D3D12RenderTarget*>(other_rectangle.render_target)
              ->temporary_sort_index();
      if (render_target_index != other_render_target_index) {
        return render_target_index < other_render_target_index;
      }
      if (rectangle.row_first != other_rectangle.row_first) {
        return rectangle.row_first < other_rectangle.row_first;
      }
      return rectangle.row_first_start < other_rectangle.row_first_start;
    }
  };

  struct DirectResolvePushConstants {
    draw_util::ResolveCopyShaderConstants resolve;
    uint32_t source_base_tiles;
    uint32_t source_pitch_tiles;
    uint32_t dispatch_first_tile;
  };

  struct DirectResolvePipelineKey {
    DumpPipelineKey dump_pipeline_key;
    draw_util::ResolveCopyShaderIndex copy_shader;
    bool draw_resolution_scaled;

    uint64_t packed() const {
      return uint64_t(dump_pipeline_key.key) | (uint64_t(size_t(copy_shader)) << 32) |
             (uint64_t(draw_resolution_scaled ? 1 : 0) << 40);
    }
    struct Hasher {
      size_t operator()(const DirectResolvePipelineKey& key) const {
        return std::hash<uint64_t>{}(key.packed());
      }
    };
    bool operator==(const DirectResolvePipelineKey& other_key) const {
      return packed() == other_key.packed();
    }
  };

  // Returns:
  // - A pointer to 1 pipeline for writing color or depth (or stencil via
  //   SV_StencilRef).
  // - A pointer to 8 pipelines for writing stencil by discarding samples
  //   depending on whether they have one bit set, from 1 << 0 to 1 << 7.
  // - Null if failed to create.
  ID3D12PipelineState* const* GetOrCreateTransferPipelines(TransferShaderKey key);

  static TransferMode GetTransferMode(bool dest_is_stencil_bit, bool dest_is_depth,
                                      bool source_is_depth, bool source_has_host_depth) {
    assert_true(dest_is_depth || (!dest_is_stencil_bit && !source_has_host_depth));
    if (dest_is_stencil_bit) {
      return source_is_depth ? TransferMode::kDepthToStencilBit : TransferMode::kColorToStencilBit;
    }
    if (dest_is_depth) {
      if (source_is_depth) {
        return source_has_host_depth ? TransferMode::kDepthAndHostDepthToDepth
                                     : TransferMode::kDepthToDepth;
      }
      return source_has_host_depth ? TransferMode::kColorAndHostDepthToDepth
                                   : TransferMode::kColorToDepth;
    }
    return source_is_depth ? TransferMode::kDepthToColor : TransferMode::kColorToColor;
  }

  // Do ownership transfers for render targets - each render target / vector may
  // be null / empty in case there's nothing to do for them.
  // resolve_clear_rectangle is expected to be provided by
  // PrepareHostRenderTargetsResolveClear which should do all the needed size
  // bound checks.
  void PerformTransfersAndResolveClears(
      uint32_t render_target_count, RenderTarget* const* render_targets,
      const std::vector<Transfer>* render_target_transfers,
      const uint64_t* render_target_resolve_clear_values = nullptr,
      const Transfer::Rectangle* resolve_clear_rectangle = nullptr);

  // FM4 often clears a render target with several adjacent rectangles, a narrow
  // one first, which alone doesn't cover the tiles it claims. The transfers into
  // the slots such a rectangle overwrites are kept pending, with the rectangle
  // as the cutout; a following adjacent rectangle into the same render targets
  // extends the cutout, anything else performs them outside the cutout first.
  struct PendingTransfers {
    uint32_t slots = 0;
    RenderTarget* render_targets[1 + xenos::kMaxColorRenderTargets] = {};
    std::vector<Transfer> transfers[1 + xenos::kMaxColorRenderTargets];
    Transfer::Rectangle cutout = {};
  };
  PendingTransfers pending_transfers_;

  // Accepts an array of (1 + xenos::kMaxColorRenderTargets) render targets,
  // first depth, then color.
  void SetCommandListRenderTargets(RenderTarget* const* depth_and_color_render_targets);

  ID3D12PipelineState* GetOrCreateDumpPipeline(DumpPipelineKey key);
  ID3D12PipelineState* GetOrCreateDirectResolvePipeline(DirectResolvePipelineKey key);
  bool TryResolveCopyDirectly(const draw_util::ResolveInfo& resolve_info,
                              draw_util::ResolveCopyShaderIndex copy_shader,
                              bool draw_resolution_scaled);
  struct HostResolveSource {
    RenderTarget* render_target = nullptr;
    uint32_t kind = 0;  // 0 float color, 1 uint color, 2 depth.
    uint32_t multisampled = 0;
    uint32_t host_info = 0;
    uint32_t host_offset = 0;
    // The exact source position of the first pixel minus host_offset, signed
    // 8.8 fixed point, 16:16 (xe_resolve_host_source_phase, 18.17).
    uint32_t host_phase = 0;
    // The render target's scale: the scene's for a private surface (18.15).
    DrawScale scale;
  };
  // The source's scale over a destination scale as n:d, 16:16, for the host
  // resolve shaders' xe_resolve_host_source_scale.
  static uint32_t HostSourceScale(DrawScale source, DrawScale dest) {
    uint64_t n = uint64_t(source.numerator) * dest.denominator;
    uint64_t d = uint64_t(source.denominator) * dest.numerator;
    uint64_t divisor = std::gcd(n, d);
    return uint32_t(n / divisor) | (uint32_t(d / divisor) << 16);
  }
  // Whether a resolve can read its source straight from one host render
  // target that owns every source tile with the same layout.
  bool FindHostResolveSource(const draw_util::ResolveInfo& resolve_info,
                             draw_util::ResolveCopyShaderIndex copy_shader,
                             HostResolveSource& source_out);
  // The resolve from the host render target into the destination, or with
  // check, into scratch initialized with the destination's bytes, then both
  // read back for the byte check.
  bool DispatchHostResolve(D3D12TextureCache& texture_cache, D3D12SharedMemory& shared_memory,
                           const draw_util::ResolveInfo& resolve_info,
                           const draw_util::ResolveCopyShaderConstants& constants,
                           const HostResolveSource& source, bool scaled, bool check);

 public:
  // At each frame's start: the byte checks whose submission completed;
  // returns the compared resolves and those that differed.
  // native_rt_surfaces_depth_dump: reads back a resolve's destination bytes.
  void QueueResolveDump(D3D12SharedMemory& shared_memory,
                        const draw_util::ResolveInfo& resolve_info);
  void PollHostResolveChecks(uint64_t completed_submission, uint32_t& checked_out,
                             uint32_t& mismatched_out);

 private:
  // After the resolve copy: writes the host textures of a full-screen 32bpp
  // destination straight from the EDRAM buffer (native_rt_resolve).
  void ResolveIntoHostTextures(D3D12TextureCache& texture_cache,
                               const draw_util::ResolveInfo& resolve_info,
                               draw_util::ResolveCopyShaderIndex copy_shader,
                               const draw_util::ResolveCopyShaderConstants& constants);
  // After a resolve from a private surface (native_rt_surfaces bit 32): writes
  // the host textures of a 32bpp destination straight from the host render
  // target, or with native_rt_resolve_check scratch copies that the
  // textures' next loads are compared with.
  // With scene, a full-screen destination at the scene's scale (18.15).
  // A tile's band of a full-screen destination (photo mode's three tiles in
  // entry 0's private pair, docs/analysis/d3d-hle-n5.md 18.27): the texture's
  // base and height in guest pixels, and the band's first row in it.
  struct SceneBand {
    uint32_t texture_base = 0;
    uint32_t texture_height = 0;
    uint32_t top = 0;
  };
  // Whether the resolve is a tile's band: entry 0's private pair resolved
  // across the full width into the rows of a 1280x720 or larger destination
  // that the tile's window offset gives, its base moved to those rows.
  bool GetSceneBand(const draw_util::ResolveInfo& resolve_info, RenderTargetKey source_key,
                    SceneBand& band_out) const;
  void ResolveHostIntoTextures(D3D12TextureCache& texture_cache,
                               const draw_util::ResolveInfo& resolve_info,
                               const draw_util::ResolveCopyShaderConstants& constants,
                               const HostResolveSource& source, bool scene,
                               const SceneBand* band = nullptr);

  // Writes contents of host render targets within rectangles from
  // ResolveInfo::GetCopyEdramTileSpan to edram_buffer_.
  // With only set, dumps that target alone over the span (a private scene
  // surface of native_one_pass_msaa), not the owners of the tile map.
  bool DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used, uint32_t dump_rows,
                         uint32_t dump_pitch, RenderTarget* only = nullptr);
  // The private scene surface (depth, or the 4x colour) a resolve reads, or
  // nullptr.
  RenderTarget* GetResolvePrivateSurface(const draw_util::ResolveInfo& resolve_info);

  bool use_stencil_reference_output_ = false;

  bool gamma_render_target_as_unorm16_ = false;

  bool depth_float24_round_ = false;
  bool depth_float24_convert_in_pixel_shader_ = false;

  bool msaa_2x_supported_ = false;

  std::shared_ptr<ui::d3d12::D3D12CpuDescriptorPool> descriptor_pool_color_;
  std::shared_ptr<ui::d3d12::D3D12CpuDescriptorPool> descriptor_pool_depth_;
  std::shared_ptr<ui::d3d12::D3D12CpuDescriptorPool> descriptor_pool_srv_;
  ui::d3d12::D3D12CpuDescriptorPool::Descriptor null_rtv_descriptor_ss_;
  ui::d3d12::D3D12CpuDescriptorPool::Descriptor null_rtv_descriptor_ms_;

  // Possible tile ownership transfer paths:
  // - To color:
  //   - From color: 1 SRV (color).
  //   - From depth: 2 SRVs (depth, stencil).
  // - To depth / stencil (with SV_StencilRef):
  //   - From color: 1 SRV (color).
  //   - From depth: 2 SRVs (depth, stencil).
  //   - From color and float32 depth: 2 SRVs (color with stencil, depth).
  //     - Different depth buffer: depth SRV is a texture.
  //     - Same depth buffer: depth SRV is a buffer (pre-copied).
  // - To depth (no SV_StencilRef):
  //   - From color: 1 SRV (color).
  //   - From depth: 1 SRV (depth).
  //   - From color and float32 depth: 2 SRVs (color, depth).
  //     - Different depth buffer: depth SRV is a texture.
  //     - Same depth buffer: depth SRV is a buffer (pre-copied).
  // - To stencil (no SV_StencilRef):
  //   - From color: 1 SRV (color).
  //   - From depth: 1 SRV (stencil).

  const RenderTarget* const*
      current_command_list_render_targets_[1 + xenos::kMaxColorRenderTargets];
  bool are_current_command_list_render_targets_valid_ = false;

  // Temporary storage for descriptors used in PerformTransfersAndResolveClears
  // and DumpRenderTargets.
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> current_temporary_descriptors_cpu_;
  std::vector<ui::d3d12::util::DescriptorCpuGpuHandlePair> current_temporary_descriptors_gpu_;

  ID3D12RootSignature* host_depth_store_root_signature_ = nullptr;
  ID3D12PipelineState* host_depth_store_pipelines_[size_t(xenos::MsaaSamples::k4X) + 1] = {};

  std::unique_ptr<ui::d3d12::D3D12UploadBufferPool> transfer_vertex_buffer_pool_;

  ID3D12RootSignature* transfer_root_signatures_[size_t(TransferRootSignatureIndex::kCount)] = {};
  std::unordered_map<TransferShaderKey, ID3D12PipelineState*, TransferShaderKey::Hasher>
      transfer_pipelines_;
  std::unordered_map<TransferShaderKey, std::array<ID3D12PipelineState*, 8>,
                     TransferShaderKey::Hasher>
      transfer_stencil_bit_pipelines_;

  // Temporary storage for PerformTransfersAndResolveClears.
  std::vector<TransferInvocation> current_transfer_invocations_;

  // Temporary storage for DumpRenderTargets.
  std::vector<ResolveCopyDumpRectangle> dump_rectangles_;
  std::vector<DumpInvocation> dump_invocations_;
  std::vector<ResolveCopyDispatch> direct_resolve_dispatches_;

  ID3D12RootSignature* dump_root_signature_color_ = nullptr;
  ID3D12RootSignature* dump_root_signature_depth_ = nullptr;
  // Compute pipelines for copying host render target contents to the EDRAM
  // buffer. May be null if failed to create.
  std::unordered_map<DumpPipelineKey, ID3D12PipelineState*, DumpPipelineKey::Hasher>
      dump_pipelines_;
  ID3D12RootSignature* direct_resolve_root_signature_color_ = nullptr;
  ID3D12RootSignature* direct_resolve_root_signature_depth_ = nullptr;
  std::unordered_map<DirectResolvePipelineKey, ID3D12PipelineState*,
                     DirectResolvePipelineKey::Hasher>
      direct_resolve_pipelines_;
  uint64_t direct_resolve_attempt_count_ = 0;
  uint64_t direct_resolve_success_count_ = 0;
  uint64_t direct_resolve_fallback_count_ = 0;

  // Parameter 0 - 2 root constants (red, green).
  ID3D12RootSignature* uint32_rtv_clear_root_signature_ = nullptr;
  // [32 or 32_32][MSAA samples].
  ID3D12PipelineState* uint32_rtv_clear_pipelines_[2][size_t(xenos::MsaaSamples::k4X) + 1] = {};

  std::vector<Transfer> clear_transfers_[2];
  std::vector<std::pair<RenderTarget*, std::vector<Transfer>>> lazy_flush_groups_;
  // native_rt_surfaces bit 8: replaced and evicted targets, destroyed once
  // their submission has completed.
  std::vector<std::pair<uint64_t, RenderTarget*>> retired_render_targets_;
  // native_rt_surfaces_transfer_inventory: transfers and tiles by kind.
  struct TransferInventoryEntry {
    uint64_t transfers = 0, tiles = 0;
    // The destination-relative tile columns (bit per column, the last bit for
    // 63 and above) and the first and last tile rows the transfers wrote.
    uint64_t columns = 0;
    uint32_t row_first = UINT32_MAX, row_last = 0;
  };
  std::map<std::string, TransferInventoryEntry> transfer_inventory_;
  uint64_t transfer_inventory_frame_ = 0;
  void CountTransferInventory(RenderTargetKey dest, const std::vector<Transfer>& transfers,
                              bool stencil_bits);

  // Temporary storage for DXBC building.
  std::vector<uint32_t> built_shader_;

  // For rasterizer-ordered view (pixel shader interlock).

  ID3D12RootSignature* resolve_rov_clear_root_signature_ = nullptr;
  // Clearing 32bpp color or depth.
  ID3D12PipelineState* resolve_rov_clear_32bpp_pipeline_ = nullptr;
  // Clearing 64bpp color.
  ID3D12PipelineState* resolve_rov_clear_64bpp_pipeline_ = nullptr;
};

}  // namespace rex::graphics::d3d12
