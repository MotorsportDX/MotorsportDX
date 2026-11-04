// The in-game settings overlay (fm4-settings-overlay.h, docs/analysis/ingame-settings.md): its
// cvars, the state of the player's settings, the pad's chord and navigation, the menu and the
// window capture of its screenshots. The launchers' launch plan passes the table of the player's
// settings (launcher.json, whose "settings" pipeline.py writes from player_settings.py); the GUI
// launcher alone passes the file it folds into the profile (overlay-changes.json beside
// launcher-settings.json), so a run started without it applies live rows but saves nothing.

#include "fm4-settings-overlay.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(fm4_settings_table, "", "FM4",
                      "The in-game settings overlay's table of the player's settings: the "
                      "launcher's launcher.json (empty: the overlay shows its info page only)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(fm4_settings_changes, "", "FM4",
                      "The GUI launcher's overlay-changes.json: the profile and options the run "
                      "started with, and the in-game settings overlay's changes, which the "
                      "launcher folds into that profile (empty: the overlay cannot save)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_DOUBLE(fm4_settings_overlay_scale, 0.0, "FM4",
                      "The in-game settings overlay's scale (0: from the window's height, 1 at "
                      "720 logical lines)")
    .range(0.0, 4.0);
// The e2e check of the overlay (docs/analysis/ingame-settings.md 5): fm4_rex_app.h SettingsTest.
REXCVAR_DEFINE_DOUBLE(fm4_settings_overlay_test, 0.0, "FM4",
                      "Test: this many seconds into the first race, open the in-game settings "
                      "overlay, show each page, change a live control, a live look param and two "
                      "restart rows through it, and close it (0: off)")
    .range(0.0, 600.0)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(fm4_settings_overlay_shot, "", "FM4",
                      "Test, with fm4_settings_overlay_test: a screenshot of the window with the "
                      "overlay on each page, <this>-<page>.bmp (empty: none)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

namespace fs = std::filesystem;
using fm4settings::Row;

// The launcher's racing theme (work/fm4-launcher/src/app.cpp: kBackground and the others): the
// runtime cannot link the launcher's code, so the colours are copied.
const ImVec4 kBackground(0.051f, 0.055f, 0.067f, 0.94f);
const ImVec4 kField(0.118f, 0.129f, 0.157f, 1.0f);
const ImVec4 kBorder(0.165f, 0.180f, 0.216f, 1.0f);
const ImVec4 kText(0.929f, 0.937f, 0.953f, 1.0f);
const ImVec4 kMuted(0.541f, 0.565f, 0.612f, 1.0f);
const ImVec4 kAccent(0.902f, 0.231f, 0.180f, 1.0f);
const ImVec4 kAccentDark(0.420f, 0.098f, 0.075f, 1.0f);
const ImVec4 kSuccess(0.235f, 0.796f, 0.498f, 1.0f);
const ImVec4 kWarning(0.949f, 0.663f, 0.231f, 1.0f);

constexpr uint16_t kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008;
constexpr uint16_t kPadStart = 0x0010, kPadBack = 0x0020, kPadLeftShoulder = 0x0100,
                   kPadRightShoulder = 0x0200, kPadA = 0x1000, kPadB = 0x2000;

const char* const kPageNames[] = {"CONTROLS", "LOOK", "DISPLAY", "INFO"};
const char* const kPageIds[] = {"controls", "look", "display", "info"};

// A path of UTF-8 text (the cvars').
fs::path Utf8Path(const std::string& text) { return fs::path(reinterpret_cast<const char8_t*>(text.c_str())); }

std::string ReadText(const std::string& path) {
  std::ifstream stream(Utf8Path(path), std::ios::binary);
  return stream ? std::string(std::istreambuf_iterator<char>(stream), {}) : std::string();
}

std::string DefineNumber(const std::map<std::string, std::string>& defines, const std::string& name) {
  const auto found = defines.find(name);
  return found == defines.end() ? "0" : fm4settings::NumberText(std::strtod(found->second.c_str(), nullptr), 3);
}

}  // namespace

