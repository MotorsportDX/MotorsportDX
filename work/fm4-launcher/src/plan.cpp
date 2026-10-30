#include "plan.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <tuple>

namespace fm4 {

namespace fs = std::filesystem;

void Pairs::Set(const std::string& key, const std::string& value) {
  for (auto& item : items_) {
    if (item.first == key) {
      item.second = value;
      return;
    }
  }
  items_.emplace_back(key, value);
}

void Pairs::Erase(const std::string& key) {
  std::erase_if(items_, [&](const auto& item) { return item.first == key; });
}

const std::string* Pairs::Find(const std::string& key) const {
  for (const auto& item : items_)
    if (item.first == key) return &item.second;
  return nullptr;
}

std::string Pairs::Get(const std::string& key, const std::string& fallback) const {
  const std::string* value = Find(key);
  return value ? *value : fallback;
}

LauncherData LauncherData::Load(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Unable to read " + Utf8(path));
  LauncherData data;
  data.json = Json::parse(stream);
  return data;
}

namespace {

std::string Strip(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) return {};
  const auto last = text.find_last_not_of(" \t\r\n\f\v");
  return text.substr(first, last - first + 1);
}

std::vector<std::string> Split(const std::string& text, char separator) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    const size_t end = text.find(separator, start);
    parts.push_back(text.substr(start, end - start));
    if (end == std::string::npos) return parts;
    start = end + 1;
  }
}

std::string Join(const std::vector<std::string>& parts, const std::string& separator) {
  std::string text;
  for (size_t i = 0; i < parts.size(); ++i) text += (i ? separator : "") + parts[i];
  return text;
}

bool Contains(const Json& list, const std::string& name) {
  for (const auto& item : list)
    if (item.get<std::string>() == name) return true;
  return false;
}

std::string Names(const Json& object) {
  std::vector<std::string> names;
  for (const auto& [name, value] : object.items()) names.push_back(name);
  return Join(names, ", ");
}

// str(Path(root) / name) on Windows.
std::string JoinPath(const std::string& root, const std::string& name) {
  if (!root.empty() && (root.back() == '\\' || root.back() == '/')) return root + name;
  return root + "\\" + name;
}

// fsr_sharpness of pipeline.py: the fsr_sharpen knob, or the look's sharpening plus 0.4 times the
// upscale ratio above 1, at most 1.
double FsrSharpness(const Pairs& knobs, double upscale_ratio) {
  if (knobs.Find("fsr_sharpen")) {
    const std::string text = knobs.Get("fsr_sharpen");
    const double value = PyFloat(text);
    if (!(value >= 0 && value <= 1))
      throw PlanError("fsr_sharpen is a strength from 0 to 1, not '" + text + "'.");
    return value;
  }
  const double sharpen = PyFloat(knobs.Get("sharpen", "0"));
  if (upscale_ratio == 1.0) return sharpen;
  // Rounded to thousandths, as pipeline.py rounds it.
  return std::nearbyint(std::min(std::max(sharpen + 0.4 * (upscale_ratio - 1.0), 0.0), 1.0) *
                        1000.0) /
         1000.0;
}

double WindshieldStrength(const Pairs& knobs) {
  const std::string text = knobs.Get("windshield", "0");
  const double value = PyFloat(text);
  if (!(value >= 0 && value <= 2))
    throw PlanError("windshield is a strength from 0 (off) to 2, not '" + text + "'.");
  return value;
}

double HazeStrength(const Pairs& knobs) {
  const std::string text = knobs.Get("haze", "0");
  const double value = PyFloat(text);
  if (!(value >= 0 && value <= 1))
    throw PlanError("haze is a strength from 0 (off) to 1, not '" + text + "'.");
  return value;
}

std::string AaMethod(const Pairs& knobs) {
  const std::string value = knobs.Get("aa", "taa");
  if (value != "taa" && value != "fsr" && value != "taau")
    throw PlanError("aa is taa, fsr or taau, not '" + value + "'.");
  return value;
}

bool TaaScenePoint(const Pairs& knobs) {
  const std::string value = knobs.Get("taa_point", "0");
  if (value != "0" && value != "1")
    throw PlanError("taa_point is 0 (final) or 1 (scene_color), not '" + value + "'.");
  return value == "1";
}

std::string LookTrackSettings(const LauncherData& data, const Pairs& knobs) {
  std::vector<std::string> items;
  for (const auto& [name, attributes] : data.Look().at("track_knobs").items()) {
    const std::string* text = knobs.Find(name);
    if (!text) continue;
    const double value = PyFloat(*text);
    if (value < 0) throw PlanError(name + " is a multiplier, 0 or more, not '" + *text + "'.");
    if (value == 1) continue;
    for (const auto& attribute : attributes)
      items.push_back(attribute.get<std::string>() + "*=" + PyRepr(value));
  }
  return Join(items, ";");
}

Pairs SunCvars(const LauncherData& data, const std::string* strength) {
  Pairs cvars;
  if (!strength) return cvars;
  const double value = PyFloat(*strength);
  if (value < 0) throw PlanError("The sun strength must be 0 or more, not '" + *strength + "'.");
  cvars.Set("rtv_occlusion_awaited_scale", PyG(value));
  cvars.Set("rtv_occlusion_awaited_limit", std::to_string(data.json.at("sun_limit").get<int>()));
  return cvars;
}

Pairs AoCvars(const std::string& root, bool enabled) {
  Pairs cvars;
  if (enabled) cvars.Set("post_pass_path", JoinPath(root, "passes"));
  return cvars;
}

void Update(Pairs& target, const Pairs& source) {
  for (const auto& [key, value] : source.Items()) target.Set(key, value);
}

Pairs LookCvars(const LauncherData& data, const std::string& root, const std::string& look,
                double upscale_ratio) {
  const Json& tables = data.Look();
  Pairs knobs;
  for (const auto& [name, value] : LookKnobs(data, look).Items())
    if (!tables.at("track_knobs").contains(name)) knobs.Set(name, value);
  Pairs cvars = SunCvars(data, knobs.Find("sun"));
  bool mods = false;
  for (const auto& [name, value] : knobs.Items())
    if (!Contains(tables.at("runtime_knobs"), name) && !Contains(tables.at("pass_knobs"), name))
      mods = true;
  std::vector<std::string> defines = LookDefines(data, knobs, upscale_ratio);
  if (!mods) {
    std::erase_if(defines, [](const std::string& define) { return define.starts_with("FM4_LOOK_"); });
    if (!defines.empty()) cvars.Set("shader_mod_defines", Join(defines, ";"));
    Update(cvars, AoCvars(root, TaaEnabled(knobs) || WindshieldStrength(knobs) > 0));
    return cvars;
  }
  cvars.Set("shader_mod_path", JoinPath(root, "look"));
  cvars.Set("shader_mod_defines", Join(defines, ";"));
  Update(cvars, AoCvars(root, PyFloat(knobs.Get("ao", "0")) > 0 || TaaEnabled(knobs) ||
                                  WindshieldStrength(knobs) > 0 || HazeStrength(knobs) > 0));
  return cvars;
}

