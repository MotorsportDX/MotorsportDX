// The launcher's own settings, launcher-settings.json beside launcher.json: the Discs and
// content, the profiles of run options, the saved looks and the key bindings.
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "plan.h"

namespace fm4 {

struct Profile {
  std::string name;
  Options options;
};

struct Settings {
  std::string disc1;
  std::string disc2;
  std::vector<std::string> content;
  std::string user_data;  // empty: <root>\user-data
  std::vector<Profile> profiles;
  std::string active;
  std::vector<std::pair<std::string, std::string>> looks;  // name, look value
  Pairs binds;                                              // keybind_* cvars the player changed
  bool mouse_right_stick = false;
  bool developer = false;  // the developer options on every page (never part of a plan)
  // The Mods page (docs/running-mods.md): the mods Play races with, together (BuildRoot\mods\<id>,
  // none for Disc 1 as it is), and the discs they are converted from (folders or disc images).
  std::vector<std::string> mods;
  std::string mod_fm3_disc1, mod_fm3_disc2, mod_fm2_disc, mod_fm2_bonus;

  static Settings Load(const std::filesystem::path& path);
  void Save(const std::filesystem::path& path) const;
  Profile& Active();
};

Json OptionsToJson(const Options& options);
Options OptionsFromJson(const Json& json);

}  // namespace fm4
