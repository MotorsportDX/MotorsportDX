/**
 * @file        graphics/d3d12/fsr.cpp
 * @brief       AMD FidelityFX FSR 3.1's upscaler, built from the FidelityFX SDK source when
 *              REX_FSR is on (cmake/rex_fsr.cmake, docs/analysis/fsr-plan.md)
 */

#include <rex/graphics/d3d12/fsr.h>

#if REX_FSR

// ffx_util.h calls std::popcount in C++20 and later without including <bit>.
#include <bit>
#include <cfloat>
#include <cstddef>
#include <cstdio>
#include <vector>

#include <wrl/client.h>

#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#endif  // REX_FSR

namespace rex::graphics::d3d12 {

namespace {

FsrUpscaler::MessageSink message_sink = nullptr;

}  // namespace

void FsrUpscaler::SetMessageSink(MessageSink sink) { message_sink = sink; }

#if REX_FSR

namespace {

std::string Narrow(const wchar_t* text) {
  std::string out;
  for (; text && *text; ++text) {
    out += *text < 0x80 ? char(*text) : '?';
  }
  return out;
}

void OnMessage(FfxMsgType type, const wchar_t* message) {
  if (message_sink) {
    message_sink(type == FFX_MESSAGE_TYPE_ERROR, "FSR: " + Narrow(message));
  }
}

std::string ErrorText(const char* what, FfxErrorCode error) {
  char text[96];
  std::snprintf(text, sizeof(text), "%s failed with FidelityFX error 0x%08X", what,
                unsigned(error));
  return text;
}

// The backend's device capabilities, without FP16 while a context of a Description with fp16
// false is created (on the creating thread).
FfxGetDeviceCapabilitiesFunc backend_capabilities = nullptr;
thread_local bool creating_without_fp16 = false;

FfxErrorCode Capabilities(FfxInterface* backend, FfxDeviceCapabilities* capabilities) {
  FfxErrorCode error = backend_capabilities(backend, capabilities);
  if (creating_without_fp16 && capabilities) capabilities->fp16Supported = false;
  return error;
}

FfxResource Input(ID3D12Resource* resource, const wchar_t* name) {
  if (!resource) return {};
  return ffxGetResourceDX12(resource, ffxGetResourceDescriptionDX12(resource), name,
                            FFX_RESOURCE_STATE_COMPUTE_READ);
}

}  // namespace

struct FsrUpscaler::Context {
  std::vector<uint8_t> scratch;
  FfxFsr3UpscalerContext fsr = {};
  bool created = false;
  // The resources FSR 3.1 shares with the effects after it, which the application provides
  // (ffxFsr3UpscalerGetSharedResourceDescriptions): the dilated depth and motion vectors and
  // the reconstructed previous nearest depth, at the render size, in UNORDERED_ACCESS.
  Microsoft::WRL::ComPtr<ID3D12Resource> dilated_depth, dilated_motion_vectors,
      previous_nearest_depth;
  ~Context() {
    if (created) ffxFsr3UpscalerContextDestroy(&fsr);
  }
};

bool FsrUpscaler::Available() { return true; }

std::string FsrUpscaler::Version() {
  return std::to_string(FFX_FSR3UPSCALER_VERSION_MAJOR) + "." +
         std::to_string(FFX_FSR3UPSCALER_VERSION_MINOR) + "." +
         std::to_string(FFX_FSR3UPSCALER_VERSION_PATCH) + " (FidelityFX SDK v1.1.4)";
}

std::unique_ptr<FsrUpscaler> FsrUpscaler::Create(ID3D12Device* device,
                                                 const Description& description,
                                                 std::string* error_out) {
  auto context = std::make_unique<Context>();
  context->scratch.resize(ffxGetScratchMemorySizeDX12(FFX_FSR3UPSCALER_CONTEXT_COUNT));
  FfxFsr3UpscalerContextDescription create = {};
  FfxErrorCode error =
      ffxGetInterfaceDX12(&create.backendInterface, ffxGetDeviceDX12(device),
                          context->scratch.data(), context->scratch.size(),
                          FFX_FSR3UPSCALER_CONTEXT_COUNT);
  if (error != FFX_OK) {
    if (error_out) *error_out = ErrorText("ffxGetInterfaceDX12", error);
    return nullptr;
  }
  // The depth of Dispatch: inverted (1 near) with an infinite far plane.
  create.flags = FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED | FFX_FSR3UPSCALER_ENABLE_DEPTH_INFINITE;
  if (description.hdr) create.flags |= FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE;
  if (description.auto_exposure) create.flags |= FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
  if (description.debug_checking) {
    create.flags |= FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;
    create.fpMessage = OnMessage;
  }
  create.maxRenderSize = {description.render_width, description.render_height};
  create.maxUpscaleSize = {description.output_width, description.output_height};
  backend_capabilities = create.backendInterface.fpGetDeviceCapabilities;
  create.backendInterface.fpGetDeviceCapabilities = Capabilities;
  creating_without_fp16 = !description.fp16;
  error = ffxFsr3UpscalerContextCreate(&context->fsr, &create);
  creating_without_fp16 = false;
  if (error != FFX_OK) {
    if (error_out) *error_out = ErrorText("ffxFsr3UpscalerContextCreate", error);
    return nullptr;
  }
  context->created = true;
  FfxFsr3UpscalerSharedResourceDescriptions shared = {};
  error = ffxFsr3UpscalerGetSharedResourceDescriptions(&context->fsr, &shared);
  if (error != FFX_OK) {
    if (error_out) *error_out = ErrorText("ffxFsr3UpscalerGetSharedResourceDescriptions", error);
    return nullptr;
  }
  struct Shared {
    const FfxCreateResourceDescription& description;
    DXGI_FORMAT format;
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource;
  };
  for (const Shared& item :
       {Shared{shared.dilatedDepth, DXGI_FORMAT_R32_FLOAT, context->dilated_depth},
        Shared{shared.dilatedMotionVectors, DXGI_FORMAT_R16G16_FLOAT,
               context->dilated_motion_vectors},
        Shared{shared.reconstructedPrevNearestDepth, DXGI_FORMAT_R32_UINT,
               context->previous_nearest_depth}}) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = item.description.resourceDescription.width;
    desc.Height = item.description.resourceDescription.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = item.format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                               IID_PPV_ARGS(&item.resource)))) {
      if (error_out) *error_out = "FSR's shared resources could not be created";
      return nullptr;
    }
  }
  return std::unique_ptr<FsrUpscaler>(new FsrUpscaler(description, std::move(context)));
}

