/**
 * @file        graphics/d3d12/fsr.h
 * @brief       AMD FidelityFX FSR 3.1's upscaler (the FSR 3 upscaler of the FidelityFX SDK
 *              v1.1.4), built from source when the runtime is built with REX_FSR
 *
 * A context per render and output size, and its dispatch into a Direct3D 12 command list.
 * Free of runtime dependencies (no logging), so the CPU tests on WARP run the same code as
 * the post pass framework (post_pass.cpp, the passes of type "fsr"). Without REX_FSR,
 * Available() is false and Create() returns nullptr. See docs/analysis/fsr-plan.md.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <d3d12.h>

namespace rex::graphics::d3d12 {

class FsrUpscaler {
 public:
  struct Description {
    uint32_t render_width = 0, render_height = 0;
    // The output: the render size for native anti-aliasing (ratio 1).
    uint32_t output_width = 0, output_height = 0;
    // The input color is linear and may exceed 1 (FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE).
    bool hdr = false;
    // FSR's own exposure from the color; else an exposure of 1.
    bool auto_exposure = false;
    // FSR's checks of the dispatch values, reported to the message sink.
    bool debug_checking = false;
    // FSR's 16-bit shader permutations where the device runs 16-bit shader operations
    // natively, its default; false keeps them to 32 bits (an A/B, FSR_FP16=0).
    bool fp16 = true;
    bool operator==(const Description&) const = default;
  };
  // The inputs of a dispatch, at the render size: the color (linear), the depth (an inverted,
  // infinite projection: 1 at camera_near_plane, 0 at infinity), the motion vectors (host
  // pixels, the previous position less this one, without the jitter), and optionally the
  // reactive mask (R, 0 to 1: how much of the current frame a pixel takes) and the
  // transparency and composition mask (R: pixels whose history is less reliable). The output
  // is at the output size. Inputs in D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, the
  // output in D3D12_RESOURCE_STATE_UNORDERED_ACCESS; they stay so.
  struct Dispatch {
    ID3D12Resource* color = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion_vectors = nullptr;
    ID3D12Resource* reactive = nullptr;
    ID3D12Resource* composition = nullptr;
    ID3D12Resource* output = nullptr;
    // The jitter of the frame's draws in render pixels (x right, y down): the content at
    // pixel p is the unjittered scene at p - jitter.
    float jitter[2] = {};
    float frame_milliseconds = 16.7f;
    // RCAS sharpening, 0 to 1 (0: none).
    float sharpness = 0.0f;
    // The camera moved discontinuously, or there is no history: the accumulation restarts.
    bool reset = false;
    // The view distance at which the depth is 1, and the vertical field of view (radians).
    float camera_near_plane = 0.01f;
    float vertical_fov = 1.0f;
    // FSR's debug view in the output instead of the image.
    bool debug_view = false;
  };
  // Called with FSR's warnings and errors (debug_checking, and dispatch failures).
  using MessageSink = void (*)(bool error, const std::string& message);

  static bool Available();
  // "3.1.4 (FidelityFX SDK v1.1.4)", or empty without REX_FSR.
  static std::string Version();
  static void SetMessageSink(MessageSink sink);
  // nullptr without REX_FSR or when the context cannot be created (error_out says why).
  static std::unique_ptr<FsrUpscaler> Create(ID3D12Device* device, const Description& description,
                                             std::string* error_out);
  ~FsrUpscaler();
  FsrUpscaler(const FsrUpscaler&) = delete;
  FsrUpscaler& operator=(const FsrUpscaler&) = delete;

  const Description& description() const { return description_; }
  // Records the upscaler's passes into command_list. It binds its own descriptor heap, root
  // signatures and pipelines: whoever records after it binds theirs again. False on an error
  // (error_out says why).
  bool Record(ID3D12GraphicsCommandList* command_list, const Dispatch& dispatch,
              std::string* error_out);

 private:
  struct Context;
  FsrUpscaler(const Description& description, std::unique_ptr<Context> context);

  Description description_;
  std::unique_ptr<Context> context_;
};

}  // namespace rex::graphics::d3d12
