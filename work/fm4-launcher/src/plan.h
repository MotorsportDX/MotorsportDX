// The launch plan of the GUI launcher: a port of launch_plan in outputs/FM4-build/pipeline.py,
// driven by the tables of launcher.json, which pipeline.py writes from the same constants.
// launcher_tests in pipeline.py compares the two on every build.
#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace fm4 {

using Json = nlohmann::ordered_json;

// An ordered string map with the update semantics of a Python dict: setting an existing key
// keeps its position.
class Pairs {
 public:
  void Set(const std::string& key, const std::string& value);
  void Erase(const std::string& key);
  void Clear() { items_.clear(); }
  bool Has(const std::string& key) const { return Find(key) != nullptr; }
  const std::string* Find(const std::string& key) const;
  std::string Get(const std::string& key, const std::string& fallback = {}) const;
  const std::vector<std::pair<std::string, std::string>>& Items() const { return items_; }
  bool Empty() const { return items_.empty(); }

 private:
  std::vector<std::pair<std::string, std::string>> items_;
};

// An option that pipeline.py rejects with RuntimeError or ValueError.
class PlanError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Options::resolution_scale of -ResolutionScale panel.
constexpr int kPanelScale = -1;

// The options of Build.ps1 -Action run that the launcher offers (no -ShaderDev).
struct Options {
  bool single_tile = false;
  bool fixed_vblank = false;
  bool rov = false;
  bool full_rate_mirror = false;
  bool shadow_workaround = false;
  bool fullscreen = false;
  bool supersample = false;
  bool info = false;
  bool ao = false;
  bool msaa = false;
  int native = -1;           // 1: -Native, 0: -Rtv, -1: the default (native with integrated GPUs only)
  int msaa_samples = -1;     // -MsaaSamples; -1: unset (2 with -Native)
  int frame_rate = 0;        // 0: the title's 60
  std::string vsync;         // -VSync auto, on or off; empty: the default (auto)
  int resolution_scale = 0;  // 0: from the display; kPanelScale: the screen's own (panel)
  std::string look;          // empty: the default look
  std::string quality;       // empty: the default quality
  std::string display;       // empty: the default display
  std::string texture_dump;     // empty: off
  std::string texture_replace;  // empty: off
  // Applied at Play, outside the launch plan (pipeline.py has no counterpart): the performance
  // trace into logs\perf-<profile>-<time>.jsonl, and cvars given as --name=value, applied last.
  bool perf_trace = false;
  std::string extra_cvars;
};

struct Monitor {
  int width = 1280;
  int height = 720;
  double dpi = 1.0;
  // The refresh rate and the variable refresh rate range of its EDID (pipeline.py
  // primary_display); 0 when unknown.
  int refresh = 0;
  int vrr_min = 0;
  int vrr_max = 0;
  // Integrated GPUs only (pipeline.py integrated_gpu): the automatic render scale is 1x.
  bool integrated_gpu = false;
};

struct Plan {
  Pairs cvars;
  Pairs env;
  std::string description;
};

// launcher.json.
struct LauncherData {
  Json json;
  static LauncherData Load(const std::filesystem::path& path);
  const Json& Look() const { return json.at("look"); }
};

// Python's float() of a string and the float formats the plan uses: repr() and format(x, 'g').
double PyFloat(const std::string& text);
std::string PyRepr(double value);
std::string PyG(double value);

// UTF-8 strings and paths.
std::filesystem::path PathFromUtf8(const std::string& text);
std::string Utf8(const std::filesystem::path& path);
// Path(text).resolve() of Python on Windows.
std::string ResolvePath(const std::string& text);

// The knobs of a look value ('aces', 'sharpen=0.5' or both, later items win).
Pairs LookKnobs(const LauncherData& data, const std::string& look);
bool TaaEnabled(const Pairs& knobs);
std::vector<std::string> LookDefines(const LauncherData& data, const Pairs& knobs);

// screen_scale of pipeline.py: the largest 16:9 picture that fits the screen, as a reduced
// fraction of 1280x720.
std::pair<int, int> ScreenScale(int width, int height);

// display_settings of pipeline.py: cvars and the description. scene_scale: the native path in
// one pass with 2x MSAA, which renders at the screen's own scale.
std::pair<Pairs, std::string> DisplaySettings(const LauncherData& data, const std::string& display,
                                              bool fullscreen, bool supersample,
                                              int resolution_scale, const Monitor& monitor,
                                              bool scene_scale = false);

// The render scale of a plan's cvars for the GUI: "3/2, 1920x1080" or "2x, 2560x1440".
std::string RenderScaleText(const Pairs& cvars);

Plan LaunchPlan(const LauncherData& data, const std::string& root, const std::string& disc1,
                const Options& options, const Monitor& monitor,
                const std::string& content_manifest);

}  // namespace fm4
