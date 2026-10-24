#pragma once
#include <rex/ui/presenter.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <renderdoc/renderdoc_app.h>
#endif

namespace rex { class Runtime; }
void FM4QueueGpuProbe(rex::Runtime*, std::filesystem::path, unsigned);

// A host Present may only copy an already rendered guest image. Opt-in bounded
// interval captures also include the guest command lists between host Presents.
inline void FM4CaptureGpuCommands(std::stop_token stop, const std::filesystem::path& directory) {
#if defined(_WIN32)
  const auto request = directory / "renderdoc-request.txt";
  if (!std::filesystem::exists(request)) return;
  unsigned duration_ms = 0;
  { std::ifstream input(request); input >> duration_ms; }
  std::filesystem::remove(request);
  std::ofstream result(directory / "renderdoc-result.txt");
  if (duration_ms < 50 || duration_ms > 1000) {
    result << "error=duration must be 50..1000 milliseconds\n";
    return;
  }
  auto module = GetModuleHandleW(L"renderdoc.dll");
  auto get_api = module ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI")) : nullptr;
  RENDERDOC_API_1_0_0* api = nullptr;
  if (!get_api || !get_api(eRENDERDOC_API_Version_1_0_0, reinterpret_cast<void**>(&api)) || !api) {
    result << "error=RenderDoc is not attached\n";
    return;
  }
  if (api->IsFrameCapturing()) {
    result << "error=another capture is active\n";
    return;
  }
  const auto before = api->GetNumCaptures();
  api->StartFrameCapture(nullptr, nullptr);
  if (!api->IsFrameCapturing()) {
    result << "error=capture did not start\n";
    return;
  }
  for (unsigned elapsed = 0; elapsed < duration_ms && !stop.stop_requested(); elapsed += 10)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const auto success = api->EndFrameCapture(nullptr, nullptr);
  result << "success=" << success << "\nduration_ms=" << duration_ms << '\n';
  if (success && api->GetNumCaptures() > before) {
    uint32_t size = 0;
    api->GetCapture(before, nullptr, &size, nullptr);
    std::string path(size + 1, '\0');
    if (api->GetCapture(before, path.data(), &size, nullptr))
      result << "path=" << path.c_str() << '\n';
  }
#endif
}

// Diagnostic GPU readback only. No desktop capture or input injection.
// FM4_CAPTURE_STATS=1: between the captures, the guest output is captured as
// often as it can be (tens a second) and the mean Rec. 709 luma of the frame and
// of six patches is appended to capture/stats.csv with the steady clock time, so
// a jump from one frame to the next shows (the patches of
// docs/analysis/cockpit-brightness.md, scaled from 1280x720).
inline void FM4CaptureStats(rex::ui::Presenter* presenter, std::ofstream& output) {
  rex::ui::RawImage frame;
  if (!presenter->CaptureGuestOutput(frame) || !frame.width || !frame.height ||
      frame.stride < size_t(frame.width) * 4 ||
      frame.data.size() < size_t(frame.height - 1) * frame.stride + size_t(frame.width) * 4) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return;
  }
  struct Patch { unsigned x, y, w, h; };
  // The frame, then dashboard top, gauges, wheel hub, right hand, road, sky.
  static const Patch kPatches[] = {{0, 0, 1280, 720},  {520, 490, 180, 30}, {530, 540, 210, 60},
                                   {580, 610, 100, 40}, {800, 540, 80, 60},  {450, 400, 150, 40},
                                   {420, 190, 100, 40}};
  const double sx = frame.width / 1280.0, sy = frame.height / 720.0;
  output << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
  for (const Patch& patch : kPatches) {
    const unsigned x0 = unsigned(patch.x * sx), y0 = unsigned(patch.y * sy);
    const unsigned x1 = std::min(frame.width, unsigned((patch.x + patch.w) * sx));
    const unsigned y1 = std::min(frame.height, unsigned((patch.y + patch.h) * sy));
    const unsigned step = patch.w >= 1280 ? 4 : 1;
    double sum = 0;
    size_t count = 0;
    for (unsigned y = y0; y < y1; y += step) {
      const uint8_t* row = frame.data.data() + size_t(y) * frame.stride;
      for (unsigned x = x0; x < x1; x += step) {
        const uint8_t* pixel = row + size_t(x) * 4;
        sum += 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2];
        ++count;
      }
    }
    output << ',' << (count ? sum / double(count) : 0.0);
  }
  output << std::endl;
}