FM4SettingsState::FM4SettingsState() {
  const std::string table = REXCVAR_GET(fm4_settings_table);
  std::string error;
  if (table.empty()) {
    status_ = "No settings table: this run shows its info only.";
  } else if (!fm4settings::ParseJson(ReadText(table), launcher_, error)) {
    status_ = "Cannot read the settings table.";
    REXLOG_ERROR("FM4 settings overlay: cannot read {}: {}", table, error);
  } else {
    rows_ = fm4settings::ReadRows(launcher_);
  }
  // The look the run started with: the post pass params of shader_mod_defines.
  start_defines_ = fm4settings::ParseDefines(rex::cvar::GetFlagByName("shader_mod_defines"));
  post_passes_ = !rex::cvar::GetFlagByName("post_pass_path").empty();
  const auto define = [this](const char* name) {
    const auto found = start_defines_.find(name);
    return found == start_defines_.end() ? std::string() : found->second;
  };
  taa_final_ = std::strtod(define("TAA").c_str(), nullptr) > 0 &&
               std::strtod(define("TAA_POINT").c_str(), nullptr) == 0 &&
               std::strtod(define("FSR").c_str(), nullptr) == 0;
  read_only_ = rex::cvar::GetFlagByName("host_render_native") != "true";
  changes_path_ = REXCVAR_GET(fm4_settings_changes);
  if (!changes_path_.empty()) {
    if (changes_.Read(ReadText(changes_path_), error)) {
      can_save_ = true;
    } else {
      REXLOG_ERROR("FM4 settings overlay: cannot read {}: {}", changes_path_, error);
    }
  }
  if (rows_.empty()) {
    if (status_.empty()) status_ = "The settings table has no settings.";
  } else if (read_only_) {
    status_ = "Read-only on the RTV and ROV paths: the launcher's Native path changes settings.";
  } else if (can_save_) {
    status_ = "Saved for the launcher's profile '" + changes_.profile + "'.";
  } else {
    status_ = "Started without the launcher: changes apply now and are not saved.";
  }
  REXLOG_INFO("FM4 settings overlay: {} settings; {}", rows_.size(), status_);
}

const Row* FM4SettingsState::Find(const std::string& option, const std::string& knob) const {
  for (const auto& row : rows_)
    if (row.option == option && row.knob == knob) return &row;
  return nullptr;
}

std::string FM4SettingsState::StartValue(const Row& row) const {
  if (!row.cvar.empty()) return fm4settings::KnobOfCvar(row, rex::cvar::GetFlagByName(row.cvar));
  if (row.option == "fullscreen") return rex::cvar::GetFlagByName("fullscreen") == "true" ? "1" : "0";
  if (row.option == "look") {
    if (row.knob == "taa") return DefineNumber(start_defines_, "TAA") == "0" ? "0" : "1";
    if (row.knob == "sharpen")
      return DefineNumber(start_defines_, taa_final_ ? "TAA_SHARPEN" : "FM4_LOOK_SHARPEN");
    if (row.knob == "windshield") return DefineNumber(start_defines_, "WINDSHIELD");
    if (row.knob == "ao") return DefineNumber(start_defines_, "FM4_LOOK_AO");
    if (row.knob == "preset" && can_save_)
      return fm4settings::LookPreset(launcher_, changes_.Option("look"));
    return {};
  }
  return can_save_ ? changes_.Option(row.option) : std::string();
}

std::string FM4SettingsState::Value(const Row& row) const {
  if (const std::string* change = changes_.Change(row.option, row.knob)) return *change;
  return StartValue(row);
}

