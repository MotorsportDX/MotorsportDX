#include "screenshot.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "plan.h"

namespace fm4 {

std::optional<ScreenshotRequest> ParseScreenshot(int argc, char** argv) {
  int at = -1;
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--screenshot") at = i;
  if (at < 0) return std::nullopt;
  if (at + 4 >= argc)
    throw std::runtime_error("--screenshot <page> <width>x<height> <scale> <out.png> [--state <state>] [--settings <file>]");
  ScreenshotRequest request;
  request.page = argv[at + 1];
  if (sscanf_s(argv[at + 2], "%dx%d", &request.width, &request.height) != 2 || request.width < 320 ||
      request.height < 240 || request.width > 8192 || request.height > 8192)
    throw std::runtime_error(std::string("Screenshot size: ") + argv[at + 2]);
  request.scale = std::strtof(argv[at + 3], nullptr);
  if (request.scale < 0.5f || request.scale > 4) throw std::runtime_error(std::string("Screenshot scale: ") + argv[at + 3]);
  request.out = PathFromUtf8(argv[at + 4]);
  for (int i = at + 5; i < argc; ++i) {
    const std::string name = argv[i];
    if (name == "--touch") request.touch = true;
    else if (i + 1 >= argc) break;
    else if (name == "--state") request.state = argv[++i];
    else if (name == "--backdrop") request.backdrop = PathFromUtf8(argv[++i]);
    else if (name == "--settings") request.settings = PathFromUtf8(argv[++i]);
    else if (name == "--pad") {
      std::string list = argv[++i];
      for (size_t start = 0; start <= list.size();) {
        const size_t comma = list.find(',', start);
        request.pad.push_back(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
    }
  }
  return request;
}

bool ReadImage(const std::filesystem::path& path, int max_width, std::vector<uint8_t>& rgba, int& width, int& height) {
  const bool initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  bool ok = false;
  IWICImagingFactory* factory = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICBitmapScaler* scaler = nullptr;
  IWICFormatConverter* converter = nullptr;
  UINT w = 0, h = 0;
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                   &decoder)) &&
      SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0) {
    const UINT out_w = std::min<UINT>(w, static_cast<UINT>(max_width));
    const UINT out_h = std::max<UINT>(1, static_cast<UINT>(static_cast<uint64_t>(h) * out_w / w));
    if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(frame, out_w, out_h, WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom))) {
      rgba.resize(static_cast<size_t>(out_w) * out_h * 4);
      ok = SUCCEEDED(converter->CopyPixels(nullptr, out_w * 4, static_cast<UINT>(rgba.size()), rgba.data()));
      width = static_cast<int>(out_w);
      height = static_cast<int>(out_h);
    }
  }
  if (converter) converter->Release();
  if (scaler) scaler->Release();
  if (frame) frame->Release();
  if (decoder) decoder->Release();
  if (factory) factory->Release();
  if (initialized) CoUninitialize();
  return ok;
}

bool WritePng(const std::filesystem::path& path, int width, int height, const uint8_t* rgba, int pitch) {
  const bool initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  bool ok = false;
  IWICImagingFactory* factory = nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapEncoder* encoder = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  // BGRA, the PNG encoder's own 32-bit format.
  std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const uint8_t* in = rgba + static_cast<size_t>(y) * pitch + x * 4;
      uint8_t* out = bgra.data() + (static_cast<size_t>(y) * width + x) * 4;
      out[0] = in[2], out[1] = in[1], out[2] = in[0], out[3] = in[3];
    }
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->CreateStream(&stream)) &&
      SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
      SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
      SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
      SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
      SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format))) {
    ok = format == GUID_WICPixelFormat32bppBGRA &&
         SUCCEEDED(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data())) &&
         SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
  }
  if (frame) frame->Release();
  if (encoder) encoder->Release();
  if (stream) stream->Release();
  if (factory) factory->Release();
  if (initialized) CoUninitialize();
  return ok;
}

}  // namespace fm4
