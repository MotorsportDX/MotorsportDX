// The launcher's side of the in-game settings overlay (docs/analysis/ingame-settings.md 4.3):
// the player's settings of launcher.json ("settings", player_settings in pipeline.py), and
// overlay-changes.json beside launcher-settings.json. At Play the launcher writes that file with
// the profile's name and options, and passes its path in the cvar fm4_settings_changes; the
// overlay adds the changes made in the game ("changes", in the launcher's option and knob names).
// At its start, when it gets focus and when the game exits, the launcher folds the changes into
// that profile, saves, and removes the file. The launcher stays the only writer of
// launcher-settings.json.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "settings.h"

namespace fm4 {

constexpr char kOverlayChangesFile[] = "overlay-changes.json";
constexpr char kOverlayChangesCvar[] = "fm4_settings_changes";

// The problems of launcher.json's "settings" against Options and the knob tables; none: empty.
std::vector<std::string> CheckPlayerSettings(const LauncherData& data);

// An option of Options that the player's settings change (not pad, keyboard, wheel or look,
// whose values are knob lists), as the text of its choices: fullscreen "0" or "1",
// resolution_scale and frame_rate as numbers, the others as stored. Unknown options: false.
bool SetOptionText(Options& options, const std::string& option, const std::string& value);
std::string OptionText(const Options& options, const std::string& option);

// overlay-changes.json at Play: the profile's name and options, no changes yet.
Json OverlayStart(const Profile& profile);

// Folds the "changes" of an overlay-changes.json into the profile it names. A change of an
// unknown profile, option, knob or value is dropped with a line in log. Returns whether the
// profile changed.
bool FoldOverlayChanges(const LauncherData& data, const Json& file, Settings& settings,
                        std::vector<std::string>& log);

// Reads path and folds it (FoldOverlayChanges), then removes it unless the overlay wrote it again
// meanwhile. Returns whether settings changed; the caller saves them.
bool FoldOverlayFile(const LauncherData& data, const std::filesystem::path& path, Settings& settings,
                     std::vector<std::string>& log);

}  // namespace fm4