// quality_preset of pipeline.py: the tier of a -Quality value, the default for an empty one,
// an alias resolved.
std::string QualityPreset(const LauncherData& data, const std::string& quality) {
  const Json& tables = data.json.at("quality");
  std::string preset = quality.empty() ? tables.at("default").get<std::string>() : quality;
  if (tables.at("aliases").contains(preset)) preset = tables.at("aliases").at(preset).get<std::string>();
  if (!tables.at("presets").contains(preset))
    throw PlanError("Unknown quality '" + preset + "'; use " + Names(tables.at("presets")) + ", " +
                    Names(tables.at("aliases")) + ".");
  return preset;
}

std::string RaceSettings(const LauncherData& data, const std::string& quality, bool single_tile) {
  std::vector<std::string> settings;
  for (const auto& setting : data.json.at("quality").at("presets").at(QualityPreset(data, quality))) {
    const std::string text = setting.get<std::string>();
    if (!(single_tile && text.starts_with("TilingScenario="))) settings.push_back(text);
  }
  return Join(settings, ";");
}

// quality_cvars of pipeline.py: the tier's GTAO_QUALITY added to shader_mod_defines where the
// post passes run and it differs from passes.toml's; a GTAO_QUALITY already there wins.
Pairs QualityCvars(const LauncherData& data, const std::string& quality, const Pairs& cvars) {
  Pairs result;
  const int gtao = data.json.at("quality").at("gtao").at(QualityPreset(data, quality)).get<int>();
  std::vector<std::string> defines;
  for (const auto& part : Split(cvars.Get("shader_mod_defines"), ';'))
    if (!part.empty()) defines.push_back(part);
  if (!cvars.Has("post_pass_path") ||
      std::any_of(defines.begin(), defines.end(),
                  [](const std::string& d) { return d.starts_with("GTAO_QUALITY="); }) ||
      gtao == static_cast<int>(data.json.at("gtao").at("GTAO_QUALITY").get<double>()))
    return result;
  defines.push_back("GTAO_QUALITY=" + std::to_string(gtao));
  result.Set("shader_mod_defines", Join(defines, ";"));
  return result;
}

// quality_msaa_samples of pipeline.py: the tier's one-pass MSAA on the native path (ultra's 4x
// where the native plan is a whole scale at 16:9 without -Upscale, so that 4x renders the same
// picture as 2x; otherwise msaa_samples as given) and a note for the description.
std::pair<int, std::string> QualityMsaaSamples(const LauncherData& data, const Options& options,
                                               bool native, int msaa_samples,
                                               const std::string& display, const Monitor& monitor) {
  const std::string preset = QualityPreset(data, options.quality);
  const Json& tiers = data.json.at("quality").at("msaa");
  if (!tiers.contains(preset) || !native || msaa_samples >= 0 || options.single_tile ||
      options.msaa || options.rov)
    return {msaa_samples, ""};
  const int samples = tiers.at(preset).get<int>();
  const std::string name = data.json.at("quality").at("names").at(preset).get<std::string>();
  bool whole = false;
  try {
    if (UpscaleRatio(data, options.upscale) == 1.0) {
      const auto scene = DisplaySettings(data, display, options.fullscreen, options.supersample,
                                         options.resolution_scale, monitor, true).first.Items();
      const auto plain = DisplaySettings(data, display, options.fullscreen, options.supersample,
                                         options.resolution_scale, monitor, false).first.Items();
      whole = std::map<std::string, std::string>(scene.begin(), scene.end()) ==
              std::map<std::string, std::string>(plain.begin(), plain.end());
    }
  } catch (const PlanError&) {
    whole = false;
  }
  const std::string count = std::to_string(samples);
  if (whole) return {samples, "; " + name + ": " + count + "x MSAA"};
  return {msaa_samples,
          "; " + name + ": 2x MSAA (" + count + "x needs a whole scale at 16:9 without -Upscale)"};
}