inline void FM4CaptureFrames(std::stop_token stop, rex::ui::Presenter* presenter,
                             const std::filesystem::path& directory, rex::Runtime* runtime) {
  unsigned saved = 0;
  unsigned archive_limit = 450;
  if (const char* configured = std::getenv("FM4_CAPTURE_ARCHIVE_LIMIT")) {
    char* end = nullptr;
    const auto value = std::strtoul(configured, &end, 10);
    if (end != configured && *end == '\0' && value <= 450)
      archive_limit = static_cast<unsigned>(value);
  }
  const char* stats_setting = std::getenv("FM4_CAPTURE_STATS");
  const bool stats = stats_setting && *stats_setting && *stats_setting != '0';
  try {
    std::filesystem::create_directories(directory);
    std::ofstream stats_output;
    if (stats) {
      stats_output.open(directory / "stats.csv", std::ios::app);
      stats_output << "ms,frame,dashboard,gauges,wheel,hand,road,sky" << std::endl;
    }
    while (!stop.stop_requested()) {
      const auto next = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(saved ? 2000 : 500);
      while (std::chrono::steady_clock::now() < next) {
        if (stop.stop_requested()) return;
        if (stats) {
          FM4CaptureStats(presenter, stats_output);
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
      if (stop.stop_requested()) return;
      FM4CaptureGpuCommands(stop, directory);
      if (stop.stop_requested()) return;
      rex::ui::RawImage frame;
      if (!presenter->CaptureGuestOutput(frame)) continue;
      if (!frame.width || !frame.height || frame.width > 8192 || frame.height > 8192 ||
          frame.stride < size_t(frame.width) * 4 ||
          frame.data.size() < size_t(frame.height - 1) * frame.stride + size_t(frame.width) * 4)
        continue;
      char name[32];
      ++saved;
      // Keep the original archival bound, but retain one fresh image for long
      // user-driven sessions. A full archive must not end live observation.
      if (saved <= archive_limit)
        std::snprintf(name, sizeof(name), "frame-%03u.ppm", saved);
      else
        std::snprintf(name, sizeof(name), "live-latest.ppm");
      const auto target = directory / name;
      const auto temporary = directory / "capture-in-progress.tmp";
      std::ofstream output(temporary, std::ios::binary);
      output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
      for (uint32_t y = 0; y < frame.height; ++y)
        for (uint32_t x = 0; x < frame.width; ++x)
          output.write(reinterpret_cast<const char*>(frame.data.data() + size_t(y) * frame.stride + size_t(x) * 4), 3);
      output.close();
      if (!output) throw std::runtime_error("Cannot write GPU capture");
#if defined(_WIN32)
      bool published = false;
      for (unsigned retry = 0; retry < 25 && !stop.stop_requested(); ++retry) {
        if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
          published = true;
          break;
        }
        if (GetLastError() != ERROR_SHARING_VIOLATION && GetLastError() != ERROR_ACCESS_DENIED)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (stop.stop_requested()) return;
      if (!published)
        throw std::runtime_error("Cannot publish GPU capture");
#else
      std::filesystem::rename(temporary, target);
#endif
      // Capture times on the steady clock, to match per-frame GPU traces.
      std::ofstream(directory / "captures.txt", std::ios::app)
          << saved << ' '
          << std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
                 .count()
          << '\n';
      FM4QueueGpuProbe(runtime, directory, saved);
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FM4 GPU capture failed: %s\n", error.what());
  }
}
