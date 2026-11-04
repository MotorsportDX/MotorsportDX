// The in-game settings overlay (docs/analysis/ingame-settings.md): a menu over the guest output
// that changes the player's settings while the game runs. F2 opens and closes it, and so does
// holding View and Menu (Back and Start) together for a second on a pad. Its pages are Controls,
// Look, Display and Info (the text of the F2 info panel and the frame rate).
//
// A live row applies at once, through rex::cvar::SetFlagByName for cvars and shader_mod_set for
// post pass params; a restart row is saved and labelled "next start". Every change goes into the
// GUI launcher's overlay-changes.json (fm4_settings_changes), which the launcher folds into the
// profile; a run without it applies live rows but saves nothing. On the RTV and ROV paths every
// row is read-only (the owner's decision of 4 November 2026).
//
// While the menu is open the guest reads a neutral pad and no keystrokes, and the menu is not
// idle, so the host presents at UI ticks; closed, the dialog does not exist and the present path
// is the players' own. The presenter draws it over the guest output, so captures
// (FM4_CAPTURE_DIR) never contain it.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>

#include "fm4-settings-model.h"

struct ImFont;
struct ImFontAtlas;

// The settings and their state for the whole run: the rows of launcher.json, the values the
// run started with, the changes made, and overlay-changes.json. Used on the UI thread.
class FM4SettingsState {
 public:
  FM4SettingsState();

  const std::vector<fm4settings::Row>& rows() const { return rows_; }
  // The status line of the menu: where changes go, or why they cannot.
  const std::string& status() const { return status_; }
  bool read_only() const { return read_only_; }

  // The row's value now: this run's change, else the value the run started with ("" unknown).
  std::string Value(const fm4settings::Row& row) const;
  // A row the player can change in this run.
  bool Editable(const fm4settings::Row& row) const;
  // The row applies at once in this run (else at the next start).
  bool LiveNow(const fm4settings::Row& row) const;
  // A note of the row's state, such as "now up to 0.6" for the occlusion; empty: none.
  std::string Note(const fm4settings::Row& row) const;
  // Sets a row: applies a live row, records the change and saves the changes file. Returns
  // false if the row cannot change.
  bool Set(const fm4settings::Row& row, const std::string& value);
  // The row of an option and knob, or nullptr.
  const fm4settings::Row* Find(const std::string& option, const std::string& knob) const;

 private:
  std::string StartValue(const fm4settings::Row& row) const;
  void Save();

  fm4settings::Json launcher_;
  std::vector<fm4settings::Row> rows_;
  fm4settings::Changes changes_;
  std::string changes_path_;
  bool can_save_ = false;
  bool read_only_ = false;
  bool post_passes_ = false;
  bool taa_final_ = false;
  std::map<std::string, std::string> start_defines_;
  std::string status_;
};

// The pad as the guest reads it (InputSystem's guest pad filter): the View and Menu chord, the
// menu's pad navigation, and the neutral pad the guest reads while the menu is open.
class FM4SettingsPad {
 public:
  // on_chord: called on the guest's input thread when View and Menu were held for a second.
  explicit FM4SettingsPad(std::function<void()> on_chord) : on_chord_(std::move(on_chord)) {}

  void Filter(uint32_t user_index, uint16_t& buttons, uint8_t& left_trigger, uint8_t& right_trigger,
              int16_t& lx, int16_t& ly, int16_t& rx, int16_t& ry);
  void SetOpen(bool open) { open_ = open; }
  uint16_t buttons() const { return buttons_.load(std::memory_order_relaxed); }
  int16_t stick_x() const { return lx_.load(std::memory_order_relaxed); }
  int16_t stick_y() const { return ly_.load(std::memory_order_relaxed); }

 private:
  std::function<void()> on_chord_;
  std::atomic<bool> open_{false};
  std::atomic<uint16_t> buttons_{0};
  std::atomic<int16_t> lx_{0}, ly_{0};
  std::chrono::steady_clock::time_point chord_since_{};
  bool chord_held_ = false, chord_fired_ = false;
};

// The menu's fonts (FM4SettingsFonts, added in the app's OnConfigureFonts): Segoe UI for the
// text and Bahnschrift for the headings, as the launcher; nullptr: ImGui's default.
struct FM4SettingsFonts {
  ImFont* body = nullptr;
  ImFont* heading = nullptr;
};
void FM4AddSettingsFonts(ImFontAtlas* atlas, FM4SettingsFonts& fonts);

class FM4SettingsOverlay : public rex::ui::ImGuiDialog {
 public:
  enum Page { kControls, kLook, kDisplay, kInfo, kPageCount };

  FM4SettingsOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window, FM4SettingsState* state,
                     const FM4SettingsPad* pad, const FM4SettingsFonts* fonts,
                     std::function<uint64_t()> frames, std::vector<std::string> info_lines,
                     std::function<void()> on_close);
  ~FM4SettingsOverlay() override;

  bool IsIdle() const override { return false; }
  void SetPage(Page page) { page_ = page; focus_ = 0; }
  Page page() const { return page_; }
  // The scale of the menu: fm4_settings_overlay_scale, else from the window's logical height.
  float Scale() const;
  // The rows of the page, in order.
  std::vector<const fm4settings::Row*> PageRows(Page page) const;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  // Steps the focused row; delta -1 or 1, wrap for A and Enter.
  void Change(const fm4settings::Row& row, int delta, bool wrap);
  // The presses of a pad button (the stick's directions count as the D-pad's), repeating while
  // held.
  int PadPresses(uint16_t mask, std::chrono::steady_clock::time_point now);

  rex::ui::Window* window_;
  FM4SettingsState* state_;
  const FM4SettingsPad* pad_;
  const FM4SettingsFonts* fonts_;
  std::function<uint64_t()> frames_;
  std::vector<std::string> info_lines_;
  std::function<void()> on_close_;
  Page page_ = kControls;
  int focus_ = 0;
  // Pad buttons held when the menu opened are ignored until released.
  uint16_t ignored_buttons_ = 0, last_buttons_ = 0;
  std::chrono::steady_clock::time_point repeat_at_[16] = {};
  std::chrono::steady_clock::time_point rate_time_;
  uint64_t rate_frames_ = 0;
  double fps_ = 0.0;
  bool scroll_to_focus_ = true;
};

// A picture of the window as shown, with the menu: its client area copied from the screen into a
// 24-bit BMP (the presenter's own output, not the guest captures). Returns false on failure.
bool FM4CaptureWindow(rex::ui::Window* window, const std::string& path);