std::string InfoOverlay(const LauncherData& data, const Pairs& cvars, const Pairs& env,
                        const std::string& look, const std::string& quality,
                        const std::string& display) {
  std::vector<std::string> lines;
  const int scale = std::stoi(cvars.Get("draw_resolution_scale_x", "1"));
  const std::string downscale =
      cvars.Get("present_area_downscale", "true") == "true" ? "area" : "bilinear";
  lines.push_back((display.empty() ? "Render " + std::to_string(scale) + "x (" +
                                         std::to_string(1280 * scale) + "x" +
                                         std::to_string(720 * scale) + ")"
                                   : display) +
                  ", " + downscale + " downscale");
  std::string tiling;
  if (env.Get("FM4_TILING_SCENARIO") == "2")
    tiling = std::string(", single tile") +
             (cvars.Get("swap_post_effect") == "fxaa" ? " with FXAA" : "");
  const Json& qualities = data.json.at("quality");
  const std::string preset = QualityPreset(data, quality);
  std::string msaa;
  const std::string samples = env.Get("FM4_ONE_PASS_MSAA");
  if (qualities.at("msaa").contains(preset) && !samples.empty()) {
    const int tier_samples = qualities.at("msaa").at(preset).get<int>();
    msaa = ", " + samples + "x MSAA" +
           (std::stoi(samples) < tier_samples
                ? " (" + std::to_string(tier_samples) + "x needs a whole scale at 16:9 without -Upscale)"
                : "");
  }
  lines.push_back("Quality: " + qualities.at("names").at(preset).get<std::string>() +
                  (preset == "medium" ? " (the game's own with soft particles)" : "") + tiling + msaa);
  const Json& tables = data.Look();
  std::map<std::string, std::string> knob_names, tonemaps;
  for (const auto& [name, define] : tables.at("knobs").items())
    knob_names[define.get<std::string>()] = name;
  for (const auto& [name, number] : tables.at("names").at("tonemap").items())
    tonemaps[PyRepr(number.get<double>())] = name;
  std::vector<std::pair<std::string, std::string>> defines;
  std::map<std::string, std::string> define_values;
  for (const auto& part : Split(cvars.Get("shader_mod_defines"), ';')) {
    if (part.empty()) continue;
    const auto equals = part.find('=');
    const std::string name = part.substr(0, equals);
    const std::string value = equals == std::string::npos ? "" : part.substr(equals + 1);
    defines.emplace_back(name, value);
    define_values[name] = value;
  }
  std::vector<std::string> settings;
  for (const auto& [name, value] : defines) {
    const auto knob = knob_names.find(name);
    std::string text = (knob != knob_names.end() ? knob->second : name) + " ";
    if (name == "FM4_LOOK_TONEMAP") {
      const auto tonemap = tonemaps.find(value);
      text += tonemap != tonemaps.end() ? tonemap->second : value;
    } else {
      text += PyG(PyFloat(value));
    }
    settings.push_back(text);
  }
  const std::string setting_text = Join(settings, ", ");
  if (!cvars.Has("shader_mod_path")) {
    lines.push_back("Look: original (no shader mods)");
  } else if (PathFromUtf8(cvars.Get("shader_mod_path")).filename() == "look") {
    lines.push_back("Look: " + look + (setting_text.empty() ? "" : " (" + setting_text + ")"));
  } else {
    lines.push_back("Shader mods: " + cvars.Get("shader_mod_path") +
                    (setting_text.empty() ? "" : " (" + setting_text + ")"));
  }
  if (!env.Get("FM4_TRACK_SETTINGS").empty()) {
    const Pairs knobs = LookKnobs(data, look);
    std::vector<std::string> track;
    for (const auto& [name, attributes] : tables.at("track_knobs").items()) {
      const std::string* value = knobs.Find(name);
      if (!value) continue;
      std::string label = name;
      std::replace(label.begin(), label.end(), '_', ' ');
      track.push_back(label + " x" + PyG(PyFloat(*value)));
    }
    lines.push_back("Track lighting: " +
                    (track.empty() ? env.Get("FM4_TRACK_SETTINGS") : Join(track, ", ")));
  }
  const std::string shadows = env.Get("FM4_SHADOW_WORKAROUND") == "1"
                                  ? "cockpit only (workaround)"
                                  : "shadow map on every camera";
  const std::map<std::string, std::string> anisotropies = {
      {"5", "16x"}, {"4", "8x"}, {"3", "4x"}, {"2", "2x"}};
  const auto anisotropy = anisotropies.find(cvars.Get("anisotropic_override"));
  lines.push_back("Shadows: " + shadows + "; textures: " +
                  (anisotropy != anisotropies.end() ? anisotropy->second : "game") +
                  " anisotropic");
  const std::string rate = env.Get("FM4_FRAME_RATE", "60");
  const std::string pacing =
      cvars.Get("vblank_vrr") == "true" ? "VRR pacing" : "fixed vblank grid";
  const std::string mirror = env.Get("FM4_HALF_RATE_MIRROR") == "1" ? "half rate" : "every frame";
  std::string path = cvars.Get("render_target_path_d3d12", "rtv");
  std::transform(path.begin(), path.end(), path.begin(), ::toupper);
  if (cvars.Get("host_render_thread") == "true") path += " (host render thread)";
  lines.push_back("Races: " + rate + " fps, " + pacing + "; mirror " + mirror + "; " + path +
                  " render targets");
  std::vector<std::string> full;
  for (const char* name : {"replay", "countdown"})
    if (env.Get("FM4_FULL_RATE").find(name) != std::string::npos) full.push_back(name);
  if (!full.empty()) {
    std::string text = Join(full, " and ");
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    text[0] = static_cast<char>(::toupper(text[0]));
    lines.push_back(text + ": " + rate + " fps (post-race and photo mode 30)");
  }
  const auto define = [&](const std::string& name) -> const std::string* {
    const auto found = define_values.find(name);
    return found == define_values.end() ? nullptr : &found->second;
  };
  if (cvars.Has("post_pass_path")) {
    const std::string* strength = define("FM4_LOOK_AO");
    const std::string applied = strength && !strength->empty()
                                    ? "strength " + PyG(PyFloat(*strength))
                                    : "pass only (no look applies it)";
    const Json& gtao = data.json.at("gtao");
    const double radius = define("GTAO_RADIUS") ? PyFloat(*define("GTAO_RADIUS"))
                                                : gtao.at("GTAO_RADIUS").get<double>();
    const double quality_value = define("GTAO_QUALITY") ? PyFloat(*define("GTAO_QUALITY"))
                                                        : gtao.at("GTAO_QUALITY").get<double>();
    const std::map<int, std::string> qualities = {{0, "low"}, {1, "medium"}, {2, "high"}, {3, "ultra"}};
    const auto gtao_quality = qualities.find(static_cast<int>(quality_value));
    lines.push_back("Ambient occlusion: GTAO, " + applied + ", radius " + PyG(radius) + " m, " +
                    (gtao_quality != qualities.end() ? gtao_quality->second : "high"));
  }
  if (define("FSR") && *define("FSR") == "1") {
    lines.push_back("Anti-aliasing: FSR 3.1 before the post chain (scene_color), sharpen " +
                    PyG(PyFloat(*define("FSR_SHARPNESS"))) +
                    ", scene jittered every frame (TAA there in a runtime without FSR)");
  } else if (define("TAA_UPSCALE") && *define("TAA_UPSCALE") == "1") {
    lines.push_back("Anti-aliasing: TAA as the upscaler before the post chain (scene_color), "
                    "sharpen " +
                    PyG(PyFloat(*define("TAA_SHARPEN"))) + ", scene jittered every frame");
  } else if (define("TAA") && *define("TAA") == "1" && define("TAA_POINT") &&
             *define("TAA_POINT") == "1") {
    lines.push_back(
        "Anti-aliasing: TAA before the post chain (scene_color), the look's sharpening, "
        "scene jittered while the camera moves, a still one accumulated");
  } else if (define("TAA") && *define("TAA") == "1") {
    lines.push_back("Anti-aliasing: TAA before the HUD, sharpen " +
                    PyG(PyFloat(*define("TAA_SHARPEN"))) +
                    ", scene jittered while the camera moves, a still one accumulated");
  }
  if (define("WINDSHIELD"))
    lines.push_back("Cockpit glass: strength " + PyG(PyFloat(*define("WINDSHIELD"))));
  if (cvars.Has("rtv_occlusion_awaited_scale")) {
    const double strength = PyFloat(cvars.Get("rtv_occlusion_awaited_scale"));
    lines.push_back("Sun glare: " + PyG(strength) + "x per pixel, soft limit " +
                    cvars.Get("rtv_occlusion_awaited_limit") + " pixels" +
                    (path == "ROV" ? " (RTV only)" : ""));
  }
  const std::string ab = cvars.Get("shader_override_ab_ms", "0");
  if (ab != "" && ab != "0")
    lines.push_back("Shader A/B: game shaders every other " + ab + " ms");
  return Join(lines, "|");
}

}  // namespace

double PyFloat(const std::string& text) {
  const std::string stripped = Strip(text);
  if (!stripped.empty()) {
    char* end = nullptr;
    const double value = std::strtod(stripped.c_str(), &end);
    const bool hex = stripped.find_first_of("xX") != std::string::npos &&
                     stripped.find("inf") == std::string::npos;
    if (end == stripped.c_str() + stripped.size() && !hex) return value;
  }
  throw PlanError("could not convert string to float: '" + text + "'");
}

