// Motorsport DX launcher: a window to pick the run options of Build.ps1 -Action run, the Discs and the
// downloadable content, and to start the game without Python, Visual Studio or the repository
// (docs/running.md#gui-launcher). The launch rules are those of pipeline.py (plan.cpp).
#include <windows.h>
#include <d3d11.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_dx11.h"
#include "misc/cpp/imgui_stdlib.h"

#include "discs.h"
#include "runner.h"
#include "settings.h"

namespace fm4 {
int RunTests(const std::filesystem::path& cases, const std::filesystem::path& result);
}

namespace {

namespace fs = std::filesystem;
using namespace fm4;

// Colors of the dark theme.
const ImVec4 kBackground = ImVec4(0.110f, 0.110f, 0.102f, 1.0f);
const ImVec4 kPanel = ImVec4(0.137f, 0.137f, 0.129f, 1.0f);
const ImVec4 kField = ImVec4(0.165f, 0.165f, 0.153f, 1.0f);
const ImVec4 kBorder = ImVec4(0.267f, 0.267f, 0.255f, 1.0f);
const ImVec4 kText = ImVec4(0.910f, 0.902f, 0.875f, 1.0f);
const ImVec4 kMuted = ImVec4(0.533f, 0.529f, 0.502f, 1.0f);
const ImVec4 kSecondary = ImVec4(0.663f, 0.655f, 0.624f, 1.0f);
const ImVec4 kAccent = ImVec4(0.216f, 0.541f, 0.867f, 1.0f);
const ImVec4 kAccentDark = ImVec4(0.047f, 0.267f, 0.486f, 1.0f);
const ImVec4 kSuccess = ImVec4(0.592f, 0.769f, 0.349f, 1.0f);
const ImVec4 kDanger = ImVec4(0.941f, 0.584f, 0.584f, 1.0f);
const ImVec4 kDangerBackground = ImVec4(0.314f, 0.075f, 0.075f, 1.0f);
const ImVec4 kWarning = ImVec4(0.937f, 0.624f, 0.153f, 1.0f);

struct Knob {
  const char* name;
  const char* label;
  float min, max;
  float step;
};

struct KnobGroup {
  const char* title;
  std::vector<Knob> knobs;
};

const std::vector<KnobGroup> kKnobGroups = {
    {"Picture",
     {{"exposure", "Exposure", -2, 2, 0.01f},
      {"contrast", "Contrast", -1, 1, 0.01f},
      {"saturation", "Saturation", 0, 2, 0.01f},
      {"bloom", "Bloom", 0, 2, 0.01f},
      {"sharpen", "Sharpen", 0, 1.5f, 0.01f},
      {"vignette", "Vignette", 0, 2, 0.01f},
      {"motion_blur", "Motion blur", 0, 2, 0.01f}}},
    {"Lighting",
     {{"sun", "Sun glare", 0, 4, 0.01f},
      {"sky", "Sky brightness", 0, 2, 0.01f},
      {"ao", "Ambient occlusion", 0, 1.5f, 0.01f},
      {"cockpit_fill", "Cockpit fill", 0, 2, 0.01f},
      {"cockpit_light", "Cockpit sun floor", 0, 1, 0.01f},
      {"windshield", "Cockpit glass", 0, 2, 0.01f}}},
    {"Car",
     {{"clearcoat", "Clear coat", 0, 2, 0.01f},
      {"clearcoat_roughness", "Clear coat roughness", 0.02f, 1, 0.01f},
      {"clearcoat_glint", "Highlight limit", 0, 8, 0.01f},
      {"flakes", "Paint flakes", 0, 2, 0.01f},
      {"flake_scale", "Flake scale", 100, 4000, 10},
      {"glass", "Glass", 0, 2, 0.01f},
      {"glass_roughness", "Glass roughness", 0.01f, 0.5f, 0.01f},
      {"chrome", "Chrome", 0, 2, 0.01f},
      {"chrome_roughness", "Chrome roughness", 0.01f, 0.5f, 0.01f},
      {"rims", "Rims", 0, 2, 0.01f},
      {"rims_roughness", "Rim roughness", 0.02f, 1, 0.01f}}},
    {"Sky and foliage",
     {{"sky_saturation", "Sky saturation", 0, 2, 0.01f},
      {"sky_contrast", "Sky contrast", 0, 1, 0.01f},
      {"sky_warmth", "Sky warmth", -1, 1, 0.01f},
      {"sky_horizon", "Sky horizon", 0, 1, 0.01f},
      {"foliage_hue", "Foliage hue", -90, 90, 1},
      {"foliage_saturation", "Foliage saturation", 0, 2, 0.01f},
      {"foliage_brightness", "Foliage brightness", 0, 2, 0.01f}}},
    {"Road",
     {{"road_brightness", "Road brightness", 0, 2, 0.01f},
      {"road_contrast", "Road contrast", -1, 1, 0.01f},
      {"road_specular", "Game highlight", 0, 2, 0.01f},
      {"road_sheen", "Sheen", 0, 1, 0.01f},
      {"road_roughness", "Sheen roughness", 0.05f, 1, 0.01f},
      {"road_sheen_grain", "Sheen grain", 0, 3, 0.01f},
      {"road_gloss", "Road gloss", 0, 2, 0.01f}}},
};

// The defaults of the knobs that are not shader defines (launcher.json has the others).
const std::map<std::string, std::string> kOtherKnobDefaults = {
    {"sky", "1"}, {"road_gloss", "1"}, {"taa", "0"}, {"aa", "taa"}, {"windshield", "0"}};

struct Bind {
  const char* cvar;
  const char* label;
  const char* runtime_default;
};

// By their function in FM4's DEFAULT LAYOUT 1 (docs/analysis/keyboard-controls.md); the keys
// press the pad input in brackets, so another game layout moves the functions.
const std::vector<Bind> kBinds = {
    {"keybind_right_trigger", "Gas (right trigger)", "E,O"},
    {"keybind_left_trigger", "Brake (left trigger)", "Q,I"},
    {"keybind_lstick_left", "Steer left (left stick)", "A"},
    {"keybind_lstick_right", "Steer right (left stick)", "D"},
    {"keybind_precision", "Precision, held", ""},
    {"keybind_b", "Shift up, menu back (B)", "Quote,Backspace"},
    {"keybind_x", "Shift down (X)", "L"},
    {"keybind_a", "Handbrake, menu select (A)", "Semicolon,Space"},
    {"keybind_left_shoulder", "Clutch (LB)", "1"},
    {"keybind_right_shoulder", "Look back (RB)", "3"},
    {"keybind_y", "Rewind (Y)", "P"},
    {"keybind_back", "Camera (Back)", "Z,Tab"},
    {"keybind_start", "Pause (Start)", "X,Return"},
    {"keybind_dpad_up", "Menu up, telemetry (D-pad)", "Shift+Up"},
    {"keybind_dpad_down", "Menu down, telemetry (D-pad)", "Shift+Down"},
    {"keybind_dpad_left", "Menu left, telemetry (D-pad)", "Shift+Left"},
    {"keybind_dpad_right", "Menu right, telemetry (D-pad)", "Shift+Right"},
    {"keybind_rstick_up", "Free look up (right stick)", "Up"},
    {"keybind_rstick_down", "Free look down (right stick)", "Down"},
    {"keybind_rstick_left", "Free look left (right stick)", "Left"},
    {"keybind_rstick_right", "Free look right (right stick)", "Right"},
    {"keybind_rstick_press", "Heat damage (right stick press)", "K"},
    {"keybind_lstick_press", "Left stick press", "F"},
    {"keybind_lstick_up", "Left stick up", "W"},
    {"keybind_lstick_down", "Left stick down", "S"},
    {"keybind_guide", "Guide", ""},
};

// The key names of the runtime's bindings (work/runtime/src/ui/keybinds.cpp).
const std::set<std::string> kKeyNames = {
    "Backtick", "Minus", "Plus", "Comma", "Period", "Semicolon", "Slash", "Backslash", "LBracket",
    "RBracket", "Quote", "Escape", "Return", "Space", "Tab", "Backspace", "Delete", "Insert",
    "Home", "End", "PageUp", "PageDown", "Left", "Right", "Up", "Down", "Shift", "Control", "Alt",
    "NumpadEnter", "NumpadPlus", "NumpadMinus", "NumpadStar", "NumpadSlash", "PrintScreen",
    "Pause", "CapsLock", "NumLock", "ScrollLock", "LMB", "RMB", "MMB"};

bool KnownKey(const std::string& name) {
  if (kKeyNames.contains(name)) return true;
  if (name.size() == 1 && (std::isupper(static_cast<unsigned char>(name[0])) ||
                           std::isdigit(static_cast<unsigned char>(name[0]))))
    return true;
  if (name.size() >= 2 && name[0] == 'F') {
    const int number = std::atoi(name.c_str() + 1);
    return number >= 1 && number <= 24 && std::to_string(number) == name.substr(1);
  }
  return name.size() == 7 && name.starts_with("Numpad") && std::isdigit(static_cast<unsigned char>(name[6]));
}

// Why a binding value does not parse, or empty.
std::string BindError(const std::string& value) {
  size_t start = 0;
  while (start <= value.size()) {
    const size_t comma = value.find(',', start);
    std::string token = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    token.erase(0, token.find_first_not_of(' '));
    token.erase(token.find_last_not_of(' ') + 1);
    if (!token.empty()) {
      size_t part_start = 0;
      while (true) {
        const size_t plus = token.find('+', part_start);
        const std::string part = token.substr(part_start, plus == std::string::npos ? std::string::npos : plus - part_start);
        if (!KnownKey(part)) return "Unknown key '" + part + "'";
        if (plus == std::string::npos) break;
        part_start = plus + 1;
      }
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return {};
}

std::string ScancodeName(SDL_Scancode code) {
  if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) return std::string(1, char('A' + (code - SDL_SCANCODE_A)));
  if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) return std::string(1, char('1' + (code - SDL_SCANCODE_1)));
  if (code == SDL_SCANCODE_0) return "0";
  if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) return "F" + std::to_string(1 + code - SDL_SCANCODE_F1);
  if (code >= SDL_SCANCODE_F13 && code <= SDL_SCANCODE_F24) return "F" + std::to_string(13 + code - SDL_SCANCODE_F13);
  if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9) return "Numpad" + std::to_string(1 + code - SDL_SCANCODE_KP_1);
  static const std::map<SDL_Scancode, const char*> names = {
      {SDL_SCANCODE_KP_0, "Numpad0"}, {SDL_SCANCODE_RETURN, "Return"}, {SDL_SCANCODE_BACKSPACE, "Backspace"},
      {SDL_SCANCODE_TAB, "Tab"}, {SDL_SCANCODE_SPACE, "Space"}, {SDL_SCANCODE_MINUS, "Minus"},
      {SDL_SCANCODE_EQUALS, "Plus"}, {SDL_SCANCODE_LEFTBRACKET, "LBracket"},
      {SDL_SCANCODE_RIGHTBRACKET, "RBracket"}, {SDL_SCANCODE_BACKSLASH, "Backslash"},
      {SDL_SCANCODE_SEMICOLON, "Semicolon"}, {SDL_SCANCODE_APOSTROPHE, "Quote"},
      {SDL_SCANCODE_GRAVE, "Backtick"}, {SDL_SCANCODE_COMMA, "Comma"}, {SDL_SCANCODE_PERIOD, "Period"},
      {SDL_SCANCODE_SLASH, "Slash"}, {SDL_SCANCODE_CAPSLOCK, "CapsLock"},
      {SDL_SCANCODE_PRINTSCREEN, "PrintScreen"}, {SDL_SCANCODE_SCROLLLOCK, "ScrollLock"},
      {SDL_SCANCODE_PAUSE, "Pause"}, {SDL_SCANCODE_INSERT, "Insert"}, {SDL_SCANCODE_HOME, "Home"},
      {SDL_SCANCODE_PAGEUP, "PageUp"}, {SDL_SCANCODE_DELETE, "Delete"}, {SDL_SCANCODE_END, "End"},
      {SDL_SCANCODE_PAGEDOWN, "PageDown"}, {SDL_SCANCODE_RIGHT, "Right"}, {SDL_SCANCODE_LEFT, "Left"},
      {SDL_SCANCODE_DOWN, "Down"}, {SDL_SCANCODE_UP, "Up"}, {SDL_SCANCODE_NUMLOCKCLEAR, "NumLock"},
      {SDL_SCANCODE_KP_DIVIDE, "NumpadSlash"}, {SDL_SCANCODE_KP_MULTIPLY, "NumpadStar"},
      {SDL_SCANCODE_KP_MINUS, "NumpadMinus"}, {SDL_SCANCODE_KP_PLUS, "NumpadPlus"},
      {SDL_SCANCODE_KP_ENTER, "NumpadEnter"}, {SDL_SCANCODE_LSHIFT, "Shift"},
      {SDL_SCANCODE_RSHIFT, "Shift"}, {SDL_SCANCODE_LCTRL, "Control"}, {SDL_SCANCODE_RCTRL, "Control"},
      {SDL_SCANCODE_LALT, "Alt"}, {SDL_SCANCODE_RALT, "Alt"}};
  const auto found = names.find(code);
  return found == names.end() ? "" : found->second;
}

