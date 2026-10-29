// The launcher's headless screenshot mode (MotorsportDX.exe --screenshot, docs/running.md#gui-launcher):
// one page rendered offscreen with WARP and written as a PNG, without a window, for reviews of
// the launcher's layout at a given size and DPI scale.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fm4 {

struct ScreenshotRequest {
  std::string page;   // display, look, controls, game-data, advanced, shaders or about
  int width = 0, height = 0;
  float scale = 1;
  std::filesystem::path out;
  std::string state;  // empty, first-run, error-banner or load-error
  std::filesystem::path settings;  // a launcher-settings.json to read instead of the defaults
  // Gamepad buttons pressed one after another before the capture (up, down, left, right, a, b, x,
  // y, lb, rb, start, back), to check the pad navigation.
  std::vector<std::string> pad;
};

// --screenshot <page> <width>x<height> <scale> <out.png> [--state <state>] [--settings <file>]
// [--pad <button>,<button>...],
// or nothing when argv has no --screenshot. Throws on a malformed request.
std::optional<ScreenshotRequest> ParseScreenshot(int argc, char** argv);

// Writes width x height RGBA8 pixels, pitch bytes a row, as a PNG through WIC.
bool WritePng(const std::filesystem::path& path, int width, int height, const uint8_t* rgba, int pitch);

}  // namespace fm4
