// The in-game settings overlay (fm4-settings-overlay.h, docs/analysis/ingame-settings.md): its
// cvars. The launchers' launch plan passes the player's settings table (launcher.json, whose
// "settings" pipeline.py writes from player_settings.py); the GUI launcher alone passes the file it
// folds into the profile (overlay-changes.json beside launcher-settings.json), so a run started
// without it shows the values but cannot save them.

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(fm4_settings_table, "", "FM4",
                      "The in-game settings overlay's table of the player's settings: the "
                      "launcher's launcher.json (empty: the overlay shows its info page only)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(fm4_settings_changes, "", "FM4",
                      "The GUI launcher's overlay-changes.json: the profile and options the run "
                      "started with, and the in-game settings overlay's changes, which the "
                      "launcher folds into that profile (empty: the overlay cannot save)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