std::string FormatKnob(double value, float step) {
  char text[32];
  if (step >= 1) {
    std::snprintf(text, sizeof(text), "%.0f", std::round(value / step) * step);
    return text;
  }
  std::snprintf(text, sizeof(text), "%.2f", value);
  std::string result = text;
  result.erase(result.find_last_not_of('0') + 1);
  if (result.back() == '.') result.pop_back();
  if (result == "-0") result = "0";
  return result;
}

std::string FormatSize(std::uintmax_t bytes) {
  char text[32];
  if (bytes >= (1ull << 30))
    std::snprintf(text, sizeof(text), "%.1f GB", bytes / double(1ull << 30));
  else
    std::snprintf(text, sizeof(text), "%.0f MB", bytes / double(1ull << 20));
  return text;
}

std::string FormatDuration(double seconds) {
  const int total = static_cast<int>(seconds);
  if (total < 60) return std::to_string(total) + " s";
  if (total < 3600) return std::to_string(total / 60) + " min " + std::to_string(total % 60) + " s";
  return std::to_string(total / 3600) + " h " + std::to_string(total / 60 % 60) + " min";
}

// A look value split into its presets and its knobs (later knobs win).
struct LookParts {
  std::string base;
  Pairs knobs;

  static LookParts Parse(const std::string& look) {
    LookParts parts;
    size_t start = 0;
    while (start <= look.size()) {
      const size_t comma = look.find(',', start);
      std::string item = look.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      item.erase(0, item.find_first_not_of(" \t"));
      item.erase(item.find_last_not_of(" \t") + 1);
      if (!item.empty()) {
        const size_t equals = item.find('=');
        if (equals == std::string::npos)
          parts.base += (parts.base.empty() ? "" : ",") + item;
        else
          parts.knobs.Set(item.substr(0, equals), item.substr(equals + 1));
      }
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    return parts;
  }

  std::string Value() const {
    std::string value = base;
    for (const auto& [name, knob] : knobs.Items()) value += (value.empty() ? "" : ",") + name + "=" + knob;
    return value;
  }
};

enum DialogTarget { kDisc1, kDisc2, kContentFolder, kContentFiles, kTextureReplace, kTextureDump, kUserData };

class App;
struct DialogRequest {
  App* app;
  DialogTarget target;
};

class App {
 public:
  bool Init(int argc, char** argv);
  void Run();

 private:
  void LoadFonts(float scale);
  void ApplyTheme(float scale);
  void Frame();
  void Header();
  void Banner();
  void Navigation();
  void DisplayPage();
  void LookPage();
  void ControlsPage();
  void ControllerSection();
  void KeyboardSection();
  void GameDataPage();
  void AdvancedPage();
  void AboutPage();
  void Footer();
  void Play();
  void Refresh(bool discs);
  std::optional<Plan> CurrentPlan(std::string& error);
  std::optional<Plan> PlanOf(const Options& options, std::string& error);
  std::vector<std::string> GameArgs(const Plan& plan);
  fs::path UserData() const;
  bool Imported(const Package& package) const;
  void RemoveImport(const Package& package);
  void SaveSettings();
  void OpenDialog(DialogTarget target, bool folder);
  static void DialogDone(void* userdata, const char* const* files, int filter);
  void ApplyDialogs();
  std::string BindValue(const Bind& bind) const;
  bool PathRow(const char* label, const char* id, std::string& value, DialogTarget target,
               bool clearable);
  void Label(const char* text, const char* help = nullptr);
  bool Segment(const char* id, int& value, const std::vector<const char*>& labels);
  void Muted(const std::string& text);
  void Colored(const ImVec4& color, const std::string& text);

  fs::path root_;
  LauncherData data_;
  Settings settings_;
  fs::path settings_path_;
  Monitor monitor_;
  std::string load_error_;
  std::string build_date_;
  DiscCheck disc1_, disc2_;
  ContentCheck content_;
  std::string checked_disc1_ = "\x01";
  std::vector<bool> imported_;
  bool artifacts_checked_ = false;

  SDL_Window* window_ = nullptr;
  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
  IDXGISwapChain* swap_chain_ = nullptr;
  ID3D11RenderTargetView* target_ = nullptr;
  bool CreateDevice(HWND window);
  void CreateTarget();
  void ReleaseTarget();
  Uint32 game_event_ = 0;
  float scale_ = 1;
  ImFont* body_font_ = nullptr;
  ImFont* heading_font_ = nullptr;
  int page_ = 0;
  std::atomic<bool> running_ = false;
  std::thread waiter_;
  std::chrono::steady_clock::time_point started_;
  std::string banner_;
  bool banner_error_ = false;
  std::string capture_;  // the binding waiting for a key
  std::string new_name_;
  std::string look_edit_;
  bool look_editing_ = false;
  std::string pad_edit_;
  bool pad_editing_ = false;
  std::string keyboard_edit_;
  bool keyboard_editing_ = false;
  std::optional<Package> remove_candidate_;
  std::mutex dialog_mutex_;
  std::vector<std::pair<DialogTarget, std::vector<std::string>>> dialog_results_;
  bool quit_ = false;
};

bool App::Init(int argc, char** argv) {
  wchar_t module[MAX_PATH * 4];
  GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
  const fs::path directory = fs::path(module).parent_path();
  // An export keeps the launcher beside game-build; a BuildRoot keeps it in game-build.
  root_ = fs::exists(directory / "game-build" / "fm4_rex.exe") ? directory : directory.parent_path();
  if (argc >= 4 && std::string(argv[1]) == "--test") {
    std::exit(RunTests(PathFromUtf8(argv[2]), PathFromUtf8(argv[3])));
  }
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  monitor_ = PrimaryMonitor();
  try {
    data_ = LauncherData::Load(root_ / "launcher.json");
  } catch (const std::exception& error) {
    load_error_ = std::string(error.what()) + ". Run Build.ps1 -Action build or -Action export again.";
  }
  if (!fs::exists(root_ / "game-build" / "fm4_rex.exe"))
    load_error_ = "No game-build\\fm4_rex.exe beside the launcher. Keep fm4_launcher.exe in its export folder.";
  settings_path_ = root_ / "launcher-settings.json";
  settings_ = Settings::Load(settings_path_);
  if (settings_.disc1.empty() && fs::exists(root_ / "local.json")) {
    // A BuildRoot: the Discs and content that generate saved.
    try {
      std::ifstream stream(root_ / "local.json", std::ios::binary);
      const Json local = Json::parse(stream);
      settings_.disc1 = local.value("disc1", "");
      if (local.contains("disc2") && local.at("disc2").is_string()) settings_.disc2 = local.at("disc2");
      if (local.contains("content"))
        for (const auto& item : local.at("content")) settings_.content.push_back(item.get<std::string>());
    } catch (const std::exception&) {
    }
  }
  std::error_code error;
  const auto time = fs::last_write_time(root_ / "build-result.json", error);
  if (!error) {
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    const std::time_t stamp = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
    localtime_s(&local, &stamp);
    char text[64];
    std::strftime(text, sizeof(text), "%d %B %Y", &local);
    build_date_ = text[0] == '0' ? text + 1 : text;
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return false;
  game_event_ = SDL_RegisterEvents(1);
  scale_ = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (scale_ <= 0) scale_ = 1;
  window_ = SDL_CreateWindow("Motorsport DX", static_cast<int>(980 * scale_), static_cast<int>(700 * scale_),
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window_) return false;
  SDL_SetWindowMinimumSize(window_, static_cast<int>(760 * scale_), static_cast<int>(520 * scale_));
  const HWND hwnd = static_cast<HWND>(
      SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
  if (!CreateDevice(hwnd)) return SDL_SetError("Direct3D 11 is not available"), false;
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window_);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  io.IniFilename = nullptr;
  ApplyTheme(scale_);
  LoadFonts(scale_);
  ImGui_ImplSDL3_InitForD3D(window_);
  ImGui_ImplDX11_Init(device_, context_);
  if (load_error_.empty()) Refresh(true);
  // --play: press Play at start with the active profile, for a desktop shortcut.
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--play" && load_error_.empty()) Play();
  return true;
}

bool App::CreateDevice(HWND window) {
  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferCount = 2;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.OutputWindow = window;
  desc.SampleDesc.Count = 1;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL level;
  HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                                 D3D11_SDK_VERSION, &desc, &swap_chain_, &device_, &level, &context_);
  if (result == DXGI_ERROR_UNSUPPORTED)
    result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &desc, &swap_chain_, &device_, &level, &context_);
  if (result != S_OK) return false;
  CreateTarget();
  return true;
}