std::string PyRepr(double value) {
  if (std::isnan(value)) return "nan";
  if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
  if (value == 0) return std::signbit(value) ? "-0.0" : "0.0";
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                    std::chars_format::scientific);
  std::string text(buffer, result.ptr);
  const bool negative = text[0] == '-';
  if (negative) text.erase(0, 1);
  const size_t e = text.find('e');
  const int exponent = std::stoi(text.substr(e + 1));
  std::string digits;
  for (char c : text.substr(0, e))
    if (c != '.') digits += c;
  std::string out;
  if (exponent >= -4 && exponent < 16) {
    if (exponent >= 0) {
      const size_t whole = static_cast<size_t>(exponent) + 1;
      if (digits.size() <= whole)
        out = digits + std::string(whole - digits.size(), '0') + ".0";
      else
        out = digits.substr(0, whole) + "." + digits.substr(whole);
    } else {
      out = "0." + std::string(static_cast<size_t>(-exponent - 1), '0') + digits;
    }
  } else {
    out = digits.substr(0, 1);
    if (digits.size() > 1) out += "." + digits.substr(1);
    const int magnitude = std::abs(exponent);
    out += std::string("e") + (exponent < 0 ? "-" : "+") + (magnitude < 10 ? "0" : "") +
           std::to_string(magnitude);
  }
  return (negative ? "-" : "") + out;
}

std::string PyG(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%g", value);
  return buffer;
}

fs::path PathFromUtf8(const std::string& text) {
  return fs::path(std::u8string(text.begin(), text.end()));
}

std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::string ResolvePath(const std::string& text) {
  std::error_code error;
  fs::path path = fs::weakly_canonical(PathFromUtf8(text), error);
  if (error) path = fs::absolute(PathFromUtf8(text), error);
  std::string resolved = Utf8(path);
  if (resolved.starts_with("\\\\?\\UNC\\"))
    resolved = "\\\\" + resolved.substr(8);
  else if (resolved.starts_with("\\\\?\\"))
    resolved = resolved.substr(4);
  if (resolved.size() > 3 && (resolved.back() == '\\' || resolved.back() == '/'))
    resolved.pop_back();
  return resolved;
}

Pairs LookKnobs(const LauncherData& data, const std::string& look) {
  const Json& tables = data.Look();
  const Json& presets = tables.at("presets");
  const Json& aliases = tables.at("aliases");
  Pairs knobs;
  for (const auto& part : Split(look, ',')) {
    std::string item = Strip(part);
    if (item.empty()) continue;
    const auto equals = item.find('=');
    if (equals == std::string::npos) {
      if (aliases.contains(item)) item = aliases.at(item).get<std::string>();
      if (!presets.contains(item)) {
        std::vector<std::string> names;
        for (const auto& [name, value] : presets.items()) names.push_back(name);
        for (const auto& [name, value] : aliases.items()) names.push_back(name);
        throw PlanError("Unknown look '" + item + "'; use " + Join(names, ", ") + " or knobs.");
      }
      if (item == "original") knobs.Clear();
      for (const auto& preset_part : Split(presets.at(item).get<std::string>(), ',')) {
        if (preset_part.empty()) continue;
        const auto preset_equals = preset_part.find('=');
        knobs.Set(preset_part.substr(0, preset_equals), preset_part.substr(preset_equals + 1));
      }
      continue;
    }
    knobs.Set(item.substr(0, equals), item.substr(equals + 1));
  }
  return knobs;
}

namespace {

// option_knobs of pipeline.py: the knobs of a -Pad or -Keyboard value.
Pairs OptionKnobs(const Json& table, const std::string& text, const std::string& option) {
  Pairs knobs;
  for (const auto& part : Split(text, ',')) {
    const std::string item = Strip(part);
    if (item.empty()) continue;
    const auto equals = item.find('=');
    const std::string name = Strip(item.substr(0, equals));
    if (equals == std::string::npos || !table.contains(name)) {
      std::vector<std::string> names;
      for (const auto& [knob, value] : table.items()) names.push_back(knob);
      throw PlanError("Unknown " + option + " knob '" + item + "'; use name=value with " +
                      Join(names, ", ") + ".");
    }
    knobs.Set(name, Strip(item.substr(equals + 1)));
  }
  return knobs;
}

// option_cvars of pipeline.py: over the launchers' defaults that differ from the runtime's
// (runtime_default), the runtime cvars of the knobs that differ from the runtime's defaults.
Pairs OptionCvars(const Json& table, const std::string& text, const std::string& option) {
  Pairs knobs;
  for (const auto& [name, knob] : table.items())
    if (knob.contains("runtime_default")) knobs.Set(name, knob.at("default").get<std::string>());
  for (const auto& [name, value] : OptionKnobs(table, text, option).Items()) knobs.Set(name, value);
  Pairs cvars;
  for (const auto& [name, value] : knobs.Items()) {
    const Json& knob = table.at(name);
    const std::string cvar = knob.at("cvar").get<std::string>();
    const std::string fallback =
        knob.at(knob.contains("runtime_default") ? "runtime_default" : "default").get<std::string>();
    std::string text;
    if (knob.contains("choices")) {
      std::vector<std::string> choices;
      for (const auto& choice : knob.at("choices")) choices.push_back(choice.get<std::string>());
      if (std::find(choices.begin(), choices.end(), value) == choices.end())
        throw PlanError(option + " " + name + " is one of " + Join(choices, ", ") + ", not '" +
                        value + "'.");
      if (value == fallback) continue;
      const bool boolean = choices == std::vector<std::string>{"0", "1"};
      text = boolean ? (value == "1" ? "true" : "false") : value;
    } else {
      const double low = knob.at("min").get<double>(), high = knob.at("max").get<double>();
      const bool integer = knob.contains("integer") && knob.at("integer").get<bool>();
      double number = std::nan("");
      try {
        number = PyFloat(value);
      } catch (const PlanError&) {
      }
      if (!(low <= number && number <= high) || (integer && std::fmod(number, 1.0) != 0.0))
        throw PlanError(option + " " + name + " is " + (integer ? "a whole" : "a") +
                        " number from " + PyG(low) + " to " + PyG(high) + ", not '" + value +
                        "'.");
      if (number == PyFloat(fallback)) continue;
      text = integer ? std::to_string(static_cast<long long>(number)) : PyRepr(number);
    }
    cvars.Set(cvar, text);
  }
  return cvars;
}

}  // namespace

Pairs PadKnobs(const LauncherData& data, const std::string& pad) {
  return OptionKnobs(data.json.at("pad").at("knobs"), pad, "-Pad");
}

Pairs PadCvars(const LauncherData& data, const std::string& pad) {
  return OptionCvars(data.json.at("pad").at("knobs"), pad, "-Pad");
}

Pairs KeyboardKnobs(const LauncherData& data, const std::string& keyboard) {
  return OptionKnobs(data.json.at("keyboard").at("knobs"), keyboard, "-Keyboard");
}