bool FM4SettingsState::LiveNow(const Row& row) const {
  if (!row.live) return false;
  if (row.param.empty()) return true;
  if (!post_passes_) return false;
  if (row.knob == "taa" || row.knob == "sharpen") return taa_final_;
  if (row.knob == "ao") return std::strtod(StartValue(row).c_str(), nullptr) > 0;
  return true;
}

bool FM4SettingsState::Editable(const Row& row) const {
  if (read_only_) return false;
  // Option rows need the options the run started with (the launcher's file).
  if (row.cvar.empty() && row.param.empty() && row.option != "fullscreen" && !can_save_) return false;
  return LiveNow(row) || can_save_;
}

std::string FM4SettingsState::Note(const Row& row) const {
  if (row.option == "look" && row.knob == "ao" && LiveNow(row)) {
    const std::string start = StartValue(row);
    if (std::strtod(Value(row).c_str(), nullptr) > std::strtod(start.c_str(), nullptr))
      return "now " + start + ", the rest next start";
  }
  if (row.option == "upscale" && Value(row) != "" && Value(row) != StartValue(row))
    return "sets FSR 3.1 in the look";
  return {};
}

bool FM4SettingsState::Set(const Row& row, const std::string& value) {
  if (!Editable(row) || value == Value(row)) return false;
  if (LiveNow(row)) {
    if (!row.cvar.empty()) {
      rex::cvar::SetFlagByName(row.cvar, fm4settings::CvarOfKnob(row, value));
    } else if (row.option == "fullscreen") {
      rex::cvar::SetFlagByName("fullscreen", value == "1" ? "true" : "false");
    } else if (!row.param.empty()) {
      std::string param_value = value;
      if (row.knob == "ao") {
        // The occlusion of the start's look, scaled: GTAO_SCALE is 0 to 1 of it.
        const double start = std::strtod(StartValue(row).c_str(), nullptr);
        param_value = fm4settings::NumberText(std::clamp(std::strtod(value.c_str(), nullptr) / start, 0.0, 1.0), 3);
      }
      rex::cvar::InvokeCommand("shader_mod_set", row.param + "=" + param_value);
    }
  }
  changes_.Set(row.option, row.knob, value);
  if (row.option == "upscale" && !value.empty()) {
    // An upscaler needs FSR 3.1 or TAAU as the look's anti-aliasing, as the launcher's Display
    // page sets them.
    std::string aa = "taa", look = changes_.Option("look");
    if (look.empty() && launcher_.Find("look")) look = launcher_.Find("look")->Text("default");
    for (const auto& [knob, knob_value] : fm4settings::LookKnobs(launcher_, look))
      if (knob == "aa") aa = knob_value;
    if (const std::string* change = changes_.Change("look", "aa")) aa = *change;
    if (aa != "fsr" && aa != "taau") {
      changes_.Set("look", "aa", "fsr");
      changes_.Set("look", "taa", "1");
    }
  }
  REXLOG_INFO("FM4 settings overlay: {} {}={} ({})", row.option, row.knob.empty() ? row.option : row.knob,
              value, LiveNow(row) ? "now" : "next start");
  Save();
  return true;
}

void FM4SettingsState::Save() {
  if (!can_save_) return;
  const fs::path path = Utf8Path(changes_path_);
  const fs::path temporary = fs::path(path).concat(L".tmp");
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << changes_.Write();
    if (!stream) {
      REXLOG_ERROR("FM4 settings overlay: cannot write {}.tmp", changes_path_);
      return;
    }
  }
  std::error_code error;
  fs::rename(temporary, path, error);
  if (error) REXLOG_ERROR("FM4 settings overlay: cannot replace {}: {}", changes_path_, error.message());
}