void App::CreateTarget() {
  ID3D11Texture2D* buffer = nullptr;
  swap_chain_->GetBuffer(0, IID_PPV_ARGS(&buffer));
  device_->CreateRenderTargetView(buffer, nullptr, &target_);
  buffer->Release();
}

void App::ReleaseTarget() {
  if (target_) target_->Release();
  target_ = nullptr;
}

void App::LoadFonts(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  wchar_t windows[MAX_PATH];
  GetWindowsDirectoryW(windows, MAX_PATH);
  const fs::path fonts = fs::path(windows) / "Fonts";
  const auto load = [&](const wchar_t* name, float size) -> ImFont* {
    const fs::path path = fonts / name;
    if (!fs::exists(path)) return nullptr;
    return io.Fonts->AddFontFromFileTTF(Utf8(path).c_str(), size);
  };
  body_font_ = load(L"segoeui.ttf", 17);
  heading_font_ = load(L"seguisb.ttf", 17);
  if (!body_font_) body_font_ = io.Fonts->AddFontDefault();
  if (!heading_font_) heading_font_ = body_font_;
  io.FontDefault = body_font_;
  ImGui::GetStyle().FontScaleDpi = scale;
}

void App::ApplyTheme(float scale) {
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = ImVec2(16, 14);
  style.FramePadding = ImVec2(10, 6);
  style.ItemSpacing = ImVec2(10, 8);
  style.ItemInnerSpacing = ImVec2(8, 6);
  style.ScrollbarSize = 12;
  style.GrabMinSize = 12;
  style.WindowRounding = 0;
  style.ChildRounding = 8;
  style.FrameRounding = 6;
  style.PopupRounding = 8;
  style.GrabRounding = 6;
  style.ScrollbarRounding = 6;
  style.TabRounding = 6;
  style.WindowBorderSize = 0;
  style.ChildBorderSize = 1;
  style.FrameBorderSize = 1;
  style.PopupBorderSize = 1;
  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kMuted;
  c[ImGuiCol_WindowBg] = kBackground;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = kPanel;
  c[ImGuiCol_Border] = kBorder;
  c[ImGuiCol_FrameBg] = kField;
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.204f, 0.204f, 0.192f, 1);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.235f, 0.235f, 0.220f, 1);
  c[ImGuiCol_TitleBg] = kPanel;
  c[ImGuiCol_TitleBgActive] = kPanel;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = kBorder;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = ImVec4(0.522f, 0.718f, 0.922f, 1);
  c[ImGuiCol_Button] = kField;
  c[ImGuiCol_ButtonHovered] = ImVec4(0.220f, 0.220f, 0.204f, 1);
  c[ImGuiCol_ButtonActive] = ImVec4(0.267f, 0.267f, 0.251f, 1);
  c[ImGuiCol_Header] = kAccentDark;
  c[ImGuiCol_HeaderHovered] = ImVec4(0.094f, 0.373f, 0.647f, 1);
  c[ImGuiCol_HeaderActive] = ImVec4(0.094f, 0.373f, 0.647f, 1);
  c[ImGuiCol_Separator] = kBorder;
  c[ImGuiCol_NavCursor] = kAccent;
  c[ImGuiCol_TableHeaderBg] = kPanel;
  c[ImGuiCol_TableBorderStrong] = kBorder;
  c[ImGuiCol_TableBorderLight] = ImVec4(0.2f, 0.2f, 0.19f, 1);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
  style.ScaleAllSizes(scale);
}

void App::Run() {
  while (!quit_) {
    SDL_Event event;
    // While the game runs the window is hidden: wait for events instead of drawing.
    if (running_ ? SDL_WaitEventTimeout(&event, 200) : SDL_PollEvent(&event)) {
      do {
        if (event.type == game_event_) {
          const unsigned long code = static_cast<unsigned long>(event.user.code);
          const double seconds =
              std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
          if (waiter_.joinable()) waiter_.join();
          running_ = false;
          if (code != 0) {
            banner_ = "The game exited with code " + std::to_string(code) + " after " +
                      FormatDuration(seconds) + ".";
            banner_error_ = true;
          } else {
            banner_.clear();
          }
          Refresh(false);
          SDL_ShowWindow(window_);
          SDL_RaiseWindow(window_);
          continue;
        }
        if (!capture_.empty() && event.type == SDL_EVENT_KEY_DOWN) {
          const SDL_Scancode code = event.key.scancode;
          if (code == SDL_SCANCODE_ESCAPE) {
            capture_.clear();
          } else {
            const std::string name = ScancodeName(code);
            if (!name.empty()) {
              std::string value = name;
              const bool modifier = name == "Shift" || name == "Control" || name == "Alt";
              if (!modifier) {
                if (event.key.mod & SDL_KMOD_ALT) value = "Alt+" + value;
                if (event.key.mod & SDL_KMOD_CTRL) value = "Control+" + value;
                if (event.key.mod & SDL_KMOD_SHIFT) value = "Shift+" + value;
              }
              settings_.binds.Set(capture_, value);
              capture_.clear();
            }
          }
          continue;
        }
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_WINDOW_RESIZED && event.window.windowID == SDL_GetWindowID(window_)) {
          ReleaseTarget();
          swap_chain_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
          CreateTarget();
        }
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window_)))
          quit_ = !running_;
      } while (!running_ && SDL_PollEvent(&event));
    }
    if (running_ || (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED)) {
      if (!running_) SDL_Delay(10);
      continue;
    }
    ApplyDialogs();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    Frame();
    ImGui::Render();
    const float clear[4] = {kBackground.x, kBackground.y, kBackground.z, 1};
    context_->OMSetRenderTargets(1, &target_, nullptr);
    context_->ClearRenderTargetView(target_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swap_chain_->Present(1, 0);
  }
  if (load_error_.empty()) SaveSettings();
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  ReleaseTarget();
  if (swap_chain_) swap_chain_->Release();
  if (context_) context_->Release();
  if (device_) device_->Release();
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

void App::Refresh(bool discs) {
  if (discs || checked_disc1_ != settings_.disc1) {
    disc1_ = CheckDisc1(data_, settings_.disc1);
    checked_disc1_ = settings_.disc1;
  }
  disc2_ = CheckDisc2(data_, settings_.disc2);
  content_ = disc2_.error.empty() ? CheckContent(data_, settings_.disc2, settings_.content) : ContentCheck{};
  imported_.clear();
  for (const auto& package : content_.packages) imported_.push_back(Imported(package));
}

fs::path App::UserData() const {
  return settings_.user_data.empty() ? root_ / "user-data" : PathFromUtf8(settings_.user_data);
}

// The marker of fm4-content-installer.cpp: the package size, and the package's directory.
bool App::Imported(const Package& package) const {
  const fs::path name = package.path.filename();
  std::ifstream marker(UserData() / fs::path(name).concat(L".import-complete"));
  std::uintmax_t size = 0;
  if (!(marker >> size) || size != package.size) return false;
  return fs::is_directory(UserData() / "0000000000000000" / "4D530910" / "00000002" / name);
}

void App::RemoveImport(const Package& package) {
  const fs::path name = package.path.filename();
  const fs::path title = UserData() / "0000000000000000" / "4D530910";
  std::error_code error;
  fs::remove_all(title / "00000002" / name, error);
  fs::remove(UserData() / fs::path(name).concat(L".import-complete"), error);
  fs::remove(title / "Headers" / "00000002" / fs::path(name).concat(L".header"), error);
  Refresh(false);
}

void App::SaveSettings() {
  try {
    settings_.Save(settings_path_);
  } catch (const std::exception& error) {
    banner_ = error.what();
    banner_error_ = true;
  }
}

std::optional<Plan> App::CurrentPlan(std::string& error) {
  return PlanOf(settings_.Active().options, error);
}

std::optional<Plan> App::PlanOf(const Options& options, std::string& error) {
  try {
    const std::string manifest = content_.packages.empty() ? "" : Utf8(root_ / "content-packages.txt");
    return LaunchPlan(data_, Utf8(root_), disc1_.path.empty() ? settings_.disc1 : disc1_.path,
                      options, monitor_, manifest);
  } catch (const std::exception& exception) {
    error = exception.what();
    return std::nullopt;
  }
}

std::string App::BindValue(const Bind& bind) const {
  if (const std::string* value = settings_.binds.Find(bind.cvar)) return *value;
  if (data_.json.at("cvars").contains(bind.cvar)) return data_.json.at("cvars").at(bind.cvar).get<std::string>();
  return bind.runtime_default;
}

std::vector<std::string> App::GameArgs(const Plan& plan) {
  Pairs cvars = plan.cvars;
  if (!settings_.user_data.empty()) cvars.Set("user_data_root", settings_.user_data);
  for (const auto& [name, value] : settings_.binds.Items()) cvars.Set(name, value);
  if (settings_.mouse_right_stick) cvars.Set("mnk_mouse", "true");
  // Extra cvars, last: --name=value (or name=value) separated by spaces.
  std::istringstream extra(settings_.Active().options.extra_cvars);
  for (std::string item; extra >> item;) {
    if (item.starts_with("--")) item = item.substr(2);
    const size_t equals = item.find('=');
    if (equals != std::string::npos && equals > 0) cvars.Set(item.substr(0, equals), item.substr(equals + 1));
  }
  std::vector<std::string> args = {Utf8(root_ / "game-build" / "fm4_rex.exe")};
  for (const auto& [name, value] : cvars.Items()) args.push_back("--" + name + "=" + value);
  return args;
}