Pairs KeyboardCvars(const LauncherData& data, const std::string& keyboard) {
  return OptionCvars(data.json.at("keyboard").at("knobs"), keyboard, "-Keyboard");
}

Pairs WheelKnobs(const LauncherData& data, const std::string& wheel) {
  return OptionKnobs(data.json.at("wheel").at("knobs"), wheel, "-Wheel");
}

Pairs WheelCvars(const LauncherData& data, const std::string& wheel) {
  return OptionCvars(data.json.at("wheel").at("knobs"), wheel, "-Wheel");
}

bool TaaEnabled(const Pairs& knobs) {
  const std::string text = knobs.Get("taa", "0");
  const double value = PyFloat(text);
  if (value < 0) throw PlanError("taa is 0 (off) or 1 (on), not '" + text + "'.");
  return value > 0;
}

std::vector<std::string> LookDefines(const LauncherData& data, const Pairs& knobs,
                                     double upscale_ratio) {
  const Json& tables = data.Look();
  const Json& look_knobs = tables.at("knobs");
  std::vector<std::string> defines;
  for (const auto& [name, value] : knobs.Items()) {
    if (Contains(tables.at("runtime_knobs"), name) || tables.at("track_knobs").contains(name) ||
        Contains(tables.at("pass_knobs"), name))
      continue;
    if (!look_knobs.contains(name)) {
      std::vector<std::string> names;
      for (const auto& [knob, define] : look_knobs.items()) names.push_back(knob);
      for (const auto& knob : tables.at("runtime_knobs")) names.push_back(knob.get<std::string>());
      for (const auto& [knob, attributes] : tables.at("track_knobs").items()) names.push_back(knob);
      for (const auto& knob : tables.at("pass_knobs")) names.push_back(knob.get<std::string>());
      throw PlanError("Unknown look knob '" + name + "'; use " + Join(names, ", ") + ".");
    }
    double number = 0;
    if (tables.at("names").contains(name)) {
      const Json& names = tables.at("names").at(name);
      if (!names.contains(value))
        throw PlanError("Unknown " + name + " '" + value + "'; use " + Names(names) + ".");
      number = names.at(value).get<double>();
    } else {
      number = PyFloat(value);
    }
    defines.push_back(look_knobs.at(name).get<std::string>() + "=" + PyRepr(number));
  }
  const bool scene_point = TaaScenePoint(knobs);
  const std::string aa = AaMethod(knobs);
  const bool fsr = aa == "fsr";
  if (aa != "taa" && !TaaEnabled(knobs))
    throw PlanError("aa=" + aa + " needs taa above 0 (the look turns temporal anti-aliasing on).");
  if (aa == "taau" && upscale_ratio != 1.0) {
    // TAA as the upscaler at the scene_color point, jittered in every frame, with FSR's
    // sharpening for a like-for-like comparison.
    const double sharpen = FsrSharpness(knobs, upscale_ratio);
    std::erase_if(defines, [](const std::string& d) { return d.starts_with("FM4_LOOK_SHARPEN="); });
    defines.insert(defines.end(),
                   {"FM4_LOOK_SHARPEN=0.0", "TAA=1", "TAA_SHARPEN=" + PyRepr(sharpen),
                    "TAA_POINT=1", "TAA_UPSCALE=1", "TAA_JITTER_MOTION=0.0",
                    "TAA_JITTER_RATE=0.0"});
  } else if (fsr) {
    // FSR at the scene_color point, the look's sharpening as its RCAS.
    const double sharpen = FsrSharpness(knobs, upscale_ratio);
    std::erase_if(defines, [](const std::string& d) { return d.starts_with("FM4_LOOK_SHARPEN="); });
    defines.insert(defines.end(), {"FM4_LOOK_SHARPEN=0.0", "TAA=1", "TAA_SHARPEN=0.0", "TAA_POINT=1",
                                   "FSR=1", "FSR_SHARPNESS=" + PyRepr(sharpen)});
  } else if (TaaEnabled(knobs) && (scene_point || aa == "taau")) {
    defines.insert(defines.end(), {"TAA=1", "TAA_SHARPEN=0.0", "TAA_POINT=1"});
  } else if (TaaEnabled(knobs)) {
    const double sharpen = PyFloat(knobs.Get("sharpen", "0"));
    std::erase_if(defines, [](const std::string& d) { return d.starts_with("FM4_LOOK_SHARPEN="); });
    defines.insert(defines.end(),
                   {"FM4_LOOK_SHARPEN=0.0", "TAA=1", "TAA_SHARPEN=" + PyRepr(sharpen)});
  }
  if (WindshieldStrength(knobs) > 0)
    defines.push_back("WINDSHIELD=" + PyRepr(WindshieldStrength(knobs)));
  // The haze pass's strength; the mods' FM4_LOOK_HAZE mixes the scene by its amount.
  if (HazeStrength(knobs) > 0) defines.push_back("HAZE=" + PyRepr(HazeStrength(knobs)));
  return defines;
}

std::pair<int, int> ScreenScale(int width, int height) {
  long long numerator = width, denominator = 1280;
  if (static_cast<long long>(width) * 720 > static_cast<long long>(height) * 1280) {
    numerator = height;
    denominator = 720;
  }
  if (denominator > 720) {
    numerator = numerator * 720 / denominator;
    denominator = 720;
  }
  if (numerator < denominator) numerator = denominator = 1;
  const long long divisor = std::gcd(numerator, denominator);
  return {static_cast<int>(numerator / divisor), static_cast<int>(denominator / divisor)};
}

LogicalView LogicalViewOf(int width, int height) {
  LogicalView view;
  long long numerator, denominator;
  const long long w = width, h = height;
  if (w * 9 >= h * 16) {
    view.width = static_cast<int>(
        std::min<long long>(3840, std::max<long long>(1280, (1440 * w + 8 * h) / (16 * h) * 8)));
    view.height = 720;
    numerator = h;
    denominator = 720;
  } else {
    view.width = 1280;
    view.height = static_cast<int>(
        std::min<long long>(1024, std::max<long long>(720, (2560 * h + 8 * w) / (16 * w) * 8)));
    numerator = w;
    denominator = 1280;
  }
  if (denominator > 720) {
    numerator = numerator * 720 / denominator;
    denominator = 720;
  }
  if (numerator < denominator) numerator = denominator = 1;
  const long long divisor = std::gcd(numerator, denominator);
  view.numerator = static_cast<int>(numerator / divisor);
  view.denominator = static_cast<int>(denominator / divisor);
  return view;
}

