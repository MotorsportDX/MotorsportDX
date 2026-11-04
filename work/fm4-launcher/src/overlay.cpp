#include "overlay.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

namespace fm4 {

namespace fs = std::filesystem;

namespace {

// The options whose values are knob lists, with the plan's check of a value.
bool IsKnobOption(const std::string& option) {
  return option == "pad" || option == "keyboard" || option == "wheel";
}

std::string* KnobText(Options& options, const std::string& option) {
  if (option == "pad") return &options.pad;
  if (option == "keyboard") return &options.keyboard;
  if (option == "wheel") return &options.wheel;
  return nullptr;
}

void CheckKnobs(const LauncherData& data, const std::string& option, const std::string& text) {
  if (option == "pad") PadCvars(data, text);
  if (option == "keyboard") KeyboardCvars(data, text);
  if (option == "wheel") WheelCvars(data, text);
}

// A knob list ("name=value,...") with one knob set, left out when the value is the launchers'
// default (the value the option has without it).
std::string SetKnob(const Json& table, const std::string& text, const std::string& knob,
                    const std::string& value) {
  Pairs knobs;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t comma = text.find(',', start);
    std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    item.erase(0, item.find_first_not_of(" \t"));
    item.erase(item.find_last_not_of(" \t") + 1);
    const size_t equals = item.find('=');
    if (equals != std::string::npos) knobs.Set(item.substr(0, equals), item.substr(equals + 1));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (table.at(knob).at("default").get<std::string>() == value)
    knobs.Erase(knob);
  else
    knobs.Set(knob, value);
  std::string result;
  for (const auto& [name, knob_value] : knobs.Items())
    result += (result.empty() ? "" : ",") + name + "=" + knob_value;
  return result;
}

// The "choices" of a row of launcher.json's "settings".
const Json* SettingRow(const LauncherData& data, const std::string& option, const std::string& knob) {
  if (!data.json.contains("settings")) return nullptr;
  for (const auto& row : data.json.at("settings"))
    if (row.at("option") == option && row.at("knob") == knob) return &row;
  return nullptr;
}

bool InChoices(const Json& row, const std::string& value) {
  if (!row.contains("choices")) return false;
  for (const auto& choice : row.at("choices"))
    if (choice.at(0).get<std::string>() == value) return true;
  return false;
}

}  // namespace

std::vector<std::string> CheckPlayerSettings(const LauncherData& data) {
  std::vector<std::string> problems;
  if (!data.json.contains("settings") || !data.json.at("settings").is_array())
    return {"launcher.json has no player settings"};
  const Json options = OptionsToJson(Options{});
  std::set<std::pair<std::string, std::string>> seen;
  for (const auto& row : data.json.at("settings")) {
    const std::string option = row.at("option").get<std::string>();
    const std::string knob = row.at("knob").get<std::string>();
    const std::string what = "Player setting " + option + (knob.empty() ? "" : " " + knob);
    if (!seen.insert({option, knob}).second) problems.push_back(what + ": twice");
    const std::string klass = row.at("class").get<std::string>();
    if (klass != "live" && klass != "restart") problems.push_back(what + ": class " + klass);
    if (!options.contains(option)) {
      problems.push_back(what + ": no such launcher option");
      continue;
    }
    const std::string fallback = row.at("default").get<std::string>();
    if (IsKnobOption(option)) {
      const Json& table = data.json.at(option).at("knobs");
      if (!table.contains(knob)) {
        problems.push_back(what + ": no such knob");
        continue;
      }
      if (table.at(knob).at("cvar") != row.at("cvar")) problems.push_back(what + ": another cvar");
      if (table.at(knob).at("default") != row.at("default")) problems.push_back(what + ": another default");
      try {
        CheckKnobs(data, option, knob + "=" + fallback);
      } catch (const std::exception& error) {
        problems.push_back(what + ": " + error.what());
      }
    } else if (option == "look") {
      try {
        if (knob == "preset") {
          for (const auto& choice : row.at("choices")) LookKnobs(data, choice.at(0).get<std::string>());
        } else {
          LookDefines(data, LookKnobs(data, data.Look().at("default").get<std::string>() + "," + knob + "=" +
                                                fallback));
        }
      } catch (const std::exception& error) {
        problems.push_back(what + ": " + error.what());
      }
    } else {
      // Every choice is a value of the option that the profile keeps as it is.
      for (const auto& choice : row.at("choices")) {
        const std::string value = choice.at(0).get<std::string>();
        Options changed;
        if (!SetOptionText(changed, option, value) || OptionText(changed, option) != value)
          problems.push_back(what + ": the choice '" + value + "' does not round-trip");
        else if (OptionsToJson(OptionsFromJson(OptionsToJson(changed))) != OptionsToJson(changed))
          problems.push_back(what + ": the choice '" + value + "' is not saved");
      }
      if (OptionText(Options{}, option) != fallback)
        problems.push_back(what + ": default '" + fallback + "', the launcher's is '" +
                           OptionText(Options{}, option) + "'");
    }
  }
  return problems;
}

bool SetOptionText(Options& o, const std::string& option, const std::string& value) {
  const auto number = [&](int& field) {
    try {
      size_t used = 0;
      const int parsed = std::stoi(value, &used);
      if (used != value.size()) return false;
      field = parsed;
      return true;
    } catch (const std::exception&) {
      return false;
    }
  };
  if (option == "fullscreen") {
    if (value != "0" && value != "1") return false;
    o.fullscreen = value == "1";
    return true;
  }
  if (option == "resolution_scale") return number(o.resolution_scale);
  if (option == "frame_rate") return number(o.frame_rate);
  if (option == "display") return o.display = value, true;
  if (option == "upscale") return o.upscale = value, true;
  if (option == "vsync") return o.vsync = value, true;
  if (option == "quality") return o.quality = value, true;
  return false;
}

std::string OptionText(const Options& o, const std::string& option) {
  if (option == "fullscreen") return o.fullscreen ? "1" : "0";
  if (option == "resolution_scale") return std::to_string(o.resolution_scale);
  if (option == "frame_rate") return std::to_string(o.frame_rate);
  if (option == "display") return o.display;
  if (option == "upscale") return o.upscale;
  if (option == "vsync") return o.vsync;
  if (option == "quality") return o.quality;
  return {};
}

Json OverlayStart(const Profile& profile) {
  return Json{{"profile", profile.name}, {"options", OptionsToJson(profile.options)},
              {"changes", Json::object()}};
}

bool FoldOverlayChanges(const LauncherData& data, const Json& file, Settings& settings,
                        std::vector<std::string>& log) {
  if (!file.is_object() || !file.contains("changes") || !file.at("changes").is_object()) {
    log.push_back("overlay-changes.json: no changes");
    return false;
  }
  const Json& changes = file.at("changes");
  if (changes.empty()) return false;
  const std::string name = file.contains("profile") && file.at("profile").is_string()
                               ? file.at("profile").get<std::string>()
                               : std::string();
  Profile* profile = nullptr;
  for (auto& candidate : settings.profiles)
    if (candidate.name == name) profile = &candidate;
  if (!profile) {
    log.push_back("overlay-changes.json: dropped the changes of profile '" + name +
                  "', which does not exist");
    return false;
  }
  Options o = profile->options;
  for (const auto& [option, change] : changes.items()) {
    const std::string what = "overlay-changes.json: profile '" + name + "', " + option;
    if (IsKnobOption(option) || option == "look") {
      if (!change.is_object()) {
        log.push_back(what + ": dropped, not a list of knobs");
        continue;
      }
      // The preset first, so that the knobs compare with the new preset's.
      std::vector<std::pair<std::string, std::string>> knobs;
      for (const auto& [knob, value] : change.items()) {
        if (!value.is_string()) {
          log.push_back(what + " " + knob + ": dropped, not text");
          continue;
        }
        if (knob == "preset")
          knobs.insert(knobs.begin(), {knob, value.get<std::string>()});
        else
          knobs.emplace_back(knob, value.get<std::string>());
      }
      for (const auto& [knob, value] : knobs) {
        const std::string item = what + " " + knob + "=" + value;
        try {
          if (option == "look") {
            const std::string fallback = data.Look().at("default").get<std::string>();
            LookParts parts = LookParts::Parse(o.look.empty() ? fallback : o.look);
            if (knob == "preset") {
              const Json* row = SettingRow(data, "look", "preset");
              if (!row || !InChoices(*row, value)) throw PlanError("not a look of the launcher");
              parts.base = value;
            } else {
              const Pairs preset = LookKnobs(data, parts.base.empty() ? fallback : parts.base);
              if (preset.Find(knob) && preset.Get(knob) == value)
                parts.knobs.Erase(knob);
              else
                parts.knobs.Set(knob, value);
            }
            const std::string look = parts.Value();
            LookDefines(data, LookKnobs(data, look));
            o.look = look;
          } else {
            const Json& table = data.json.at(option).at("knobs");
            if (!table.contains(knob)) throw PlanError("no such knob");
            std::string* text = KnobText(o, option);
            const std::string changed = SetKnob(table, *text, knob, value);
            CheckKnobs(data, option, changed);
            *text = changed;
          }
        } catch (const std::exception& error) {
          log.push_back(item + ": dropped, " + error.what());
        }
      }
      continue;
    }
    const Json* row = SettingRow(data, option, "");
    if (!change.is_string() || !row || !InChoices(*row, change.get<std::string>()) ||
        !SetOptionText(o, option, change.get<std::string>())) {
      log.push_back(what + "=" + (change.is_string() ? change.get<std::string>() : change.dump()) +
                    ": dropped, not a setting the overlay changes");
      continue;
    }
  }
  if (OptionsToJson(o) == OptionsToJson(profile->options)) return false;
  profile->options = o;
  log.push_back("overlay-changes.json: folded into profile '" + name + "'");
  return true;
}

bool FoldOverlayFile(const LauncherData& data, const fs::path& path, Settings& settings,
                     std::vector<std::string>& log) {
  const auto read = [&path]() {
    std::ifstream stream(path, std::ios::binary);
    return stream ? std::string(std::istreambuf_iterator<char>(stream), {}) : std::string();
  };
  if (!fs::exists(path)) return false;
  const std::string text = read();
  Json file;
  try {
    file = Json::parse(text);
  } catch (const Json::exception&) {
    // The overlay writes the file whole (a temporary file renamed), so this is a damaged file.
    log.push_back("overlay-changes.json: dropped, not JSON");
    file = Json::object();
  }
  const bool changed = FoldOverlayChanges(data, file, settings, log);
  // The overlay writes every change of its run, so a file written meanwhile folds next time.
  std::error_code error;
  if (read() == text) fs::remove(path, error);
  return changed;
}

}  // namespace fm4