void App::Play() {
  Refresh(false);
  const auto fail = [&](const std::string& message) {
    banner_ = message;
    banner_error_ = true;
  };
  if (!disc1_.error.empty()) return fail(disc1_.error), void(page_ = 3);
  if (!disc2_.error.empty()) return fail(disc2_.error), void(page_ = 3);
  if (!content_.error.empty()) return fail(content_.error), void(page_ = 3);
  for (const auto& bind : kBinds) {
    const std::string error = BindError(BindValue(bind));
    if (!error.empty()) return fail(std::string(bind.label) + ": " + error + "."), void(page_ = 2);
  }
  std::string error;
  const auto plan = CurrentPlan(error);
  if (!plan) return fail(error);
  if (ProcessRunning(L"fm4_rex.exe"))
    return fail("The game is already running (fm4_rex.exe). Close it first.");
  try {
    if (!artifacts_checked_) {
      std::ifstream stream(root_ / "build-result.json", std::ios::binary);
      const Json report = Json::parse(stream);
      for (const auto& [name, hash] : report.at("artifacts").items())
        if (Sha256File(root_ / "game-build" / name) != hash.get<std::string>())
          return fail("Build artifact changed: " + name + ". Build or export again.");
      artifacts_checked_ = true;
    }
    if (!content_.packages.empty()) {
      std::ofstream manifest(root_ / "content-packages.txt", std::ios::binary | std::ios::trunc);
      for (const auto& package : content_.packages) manifest << Utf8(package.path) << '\n';
    }
    SaveSettings();
    Pairs env = plan->env;
    if (settings_.Active().options.perf_trace) {
      // The performance trace: one JSON line a second (docs/e2e/traces.md).
      const std::time_t now = std::time(nullptr);
      std::tm local{};
      localtime_s(&local, &now);
      char stamp[32];
      std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
      std::string profile;
      for (const char c : settings_.Active().name) profile += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
      fs::create_directories(root_ / "logs");
      env.Set("REX_PERF_TRACE_FILE", Utf8(root_ / "logs" / ("perf-" + profile + "-" + stamp + ".jsonl")));
    }
    void* process = StartProcess(GameArgs(*plan), env, root_, root_ / "logs" / "play.log");
    banner_.clear();
    running_ = true;
    started_ = std::chrono::steady_clock::now();
    SDL_HideWindow(window_);
    waiter_ = std::thread([this, process] {
      const unsigned long code = WaitProcess(process);
      SDL_Event event{};
      event.type = game_event_;
      event.user.code = static_cast<Sint32>(code);
      SDL_PushEvent(&event);
    });
  } catch (const std::exception& exception) {
    fail(exception.what());
  }
}

void App::OpenDialog(DialogTarget target, bool folder) {
  auto* request = new DialogRequest{this, target};
  if (folder) {
    SDL_ShowOpenFolderDialog(&App::DialogDone, request, window_, nullptr, false);
  } else {
    SDL_ShowOpenFileDialog(&App::DialogDone, request, window_, nullptr, 0, nullptr, true);
  }
}

void App::DialogDone(void* userdata, const char* const* files, int) {
  auto* request = static_cast<DialogRequest*>(userdata);
  if (files && files[0]) {
    std::vector<std::string> paths;
    for (const char* const* file = files; *file; ++file) paths.emplace_back(*file);
    std::lock_guard lock(request->app->dialog_mutex_);
    request->app->dialog_results_.emplace_back(request->target, std::move(paths));
  }
  delete request;
}

void App::ApplyDialogs() {
  std::vector<std::pair<DialogTarget, std::vector<std::string>>> results;
  {
    std::lock_guard lock(dialog_mutex_);
    results.swap(dialog_results_);
  }
  for (auto& [target, paths] : results) {
    Options& options = settings_.Active().options;
    switch (target) {
      case kDisc1: settings_.disc1 = paths.front(); break;
      case kDisc2: settings_.disc2 = paths.front(); break;
      case kContentFolder:
      case kContentFiles:
        for (const auto& path : paths)
          if (std::find(settings_.content.begin(), settings_.content.end(), path) == settings_.content.end())
            settings_.content.push_back(path);
        break;
      case kTextureReplace: options.texture_replace = paths.front(); break;
      case kTextureDump: options.texture_dump = paths.front(); break;
      case kUserData: settings_.user_data = paths.front(); break;
    }
    Refresh(false);
  }
}

void App::Label(const char* text, const char* help) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(kSecondary, "%s", text);
  if (help && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help);
  ImGui::SameLine(190 * scale_);
}

bool App::Segment(const char* id, int& value, const std::vector<const char*>& labels) {
  bool changed = false;
  ImGui::PushID(id);
  for (size_t i = 0; i < labels.size(); ++i) {
    if (i) ImGui::SameLine(0, 0);
    const bool selected = value == static_cast<int>(i);
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? kAccentDark : kField);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? kAccentDark : ImVec4(0.22f, 0.22f, 0.2f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? ImVec4(0.902f, 0.945f, 0.984f, 1) : kSecondary);
    if (ImGui::Button(labels[i])) {
      changed = value != static_cast<int>(i);
      value = static_cast<int>(i);
    }
    ImGui::PopStyleColor(3);
  }
  ImGui::PopID();
  return changed;
}

void App::Muted(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

void App::Colored(const ImVec4& color, const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

bool App::PathRow(const char* label, const char* id, std::string& value, DialogTarget target,
                  bool clearable) {
  Label(label);
  ImGui::PushID(id);
  const float buttons = (clearable ? 2 : 1) * (ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2 +
                                              ImGui::GetStyle().ItemSpacing.x);
  ImGui::SetNextItemWidth(-buttons);
  ImGui::InputText("##path", &value);
  const bool changed = ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  if (ImGui::Button("Browse")) OpenDialog(target, true);
  bool cleared = false;
  if (clearable) {
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      value.clear();
      cleared = true;
    }
  }
  ImGui::PopID();
  return changed || cleared;
}

void App::Frame() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("Motorsport DX", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  if (!load_error_.empty()) {
    ImGui::SetCursorPos(ImVec2(24 * scale_, 24 * scale_));
    ImGui::PushTextWrapPos(viewport->WorkSize.x - 24 * scale_);
    Colored(kDanger, load_error_);
    ImGui::PopTextWrapPos();
    ImGui::End();
    return;
  }
  Header();
  Banner();
  const float footer = ImGui::GetFrameHeight() + 28 * scale_;
  const float body = ImGui::GetContentRegionAvail().y - footer;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
  ImGui::BeginChild("navigation", ImVec2(180 * scale_, body), ImGuiChildFlags_AlwaysUseWindowPadding);
  Navigation();
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::SameLine(0, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * scale_, 16 * scale_));
  ImGui::BeginChild("page", ImVec2(0, body), ImGuiChildFlags_AlwaysUseWindowPadding);
  switch (page_) {
    case 0: DisplayPage(); break;
    case 1: LookPage(); break;
    case 2: ControlsPage(); break;
    case 3: GameDataPage(); break;
    case 4: AdvancedPage(); break;
    default: AboutPage(); break;
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  Footer();
  ImGui::End();
}

void App::Header() {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float height = 52 * scale_;
  const float width = ImGui::GetContentRegionAvail().x;
  draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), ImGui::GetColorU32(kPanel));
  draw->AddLine(ImVec2(origin.x, origin.y + height - 1), ImVec2(origin.x + width, origin.y + height - 1),
                ImGui::GetColorU32(kBorder));
  ImGui::SetCursorScreenPos(ImVec2(origin.x + 18 * scale_, origin.y + (height - ImGui::GetFontSize() * 1.3f) / 2));
  ImGui::PushFont(heading_font_, 22);
  ImGui::TextUnformatted("Motorsport DX");
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3 * scale_);
  std::string subtitle = disc1_.release.empty() ? "Forza Motorsport 4" : disc1_.release + " release";
  if (!build_date_.empty()) subtitle += "  \xC2\xB7  build " + build_date_;
  ImGui::TextColored(kMuted, "%s", subtitle.c_str());
  const std::string status = disc1_.error.empty() ? "Disc 1 found" : "Disc 1 needed";
  const float status_width = ImGui::CalcTextSize(status.c_str()).x;
  ImGui::SameLine(width - status_width - 18 * scale_);
  ImGui::TextColored(disc1_.error.empty() ? kSuccess : kWarning, "%s", status.c_str());
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
}

void App::Banner() {
  if (banner_.empty()) return;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = ImGui::GetFrameHeight() + 14 * scale_;
  draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(banner_error_ ? kDangerBackground : kAccentDark));
  ImGui::SetCursorScreenPos(ImVec2(origin.x + 18 * scale_, origin.y + 7 * scale_));
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(banner_error_ ? kDanger : kText, "%s", banner_.c_str());
  ImGui::SameLine();
  const float buttons = ImGui::CalcTextSize("Open game log").x + ImGui::CalcTextSize("Open runtime log").x +
                        ImGui::CalcTextSize("Dismiss").x + 6 * ImGui::GetStyle().FramePadding.x +
                        2 * ImGui::GetStyle().ItemSpacing.x + 18 * scale_;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), width - buttons));
  if (ImGui::Button("Open game log")) OpenPath(root_ / "logs" / "play.log");
  ImGui::SameLine();
  if (ImGui::Button("Open runtime log")) OpenPath(root_ / "logs" / "runtime.log");
  ImGui::SameLine();
  if (ImGui::Button("Dismiss")) banner_.clear();
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
}

void App::Navigation() {
  static const char* pages[] = {"Display", "Look", "Controls", "Game data", "Advanced", "About"};
  ImGui::Dummy(ImVec2(0, 2 * scale_));
  for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(pages)); ++i) {
    const bool selected = page_ == i;
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? ImVec4(0.902f, 0.945f, 0.984f, 1) : kSecondary);
    if (ImGui::Selectable(pages[i], selected, 0, ImVec2(0, ImGui::GetFrameHeight()))) page_ = i;
    ImGui::PopStyleColor();
  }
}

