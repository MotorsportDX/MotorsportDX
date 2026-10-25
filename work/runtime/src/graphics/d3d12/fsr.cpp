/**
 * @file        graphics/d3d12/fsr.cpp
 * @brief       AMD FidelityFX FSR 3.1, built from the FidelityFX SDK source when REX_FSR is
 *              on (cmake/rex_fsr.cmake, docs/analysis/fsr-plan.md)
 */

// ffx_util.h calls std::popcount in C++20 and later without including <bit>.
#include <bit>
#include <cstddef>

#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <rex/logging.h>

namespace rex::graphics::d3d12 {

// Stage 0 links the upscaler and its DX12 backend and creates no context yet: this logs the
// linked version and the backend's scratch memory for one upscaler context.
void LogFsrLink() {
  const size_t scratch = ffxGetScratchMemorySizeDX12(FFX_FSR3UPSCALER_CONTEXT_COUNT);
  REXGPU_INFO("FSR {}.{}.{}: linked (FidelityFX SDK v1.1.4), {} bytes of backend scratch memory, "
              "Quality ratio {:.2f}",
              FFX_FSR3UPSCALER_VERSION_MAJOR, FFX_FSR3UPSCALER_VERSION_MINOR,
              FFX_FSR3UPSCALER_VERSION_PATCH, scratch,
              ffxFsr3UpscalerGetUpscaleRatioFromQualityMode(FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY));
}

}  // namespace rex::graphics::d3d12