bool FsrUpscaler::Record(ID3D12GraphicsCommandList* command_list, const Dispatch& dispatch,
                         std::string* error_out) {
  FfxFsr3UpscalerDispatchDescription description = {};
  description.commandList = ffxGetCommandListDX12(command_list);
  description.color = Input(dispatch.color, L"fsr_color");
  description.depth = Input(dispatch.depth, L"fsr_depth");
  description.motionVectors = Input(dispatch.motion_vectors, L"fsr_motion_vectors");
  description.reactive = Input(dispatch.reactive, L"fsr_reactive");
  description.transparencyAndComposition = Input(dispatch.composition, L"fsr_composition");
  auto shared = [](ID3D12Resource* resource, const wchar_t* name) {
    return ffxGetResourceDX12(resource,
                              ffxGetResourceDescriptionDX12(resource, FFX_RESOURCE_USAGE_UAV),
                              name, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
  };
  description.dilatedDepth = shared(context_->dilated_depth.Get(), L"fsr_dilated_depth");
  description.dilatedMotionVectors =
      shared(context_->dilated_motion_vectors.Get(), L"fsr_dilated_motion_vectors");
  description.reconstructedPrevNearestDepth =
      shared(context_->previous_nearest_depth.Get(), L"fsr_previous_nearest_depth");
  description.output = ffxGetResourceDX12(
      dispatch.output, ffxGetResourceDescriptionDX12(dispatch.output, FFX_RESOURCE_USAGE_UAV),
      L"fsr_output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
  description.jitterOffset = {dispatch.jitter[0], dispatch.jitter[1]};
  // Motion vectors in render pixels (FSR divides the scale by the render size).
  description.motionVectorScale = {1.0f, 1.0f};
  description.renderSize = {description_.render_width, description_.render_height};
  description.upscaleSize = {description_.output_width, description_.output_height};
  description.enableSharpening = dispatch.sharpness > 0.0f;
  description.sharpness = dispatch.sharpness;
  description.frameTimeDelta = dispatch.frame_milliseconds;
  description.preExposure = 1.0f;
  description.reset = dispatch.reset;
  // Inverted infinite depth: the near plane goes into cameraFar, FLT_MAX into cameraNear.
  description.cameraNear = FLT_MAX;
  description.cameraFar = dispatch.camera_near_plane;
  description.cameraFovAngleVertical = dispatch.vertical_fov;
  description.viewSpaceToMetersFactor = 1.0f;
  description.flags = dispatch.debug_view ? FFX_FSR3UPSCALER_DISPATCH_DRAW_DEBUG_VIEW : 0;
  FfxErrorCode error = ffxFsr3UpscalerContextDispatch(&context_->fsr, &description);
  if (error != FFX_OK) {
    if (error_out) *error_out = ErrorText("ffxFsr3UpscalerContextDispatch", error);
    return false;
  }
  return true;
}

#else  // REX_FSR

struct FsrUpscaler::Context {};

bool FsrUpscaler::Available() { return false; }

std::string FsrUpscaler::Version() { return std::string(); }

std::unique_ptr<FsrUpscaler> FsrUpscaler::Create(ID3D12Device*, const Description&,
                                                 std::string* error_out) {
  if (error_out) *error_out = "the runtime is built without FSR (REX_FSR)";
  return nullptr;
}

bool FsrUpscaler::Record(ID3D12GraphicsCommandList*, const Dispatch&, std::string* error_out) {
  if (error_out) *error_out = "the runtime is built without FSR (REX_FSR)";
  return false;
}

#endif  // REX_FSR

FsrUpscaler::FsrUpscaler(const Description& description, std::unique_ptr<Context> context)
    : description_(description), context_(std::move(context)) {}

FsrUpscaler::~FsrUpscaler() = default;

}  // namespace rex::graphics::d3d12