void App::DisplayPage() {
  Options& o = settings_.Active().options;
  const char* displays[] = {"native", "720p", "1080p", "1440p", "4k"};
  const std::string native = "Native (borderless, " + std::to_string(monitor_.width) + "x" +
                             std::to_string(monitor_.height) + ")";
  const char* labels[] = {native.c_str(), "1280x720 window", "1920x1080 window", "2560x1440 window",
                          "3840x2160 window"};
  int display = 0;
  for (int i = 0; i < 5; ++i)
    if (o.display == displays[i]) display = i;
  Label("Screen");
  ImGui::SetNextItemWidth(320 * scale_);
  if (ImGui::Combo("##screen", &display, labels, 5)) o.display = display ? displays[display] : "";
  Label("Fullscreen", "A window size shown fullscreen, with its render scale. Native is always fullscreen.");
  ImGui::BeginDisabled(display == 0);
  ImGui::Checkbox("##fullscreen", &o.fullscreen);
  ImGui::EndDisabled();

  // The automatic render scale of this screen and the screen's own, as display_settings picks
  // them for these options (the native path in one pass renders at the screen's own scale).
  std::string auto_text = "1x, 1280x720", panel_text = "native path in one pass only", unused;
  Options automatic = o;
  automatic.resolution_scale = 0;
  if (const auto plan = PlanOf(automatic, unused)) auto_text = RenderScaleText(plan->cvars);
  automatic.resolution_scale = kPanelScale;
  if (const auto plan = PlanOf(automatic, unused)) panel_text = RenderScaleText(plan->cvars);
  const std::string auto_label = "Auto (" + auto_text + ")";
  const std::string panel_label = "Screen's own resolution (" + panel_text + ")";
  const char* scales[] = {auto_label.c_str(), panel_label.c_str(), "1x (1280x720)",
                          "2x (2560x1440)", "3x (3840x2160)", "4x (5120x2880)"};
  // Combo items: Auto (0), the screen's own (kPanelScale), then 1x to 4x.
  int scale_item =
      o.resolution_scale == kPanelScale ? 1 : o.resolution_scale ? o.resolution_scale + 1 : 0;
  Label("Render scale",
        "Auto covers the screen; on the native path in one pass it is the screen's own resolution "
        "(1080p renders 1920x1080), except with integrated GPUs (1x). The screen's own resolution "
        "renders the largest 16:9 picture that fits the screen, letterboxed on 16:10 until the "
        "aspect work; 1x to 4x render at that multiple of 1280x720.");
  ImGui::SetNextItemWidth(320 * scale_);
  if (ImGui::Combo("##scale", &scale_item, scales, 6))
    o.resolution_scale = scale_item == 1 ? kPanelScale : scale_item ? scale_item - 1 : 0;
  Label("Supersample", "Render one scale above the screen (up to 4x), then average it down.");
  ImGui::BeginDisabled(o.resolution_scale != 0);
  ImGui::Checkbox("##supersample", &o.supersample);
  ImGui::EndDisabled();

  // -Upscale: native (the default), quality, balanced or performance; a ratio typed for
  // Build.ps1 shows as native here until one of these is picked.
  const char* upscales[] = {"native", "quality", "balanced", "performance"};
  int upscale = 0;
  for (int i = 1; i < 4; ++i)
    if (o.upscale == upscales[i]) upscale = i;
  Label("Upscale",
        "FSR 3.1 renders the scene at fewer lines and upscales it to the render scale: Quality "
        "1.5x, Balanced 1.7x, Performance 2x fewer, at least 720p. Needs the native path in one "
        "pass and FSR 3.1 as the look's anti-aliasing method.");
  if (Segment("upscale", upscale, {"Native", "Quality", "Balanced", "Performance"}))
    o.upscale = upscale == 0 ? "" : upscales[upscale];
  if (upscale) {
    std::string upscale_error;
    ImGui::SameLine();
    if (const auto plan = PlanOf(o, upscale_error)) {
      const std::string text = UpscaleText(plan->cvars);
      ImGui::TextColored(kMuted, "%s",
                         !text.empty() ? text.c_str()
                         : plan->description.find("clamped to 720p") != std::string::npos
                             ? "clamped to 720p: renders at the render scale"
                             : "renders at the render scale");
    } else {
      ImGui::TextColored(kMuted, "%s", upscale_error.c_str());
    }
  }

  int rate = o.frame_rate ? o.frame_rate : 60;
  Label("Race frame rate", "Races, replays and the countdown. Menus, post-race and photo mode keep their own timing.");
  ImGui::SetNextItemWidth(320 * scale_);
  if (ImGui::SliderInt("##rate", &rate, 30, 240, "%d fps")) o.frame_rate = rate == 60 ? 0 : rate;

  // -VSync: auto (the default), on or off.
  const char* vsyncs[] = {"auto", "on", "off"};
  int vsync = o.vsync == "on" ? 1 : o.vsync == "off" ? 2 : 0;
  Label("VSync", "Auto: on a variable refresh rate screen, frames as they come but at most one a refresh, "
                 "races just under the refresh; otherwise On. On: on the screen's refresh, no tearing. "
                 "Off: at once, tearing allowed.");
  if (Segment("vsync", vsync, {"Auto", "On", "Off"})) o.vsync = vsync == 0 ? "" : vsyncs[vsync];

  const char* qualities[] = {"xbox", "high", "ultra"};
  int quality = 1;
  for (int i = 0; i < 3; ++i)
    if (o.quality == qualities[i]) quality = i;
  Label("Race quality");
  if (Segment("quality", quality, {"Xbox", "High", "Ultra"})) o.quality = quality == 1 ? "" : qualities[quality];
  ImGui::SameLine();
  static const char* quality_help[] = {"The game's own race settings.",
                                       "Soft particles, object shadows, full detail cars and wheels.",
                                       "High, every car at top detail, the mirror every frame."};
  ImGui::TextColored(kMuted, "%s", quality_help[quality]);

  bool taa = true;
  try {
    taa = TaaEnabled(LookKnobs(data_, o.look.empty() ? data_.Look().at("default").get<std::string>() : o.look));
  } catch (const std::exception&) {
  }
  int tiles = o.msaa ? 1 : 0;
  Label("Race rendering", "One tile: TAA anti-aliases the races. MSAA tiles: the tiles and MSAA of the quality.");
  ImGui::BeginDisabled(!taa);
  if (Segment("tiles", tiles, {"One tile with TAA", "MSAA tiles"})) o.msaa = tiles == 1;
  ImGui::EndDisabled();
  if (!taa) {
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "The look has no TAA.");
  }

  ImGui::Dummy(ImVec2(0, 6 * scale_));
  ImGui::TextColored(kMuted, "Options");
  ImGui::Checkbox("Rear-view mirror every frame", &o.full_rate_mirror);
  ImGui::Checkbox("Show the settings overlay at start (F2 in game)", &o.info);

  ImGui::Dummy(ImVec2(0, 10 * scale_));
  ImGui::Separator();
  std::string error;
  if (const auto plan = CurrentPlan(error))
    Muted(plan->description + ".");
  else
    Colored(kDanger, error);
}