void FM4SettingsPad::Filter(uint32_t user_index, uint16_t& buttons, uint8_t& left_trigger,
                            uint8_t& right_trigger, int16_t& lx, int16_t& ly, int16_t& rx, int16_t& ry) {
  const bool open = open_.load(std::memory_order_relaxed);
  if (user_index == 0) {
    buttons_.store(buttons, std::memory_order_relaxed);
    lx_.store(lx, std::memory_order_relaxed);
    ly_.store(ly, std::memory_order_relaxed);
    // View and Menu together: the guest sees neither while both are down, and after a second
    // the menu opens or closes. The press that came first has reached the guest already.
    const auto now = std::chrono::steady_clock::now();
    if ((buttons & (kPadBack | kPadStart)) == (kPadBack | kPadStart)) {
      if (!chord_held_) {
        chord_held_ = true;
        chord_since_ = now;
      } else if (!chord_fired_ && now - chord_since_ >= std::chrono::seconds(1)) {
        chord_fired_ = true;
        if (on_chord_) on_chord_();
      }
      buttons &= uint16_t(~(kPadBack | kPadStart));
    } else {
      chord_held_ = chord_fired_ = false;
    }
  }
  if (open) {
    buttons = 0;
    left_trigger = right_trigger = 0;
    lx = ly = rx = ry = 0;
  }
}

void FM4AddSettingsFonts(ImFontAtlas* atlas, FM4SettingsFonts& fonts) {
  // Rasterised large and drawn smaller, since the drawer's atlas is built once.
  constexpr float kSize = 40.0f;
  wchar_t windows[MAX_PATH];
  if (!GetWindowsDirectoryW(windows, MAX_PATH)) return;
  const fs::path directory = fs::path(windows) / "Fonts";
  const auto load = [&](const wchar_t* name) -> ImFont* {
    const fs::path path = directory / name;
    if (!fs::exists(path)) return nullptr;
    ImFontConfig config;
    config.OversampleH = config.OversampleV = 2;
    return atlas->AddFontFromFileTTF(path.string().c_str(), kSize, &config);
  };
  fonts.body = load(L"segoeui.ttf");
  fonts.heading = load(L"bahnschrift.ttf");
  if (!fonts.heading) fonts.heading = fonts.body;
}

FM4SettingsOverlay::FM4SettingsOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
                                       FM4SettingsState* state, const FM4SettingsPad* pad,
                                       const FM4SettingsFonts* fonts, std::function<uint64_t()> frames,
                                       std::vector<std::string> info_lines, std::function<void()> on_close)
    : ImGuiDialog(drawer),
      window_(window),
      state_(state),
      pad_(pad),
      fonts_(fonts),
      frames_(std::move(frames)),
      info_lines_(std::move(info_lines)),
      on_close_(std::move(on_close)),
      rate_time_(std::chrono::steady_clock::now()),
      rate_frames_(frames_ ? frames_() : 0) {
  ignored_buttons_ = pad_ ? pad_->buttons() : 0;
  REXLOG_INFO("FM4 settings overlay: open");
}

FM4SettingsOverlay::~FM4SettingsOverlay() { REXLOG_INFO("FM4 settings overlay: closed"); }

float FM4SettingsOverlay::Scale() const {
  const double fixed = REXCVAR_GET(fm4_settings_overlay_scale);
  if (fixed > 0) return float(fixed);
  const double height = window_ ? double(window_->GetActualLogicalHeight()) : 720.0;
  return float(std::clamp(height / 720.0, 1.0, 2.5));
}

std::vector<const Row*> FM4SettingsOverlay::PageRows(Page page) const {
  std::vector<const Row*> rows;
  if (page == kInfo) return rows;
  for (const auto& row : state_->rows())
    if (row.page == kPageIds[page]) rows.push_back(&row);
  return rows;
}

void FM4SettingsOverlay::Change(const Row& row, int delta, bool wrap) {
  const std::string value = state_->Value(row);
  if (value.empty() && row.choices.empty()) return;
  state_->Set(row, fm4settings::Step(row, value, delta, wrap));
}

