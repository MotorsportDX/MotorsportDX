// fm4_rex - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <chrono>
#include <cstdlib>
#include <functional>
#include "fm4-frame-capture.h"
#include "fm4-diagnostic-state.h"
#include "fm4-info-overlay.h"
#include "fm4-settings-overlay.h"
#include "fm4-test-input.h"
#include <rex/input/input_system.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <string>
#include <string_view>
#include <thread>

void FM4InstallCrashDiagnostics();
void FM4InstallContent(rex::Runtime*);
void FM4MountCache(rex::Runtime*);
void FM4MountRenderOverrides(rex::Runtime*);
void FM4MountTrackSettings(rex::Runtime*);
void FM4InstallVblankRate(rex::Runtime*);
void FM4InstallCommandLine();
void FM4InstallInputRanges();

class Fm4RexApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fm4RexApp>(new Fm4RexApp(ctx, "fm4_rex",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin = "xenos";
  }

  void OnPostLaunchModule(rex::system::XThread*) override {
    const char* directory = std::getenv("FM4_CAPTURE_DIR");
    if (!directory || !runtime()->graphics_system()) return;
    auto* presenter = runtime()->graphics_system()->presenter();
    if (!presenter) return;
    capture_thread_ = std::jthread(FM4CaptureFrames, presenter, std::filesystem::path(directory), runtime());
  }

  void OnPostSetup() override {
    // The GPU plugin has registered its flags by this point, and guest
    // execution has not started. FM4's one-shot photo draws must wait for
    // shaders rather than being skipped while compilation is pending.
    // Preserve explicit config, environment and command-line overrides.
    constexpr auto shader_flag = "async_shader_compilation";
    if (rex::cvar::GetFlagInfo(shader_flag) &&
        rex::cvar::GetFlagSource(shader_flag) == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName(shader_flag, "false");
    }
    std::fprintf(stderr, "FM4 shader policy: async_shader_compilation=%s\n",
                 rex::cvar::GetFlagByName(shader_flag).c_str());
    // The window shows only the project's name, without the runtime's build stamp; the runtime
    // version stays in the log. GetName() stays fm4_rex, the name of the config file, the log
    // and the user folder.
    if (auto* game_window = window()) game_window->SetTitle("Motorsport DX");
    FM4InstallCrashDiagnostics();
    FM4MountCache(runtime());
    FM4MountRenderOverrides(runtime());
    FM4MountTrackSettings(runtime());
    FM4InstallVblankRate(runtime());
    FM4InstallCommandLine();
    FM4InstallContent(runtime());
    FM4InstallInputRanges();
    const char* input_file = std::getenv("FM4_TEST_INPUT_FILE");
    if (input_file && *input_file && runtime()->input_system()) {
      auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system());
      input->AddDriver(std::make_unique<FM4TestInput>(input_file));
    }
    // The settings overlay's pad (fm4-settings-overlay.h): the View and Menu chord, its
    // navigation, and a neutral pad for the guest while it is open.
    if (auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system())) {
      settings_pad_ = std::make_unique<FM4SettingsPad>([this] {
        if (auto* game_window = window())
          game_window->app_context().CallInUIThread([this] { ToggleSettings(); });
      });
      using Gamepad = rex::input::X_INPUT_GAMEPAD;
      input->SetGuestPadFilter([pad = settings_pad_.get()](uint32_t user, Gamepad& gamepad) {
        uint16_t buttons = gamepad.buttons;
        uint8_t left = gamepad.left_trigger, right = gamepad.right_trigger;
        int16_t lx = gamepad.thumb_lx, ly = gamepad.thumb_ly, rx = gamepad.thumb_rx, ry = gamepad.thumb_ry;
        pad->Filter(user, buttons, left, right, lx, ly, rx, ry);
        gamepad.buttons = buttons;
        gamepad.left_trigger = left;
        gamepad.right_trigger = right;
        gamepad.thumb_lx = lx;
        gamepad.thumb_ly = ly;
        gamepad.thumb_rx = rx;
        gamepad.thumb_ry = ry;
      });
    }
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override { FM4AddSettingsFonts(atlas, settings_fonts_); }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    // The info panel (fm4-info-overlay.h): shown at start with FM4_INFO_SHOW=1, without input.
    drawer_ = drawer;
    info_lines_ = FM4InfoLines();
    const char* show = std::getenv("FM4_INFO_SHOW");
    if (show && *show == '1' && !info_lines_.empty()) {
      info_overlay_ =
          std::make_unique<FM4InfoOverlay>(drawer, window(), guest_frames(), info_lines_);
    }
    // The settings menu (fm4-settings-overlay.h), whose last page is the info text: F2, or View
    // and Menu held on a pad.
    settings_state_ = std::make_unique<FM4SettingsState>();
    rex::ui::RegisterBind("bind_fm4_info", "F2", "Open and close the settings menu",
                          [this] { ToggleSettings(); });
    const double test = rex::cvar::Query<double>("fm4_settings_overlay_test");
    if (test > 0) settings_test_ = std::jthread([this, test](std::stop_token stop) { SettingsTest(stop, test); });
  }

  void ToggleSettings() {
    if (settings_overlay_) {
      CloseSettings();
      return;
    }
    if (!drawer_ || !settings_state_) return;
    info_overlay_.reset();
    settings_overlay_ = std::make_unique<FM4SettingsOverlay>(
        drawer_, window(), settings_state_.get(), settings_pad_.get(), &settings_fonts_, guest_frames(),
        info_lines_, [this] {
          // From the menu's own draw: closed after it.
          if (auto* game_window = window())
            game_window->app_context().CallInUIThreadDeferred([this] { CloseSettings(); });
        });
    SetGuestInputHeld(true);
  }

  void CloseSettings() {
    settings_overlay_.reset();
    SetGuestInputHeld(false);
  }

  void SetGuestInputHeld(bool held) {
    if (settings_pad_) settings_pad_->SetOpen(held);
    if (auto* input = static_cast<rex::input::InputSystem*>(runtime() ? runtime()->input_system() : nullptr))
      input->SetGuestKeystrokesBlocked(held);
  }

  // fm4_settings_overlay_test (the e2e check of docs/analysis/ingame-settings.md 5): that many
  // seconds into the first race, open the menu, show each page (a screenshot of each with
  // fm4_settings_overlay_shot), change a live control, a live look param and two restart rows
  // through the menu's own path, and close it.
  void SettingsTest(std::stop_token stop, double seconds) {
    const auto wait = [&stop](double duration) {
      const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(duration);
      while (!stop.stop_requested() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      return !stop.stop_requested();
    };
    const auto ui = [this](std::function<void()> work) {
      if (auto* game_window = window()) game_window->app_context().CallInUIThread(std::move(work));
    };
    while (fm4_diagnostic_race_update.load(std::memory_order_relaxed) == 0)
      if (!wait(0.1)) return;
    if (!wait(seconds)) return;
    REXLOG_INFO("FM4 settings overlay test: open");
    ui([this] {
      if (!settings_overlay_) ToggleSettings();
    });
    const std::string shot = rex::cvar::GetFlagByName("fm4_settings_overlay_shot");
    const char* const pages[] = {"controls", "look", "display", "info"};
    for (int page = 0; page < FM4SettingsOverlay::kPageCount; ++page) {
      ui([this, page] {
        if (settings_overlay_) settings_overlay_->SetPage(FM4SettingsOverlay::Page(page));
      });
      if (!wait(page ? 0.8 : 1.5)) return;
      if (!shot.empty()) {
        const std::string path = shot + "-" + pages[page] + ".bmp";
        REXLOG_INFO("FM4 settings overlay test: screenshot {} {}", path,
                    FM4CaptureWindow(window(), path) ? "written" : "failed");
      }
    }
    ui([this] {
      const auto set = [this](const char* option, const char* knob, const char* value) {
        const fm4settings::Row* row = settings_state_->Find(option, knob);
        const bool changed = row && settings_state_->Set(*row, value);
        REXLOG_INFO("FM4 settings overlay test: {} {}={} {}", option, knob, value,
                    changed ? "set" : "not set");
      };
      set("pad", "rumble", "0.5");
      set("look", "sharpen", "0.1");
      set("quality", "", "ultra");
      set("look", "preset", "gt7");
      REXLOG_INFO("FM4 settings overlay test: pad_rumble_strength={}",
                  rex::cvar::GetFlagByName("pad_rumble_strength"));
      if (settings_overlay_) settings_overlay_->SetPage(FM4SettingsOverlay::kLook);
    });
    if (!wait(0.8)) return;
    if (!shot.empty()) FM4CaptureWindow(window(), shot + "-look-changed.bmp");
    rex::cvar::InvokeCommand("shader_mod_set", "");
    ui([this] {
      CloseSettings();
      REXLOG_INFO("FM4 settings overlay test: closed");
    });
  }

  // The guest frames so far, for the settings overlay's frame rate (0 before the presenter).
  std::function<uint64_t()> guest_frames() const {
    return [this]() -> uint64_t {
      auto* graphics = runtime() ? runtime()->graphics_system() : nullptr;
      auto* presenter = graphics ? graphics->presenter() : nullptr;
      return presenter ? presenter->GetGuestOutputRefreshCount() : 0;
    };
  }

  void OnShutdown() override {
    // Before the runtime destroys the ImGui drawer.
    settings_test_.request_stop();
    if (settings_test_.joinable()) settings_test_.join();
    rex::ui::UnregisterBind("bind_fm4_info");
    info_overlay_.reset();
    settings_overlay_.reset();
    capture_thread_.request_stop();
    if (capture_thread_.joinable()) capture_thread_.join();
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
 private:
  std::jthread capture_thread_;
  std::vector<std::string> info_lines_;
  std::unique_ptr<FM4InfoOverlay> info_overlay_;
  rex::ui::ImGuiDrawer* drawer_ = nullptr;
  FM4SettingsFonts settings_fonts_;
  std::unique_ptr<FM4SettingsState> settings_state_;
  std::unique_ptr<FM4SettingsPad> settings_pad_;
  std::unique_ptr<FM4SettingsOverlay> settings_overlay_;
  std::jthread settings_test_;
};