void App::LookPage() {
  Options& o = settings_.Active().options;
  const std::string look = o.look.empty() ? data_.Look().at("default").get<std::string>() : o.look;
  LookParts parts = LookParts::Parse(look);
  struct Entry {
    std::string label, value;
    bool saved;
  };
  std::vector<Entry> entries = {{"aces (default)", "aces", false},
                                {"gt", "gt", false},
                                {"neutral", "neutral", false},
                                {"original", "original", false},
                                {"original, no TAA", "original,taa=0", false}};
  for (const auto& [name, value] : settings_.looks) entries.push_back({name, value, true});
  int current = -1;
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].value == look) current = static_cast<int>(i);
  std::string preview = current >= 0 ? entries[current].label
                                     : (parts.base.empty() ? "custom" : parts.base) + " (changed)";
  Label("Look");
  ImGui::SetNextItemWidth(260 * scale_);
  if (ImGui::BeginCombo("##look", preview.c_str())) {
    for (size_t i = 0; i < entries.size(); ++i) {
      if (i == 5 && entries.size() > 5) ImGui::Separator();
      if (ImGui::Selectable(entries[i].label.c_str(), current == static_cast<int>(i)))
        o.look = entries[i].value == "aces" ? "" : entries[i].value;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Save as look")) {
    new_name_.clear();
    ImGui::OpenPopup("Save look");
  }
  if (current >= 5) {
    ImGui::SameLine();
    if (ImGui::Button("Delete look")) {
      std::erase_if(settings_.looks, [&](const auto& item) { return item.first == entries[current].label; });
      o.look = "";
    }
  }
  if (!parts.knobs.Empty() && current < 0) {
    ImGui::SameLine();
    if (ImGui::Button("Reset changes")) o.look = parts.base == "aces" ? "" : parts.base;
  }
  if (ImGui::BeginPopupModal("Save look", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(280 * scale_);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##name", &new_name_, ImGuiInputTextFlags_EnterReturnsTrue);
    if ((ImGui::Button("Save") || enter) && !new_name_.empty()) {
      std::erase_if(settings_.looks, [&](const auto& item) { return item.first == new_name_; });
      settings_.looks.emplace_back(new_name_, look);
      o.look = look;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  Pairs base;
  try {
    base = LookKnobs(data_, parts.base);
  } catch (const std::exception&) {
  }
  const Json& defaults = data_.Look().at("defaults");
  const auto base_value = [&](const std::string& name) -> std::optional<std::string> {
    if (const std::string* value = base.Find(name)) return *value;
    if (defaults.contains(name)) return defaults.at(name).get<std::string>();
    if (kOtherKnobDefaults.contains(name)) return kOtherKnobDefaults.at(name);
    return std::nullopt;
  };
  const auto set_knob = [&](const std::string& name, const std::string& value) {
    const auto original = base_value(name);
    bool same = false;
    try {
      same = original && PyFloat(*original) == PyFloat(value);
    } catch (const std::exception&) {
      same = original && *original == value;
    }
    // A value equal to the preset's (or the knob's default) is no change.
    if (same)
      parts.knobs.Erase(name);
    else
      parts.knobs.Set(name, value);
    const std::string value_text = parts.Value();
    o.look = value_text == "aces" ? "" : value_text;
  };
  const auto changed_label = [&](const std::string& name, const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(parts.knobs.Has(name) ? kAccent : kSecondary, "%s", label);
    ImGui::SameLine(190 * scale_);
  };
  const auto reset_button = [&](const std::string& name) {
    if (!parts.knobs.Has(name)) return;
    ImGui::SameLine();
    ImGui::PushID(name.c_str());
    if (ImGui::SmallButton("Reset")) {
      parts.knobs.Erase(name);
      const std::string value_text = parts.Value();
      o.look = value_text == "aces" ? "" : value_text;
    }
    ImGui::PopID();
  };
  const auto effective = [&](const std::string& name) -> std::optional<std::string> {
    if (const std::string* value = parts.knobs.Find(name)) return *value;
    return base_value(name);
  };

  ImGui::Dummy(ImVec2(0, 4 * scale_));
  // Tone curve and TAA, then the sliders of each group.
  {
    const Json& tonemaps = data_.Look().at("names").at("tonemap");
    std::vector<std::string> names;
    for (const auto& [name, number] : tonemaps.items()) names.push_back(name);
    int tonemap = 0;
    const std::string value = effective("tonemap").value_or("filmic");
    for (size_t i = 0; i < names.size(); ++i)
      if (names[i] == value || PyRepr(tonemaps.at(names[i]).get<double>()) == value) tonemap = static_cast<int>(i);
    changed_label("tonemap", "Tone curve");
    std::vector<const char*> labels;
    for (const auto& name : names) labels.push_back(name.c_str());
    if (Segment("tonemap", tonemap, labels)) set_knob("tonemap", names[tonemap]);
    reset_button("tonemap");
    bool taa = false;
    try {
      taa = PyFloat(effective("taa").value_or("0")) > 0;
    } catch (const std::exception&) {
    }
    changed_label("taa", "Temporal AA");
    if (ImGui::Checkbox("##taa", &taa)) set_knob("taa", taa ? "1" : "0");
    reset_button("taa");
    // TAA or FSR 3.1's native anti-aliasing (aa=fsr, TAA in a runtime without FSR).
    if (taa) {
      int method = effective("aa").value_or("taa") == "fsr" ? 1 : 0;
      changed_label("aa", "Method");
      if (Segment("aa", method, {"TAA", "FSR 3.1"})) set_knob("aa", method ? "fsr" : "taa");
      reset_button("aa");
    }
    // The auto exposure at the game's speed above 60 fps (adaptation).
    bool adaptation = false;
    try {
      adaptation = PyFloat(effective("adaptation").value_or("0")) > 0;
    } catch (const std::exception&) {
    }
    changed_label("adaptation", "60 fps exposure speed");
    if (ImGui::Checkbox("##adaptation", &adaptation)) set_knob("adaptation", adaptation ? "1" : "0");
    reset_button("adaptation");
  }
  for (const auto& group : kKnobGroups) {
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    ImGui::PushStyleColor(ImGuiCol_Header, kField);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.22f, 0.2f, 1));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.25f, 0.24f, 1));
    const bool open = ImGui::CollapsingHeader(
        group.title, group.title == kKnobGroups.front().title ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopStyleColor(3);
    if (!open) continue;
    for (const auto& knob : group.knobs) {
      if (!data_.Look().at("knobs").contains(knob.name) && !data_.Look().at("track_knobs").contains(knob.name) &&
          !kOtherKnobDefaults.contains(knob.name) && std::string(knob.name) != "sun")
        continue;
      const auto text = effective(knob.name);
      float value = 1;
      try {
        if (text) value = static_cast<float>(PyFloat(*text));
      } catch (const std::exception&) {
      }
      changed_label(knob.name, knob.label);
      ImGui::PushID(knob.name);
      ImGui::SetNextItemWidth(300 * scale_);
      const char* format = !text ? "game" : knob.step >= 1 ? "%.0f" : "%.2f";
      if (ImGui::SliderFloat("##value", &value, knob.min, knob.max, format)) set_knob(knob.name, FormatKnob(value, knob.step));
      ImGui::PopID();
      reset_button(knob.name);
    }
  }
  ImGui::Dummy(ImVec2(0, 8 * scale_));
  ImGui::Separator();
  ImGui::TextColored(kMuted, "Look value (Build.ps1 -Look)");
  // The field keeps its own text while it is edited and applies it when it is left.
  if (!look_editing_) look_edit_ = look;
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##value", &look_edit_);
  look_editing_ = ImGui::IsItemActive();
  if (ImGui::IsItemDeactivatedAfterEdit()) o.look = look_edit_ == "aces" ? "" : look_edit_;
  try {
    Pairs knobs = LookKnobs(data_, look);
    LookDefines(data_, knobs);
  } catch (const std::exception& error) {
    Colored(kDanger, error.what());
  }
  Muted("Changes apply when the game starts: the look mods are compiled then.");
}

// The physical controller's options (-Pad, PAD_KNOBS of pipeline.py, docs/running.md#controller):
// every widget edits one knob of Options::pad, and a knob at its default is left out.
void App::ControllerSection() {
  Options& o = settings_.Active().options;
  const Json& table = data_.json.at("pad").at("knobs");
  Pairs knobs;
  std::string error;
  try {
    knobs = PadKnobs(data_, o.pad);
    PadCvars(data_, o.pad);
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  const auto fallback = [&](const char* name) { return table.at(name).at("default").get<std::string>(); };
  const auto value_of = [&](const char* name) {
    const std::string* value = knobs.Find(name);
    return value ? *value : fallback(name);
  };
  const auto set_knob = [&](const char* name, const std::string& value) {
    if (value == fallback(name))
      knobs.Erase(name);
    else
      knobs.Set(name, value);
    std::vector<std::string> items;
    for (const auto& [knob, text] : knobs.Items()) items.push_back(knob + "=" + text);
    std::string joined;
    for (const auto& item : items) joined += (joined.empty() ? "" : ",") + item;
    o.pad = joined;
  };
  const auto number = [&](const char* name) {
    try {
      return static_cast<float>(PyFloat(value_of(name)));
    } catch (const std::exception&) {
      return static_cast<float>(PyFloat(fallback(name)));
    }
  };
  const auto slider = [&](const char* name, const char* label, const char* help) {
    Label(label, help);
    float value = number(name);
    ImGui::PushID(name);
    ImGui::SetNextItemWidth(300 * scale_);
    const Json& knob = table.at(name);
    if (ImGui::SliderFloat("##value", &value, knob.at("min").get<float>(), knob.at("max").get<float>(), "%.2f")) {
      const std::string text = FormatKnob(value, 0.01f);
      set_knob(name, PyFloat(text) == PyFloat(fallback(name)) ? fallback(name) : text);
    }
    if (knobs.Has(name)) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Reset")) set_knob(name, fallback(name));
    }
    ImGui::PopID();
  };
  const auto toggle = [&](const char* name, const char* label, const char* help) {
    Label(label, help);
    bool on = value_of(name) == "1";
    ImGui::PushID(name);
    if (ImGui::Checkbox("##on", &on)) set_knob(name, on ? "1" : "0");
    ImGui::PopID();
  };

  ImGui::TextColored(kSecondary, "Controller");
  Muted("A connected controller works without setup. These options change what it sends and how it "
        "rumbles; the defaults keep it as the game sees it, apart from rumble strength 1.5 (the game's driving "
        "rumble is weak). The game ignores the first 24% of the steering stick, and its STEERING AXIS DEADZONE "
        "INSIDE setting can only raise that; for small corrections to steer, turn on the steering anti-deadzone "
        "with a stick deadzone of 0.05.");
  ImGui::Dummy(ImVec2(0, 2 * scale_));
  const char* drivers[] = {"default", "rawinput", "wgi", "gameinput"};
  int driver = 0;
  for (int i = 0; i < 4; ++i)
    if (value_of("driver") == drivers[i]) driver = i;
  Label("Pad driver",
        "Default: SDL's own (XInput for Xbox pads, HIDAPI for PlayStation pads). Raw Input: adds trigger "
        "rumble on Xbox One and Series pads (through Windows.Gaming.Input). WGI: Windows.Gaming.Input with "
        "XInput off, a fallback if Raw Input misbehaves. GameInput: Microsoft's GameInput first, for trigger "
        "rumble and the Share button where it has them (the ROG Xbox Ally X's triggers); without the GameInput "
        "runtime the others take the pads.");
  if (Segment("driver", driver, {"Default", "Raw Input", "WGI", "GameInput"})) set_knob("driver", drivers[driver]);
  slider("rumble", "Rumble strength",
         "Multiplies the game's two rumble motors (1: as the game sends them; 1.5 by default, since the game's "
         "driving rumble is only 0.05 to 0.2 of full).");
  toggle("trigger_rumble", "Trigger rumble",
         "Throttle trigger from wheelspin, brake trigger from locking: derived from the game's motors and how "
         "far each trigger is pressed. Needs a pad with trigger motors (Raw Input or WGI on Xbox pads).");
  slider("trigger_rumble_strength", "Trigger rumble strength", "Multiplies the derived trigger rumble.");
  toggle("anti_deadzone", "Steering anti-deadzone",
         "Left stick: steers from the first movement. The game ignores the first 24% of the stick and cannot be "
         "set lower; this maps the stick into the game's own deadzone as the game reports it. Pair it with a "
         "stick deadzone of 0.05 under Sticks and triggers so a resting stick does not steer.");
  toggle("thread", "Input thread",
         "Reads the pad every millisecond off the UI thread: fresher input when the UI thread is busy (240 fps "
         "races).");
  ImGui::PushStyleColor(ImGuiCol_Header, kField);
  if (ImGui::CollapsingHeader("Sticks and triggers")) {
    slider("stick_deadzone", "Stick deadzone", "Both sticks: read as centred up to this fraction (for a drifting stick).");
    slider("stick_outer", "Stick outer edge", "Both sticks: the deflection that gives full output.");
    slider("stick_curve", "Stick curve", "Both sticks: 1 is linear, above 1 finer near the centre.");
    slider("trigger_deadzone", "Trigger deadzone", "Triggers read 0 up to this fraction.");
    slider("trigger_outer", "Trigger outer edge", "The trigger travel that gives full output.");
  }
  if (ImGui::CollapsingHeader("Extra buttons")) {
    struct Extra {
      const char* name;
      const char* label;
    };
    static const Extra extras[] = {{"misc1", "Share / microphone"},   {"right_paddle1", "Paddle P1 (upper right)"},
                                   {"right_paddle2", "Paddle P2 (lower right)"}, {"left_paddle1", "Paddle P3 (upper left)"},
                                   {"left_paddle2", "Paddle P4 (lower left)"},    {"touchpad", "Touchpad click"}};
    for (const auto& extra : extras) {
      Label(extra.label, "The Xbox 360 button it presses. Guide works only with the guide_button cvar.");
      const std::string current = value_of(extra.name);
      ImGui::PushID(extra.name);
      ImGui::SetNextItemWidth(160 * scale_);
      if (ImGui::BeginCombo("##button", current.c_str())) {
        for (const auto& choice : table.at(extra.name).at("choices")) {
          const std::string text = choice.get<std::string>();
          if (ImGui::Selectable(text.c_str(), text == current)) set_knob(extra.name, text);
        }
        ImGui::EndCombo();
      }
      ImGui::PopID();
    }
    Muted("On Windows, Xbox pads report neither paddles nor Share: an Elite's own profile can map its paddles "
          "to other buttons. These apply to PlayStation and other pads that report them.");
  }
  ImGui::PopStyleColor();
  ImGui::TextColored(kMuted, "Controller value (Build.ps1 -Pad)");
  if (!pad_editing_) pad_edit_ = o.pad;
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##pad", &pad_edit_);
  pad_editing_ = ImGui::IsItemActive();
  if (ImGui::IsItemDeactivatedAfterEdit()) o.pad = pad_edit_;
  if (!error.empty()) Colored(kDanger, error);
  ImGui::Dummy(ImVec2(0, 6 * scale_));
  ImGui::Separator();
  ImGui::Dummy(ImVec2(0, 4 * scale_));
}