int FM4SettingsOverlay::PadPresses(uint16_t mask, std::chrono::steady_clock::time_point now) {
  if (!pad_) return 0;
  uint16_t buttons = pad_->buttons();
  // The left stick's directions count as the D-pad's.
  constexpr int kStick = 16000;
  if (pad_->stick_y() > kStick) buttons |= kPadUp;
  if (pad_->stick_y() < -kStick) buttons |= kPadDown;
  if (pad_->stick_x() < -kStick) buttons |= kPadLeft;
  if (pad_->stick_x() > kStick) buttons |= kPadRight;
  ignored_buttons_ &= buttons;
  const bool down = (buttons & mask) && !(ignored_buttons_ & mask);
  const bool was = (last_buttons_ & mask) != 0;
  auto& repeat = repeat_at_[std::countr_zero(mask) & 15];
  int presses = 0;
  const bool repeats = mask & (kPadUp | kPadDown | kPadLeft | kPadRight);
  if (down && !was) {
    presses = 1;
    repeat = now + std::chrono::milliseconds(350);
  } else if (down && repeats && now >= repeat) {
    presses = 1;
    repeat = now + std::chrono::milliseconds(90);
  }
  last_buttons_ = uint16_t(down ? last_buttons_ | mask : last_buttons_ & ~mask);
  return presses;
}