std::pair<Pairs, std::string> DisplaySettings(const LauncherData& data, const std::string& display,
                                              bool fullscreen, bool supersample,
                                              int resolution_scale, const Monitor& monitor,
                                              bool scene_scale, bool upscale) {
  int width = monitor.width, height = monitor.height;
  if (display == "native") {
    fullscreen = true;
  } else {
    const Json& displays = data.json.at("displays");
    if (!displays.contains(display))
      throw PlanError("Unknown display '" + display + "'; use native, " + Names(displays) + ".");
    width = displays.at(display).at(0).get<int>();
    height = displays.at(display).at(1).get<int>();
    fullscreen = fullscreen || width >= monitor.width || height >= monitor.height;
  }
  const bool panel = resolution_scale == kPanelScale;
  if (panel && !scene_scale)
    throw PlanError(
        "-ResolutionScale panel needs the native path in one pass with 2x MSAA (-Native without "
        "-SingleTile, -Msaa or -MsaaSamples 0 or 4).");
  const int max_scale = data.json.at("max_resolution_scale").get<int>();
  const auto own = ScreenScale(width, height);
  int numerator, denominator;
  int logical_width = 1280, logical_height = 720;
  // -Upscale renders the scene below this scale, so integrated GPUs take the screen's own too.
  const bool integrated = monitor.integrated_gpu && !(scene_scale && upscale);
  if (panel || (scene_scale && !resolution_scale && !integrated && !supersample)) {
    const LogicalView view = LogicalViewOf(width, height);
    logical_width = view.width;
    logical_height = view.height;
    numerator = view.numerator;
    denominator = view.denominator;
  } else {
    int scale;
    if (scene_scale && supersample && !resolution_scale && !integrated) {
      scale = std::min(max_scale, own.first / own.second + 1);
    } else {
      const int cover =
          integrated
              ? 1
              : static_cast<int>(std::ceil(std::min(width / 1280.0, height / 720.0) - 1e-6));
      scale = resolution_scale ? resolution_scale : std::min(max_scale, cover + (supersample ? 1 : 0));
    }
    numerator = scale;
    denominator = 1;
  }
  const int whole = numerator / denominator;
  Pairs cvars;
  cvars.Set("fullscreen", fullscreen ? "true" : "false");
  if (!fullscreen) {
    // Python's round(): halves to even, as nearbyint in the default rounding mode.
    cvars.Set("window_width", std::to_string(static_cast<long long>(std::nearbyint(width / monitor.dpi))));
    cvars.Set("window_height", std::to_string(static_cast<long long>(std::nearbyint(height / monitor.dpi))));
  }
  if (whole > 1) {
    cvars.Set("draw_resolution_scale_x", std::to_string(whole));
    cvars.Set("draw_resolution_scale_y", std::to_string(whole));
  }
  const bool logical = logical_width != 1280 || logical_height != 720;
  if (denominator > 1 || logical) {
    // The scene at the fraction (or, for a logical size, at the whole scale through the scene
    // draws' path), the other surfaces at its whole part; bit 32: the resolves write the
    // scene's textures at that scale (d3d-hle-n5.md 18.15).
    cvars.Set("native_draw_scale", denominator > 1 ? std::to_string(numerator) + "/" +
                                                         std::to_string(denominator)
                                                   : std::to_string(numerator));
    cvars.Set("native_rt_surfaces", "58");
    if (denominator == 1) cvars.Set("native_draw_scale_variant", "true");
  }
  if (logical_width != 1280) cvars.Set("native_logical_width", std::to_string(logical_width));
  if (logical_height != 720) cvars.Set("native_logical_height", std::to_string(logical_height));
  const int render_width =
      static_cast<int>((static_cast<long long>(logical_width) * numerator + denominator - 1) /
                       denominator);
  const int render_height =
      static_cast<int>((static_cast<long long>(logical_height) * numerator + denominator - 1) /
                       denominator);
  const std::string scale_text = denominator > 1
                                     ? std::to_string(numerator) + "/" + std::to_string(denominator)
                                     : std::to_string(numerator) + "x";
  const std::string screen =
      fullscreen ? "fullscreen " + std::to_string(monitor.width) + "x" + std::to_string(monitor.height)
                 : "window " + std::to_string(width) + "x" + std::to_string(height);
  const std::string gpu_note = integrated && !resolution_scale ? " for an integrated GPU" : "";
  const long long shown_width = fullscreen ? monitor.width : width;
  const long long shown_height = fullscreen ? monitor.height : height;
  std::string view;
  if (logical)
    view = ", logical " + std::to_string(logical_width) + "x" + std::to_string(logical_height) +
           (logical_width != 1280 ? " Hor+" : " Vert+");
  // What the presenter adds around the picture: the screen's aspect against the render's.
  std::string aspect_note;
  if (scene_scale && shown_width * render_height != shown_height * render_width) {
    const char* bars =
        shown_width * render_height < shown_height * render_width ? "letterboxed" : "pillarboxed";
    aspect_note = logical ? std::string(", ") + bars : std::string(", 16:9 ") + bars;
  }
  return {cvars, "Display " + display + ": " + screen + ", render " + scale_text + " (" +
                     std::to_string(render_width) + "x" + std::to_string(render_height) + ")" +
                     view + gpu_note + aspect_note};
}

// A scale cvar such as "3/2" or "2" as numerator and denominator.
std::pair<int, int> ParseScale(const std::string& text) {
  const size_t slash = text.find('/');
  if (slash == std::string::npos) return {std::stoi(text), 1};
  return {std::stoi(text.substr(0, slash)), std::stoi(text.substr(slash + 1))};
}

double UpscaleRatio(const LauncherData& data, const std::string& option) {
  const Json& upscale = data.json.at("upscale");
  const std::string mode = option.empty() ? upscale.at("default").get<std::string>() : option;
  if (upscale.at("modes").contains(mode)) return upscale.at("modes").at(mode).get<double>();
  char* end = nullptr;
  const double ratio = mode.empty() ? 0.0 : std::strtod(mode.c_str(), &end);
  if (mode.empty() || *end || !(ratio >= 1.0 && ratio <= 3.0))
    throw PlanError("Unknown -Upscale '" + mode +
                    "'; use native, quality, balanced, performance or a ratio from 1 to 3.");
  return ratio;
}