void App::KeyboardSection() {
  Options& o = settings_.Active().options;
  const Json& table = data_.json.at("keyboard").at("knobs");
  Pairs knobs;
  std::string error;
  try {
    knobs = KeyboardKnobs(data_, o.keyboard);
    KeyboardCvars(data_, o.keyboard);
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  const auto fallback = [&](const char* name) { return table.at(name).at("default").get<std::string>(); };
  const auto value_of = [&](const char* name) {
    const std::string* value = knobs.Find(name);
    return value ? *value : fallback(name);
  };
  const auto set_knob = [&](const char* name, const std::string& value) {
    if (value == fallback(name))
      knobs.Erase(name);
    else
      knobs.Set(name, value);
    std::string joined;
    for (const auto& [knob, text] : knobs.Items()) joined += (joined.empty() ? "" : ",") + knob + "=" + text;
    o.keyboard = joined;
  };
  const auto number = [&](const char* name) {
    try {
      return static_cast<float>(PyFloat(value_of(name)));
    } catch (const std::exception&) {
      return static_cast<float>(PyFloat(fallback(name)));
    }
  };
  const auto slider = [&](const char* name, const char* label, const char* help) {
    Label(label, help);
    float value = number(name);
    ImGui::PushID(name);
    ImGui::SetNextItemWidth(300 * scale_);
    const Json& knob = table.at(name);
    if (ImGui::SliderFloat("##value", &value, knob.at("min").get<float>(), knob.at("max").get<float>(), "%.2f")) {
      const std::string text = FormatKnob(value, 0.01f);
      set_knob(name, PyFloat(text) == PyFloat(fallback(name)) ? fallback(name) : text);
    }
    if (knobs.Has(name)) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Reset")) set_knob(name, fallback(name));
    }
    ImGui::PopID();
  };
  const auto toggle = [&](const char* name, const char* label, const char* help) {
    Label(label, help);
    bool on = value_of(name) == "1";
    ImGui::PushID(name);
    if (ImGui::Checkbox("##on", &on)) set_knob(name, on ? "1" : "0");
    ImGui::PopID();
  };

  ImGui::TextColored(kSecondary, "Keyboard");
  Muted("The keys move the steering, throttle and brake at limited rates instead of jumping, mapped into the "
        "game's own deadzones so they act from the first press. Hold the precision key (Ctrl) for half lock "
        "and half pedal.");
  ImGui::Dummy(ImVec2(0, 2 * scale_));
  toggle("smooth", "Smooth steering and pedals",
         "Off: the keys are digital, full lock and full pedal at once (as before).");
  slider("steer_attack", "Steering attack (s)", "Seconds from centre to full lock while a steering key is held.");
  slider("steer_release", "Steering release (s)", "Seconds from full lock back to centre after the key is released.");
  slider("throttle_attack", "Throttle attack (s)", "Seconds from 0 to full throttle.");
  slider("throttle_release", "Throttle release (s)", "Seconds from full throttle to 0.");
  slider("brake_attack", "Brake attack (s)", "Seconds from 0 to full brake.");
  slider("brake_release", "Brake release (s)", "Seconds from full brake to 0.");
  slider("precision", "Precision share", "Share of full lock and full pedal while the precision key is held.");
  ImGui::PushStyleColor(ImGuiCol_Header, kField);
  if (ImGui::CollapsingHeader("Mouse steering and deadzones")) {
    toggle("mouse_steering", "Mouse steering",
           "The mouse's horizontal motion steers; the right stick then gets no mouse motion.");
    slider("mouse_steer_sensitivity", "Mouse steering sensitivity",
           "Thousandths of full lock per count of mouse motion.");
    slider("mouse_steer_centering", "Mouse steering centring (s)",
           "Time constant of the return to centre when the mouse rests; 0 turns it off.");
    toggle("title_deadzones", "Into the game's deadzones",
           "Off: the keys' values go to the game as they are, and its deadzones apply on top (for comparisons).");
  }
  ImGui::PopStyleColor();
  ImGui::TextColored(kMuted, "Keyboard value (Build.ps1 -Keyboard)");
  if (!keyboard_editing_) keyboard_edit_ = o.keyboard;
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##keyboard", &keyboard_edit_);
  keyboard_editing_ = ImGui::IsItemActive();
  if (ImGui::IsItemDeactivatedAfterEdit()) o.keyboard = keyboard_edit_;
  if (!error.empty()) Colored(kDanger, error);
  ImGui::Dummy(ImVec2(0, 4 * scale_));
}

void App::ControlsPage() {
  ControllerSection();
  KeyboardSection();
  Muted("Keyboard bindings, by their function in the game's DEFAULT LAYOUT 1 (the pad input in brackets). "
        "Several keys are separated by commas, modifiers are written as Shift+Up. Ctrl is the precision key "
        "and also works while the other keys are held.");
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  if (ImGui::BeginTable("binds", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 190 * scale_);
    ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 150 * scale_);
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1, 1, 1, 0.02f));
    for (const auto& bind : kBinds) {
      ImGui::TableNextRow();
      ImGui::PushID(bind.cvar);
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(settings_.binds.Has(bind.cvar) ? kAccent : kSecondary, "%s", bind.label);
      ImGui::TableNextColumn();
      if (capture_ == bind.cvar) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kWarning, "Press a key (Esc cancels)");
      } else {
        std::string value = BindValue(bind);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##keys", &value)) settings_.binds.Set(bind.cvar, value);
        const std::string error = BindError(value);
        if (!error.empty()) ImGui::TextColored(kDanger, "%s", error.c_str());
      }
      ImGui::TableNextColumn();
      if (ImGui::Button("Set")) capture_ = bind.cvar;
      if (settings_.binds.Has(bind.cvar)) {
        ImGui::SameLine();
        if (ImGui::Button("Reset")) settings_.binds.Erase(bind.cvar);
      }
      ImGui::PopID();
    }
    ImGui::PopStyleColor();
    ImGui::EndTable();
  }
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  ImGui::Checkbox("Mouse moves the right stick", &settings_.mouse_right_stick);
}

void App::GameDataPage() {
  if (PathRow("Disc 1", "disc1", settings_.disc1, kDisc1, false)) Refresh(false);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 190 * scale_);
  if (disc1_.error.empty())
    ImGui::TextColored(kSuccess, "%s release, the three executables verified", disc1_.release.c_str());
  else {
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(kDanger, "%s", disc1_.error.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  if (PathRow("Disc 2", "disc2", settings_.disc2, kDisc2, true)) Refresh(false);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 190 * scale_);
  if (settings_.disc2.empty())
    ImGui::TextColored(kMuted, "Not used: the cars of Disc 2 are not imported");
  else if (disc2_.error.empty())
    ImGui::TextColored(kSuccess, "The four content packages found");
  else {
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(kDanger, "%s", disc2_.error.c_str());
    ImGui::PopTextWrapPos();
  }

  ImGui::Dummy(ImVec2(0, 4 * scale_));
  Label("Downloadable content", "Content packages or folders searched for them, such as the console's Content folder.");
  if (ImGui::Button("Add folder")) OpenDialog(kContentFolder, true);
  ImGui::SameLine();
  if (ImGui::Button("Add files")) OpenDialog(kContentFiles, false);
  for (size_t i = 0; i < settings_.content.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 190 * scale_);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(settings_.content[i].c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      settings_.content.erase(settings_.content.begin() + static_cast<long>(i));
      Refresh(false);
      ImGui::PopID();
      break;
    }
    ImGui::PopID();
  }
  if (!content_.error.empty()) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 190 * scale_);
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(kDanger, "%s", content_.error.c_str());
    ImGui::PopTextWrapPos();
  }

  ImGui::Dummy(ImVec2(0, 8 * scale_));
  ImGui::TextColored(kMuted, "Content packages");
  std::uintmax_t pending = 0;
  if (content_.packages.empty()) {
    Muted("None. The base game runs without them.");
  } else if (ImGui::BeginTable("packages", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("Package", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80 * scale_);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 250 * scale_);
    ImGui::TableHeadersRow();
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1, 1, 1, 0.02f));
    for (size_t i = 0; i < content_.packages.size(); ++i) {
      const Package& package = content_.packages[i];
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(Utf8(package.path.filename()).c_str());
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Utf8(package.path).c_str());
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(kSecondary, "%s", FormatSize(package.size).c_str());
      ImGui::TableNextColumn();
      if (i < imported_.size() && imported_[i]) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kSuccess, "Imported");
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove import")) {
          remove_candidate_ = package;
          ImGui::OpenPopup("Remove import");
        }
      } else {
        pending += package.size;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kWarning, "Imports on the next Play");
      }
      if (ImGui::BeginPopupModal("Remove import", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Remove the imported %s from the user data?", Utf8(remove_candidate_->path.filename()).c_str());
        Muted("The package file itself stays. It imports again on the next Play while it is listed.");
        if (ImGui::Button("Remove")) {
          RemoveImport(*remove_candidate_);
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ImGui::PopStyleColor();
    ImGui::EndTable();
  }
  if (pending) {
    std::error_code error;
    fs::path drive = UserData();
    while (!drive.empty() && !fs::exists(drive)) drive = drive.parent_path();
    const auto space = fs::space(drive.empty() ? root_ : drive, error);
    Muted("The first Play imports " + FormatSize(pending) + " into the user data" +
          (error ? "." : " (" + FormatSize(space.available) + " free)."));
  }
  if (!content_.skipped.empty() && ImGui::TreeNode("skipped", "Skipped files (%d)", static_cast<int>(content_.skipped.size()))) {
    for (const auto& line : content_.skipped) Muted(line);
    ImGui::TreePop();
  }

  ImGui::Dummy(ImVec2(0, 8 * scale_));
  Options& o = settings_.Active().options;
  PathRow("Texture pack", "replace", o.texture_replace, kTextureReplace, true);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 190 * scale_);
  ImGui::TextColored(kMuted, "A folder of <hash>.dds files that replace the game textures");
}

