#include "plan.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

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

double WindshieldStrength(const Pairs& knobs) {
  const std::string text = knobs.Get("windshield", "0");
  const double value = PyFloat(text);
  if (!(value >= 0 && value <= 2))
    throw PlanError("windshield is a strength from 0 (off) to 2, not '" + text + "'.");
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

Pairs LookCvars(const LauncherData& data, const std::string& root, const std::string& look) {
  const Json& tables = data.Look();
  Pairs knobs;
  for (const auto& [name, value] : LookKnobs(data, look).Items())
    if (!tables.at("track_knobs").contains(name)) knobs.Set(name, value);
  Pairs cvars = SunCvars(data, knobs.Find("sun"));
  bool mods = false;
  for (const auto& [name, value] : knobs.Items())
    if (!Contains(tables.at("runtime_knobs"), name) && !Contains(tables.at("pass_knobs"), name))
      mods = true;
  std::vector<std::string> defines = LookDefines(data, knobs);
  if (!mods) {
    std::erase_if(defines, [](const std::string& define) { return define.starts_with("FM4_LOOK_"); });
    if (!defines.empty()) cvars.Set("shader_mod_defines", Join(defines, ";"));
    Update(cvars, AoCvars(root, TaaEnabled(knobs) || WindshieldStrength(knobs) > 0));
    return cvars;
  }
  cvars.Set("shader_mod_path", JoinPath(root, "look"));
  cvars.Set("shader_mod_defines", Join(defines, ";"));
  Update(cvars, AoCvars(root, PyFloat(knobs.Get("ao", "0")) > 0 || TaaEnabled(knobs) ||
                                  WindshieldStrength(knobs) > 0));
  return cvars;
}

std::string RaceSettings(const LauncherData& data, const std::string& quality, bool single_tile) {
  const Json& presets = data.json.at("quality").at("presets");
  if (!presets.contains(quality))
    throw PlanError("Unknown quality '" + quality + "'; use " + Names(presets) + ".");
  std::vector<std::string> settings;
  for (const auto& setting : presets.at(quality)) {
    const std::string text = setting.get<std::string>();
    if (!(single_tile && text.starts_with("TilingScenario="))) settings.push_back(text);
  }
  return Join(settings, ";");
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
  lines.push_back("Quality: " + quality + (quality == "xbox" ? " (the game's own)" : "") + tiling);
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
  if (define("TAA") && *define("TAA") == "1" && define("TAA_POINT") && *define("TAA_POINT") == "1") {
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

bool TaaEnabled(const Pairs& knobs) {
  const std::string text = knobs.Get("taa", "0");
  const double value = PyFloat(text);
  if (value < 0) throw PlanError("taa is 0 (off) or 1 (on), not '" + text + "'.");
  return value > 0;
}

std::vector<std::string> LookDefines(const LauncherData& data, const Pairs& knobs) {
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
  if (TaaEnabled(knobs) && scene_point) {
    defines.insert(defines.end(), {"TAA=1", "TAA_SHARPEN=0.0", "TAA_POINT=1"});
  } else if (TaaEnabled(knobs)) {
    const double sharpen = PyFloat(knobs.Get("sharpen", "0"));
    std::erase_if(defines, [](const std::string& d) { return d.starts_with("FM4_LOOK_SHARPEN="); });
    defines.insert(defines.end(),
                   {"FM4_LOOK_SHARPEN=0.0", "TAA=1", "TAA_SHARPEN=" + PyRepr(sharpen)});
  }
  if (WindshieldStrength(knobs) > 0)
    defines.push_back("WINDSHIELD=" + PyRepr(WindshieldStrength(knobs)));
  return defines;
}

std::pair<Pairs, std::string> DisplaySettings(const LauncherData& data, const std::string& display,
                                              bool fullscreen, bool supersample,
                                              int resolution_scale, const Monitor& monitor) {
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
  const int max_scale = data.json.at("max_resolution_scale").get<int>();
  const int scale =
      resolution_scale
          ? resolution_scale
          : std::min(max_scale,
                     static_cast<int>(std::ceil(std::min(width / 1280.0, height / 720.0) - 1e-6)) +
                         (supersample ? 1 : 0));
  Pairs cvars;
  cvars.Set("fullscreen", fullscreen ? "true" : "false");
  if (!fullscreen) {
    // Python's round(): halves to even, as nearbyint in the default rounding mode.
    cvars.Set("window_width", std::to_string(static_cast<long long>(std::nearbyint(width / monitor.dpi))));
    cvars.Set("window_height", std::to_string(static_cast<long long>(std::nearbyint(height / monitor.dpi))));
  }
  if (scale > 1) {
    cvars.Set("draw_resolution_scale_x", std::to_string(scale));
    cvars.Set("draw_resolution_scale_y", std::to_string(scale));
  }
  const std::string screen =
      fullscreen ? "fullscreen " + std::to_string(monitor.width) + "x" + std::to_string(monitor.height)
                 : "window " + std::to_string(width) + "x" + std::to_string(height);
  return {cvars, "Display " + display + ": " + screen + ", render " + std::to_string(scale) +
                     "x (" + std::to_string(1280 * scale) + "x" + std::to_string(720 * scale) + ")"};
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
  // One pass with MSAA on the native path: -Native runs it with 2x unless -MsaaSamples 4 (4x),
  // -MsaaSamples 0 (one tile without MSAA), -SingleTile or -Msaa says otherwise.
  if (options.msaa_samples != -1 && options.msaa_samples != 0 && options.msaa_samples != 2 &&
      options.msaa_samples != 4)
    throw PlanError("-MsaaSamples takes 0, 2 or 4.");
  if (options.msaa_samples >= 0 && options.rov)
    throw PlanError("-MsaaSamples is the native path's one-pass MSAA; -Rov cannot run it.");
  if (options.msaa_samples >= 0 && !options.native)
    throw PlanError("-MsaaSamples is the native path's one-pass MSAA: use it with -Native.");
  int msaa_samples = options.msaa_samples;
  if (options.native && msaa_samples < 0 && !options.single_tile && !options.msaa && !options.rov)
    msaa_samples = 2;
  const bool one_pass = msaa_samples > 0;
  const bool single_tile =
      !one_pass && (options.single_tile || msaa_samples == 0 ||
                    (TaaEnabled(knobs) && !options.rov && !options.msaa));
  const std::string quality = options.quality.empty()
                                  ? data.json.at("quality").at("default").get<std::string>()
                                  : options.quality;
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
    // The uncorrected reference keeps the title's ring index wait as it is
    // (docs/analysis/d3d-hle-n4.md 17.16).
    cvars.Set("guest_ring_wait_sleep", "false");
  }
  if (options.native) {
    if (options.rov)
      throw PlanError(
          "-Native runs on the host render target path (RTV); it cannot be combined with -Rov.");
    // The native path covers one tile and one pass; the tiles of the quality (MSAA tiles) stay
    // on the PM4 RTV path.
    if (single_tile || one_pass)
      for (const auto& [name, value] : data.json.at("native_cvars").items())
        cvars.Set(name, value.get<std::string>());
    if (one_pass) {
      // Bit 8: the host render targets sized to the tiles the title uses.
      cvars.Set("native_one_pass_msaa", std::to_string(msaa_samples));
      cvars.Set("native_rt_surfaces", "10");
    }
  }
  const std::string display = options.display.empty()
                                  ? data.json.at("default_display").get<std::string>()
                                  : options.display;
  auto [screen, description] = DisplaySettings(data, display, options.fullscreen,
                                               options.supersample, options.resolution_scale,
                                               monitor);
  Update(cvars, screen);
  plan.description = description;
  if (options.frame_rate) {
    cvars.Set("video_mode_refresh_rate", std::to_string(options.frame_rate));
    env.Set("FM4_FRAME_RATE", std::to_string(options.frame_rate));
  }
  Update(cvars, LookCvars(data, root, look));
  const std::string track = LookTrackSettings(data, knobs);
  if (!track.empty()) env.Set("FM4_TRACK_SETTINGS", track);
  Update(cvars, AoCvars(root, options.ao));
  if (!options.texture_dump.empty())
    cvars.Set("texture_dump_path", ResolvePath(options.texture_dump));
  if (!options.texture_replace.empty()) {
    if (!fs::is_directory(PathFromUtf8(options.texture_replace)))
      throw PlanError("-TextureReplace is not a directory: " + options.texture_replace);
    cvars.Set("texture_replace_path", ResolvePath(options.texture_replace));
  }
  if (!content_manifest.empty()) env.Set("FM4_CONTENT_MANIFEST", content_manifest);
  env.Set("FM4_INFO", InfoOverlay(data, cvars, env, look, quality, description));
  if (options.info) env.Set("FM4_INFO_SHOW", "1");
  return plan;
}

}  // namespace fm4