std::pair<Pairs, std::string> UpscaleSettings(const LauncherData& data, const std::string& option,
                                              const Pairs& cvars, const std::string& aa,
                                              bool scene_scale) {
  const std::string upscale =
      option.empty() ? data.json.at("upscale").at("default").get<std::string>() : option;
  const double ratio = UpscaleRatio(data, upscale);
  if (ratio == 1.0) return {};
  if (!scene_scale)
    throw PlanError(
        "-Upscale needs the native path in one pass with 2x MSAA (-Native without -SingleTile, "
        "-Msaa or -MsaaSamples 0 or 4).");
  if (aa != "fsr" && aa != "taau")
    throw PlanError("-Upscale " + upscale +
                    " needs aa=fsr or aa=taau in -Look: TAA does not upscale.");
  const auto [numerator, denominator] =
      ParseScale(cvars.Get("native_draw_scale", cvars.Get("draw_resolution_scale_x", "1")));
  const int output_lines = (720 * numerator + denominator / 2) / denominator;
  int lines = static_cast<int>(output_lines / ratio + 0.5);
  const std::string clamped = lines < 720 ? " (clamped to 720p)" : "";
  lines = std::max(720, lines);
  if (lines >= output_lines)
    return {Pairs(), "; upscale " + upscale + ": none at " + std::to_string(output_lines) + "p" +
                         clamped};
  const int divisor = std::gcd(lines, 720);
  const int input_numerator = lines / divisor, input_denominator = 720 / divisor;
  const std::string whole = std::to_string(input_numerator / input_denominator);
  Pairs result;
  result.Set("native_draw_scale", denominator > 1 ? std::to_string(numerator) + "/" +
                                                        std::to_string(denominator)
                                                  : std::to_string(numerator));
  result.Set("native_scene_input_scale",
             input_denominator > 1
                 ? std::to_string(input_numerator) + "/" + std::to_string(input_denominator)
                 : std::to_string(input_numerator));
  result.Set("draw_resolution_scale_x", whole);
  result.Set("draw_resolution_scale_y", whole);
  result.Set("native_rt_surfaces", "58");
  return {result, std::string(aa == "fsr" ? "; FSR " : "; TAAU ") + upscale + " " + std::to_string(lines) + "p -> " +
                      std::to_string(output_lines) + "p" + clamped};
}

double SceneUpscaleRatio(const Pairs& cvars) {
  const std::string input = cvars.Get("native_scene_input_scale");
  const std::string output = cvars.Get("native_draw_scale");
  if (input.empty() || output.empty()) return 1.0;
  const auto lines = [](const std::string& text) {
    const auto [numerator, denominator] = ParseScale(text);
    return (720 * numerator + denominator / 2) / denominator;
  };
  return double(lines(output)) / double(lines(input));
}

std::string UpscaleText(const Pairs& cvars) {
  const std::string input = cvars.Get("native_scene_input_scale");
  if (input.empty()) return "";
  const auto [input_numerator, input_denominator] = ParseScale(input);
  const auto [numerator, denominator] = ParseScale(cvars.Get("native_draw_scale", "1"));
  return std::to_string((720 * input_numerator + input_denominator / 2) / input_denominator) +
         "p \u2192 " + std::to_string((720 * numerator + denominator / 2) / denominator) + "p";
}

std::string RenderScaleText(const Pairs& cvars) {
  int numerator = std::stoi(cvars.Get("draw_resolution_scale_x", "1")), denominator = 1;
  // The scene's scale, which -Upscale gives also when it is whole.
  const std::string fraction = cvars.Get("native_draw_scale");
  if (!fraction.empty()) std::tie(numerator, denominator) = ParseScale(fraction);
  const int width = std::stoi(cvars.Get("native_logical_width", "1280"));
  const int height = std::stoi(cvars.Get("native_logical_height", "720"));
  std::string text = (denominator > 1 ? std::to_string(numerator) + "/" + std::to_string(denominator)
                                      : std::to_string(numerator) + "x") +
                     ", " + std::to_string((width * numerator + denominator - 1) / denominator) +
                     "x" + std::to_string((height * numerator + denominator - 1) / denominator);
  if (width != 1280) text += ", Hor+";
  if (height != 720) text += ", Vert+";
  return text;
}

// pipeline.py present_settings: the cvars of -VSync, the race frame rate it leaves, and its
// description.
struct PresentSettings {
  Pairs cvars;
  int frame_rate = 0;
  std::string description;
};

PresentSettings PresentSettingsOf(const LauncherData& data, const std::string& option, int frame_rate,
                                  const Monitor& monitor) {
  const Json& vsync = data.json.at("vsync");
  const std::string mode = option.empty() ? vsync.at("default").get<std::string>() : option;
  bool known = false;
  std::string names;
  for (const auto& name : vsync.at("modes")) {
    known = known || name.get<std::string>() == mode;
    names += (names.empty() ? "" : ", ") + name.get<std::string>();
  }
  if (!known) throw PlanError("Unknown VSync mode '" + mode + "'; use " + names + ".");
  PresentSettings settings;
  settings.frame_rate = frame_rate;
  if (mode == "off") {
    settings.description = "VSync off";
    return settings;
  }
  const bool vrr = monitor.refresh > 3 && monitor.vrr_min < monitor.vrr_max &&
                   monitor.vrr_max >= monitor.refresh - 1;
  if (mode == "on" || !vrr) {
    settings.cvars.Set("present_vsync", "true");
    settings.description = std::string("VSync on") + (mode == "on" ? "" : " (no VRR range)");
    return settings;
  }
  const int cap = monitor.refresh - 3;
  settings.description = "VSync auto: VRR " + std::to_string(monitor.vrr_min) + "-" +
                         std::to_string(monitor.vrr_max) + " Hz, presents at most " +
                         std::to_string(monitor.refresh) + " a second";
  if ((frame_rate ? frame_rate : 60) > cap) {
    settings.frame_rate = cap;
    settings.description += ", races at " + std::to_string(cap) + " fps";
  }
  settings.cvars.Set("present_min_interval_us", std::to_string(1000000 / monitor.refresh));
  return settings;
}