void App::AdvancedPage() {
  Options& o = settings_.Active().options;
  ImGui::Checkbox("Fixed 60 Hz vblank (hold every swap to the vblank grid)", &o.fixed_vblank);
  ImGui::Checkbox("Shadow workaround (shadow map only with the cockpit camera)", &o.shadow_workaround);
  ImGui::Checkbox("One tile without MSAA also with a look without TAA (then FXAA)", &o.single_tile);
  ImGui::Checkbox("Ambient occlusion pass also without the look's ao knob", &o.ao);
  ImGui::Checkbox("ROV render path (the uncorrected reference, slower)", &o.rov);
  // The render path: -1 automatic (the native path with integrated GPUs only, else RTV), 0 RTV
  // (-Rtv), 1 the native path (-Native).
  const char* path_labels[] = {monitor_.integrated_gpu ? "Automatic (the native path: integrated GPU)"
                                                       : "Automatic (RTV)",
                               "RTV (the PM4 path on host render targets)",
                               "Native path (races in one pass with MSAA, the native port's stages)"};
  int path_choice = o.native < 0 ? 0 : o.native == 0 ? 1 : 2;
  ImGui::SetNextItemWidth(420 * scale_);
  if (ImGui::Combo("Render path", &path_choice, path_labels, IM_ARRAYSIZE(path_labels)))
    o.native = path_choice == 0 ? -1 : path_choice == 1 ? 0 : 1;
  const bool native = o.native < 0 ? monitor_.integrated_gpu && !o.rov && !o.msaa : o.native == 1;
  ImGui::BeginDisabled(!native);
  // -MsaaSamples: -1 (unset) is one pass with 2x, the default.
  const char* msaa_labels[] = {"One pass with 2x MSAA (default)", "One pass with 4x MSAA",
                               "One tile without MSAA"};
  int msaa_choice = o.msaa_samples == 4 ? 1 : o.msaa_samples == 0 ? 2 : 0;
  ImGui::SetNextItemWidth(320 * scale_);
  if (ImGui::Combo("Native MSAA", &msaa_choice, msaa_labels, IM_ARRAYSIZE(msaa_labels)))
    o.msaa_samples = msaa_choice == 1 ? 4 : msaa_choice == 2 ? 0 : -1;
  ImGui::EndDisabled();
  ImGui::Checkbox("Performance trace (logs\\perf-<profile>-<time>.jsonl)", &o.perf_trace);
  ImGui::SetNextItemWidth(420 * scale_);
  ImGui::InputTextWithHint("Extra cvars", "--name=value --name=value", &o.extra_cvars);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 190 * scale_);
  ImGui::TextColored(kMuted, "Applied last, for A/B runs (docs/running.md, Ally X run set)");
  ImGui::Dummy(ImVec2(0, 6 * scale_));
  PathRow("Texture dump", "dump", o.texture_dump, kTextureDump, true);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 190 * scale_);
  ImGui::TextColored(kMuted, "Writes each new game texture once as <hash>.dds");
  if (PathRow("User data", "userdata", settings_.user_data, kUserData, true)) Refresh(false);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 190 * scale_);
  ImGui::TextColored(kMuted, "%s", settings_.user_data.empty()
                                       ? ("Default: " + Utf8(root_ / "user-data")).c_str()
                                       : "Profile, saves and imported content. Moving it does not move them.");
  ImGui::Dummy(ImVec2(0, 10 * scale_));
  if (ImGui::Button("Open logs folder")) {
    fs::create_directories(root_ / "logs");
    OpenPath(root_ / "logs");
  }
  ImGui::SameLine();
  if (ImGui::Button("Open user data")) {
    fs::create_directories(UserData());
    OpenPath(UserData());
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy command line")) {
    std::string error;
    if (const auto plan = CurrentPlan(error)) {
      std::string text;
      for (const auto& [name, value] : plan->env.Items()) text += "set \"" + name + "=" + value + "\"\r\n";
      text += CommandLine(GameArgs(*plan)) + "\r\n";
      SDL_SetClipboardText(text.c_str());
    }
  }
}

// Who made this possible, in short; CREDITS.txt beside the launcher (outputs/FM4-build/CREDITS.txt,
// copied by build and export) says it in full, and third-party-notices holds the licenses.
void App::AboutPage() {
  ImGui::PushFont(heading_font_, 22);
  ImGui::TextUnformatted("Motorsport DX");
  ImGui::PopFont();
  ImGui::TextWrapped("Runs Forza Motorsport 4 (Xbox 360), recompiled for Windows from your own discs.");
  ImGui::Dummy(ImVec2(0, 8 * scale_));
  ImGui::PushFont(heading_font_, 17);
  ImGui::TextUnformatted("Made possible by");
  ImGui::PopFont();
  static const char* const kThanks[][2] = {
      {"The ReXGlue SDK", "Tom (crack), its founder, and the ReXGlue contributors: the recompiler and runtime"},
      {"Xenia", "Ben Vanik and the Xenia project contributors: the Xbox 360 emulation it all builds on"},
      {"Xenia Canary", "its game patches and arithmetic, which parts of the FM4 fixes follow"},
      {"XenonRecomp", "hedge-dev and contributors: the modern Xbox 360 static recompiler"},
      {"rexdex's recompiler", "the original Xbox 360 static recompiler"},
      {"XenosRecomp and Unleashed Recompiled", "hedge-dev: ideas for the shader mods and the native path"},
      {"The Xbox 360 homebrew, modding and reverse-engineering communities", "their shared research"},
      {"LLVM and Clang, Python, the Windows SDK and the DirectX compilers", "the tools that build it"},
      {"Turn 10 Studios", "who made Forza Motorsport 4"},
  };
  for (const auto& [name, what] : kThanks) {
    ImGui::Bullet();
    ImGui::TextWrapped("%s", name);
    ImGui::Indent(ImGui::GetStyle().IndentSpacing);
    Muted(what);
    ImGui::Unindent(ImGui::GetStyle().IndentSpacing);
  }
  ImGui::Dummy(ImVec2(0, 8 * scale_));
  Muted("Motorsport DX is not affiliated with or endorsed by Microsoft, Xbox Game Studios or Turn 10 "
        "Studios. Forza Motorsport and Xbox are trademarks of the Microsoft group of companies.");
  ImGui::Dummy(ImVec2(0, 8 * scale_));
  const fs::path credits = root_ / "CREDITS.txt";
  const fs::path notices = root_ / "third-party-notices";
  ImGui::BeginDisabled(!fs::exists(credits));
  if (ImGui::Button("Open credits")) OpenPath(credits);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!fs::exists(notices));
  if (ImGui::Button("Open third-party notices")) OpenPath(notices);
  ImGui::EndDisabled();
  Muted(fs::exists(notices) ? "The licenses of the libraries in the program files: " + Utf8(notices)
                            : "Run build (or export) to copy CREDITS.txt and third-party-notices here.");
}

void App::Footer() {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = ImGui::GetContentRegionAvail().y;
  draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), ImGui::GetColorU32(kPanel));
  draw->AddLine(origin, ImVec2(origin.x + width, origin.y), ImGui::GetColorU32(kBorder));
  ImGui::SetCursorScreenPos(ImVec2(origin.x + 18 * scale_, origin.y + (height - ImGui::GetFrameHeight()) / 2));
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(kSecondary, "Profile");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180 * scale_);
  if (ImGui::BeginCombo("##profile", settings_.active.c_str())) {
    for (const auto& profile : settings_.profiles)
      if (ImGui::Selectable(profile.name.c_str(), profile.name == settings_.active)) settings_.active = profile.name;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Save as")) {
    new_name_.clear();
    ImGui::OpenPopup("Save profile");
  }
  if (settings_.profiles.size() > 1) {
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
      std::erase_if(settings_.profiles, [&](const Profile& p) { return p.name == settings_.active; });
      settings_.active = settings_.profiles.front().name;
    }
  }
  if (ImGui::BeginPopupModal("Save profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(280 * scale_);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##name", &new_name_, ImGuiInputTextFlags_EnterReturnsTrue);
    if ((ImGui::Button("Save") || enter) && !new_name_.empty()) {
      const Options options = settings_.Active().options;
      std::erase_if(settings_.profiles, [&](const Profile& p) { return p.name == new_name_; });
      settings_.profiles.push_back({new_name_, options});
      settings_.active = new_name_;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::TextColored(kMuted, "Saved on Play. The launcher hides while the game runs.");
  const char* play = "  Play  ";
  const float play_width = ImGui::CalcTextSize(play).x + ImGui::GetStyle().FramePadding.x * 4;
  ImGui::SameLine(width - play_width - 18 * scale_);
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.62f, 0.93f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.17f, 0.46f, 0.76f, 1));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  ImGui::PushFont(heading_font_, 0);
  if (ImGui::Button(play, ImVec2(play_width, 0))) Play();
  ImGui::PopFont();
  ImGui::PopStyleColor(5);
}

}  // namespace

int main(int argc, char** argv) {
  App app;
  if (!app.Init(argc, argv)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Motorsport DX", SDL_GetError(), nullptr);
    return 1;
  }
  app.Run();
  return 0;
}
