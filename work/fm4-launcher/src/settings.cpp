#include "settings.h"

#include <fstream>

namespace fm4 {

namespace fs = std::filesystem;

Json OptionsToJson(const Options& o) {
  return Json{{"display", o.display},
              {"fullscreen", o.fullscreen},
              {"resolution_scale", o.resolution_scale},
              {"supersample", o.supersample},
              {"frame_rate", o.frame_rate},
              {"vsync", o.vsync},
              {"upscale", o.upscale},
              {"entry0", o.entry0},
              {"pad", o.pad},
              {"keyboard", o.keyboard},
              {"quality", o.quality},
              {"look", o.look},
              {"msaa", o.msaa},
              {"native", o.native},
              {"msaa_samples", o.msaa_samples},
              {"perf_trace", o.perf_trace},
              {"extra_cvars", o.extra_cvars},
              {"single_tile", o.single_tile},
              {"full_rate_mirror", o.full_rate_mirror},
              {"info", o.info},
              {"fixed_vblank", o.fixed_vblank},
              {"shadow_workaround", o.shadow_workaround},
              {"rov", o.rov},
              {"ao", o.ao},
              {"texture_dump", o.texture_dump},
              {"texture_replace", o.texture_replace}};
}

Options OptionsFromJson(const Json& json) {
  Options o;
  const auto get = [&](const char* name, auto& field) {
    if (json.contains(name)) {
      try {
        json.at(name).get_to(field);
      } catch (const Json::exception&) {
      }
    }
  };
  get("display", o.display);
  get("fullscreen", o.fullscreen);
  // pipeline.py's resolution_scale 'panel' (the launcher tests), or the stored kPanelScale.
  if (json.contains("resolution_scale") && json.at("resolution_scale") == "panel")
    o.resolution_scale = kPanelScale;
  else
    get("resolution_scale", o.resolution_scale);
  get("supersample", o.supersample);
  get("frame_rate", o.frame_rate);
  get("vsync", o.vsync);
  get("upscale", o.upscale);
  get("entry0", o.entry0);
  get("pad", o.pad);
  get("keyboard", o.keyboard);
  get("quality", o.quality);
  get("look", o.look);
  get("msaa", o.msaa);
  // Earlier profiles stored a checkbox: true for -Native, false for RTV.
  if (json.contains("native") && json.at("native").is_boolean())
    o.native = json.at("native").get<bool>() ? 1 : 0;
  else
    get("native", o.native);
  get("msaa_samples", o.msaa_samples);
  get("perf_trace", o.perf_trace);
  get("extra_cvars", o.extra_cvars);
  get("single_tile", o.single_tile);
  get("full_rate_mirror", o.full_rate_mirror);
  get("info", o.info);
  get("fixed_vblank", o.fixed_vblank);
  get("shadow_workaround", o.shadow_workaround);
  get("rov", o.rov);
  get("ao", o.ao);
  get("texture_dump", o.texture_dump);
  get("texture_replace", o.texture_replace);
  return o;
}

Settings Settings::Load(const fs::path& path) {
  Settings settings;
  std::ifstream stream(path, std::ios::binary);
  Json json;
  if (stream) {
    try {
      json = Json::parse(stream);
    } catch (const Json::exception&) {
      json = Json::object();
    }
  }
  const auto text = [&](const char* name) {
    return json.contains(name) && json.at(name).is_string() ? json.at(name).get<std::string>()
                                                            : std::string();
  };
  settings.disc1 = text("disc1");
  settings.disc2 = text("disc2");
  settings.user_data = text("user_data");
  settings.active = text("active_profile");
  if (json.contains("content") && json.at("content").is_array())
    for (const auto& item : json.at("content"))
      if (item.is_string()) settings.content.push_back(item.get<std::string>());
  if (json.contains("profiles") && json.at("profiles").is_object())
    for (const auto& [name, options] : json.at("profiles").items())
      settings.profiles.push_back({name, OptionsFromJson(options)});
  if (json.contains("looks") && json.at("looks").is_object())
    for (const auto& [name, look] : json.at("looks").items())
      if (look.is_string()) settings.looks.emplace_back(name, look.get<std::string>());
  if (json.contains("binds") && json.at("binds").is_object())
    for (const auto& [name, keys] : json.at("binds").items())
      if (keys.is_string()) settings.binds.Set(name, keys.get<std::string>());
  if (json.contains("mouse_right_stick") && json.at("mouse_right_stick").is_boolean())
    settings.mouse_right_stick = json.at("mouse_right_stick").get<bool>();
  if (settings.profiles.empty()) settings.profiles.push_back({"Default", Options{}});
  settings.Active();
  return settings;
}

void Settings::Save(const fs::path& path) const {
  Json json = Json::object();
  json["disc1"] = disc1;
  json["disc2"] = disc2;
  json["content"] = content;
  json["user_data"] = user_data;
  json["active_profile"] = active;
  json["profiles"] = Json::object();
  for (const auto& profile : profiles) json["profiles"][profile.name] = OptionsToJson(profile.options);
  json["looks"] = Json::object();
  for (const auto& [name, look] : looks) json["looks"][name] = look;
  json["binds"] = Json::object();
  for (const auto& [name, keys] : binds.Items()) json["binds"][name] = keys;
  json["mouse_right_stick"] = mouse_right_stick;
  const fs::path temporary = fs::path(path).concat(L".tmp");
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << json.dump(1);
    if (!stream) throw std::runtime_error("Unable to write " + Utf8(temporary));
  }
  fs::rename(temporary, path);
}

Profile& Settings::Active() {
  for (auto& profile : profiles)
    if (profile.name == active) return profile;
  active = profiles.front().name;
  return profiles.front();
}

}  // namespace fm4