void FM4SettingsOverlay::OnDraw(ImGuiIO& io) {
  const auto now = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(now - rate_time_).count();
  if (seconds >= 0.5 && frames_) {
    const uint64_t frames = frames_();
    fps_ = double(frames - rate_frames_) / seconds;
    rate_frames_ = frames;
    rate_time_ = now;
  }
  const float s = Scale();
  const std::vector<const Row*> rows = PageRows(page_);
  const int count = int(rows.size());
  focus_ = std::clamp(focus_, 0, std::max(count - 1, 0));

  // Input: the pad (as the guest reads it), the keyboard and the mouse. Every pad button is read
  // each frame, so that its presses stay in step.
  const bool close = PadPresses(kPadB, now) + ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  int page_step = PadPresses(kPadRightShoulder, now) - PadPresses(kPadLeftShoulder, now);
  if (ImGui::IsKeyPressed(ImGuiKey_E, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) ++page_step;
  if (ImGui::IsKeyPressed(ImGuiKey_Q, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) --page_step;
  int move = PadPresses(kPadDown, now) - PadPresses(kPadUp, now);
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++move;
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) --move;
  int change = PadPresses(kPadRight, now) - PadPresses(kPadLeft, now);
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) ++change;
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) --change;
  const bool next = PadPresses(kPadA, now) + ImGui::IsKeyPressed(ImGuiKey_Enter, false) +
                    ImGui::IsKeyPressed(ImGuiKey_Space, false);
  if (close) {
    on_close_();
    return;
  }
  if (page_step) {
    SetPage(Page((int(page_) + page_step % kPageCount + kPageCount) % kPageCount));
    scroll_to_focus_ = true;
    return;
  }
  if (move && count) {
    focus_ = std::clamp(focus_ + move, 0, count - 1);
    scroll_to_focus_ = true;
  }
  if (count && (change || next)) Change(*rows[size_t(focus_)], change ? change : 1, !change);

  // The panel: centred, most of the screen on a small one.
  const ImVec2 display = io.DisplaySize;
  const ImVec2 size(std::min(display.x - 24.0f, 820.0f * s), std::min(display.y - 24.0f, 600.0f * s));
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, kBackground);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, kText);
  ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
  ImGui::PushStyleColor(ImGuiCol_Header, kAccentDark);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kField);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, kAccentDark);
  ImGui::PushStyleColor(ImGuiCol_Button, kField);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBorder);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentDark);
  ImGui::PushStyleColor(ImGuiCol_Separator, kBorder);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, kBorder);
  constexpr int kColors = 13;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * s, 18 * s));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * s, 6 * s));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * s, 4 * s));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * s);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * s);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10 * s);
  constexpr int kVars = 7;
  const float body_size = 18.0f * s, small_size = 15.0f * s, heading_size = 24.0f * s;
  ImGui::PushFont(fonts_ ? fonts_->body : nullptr, body_size);
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
  if (ImGui::Begin("##fm4_settings", nullptr, kFlags)) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // The title and the pages, with the bumpers' hints.
    ImGui::PushFont(fonts_ ? fonts_->heading : nullptr, heading_size);
    ImGui::TextColored(kAccent, "MOTORSPORT DX");
    ImGui::SameLine();
    ImGui::TextUnformatted("SETTINGS");
    ImGui::PopFont();
    ImGui::PushFont(fonts_ ? fonts_->heading : nullptr, body_size);
    for (int i = 0; i < kPageCount; ++i) {
      if (i) ImGui::SameLine(0, 18 * s);
      const bool current = i == page_;
      ImGui::PushStyleColor(ImGuiCol_Text, current ? kText : kMuted);
      const ImVec2 at = ImGui::GetCursorScreenPos();
      if (ImGui::Selectable(kPageNames[i], false, 0, ImGui::CalcTextSize(kPageNames[i]))) {
        SetPage(Page(i));
        scroll_to_focus_ = true;
      }
      if (current) {
        const float width = ImGui::CalcTextSize(kPageNames[i]).x;
        const float y = at.y + ImGui::GetTextLineHeight() + 3 * s;
        draw->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + width, y + 3 * s), ImGui::GetColorU32(kAccent));
      }
      ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    ImGui::PushFont(nullptr, small_size);
    ImGui::SameLine();
    const char* bumpers = "LB / RB   Q / E";
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(right - ImGui::CalcTextSize(bumpers).x);
    ImGui::TextColored(kMuted, "%s", bumpers);
    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::TextColored(state_->read_only() ? kWarning : kMuted, "%s", state_->status().c_str());
    ImGui::PopFont();
    ImGui::Separator();

    const float footer = ImGui::GetTextLineHeightWithSpacing() * 2.2f;
    ImGui::BeginChild("##rows", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
    if (page_ == kInfo) {
      for (const std::string& line : info_lines_) ImGui::TextUnformatted(line.c_str());
      ImGui::Separator();
      ImGui::Text("Window %ux%u, game %.1f fps", window_ ? window_->GetActualPhysicalWidth() : 0u,
                  window_ ? window_->GetActualPhysicalHeight() : 0u, fps_);
    } else if (rows.empty()) {
      ImGui::TextColored(kMuted, "No settings on this page.");
    }
    std::string group;
    const float row_height = body_size + 14 * s;
    const float value_x = ImGui::GetContentRegionAvail().x * 0.5f;
    for (int i = 0; i < count; ++i) {
      const Row& row = *rows[size_t(i)];
      if (row.group != group) {
        group = row.group;
        ImGui::Dummy(ImVec2(0, 4 * s));
        ImGui::PushFont(fonts_ ? fonts_->heading : nullptr, small_size);
        ImGui::TextColored(kAccent, "%s", group.c_str());
        ImGui::PopFont();
      }
      ImGui::PushID(i);
      const bool focused = i == focus_;
      const bool editable = state_->Editable(row);
      const std::string value = state_->Value(row);
      const ImVec2 at = ImGui::GetCursorScreenPos();
      if (ImGui::Selectable("##row", focused, 0, ImVec2(0, row_height))) focus_ = i;
      if (focused) {
        draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(at, ImVec2(at.x + 4 * s, at.y + row_height), ImGui::GetColorU32(kAccent));
        if (scroll_to_focus_) {
          ImGui::SetScrollHereY(0.5f);
          scroll_to_focus_ = false;
        }
      }
      const float text_y = at.y + (row_height - body_size) * 0.5f;
      draw = ImGui::GetWindowDrawList();
      draw->AddText(ImVec2(at.x + 14 * s, text_y), ImGui::GetColorU32(editable ? kText : kMuted), row.label.c_str());
      // The value between its arrows, then the class.
      std::string shown = value.empty() && row.cvar.empty() && row.param.empty() ? "-" : row.Name(value);
      const std::string arrows = editable ? "<   " + shown + "   >" : shown;
      draw->AddText(ImVec2(at.x + value_x, text_y), ImGui::GetColorU32(focused ? kText : kMuted), arrows.c_str());
      if (editable && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float x = io.MousePos.x - (at.x + value_x);
        if (x >= 0 && x < body_size) Change(row, -1, false);
        const float right = ImGui::CalcTextSize(arrows.c_str()).x;
        if (x > right - body_size && x < right) Change(row, 1, false);
      }
      const bool live = state_->LiveNow(row);
      const char* badge = state_->read_only() ? "read-only" : live ? "now" : "next start";
      const ImVec4 badge_color = state_->read_only() ? kMuted : live ? kSuccess : kWarning;
      ImGui::PushFont(nullptr, small_size);
      const ImVec2 badge_size = ImGui::CalcTextSize(badge);
      const float badge_x = at.x + ImGui::GetContentRegionAvail().x - badge_size.x - 10 * s;
      const float badge_y = at.y + (row_height - small_size) * 0.5f;
      draw->AddRect(ImVec2(badge_x - 6 * s, badge_y - 2 * s), ImVec2(badge_x + badge_size.x + 6 * s, badge_y + small_size + 2 * s),
                    ImGui::GetColorU32(badge_color), 4 * s);
      draw->AddText(ImVec2(badge_x, badge_y), ImGui::GetColorU32(badge_color), badge);
      ImGui::PopFont();
      ImGui::PopID();
    }
    ImGui::EndChild();
    // The focused row's note, and the controls.
    ImGui::Separator();
    ImGui::PushFont(nullptr, small_size);
    std::string note;
    if (count) {
      const Row& row = *rows[size_t(focus_)];
      note = state_->Note(row);
      if (note.empty() && !state_->Editable(row) && !state_->read_only())
        note = "Changes from the launcher only in this run.";
    }
    ImGui::TextColored(kWarning, "%s", note.c_str());
    ImGui::TextColored(kMuted, "D-pad or arrows: choose and change   A or Enter: next   B or Esc: close   "
                               "View + Menu or F2: open and close");
    ImGui::PopFont();
  }
  ImGui::End();
  ImGui::PopFont();
  ImGui::PopStyleVar(kVars);
  ImGui::PopStyleColor(kColors);
}

bool FM4CaptureWindow(rex::ui::Window* window, const std::string& path) {
  HWND hwnd = window ? static_cast<HWND>(window->GetNativeWindowHandle()) : nullptr;
  RECT rect;
  if (!hwnd || !GetClientRect(hwnd, &rect)) return false;
  const int width = rect.right - rect.left, height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) return false;
  HDC screen = GetDC(hwnd);
  HDC memory = CreateCompatibleDC(screen);
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = height;  // bottom-up, as BMP files store rows
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 24;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  bool ok = false;
  if (bitmap && bits) {
    HGDIOBJ old = SelectObject(memory, bitmap);
    // PW_CLIENTONLY | PW_RENDERFULLCONTENT: the client area as composed, flip model included.
    ok = PrintWindow(hwnd, memory, 0x1 | 0x2) != 0;
    SelectObject(memory, old);
    GdiFlush();
  }
  if (ok) {
    const DWORD stride = DWORD((width * 3 + 3) & ~3);
    BITMAPFILEHEADER header = {};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + stride * DWORD(height);
    std::ofstream file(Utf8Path(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(static_cast<const char*>(bits), std::streamsize(stride) * height);
    ok = bool(file);
  }
  if (bitmap) DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(hwnd, screen);
  return ok;
}