Plan LaunchPlan(const LauncherData& data, const std::string& root, const std::string& disc1,
                const Options& options, const Monitor& monitor,
                const std::string& content_manifest) {
  Plan plan;
  Pairs& cvars = plan.cvars;
  Pairs& env = plan.env;
  for (const auto& [name, value] : data.json.at("env").items()) env.Set(name, value.get<std::string>());
  if (options.full_rate_mirror) env.Set("FM4_HALF_RATE_MIRROR", "0");
  if (options.shadow_workaround) env.Set("FM4_SHADOW_WORKAROUND", "1");
  const std::string look =
      options.look.empty() ? data.Look().at("default").get<std::string>() : options.look;
  const Pairs knobs = LookKnobs(data, look);
  // The quality tier (an alias resolved), and ultra's 4x MSAA where it keeps the picture.
  const std::string quality = QualityPreset(data, options.quality);
  const std::string display = options.display.empty()
                                  ? data.json.at("default_display").get<std::string>()
                                  : options.display;
  // One pass with MSAA on the native path: -Native runs it with 2x unless -MsaaSamples 4 (4x),
  // -MsaaSamples 0 (one tile without MSAA), -SingleTile or -Msaa says otherwise.
  if (options.msaa_samples != -1 && options.msaa_samples != 0 && options.msaa_samples != 2 &&
      options.msaa_samples != 4)
    throw PlanError("-MsaaSamples takes 0, 2 or 4.");
  // pipeline.py launch_plan: the native path by default, unless -Rov or -Msaa.
  const bool native = options.native < 0 ? !options.rov && !options.msaa : options.native == 1;
  if (options.msaa_samples >= 0 && options.rov)
    throw PlanError("-MsaaSamples is the native path's one-pass MSAA; -Rov cannot run it.");
  if (options.msaa_samples >= 0 && !native)
    throw PlanError("-MsaaSamples is the native path's one-pass MSAA; -Rtv and -Msaa run RTV.");
  int msaa_samples = options.msaa_samples;
  std::string quality_note;
  std::tie(msaa_samples, quality_note) =
      QualityMsaaSamples(data, options, native, msaa_samples, display, monitor);
  if (native && msaa_samples < 0 && !options.single_tile && !options.msaa && !options.rov)
    msaa_samples = 2;
  const bool one_pass = msaa_samples > 0;
  const bool single_tile =
      !one_pass && (options.single_tile || msaa_samples == 0 ||
                    (TaaEnabled(knobs) && !options.rov && !options.msaa));
  const std::string settings = RaceSettings(data, quality, single_tile || one_pass);
  if (!settings.empty()) env.Set("FM4_RACE_SETTINGS", settings);
  for (const auto& [name, value] : data.json.at("cvars").items()) {
    std::string text = value.get<std::string>();
    if (text.starts_with("<ROOT>")) text = root + text.substr(6);
    if (text == "<DISC1>") text = disc1;
    cvars.Set(name, text);
  }
  if (single_tile) {
    env.Set("FM4_TILING_SCENARIO", "2");
    if (!TaaEnabled(knobs)) cvars.Set("swap_post_effect", "fxaa");
  }
  if (one_pass) {
    // 2x: the title's 2x scenario (1); 4x: scenario 3, which the title patch makes one 4x tile.
    env.Set("FM4_TILING_SCENARIO", msaa_samples == 2 ? "1" : "3");
    env.Set("FM4_ONE_PASS_MSAA", std::to_string(msaa_samples));
  }
  if (options.fixed_vblank) cvars.Set("vblank_vrr", "false");
  if (options.rov) {
    cvars.Erase("edram_single_rt_no_wrap");
    cvars.Set("render_target_path_d3d12", "rov");
    cvars.Set("rov_occlusion_async", "all");
    // The uncorrected reference keeps the title's ring index wait and frame loop
    // as they are (docs/analysis/d3d-hle-n4.md 17.16,
    // docs/analysis/career-season-soak.md 12).
    cvars.Set("guest_ring_wait_sleep", "false");
    cvars.Set("fm4_render_thread_idle_wait", "false");
  }
  if (native) {
    if (options.rov)
      throw PlanError(
          "-Native runs on the host render target path (RTV); it cannot be combined with -Rov.");
    // The native path covers one tile and one pass; the tiles of the quality (MSAA tiles) stay
    // on the PM4 RTV path.
    if (single_tile || one_pass)
      for (const auto& [name, value] : data.json.at("native_cvars").items())
        cvars.Set(name, value.get<std::string>());
    if (one_pass) {
      // Bit 8: the host render targets sized to the tiles the title uses; bit
      // 16: every 1280-wide race surface private.
      cvars.Set("native_one_pass_msaa", std::to_string(msaa_samples));
      cvars.Set("native_rt_surfaces", "26");
    }
  }
  auto [screen, description] = DisplaySettings(data, display, options.fullscreen,
                                               options.supersample, options.resolution_scale,
                                               monitor, native && msaa_samples == 2,
                                               UpscaleRatio(data, options.upscale) > 1.0);
  Update(cvars, screen);
  const auto [upscaled, upscale_description] =
      UpscaleSettings(data, options.upscale, cvars, AaMethod(knobs), native && msaa_samples == 2);
  Update(cvars, upscaled);
  description += upscale_description + quality_note;
  if (native && msaa_samples == 2 && cvars.Get("native_draw_scale").empty() &&
      std::stoi(cvars.Get("draw_resolution_scale_x", "1")) > 1) {
    // A whole scale of 2 or more at 16:9 through the scene draws' path too, with bit 32: the
    // resolves write the scene's textures from the render target, 0.15 ms less GPU a race frame
    // at 2x on a discrete GPU; 1x keeps its loads (d3d-hle-n6.md 19.7.6).
    cvars.Set("native_draw_scale", cvars.Get("draw_resolution_scale_x"));
    cvars.Set("native_draw_scale_variant", "true");
    cvars.Set("native_rt_surfaces", "58");
  }
  if (!options.entry0.empty() && options.entry0 != "s" && options.entry0 != "k")
    throw PlanError("Unknown -Entry0 '" + options.entry0 + "'; use s, k.");
  if (native && msaa_samples == 2 && options.entry0 != "k") {
    // The title's tiling entry 0 (the boot, the menus, photo mode) at the scene's scale: one
    // pass with 4x MSAA in a private 4x pair, photo mode in its three tiles there
    // (d3d-hle-n5.md 18.26). 'k' keeps it at the whole scale, scaled to the screen.
    env.Set("FM4_ONE_PASS_ENTRY0", "4");
    cvars.Set("native_rt_surfaces", std::to_string(std::stoi(cvars.Get("native_rt_surfaces")) | 64));
  }
  const PresentSettings present = PresentSettingsOf(data, options.vsync, options.frame_rate, monitor);
  Update(cvars, present.cvars);
  plan.description = description + "; " + present.description;
  if (present.frame_rate) {
    cvars.Set("video_mode_refresh_rate", std::to_string(present.frame_rate));
    env.Set("FM4_FRAME_RATE", std::to_string(present.frame_rate));
  }
  Update(cvars, LookCvars(data, root, look, SceneUpscaleRatio(cvars)));
  const std::string track = LookTrackSettings(data, knobs);
  if (!track.empty()) env.Set("FM4_TRACK_SETTINGS", track);
  Update(cvars, AoCvars(root, options.ao));
  Update(cvars, QualityCvars(data, quality, cvars));
  if (!options.texture_dump.empty())
    cvars.Set("texture_dump_path", ResolvePath(options.texture_dump));
  if (!options.texture_replace.empty()) {
    if (!fs::is_directory(PathFromUtf8(options.texture_replace)))
      throw PlanError("-TextureReplace is not a directory: " + options.texture_replace);
    cvars.Set("texture_replace_path", ResolvePath(options.texture_replace));
  }
  if (!content_manifest.empty()) env.Set("FM4_CONTENT_MANIFEST", content_manifest);
  // The physical controller's options, the keyboard's and the racing wheel's.
  Update(cvars, PadCvars(data, options.pad));
  Update(cvars, KeyboardCvars(data, options.keyboard));
  Update(cvars, WheelCvars(data, options.wheel));
  env.Set("FM4_INFO", InfoOverlay(data, cvars, env, look, quality, plan.description));
  if (options.info) env.Set("FM4_INFO_SHOW", "1");
  return plan;
}

}  // namespace fm4
