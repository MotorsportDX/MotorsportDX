// Motorsport DX launcher: a window to pick the run options of Build.ps1 -Action run, the Discs and the
// downloadable content, and to start the game without Python, Visual Studio or the repository
// (docs/running.md#gui-launcher). The launch rules are those of pipeline.py (plan.cpp).
#include <windows.h>
#include <d3d11.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <atomic>
#include <functional>
#include <iterator>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_dx11.h"
#include "misc/cpp/imgui_stdlib.h"

#include <rex/input/wheel/wheel_host.h>

#include "discs.h"
#include "runner.h"
#include "screenshot.h"
#include "shaders.h"
#include "settings.h"

namespace fm4 {
int RunTests(const std::filesystem::path& cases, const std::filesystem::path& result);
}

namespace {

namespace fs = std::filesystem;
using namespace fm4;

// Colors of the dark theme: carbon greys and a racing red.
const ImVec4 kBackground = ImVec4(0.051f, 0.055f, 0.067f, 1.0f);
const ImVec4 kPanel = ImVec4(0.086f, 0.094f, 0.114f, 1.0f);
const ImVec4 kField = ImVec4(0.118f, 0.129f, 0.157f, 1.0f);
const ImVec4 kBorder = ImVec4(0.165f, 0.180f, 0.216f, 1.0f);
const ImVec4 kText = ImVec4(0.929f, 0.937f, 0.953f, 1.0f);
const ImVec4 kMuted = ImVec4(0.541f, 0.565f, 0.612f, 1.0f);
const ImVec4 kSecondary = ImVec4(0.725f, 0.741f, 0.776f, 1.0f);
const ImVec4 kAccent = ImVec4(0.902f, 0.231f, 0.180f, 1.0f);
const ImVec4 kAccentBright = ImVec4(1.0f, 0.353f, 0.263f, 1.0f);
const ImVec4 kAccentDark = ImVec4(0.420f, 0.098f, 0.075f, 1.0f);
const ImVec4 kSuccess = ImVec4(0.235f, 0.796f, 0.498f, 1.0f);
const ImVec4 kDanger = ImVec4(1.0f, 0.478f, 0.420f, 1.0f);
const ImVec4 kDangerBackground = ImVec4(0.314f, 0.075f, 0.063f, 1.0f);
const ImVec4 kWarning = ImVec4(0.949f, 0.663f, 0.231f, 1.0f);

// Segoe Fluent Icons (Windows 11) or Segoe MDL2 Assets (Windows 10), read from the Windows fonts.
std::string Icon(char32_t code) {
  return {static_cast<char>(0xE0 | (code >> 12)), static_cast<char>(0x80 | ((code >> 6) & 0x3F)),
          static_cast<char>(0x80 | (code & 0x3F))};
}
constexpr char32_t kIconHome = 0xE80F, kIconDisplay = 0xE7F4, kIconLook = 0xE790, kIconPad = 0xE7FC,
                   kIconDisc = 0xE958, kIconShaders = 0xE945, kIconSettings = 0xE713, kIconInfo = 0xE946,
                   kIconCheck = 0xE73E, kIconWarning = 0xE7BA, kIconLeft = 0xE76B, kIconRight = 0xE76C,
                   kIconPlay = 0xE768, kIconPackage = 0xE7B8, kIconFolder = 0xE8B7;

struct Knob {
  const char* name;
  const char* label;
  float min, max;
  float step;
  const char* help;  // the detail panel's text on the Look page
};

struct KnobGroup {
  const char* title;
  const char* tab;  // the Look page's tab
  std::vector<Knob> knobs;
};

const std::vector<KnobGroup> kKnobGroups = {
    {"Picture",
     "PICTURE",
     {{"exposure", "Exposure", -2, 2, 0.01f, "Brighter above 0, darker below, on top of the game's auto exposure."},
      {"contrast", "Contrast", -1, 1, 0.01f, "Contrast of the whole picture: deeper shadows and brighter highlights above 0."},
      {"saturation", "Saturation", 0, 2, 0.01f, "Colour strength of the whole picture: 1 keeps it, 0 is grey."},
      {"bloom", "Bloom", 0, 2, 0.01f, "The glow around bright lights and sunlit surfaces."},
      {"bloom_width", "Bloom width", 0, 2, 0.01f,
       "A wide halo around the bloom, as a low sun spreads its glow. 0 turns it off."},
      {"temperature", "Warmth", -1, 1, 0.01f, "White balance of the whole picture: warmer above 0, cooler below."},
      {"sharpen", "Sharpen", 0, 1.5f, 0.01f,
       "Sharpening of the final picture, after the anti-aliasing. With FSR 3.1 it is FSR's own sharpening."},
      {"vignette", "Vignette", 0, 2, 0.01f, "Darker corners of the picture."},
      {"motion_blur", "Motion blur", 0, 2, 0.01f,
       "The game's motion blur in races and replays. Photo mode's blur, which you set there, changes only when this "
       "is set."}}},
    {"Lighting",
     "LIGHTING",
     {{"sun", "Sun glare", 0, 4, 0.01f, "The glare around the sun. 0 turns it off."},
      {"sky", "Sky brightness", 0, 2, 0.01f, "Brightness of the sky; the Sky & foliage tab changes its colour."},
      {"ao", "Ambient occlusion", 0, 1.5f, 0.01f,
       "Soft shadows in corners and under the cars (screen-space ambient occlusion). 0 turns it off."},
      {"cockpit_fill", "Cockpit fill", 0, 2, 0.01f,
       "Light from the sky in the shaded cockpit, lessened in tunnels and under stands, so the dashboard is not "
       "near black in the sun."},
      {"cockpit_light", "Cockpit sun floor", 0, 1, 0.01f,
       "The least share of sunlight the cockpit and the driver keep in the car's own shadow. It follows the sun's "
       "direction, so the cockpit can swing between bright and dark; Cockpit fill is the steadier choice."},
      {"haze", "Haze", 0, 1, 0.01f,
       "Haze in the air: distant hills and buildings fade toward a pale sky colour, and the sky gets paler. "
       "0 turns it off."},
      {"haze_brightness", "Haze brightness", 0, 2, 0.01f, "How bright the haze is."},
      {"windshield", "Cockpit glass", 0, 2, 0.01f,
       "Glass in the cockpit camera's windshield and side windows: a light tint, a faint reflection of the "
       "dashboard, and glare and dust toward bright light. 0 leaves them open, as the game does."}}},
    {"Car",
     "CAR",
     {{"clearcoat", "Clear coat", 0, 2, 0.01f, "The clear coat over the body paint: reflections and a sun highlight."},
      {"clearcoat_roughness", "Clear coat roughness", 0.02f, 1, 0.01f,
       "Roughness of the clear coat: lower is a sharper, smaller highlight."},
      {"clearcoat_glint", "Highlight limit", 0, 8, 0.01f,
       "Soft limit of the sun highlight on paint, glass, chrome and rims. At 4 a hood toward the sun turns into a "
       "white blob; at 1 the highlight keeps its shape."},
      {"flakes", "Paint flakes", 0, 2, 0.01f, "Metallic flakes in the car paint."},
      {"flake_scale", "Flake scale", 100, 4000, 10, "The size of the paint flakes: higher is finer."},
      {"glass", "Glass", 0, 2, 0.01f,
       "Windows and lamp covers: the reflection at grazing angles and a sharp sun highlight."},
      {"glass_roughness", "Glass roughness", 0.01f, 0.5f, 0.01f, "Roughness of the glass highlight."},
      {"chrome", "Chrome", 0, 2, 0.01f, "Chrome trim and chrome rims: a sun highlight and more reflection at grazing angles."},
      {"chrome_roughness", "Chrome roughness", 0.01f, 0.5f, 0.01f, "Roughness of the chrome highlight."},
      {"rims", "Rims", 0, 2, 0.01f, "Painted and alloy rims: a clear lacquer over the game's metal."},
      {"rims_roughness", "Rim roughness", 0.02f, 1, 0.01f, "Roughness of the rim lacquer."}}},
    {"Sky and foliage",
     "SKY & FOLIAGE",
     {{"sky_saturation", "Sky saturation", 0, 2, 0.01f, "Colour strength of the sky over the tracks."},
      {"sky_contrast", "Sky contrast", 0, 1, 0.01f, "Clouds stand out more against the sky."},
      {"sky_warmth", "Sky warmth", -1, 1, 0.01f, "White balance of the sky: bluer below 0, warmer above."},
      {"sky_horizon", "Sky horizon", 0, 1, 0.01f, "A paler, brighter horizon and a deeper sky overhead."},
      {"foliage_hue", "Foliage hue", -90, 90, 1,
       "The green of trees, bushes and grass: below 0 toward yellow and orange (-30 dry, -60 autumn), above 0 toward "
       "blue green."},
      {"foliage_saturation", "Foliage saturation", 0, 2, 0.01f, "Colour strength of that green."},
      {"foliage_brightness", "Foliage brightness", 0, 2, 0.01f, "Brightness of that green."}}},
    {"Road",
     "ROAD",
     {{"road_brightness", "Road brightness", 0, 2, 0.01f, "Brightness of the asphalt and the road edges."},
      {"road_contrast", "Road contrast", -1, 1, 0.01f,
       "Light stones lighter and the binder darker in the asphalt, with the overall tone kept."},
      {"road_specular", "Game highlight", 0, 2, 0.01f,
       "The game's own sun highlight on the road. 0 removes it, and with it the bright sheet on sunlit asphalt "
       "toward the sun."},
      {"road_sheen", "Sheen", 0, 1, 0.01f,
       "A sun highlight that glints with the asphalt's stones; with the game highlight at 0 it replaces it."},
      {"road_roughness", "Sheen roughness", 0.05f, 1, 0.01f, "Roughness of the sheen: lower is a smaller, brighter glare."},
      {"road_sheen_grain", "Sheen grain", 0, 3, 0.01f,
       "How much the sheen follows the stones and the skid marks. 0 gives an even sheen."},
      {"road_gloss", "Road gloss", 0, 2, 0.01f, "Shine of the road surface."}}},
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

// Two knob values are the same text, or the same number.
bool SameValue(const std::string& a, const std::string& b) {
  if (a == b) return true;
  try {
    return PyFloat(a) == PyFloat(b);
  } catch (const std::exception&) {
    return false;
  }
}

// The step of a knob's row: about 40 presses across its range, a round number, never finer
// than the knob's own step.
float PadStep(const Knob& knob) {
  const float target = (knob.max - knob.min) / 40;
  for (const float step : {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f})
    if (step >= target && step >= knob.step) return step;
  return 100;
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

// The banner after a game that exited with an error: Windows' NTSTATUS codes in hex, with the
// common ones in words.
std::string ExitText(unsigned long code, double seconds);

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

std::string ExitText(unsigned long code, double seconds) {
  char hex[16];
  std::snprintf(hex, sizeof(hex), "0x%08lX", code);
  const std::string after = " after " + FormatDuration(seconds);
  if (code >= 0xC0000000ul) {
    const char* what = code == 0xC0000005ul ? "a memory access violation"
                       : code == 0xC0000409ul ? "a stack buffer check"
                       : code == 0xC00000FDul ? "a stack overflow"
                       : code == 0xC0000135ul ? "a missing DLL (install the Visual C++ Redistributable)"
                                              : nullptr;
    return "The game crashed" + after + " (" + (what ? std::string(what) + ", " : "") + hex +
           "). The game log says more.";
  }
  return "The game exited with code " + std::to_string(code) + after + ".";
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

// The pages, in the navigation's order, with their names and their ids of --screenshot.
enum Page { kHome, kDisplay, kLook, kControls, kGameData, kShaders, kAdvanced, kAbout, kPageCount };
const char* const kPageNames[kPageCount] = {"Home",      "Display", "Look",     "Controls",
                                            "Game data", "Shaders", "Advanced", "About"};
const char* const kPageIds[kPageCount] = {"home",      "display", "look",     "controls",
                                          "game-data", "shaders", "advanced", "about"};

// A setup step's state on the Home page and in the navigation.
enum class Status { kDone, kOptional, kNeeded, kBusy };

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
  // --screenshot (screenshot.h): Init read the request; renders the page and returns the exit code.
  bool Headless() const { return shot_.has_value(); }
  int Screenshot();

 private:
  void LoadFonts(float scale);
  void ApplyTheme(float scale);
  void Frame();
  void Banner();
  // The gamepad's shortcuts (LB and RB change the page, Start plays) and the focus ring.
  void PadShortcuts();
  // The racing theme's shell: a backdrop (a blurred picture, or a dark gradient), the emblem and
  // the wordmark, the page's badge and tabs (LB and RB), translucent panels, the red focus,
  // and the bar of pad prompts, which the mouse and touch also press.
  void Backdrop();
  void TopBar();
  // The page's tabs; returns the bar's height. LB and RB move between them.
  void TabBar(int& tab, const std::vector<const char*>& tabs);
  void PromptBar();
  // The profiles (Y): pick one, save the options under a new name, or delete one.
  void ProfilePopup();
  void PadGlyph(ImDrawList* draw, ImVec2 center, const char* label);
  void Slant(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color, float skew);
  void Emblem(ImDrawList* draw, ImVec2 origin, float height);
  void Slashes(ImDrawList* draw, ImVec2 max, float size);
  void PanelBg(ImVec2 min, ImVec2 max);
  void Focus(ImDrawList* draw, ImVec2 min, ImVec2 max);
  // One option as a row: left and right (D-pad, stick, arrow keys, or a click on a chevron)
  // return -1 and 1, A or a click on the row 2. With a fraction (0 to 1) the row is a slider: a
  // bar shows it, a click or a drag on the bar sets it and returns 3, and A does nothing. A
  // changed row is marked; X, Delete or the X prompt on it returns 4, to reset it.
  int OptionRow(const char* id, const char* label, const std::string& value, const char* help,
                bool enabled = true, const ImVec4* value_color = nullptr, float* fraction = nullptr,
                bool changed = false);
  // A themed page: its tabs (LB and RB), a list of option rows and the detail panel.
  void RowsPage(const std::vector<const char*>& tabs, const std::function<void()>& rows,
                const std::function<void()>& extra);
  // The detail panel at the right of a list: the focused entry's title and text.
  void DetailPanel(ImVec2 size, const std::function<void()>& extra);
  void HomeMenu();
  void DisplayRows();
  void LookRows();
  bool LoadBackdrop(const fs::path& path);
  void FocusRing();
  Status DiscStatus() const;
  Status ContentStatus() const;
  Status ShaderStatus();
  bool WheelKnobsOn();
  bool ShadersReady();
  // Why Play cannot start yet, or empty.
  std::string NotReady() const;
  // The Controls page: the controller (-Pad), the racing wheel in the game (-Wheel, with its
  // calibration from the wheel test), the wheel test without the game
  // (docs/running.md#racing-wheel-test), the keyboard (-Keyboard) and its key bindings.
  void ControlsRows();
  enum class KnobKind { kSlider, kToggle, kWhole, kChoice };
  // One knob of an option string (-Pad, -Keyboard or -Wheel, with its table of launcher.json) as
  // a row; a knob at its default is left out of the option. Returns OptionRow's result.
  int KnobRow(Pairs& knobs, std::string& option, const Json& table, const char* name, const char* label,
              const char* help, KnobKind kind, float step = 0.01f, const char* unit = "",
              const std::vector<std::pair<std::string, std::string>>* choices = nullptr, bool enabled = true);
  // A section's title in a list of rows.
  void RowsTitle(const char* text);
  void StartWheelTest();
  void StopWheelTest();
  void WheelLog(std::string_view text);
  // The Game data page: the Discs and the texture pack, and the content with its packages.
  void GameDataRows();
  // A folder as a row: A opens the folder picker, X clears it when it may be empty. Returns true
  // when it was cleared.
  bool PathOptionRow(const char* id, const char* label, std::string& value, DialogTarget target, const char* help,
                     bool clearable, const ImVec4* color);
  // The focused folder row's path, typed in the detail panel; true when an edit ended.
  bool PathField(std::string& value);
  // The end of a path that fits the width in the current font.
  std::string ShortPath(const std::string& path, float width);
  // The Advanced page: the user data and the developer switch, and the developer options.
  void AdvancedRows();
  // The Shaders page: the shader pass and the DirectX Shader Compiler.
  void ShadersRows();
  // The offline pass with the current plan, the DXC release fetched first when it is missing
  // (19.10's route); automatic once a build: marker() is what it last finished for.
  void CompileShaders();
  std::string ShaderPassMarker();
  // The Shaders page's task on its own thread: fetching DXC, or the offline pass.
  void StartShaderTask(std::function<std::string()> task);
  // The About page: who made Motorsport DX possible, the credits and the third-party notices.
  void AboutRows();
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
  void Muted(const std::string& text);
  void Colored(const ImVec4& color, const std::string& text);

  fs::path root_;
  std::optional<ScreenshotRequest> shot_;
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
  void FitWindow(HWND window);
  void CreateTarget();
  void ReleaseTarget();
  Uint32 game_event_ = 0;
  float scale_ = 1;
  ImFont* body_font_ = nullptr;
  ImFont* heading_font_ = nullptr;
  ImFont* din_font_ = nullptr;    // Bahnschrift: tabs, section titles, values
  ImFont* title_font_ = nullptr;  // Segoe UI Black Italic: the wordmark and the page titles
  std::string help_;              // the focused option's help, in the detail panel
  std::string help_title_;
  std::string focus_id_;          // the focused row's id, for the detail panel
  int home_focus_ = 0;            // the Home menu's focused entry
  int tab_ = 0;                   // the page's tab
  ImVec2 page_size_;              // the page's column, below the bars
  // The pages' own focus, as a game's menus keep it: always on one entry, moved by the
  // D-pad, the stick, the arrow keys or the mouse. ImGui's navigation serves the popups.
  int row_focus_ = 0, rows_seen_ = 0, row_count_ = 0;
  int MenuMove();      // -1 up, 1 down, 0
  bool MenuAccept();   // A or Enter, or the A prompt clicked
  bool prompt_accept_ = false;  // the A prompt was clicked: the next frame's MenuAccept
  bool prompt_reset_ = false;   // the X prompt was clicked: the next frame's reset
  bool focus_changed_ = false;  // the focused row is a changed one: the X prompt shows
  const char* reset_label_ = "Reset";  // the X prompt's word
  bool row_moved_ = false;      // the focus moved by the pad or the keys: the list scrolls to it
  int row_dragging_ = -1;       // the slider row whose bar the mouse drags
  // The profiles' popup keeps its own focus while it is open; MenuMove and MenuAccept serve it.
  int popup_focus_ = 0, popup_count_ = 0;
  bool in_popup_rows_ = false;
  bool profile_delete_ = false;  // the popup asks before it deletes the active profile
  ID3D11ShaderResourceView* backdrop_ = nullptr;
  ImVec2 backdrop_size_;
  int page_ = kHome;
  // Larger targets, drag scrolling and no hover help: on a touch screen (the ROG Xbox Ally X).
  bool touch_ = false;
  bool shaders_ready_ = false;  // ShadersReady(), from Refresh and the end of each pass
  bool shader_was_busy_ = false;
  std::atomic<bool> running_ = false;
  std::thread waiter_;
  // The Shaders page (shaders.h): one task at a time.
  std::thread shader_task_;
  std::atomic<bool> shader_busy_{false};
  std::mutex shader_mutex_;
  std::string shader_message_;      // shader_mutex_
  void* shader_process_ = nullptr;  // shader_mutex_: the running pass
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
  std::unique_ptr<rex::input::wheel::WheelHost> wheel_;
  bool wheel_haptic_ = false;
  float wheel_cap_ = 0.25f;
  bool wheel_invert_ = false;
  bool wheel_record_ = true;
  int wheel_test_choice_ = 1;  // the Wheel test tab's force: the direction pulse
  std::mutex wheel_log_mutex_;
  std::deque<std::string> wheel_log_;  // wheel_log_mutex_: the host's last lines
  std::string wheel_edit_;
  bool wheel_editing_ = false;
  // The knob that the next wheel button pressed is assigned to, and the buttons held when the
  // assignment started.
  std::string wheel_assign_;
  std::vector<uint8_t> wheel_assign_buttons_;
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
  try {
    shot_ = ParseScreenshot(argc, argv);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    std::exit(2);
  }
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  monitor_ = PrimaryMonitor();
  try {
    data_ = LauncherData::Load(root_ / "launcher.json");
  } catch (const std::exception& error) {
    load_error_ = std::string(error.what()) + ". Run Build.ps1 -Action build or -Action export again.";
  }
  if (!fs::exists(root_ / "game-build" / "fm4_rex.exe"))
    load_error_ = "No game-build\\fm4_rex.exe beside the launcher. Keep MotorsportDX.exe in its export folder.";
  // A screenshot reads its own settings (the defaults without --settings) and never saves them.
  settings_path_ = shot_ ? shot_->settings : root_ / "launcher-settings.json";
  settings_ = Settings::Load(settings_path_);
  if (shot_ && shot_->state == "first-run") {
    settings_.disc1.clear();
    settings_.disc2.clear();
    settings_.content.clear();
  } else if (settings_.disc1.empty() && fs::exists(root_ / "local.json")) {
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
#if defined(_LIBCPP_VERSION)
    // libc++ has no clock_cast (llvm-mingw, docs/analysis/mingw-toolchain.md), and its file
    // clock counts in a wider type than the system clock.
    const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        std::chrono::file_clock::to_sys(time));
#else
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
#endif
    const std::time_t stamp = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
    localtime_s(&local, &stamp);
    char text[64];
    std::strftime(text, sizeof(text), "%d %B %Y", &local);
    build_date_ = text[0] == '0' ? text + 1 : text;
  }
  if (shot_) return true;

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
  FitWindow(hwnd);
  SDL_ShowWindow(window_);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  io.IniFilename = nullptr;
  int touch_devices = 0;
  SDL_free(SDL_GetTouchDevices(&touch_devices));
  touch_ = touch_devices > 0;
  ApplyTheme(scale_);
  LoadFonts(scale_);
  ImGui_ImplSDL3_InitForD3D(window_);
  ImGui_ImplDX11_Init(device_, context_);
  for (const wchar_t* name : {L"launcher-backdrop.jpg", L"launcher-backdrop.png"})
    if (!backdrop_ && fs::exists(root_ / name)) LoadBackdrop(root_ / name);
  if (load_error_.empty()) Refresh(true);
  // --play: press Play at start with the active profile, for a desktop shortcut.
  bool play = false;
  bool wheel_test = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--play" && load_error_.empty()) play = true;
    // --wheel-test: the Controls page with the racing wheel's test running.
    if (std::string(argv[i]) == "--wheel-test") wheel_test = true;
  }
  if (wheel_test) {
    page_ = kControls;
    tab_ = 2;
    StartWheelTest();
  } else if (play) {
    Play();
  } else if (load_error_.empty() && disc1_.error.empty() && !ShadersReady()) {
    // The shader pass once after an install or an update (the owner's answer 5 of 19.10), once
    // Disc 1 is set; the Home page shows its progress.
    CompileShaders();
  }
  return true;
}

int App::Screenshot() {
  page_ = -1;
  for (int i = 0; i < kPageCount; ++i)
    if (shot_->page == kPageIds[i]) page_ = i;
  touch_ = shot_->touch;
  if (page_ < 0) return std::fprintf(stderr, "Unknown page: %s\n", shot_->page.c_str()), 2;
  if (shot_->state == "error-banner") {
    banner_ = ExitText(0xC0000005, 134);
    banner_error_ = true;
  } else if (shot_->state == "load-error") {
    load_error_ = "No game-build\\fm4_rex.exe beside the launcher. Keep MotorsportDX.exe in its export folder.";
  } else if (!shot_->state.empty() && shot_->state != "first-run" && shot_->state != "developer") {
    return std::fprintf(stderr, "Unknown state: %s\n", shot_->state.c_str()), 2;
  }
  // WARP: the screenshot never touches the GPU, which may be running the game.
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
  if (D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device_, nullptr,
                        &context_) != S_OK)
    return std::fprintf(stderr, "No WARP device\n"), 1;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(shot_->width);
  desc.Height = static_cast<UINT>(shot_->height);
  desc.MipLevels = desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ID3D11Texture2D* texture = nullptr;
  device_->CreateTexture2D(&desc, nullptr, &texture);
  device_->CreateRenderTargetView(texture, nullptr, &target_);
  desc.BindFlags = 0;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D* staging = nullptr;
  device_->CreateTexture2D(&desc, nullptr, &staging);

  scale_ = shot_->scale;
  if (shot_->state == "developer") settings_.developer = true;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(static_cast<float>(shot_->width), static_cast<float>(shot_->height));
  io.DeltaTime = 1.0f / 60;
  ApplyTheme(scale_);
  LoadFonts(scale_);
  ImGui_ImplDX11_Init(device_, context_);
  if (!shot_->backdrop.empty()) LoadBackdrop(shot_->backdrop);
  if (load_error_.empty()) Refresh(true);
  static const std::map<std::string, ImGuiKey> kPadKeys = {
      {"up", ImGuiKey_GamepadDpadUp},       {"down", ImGuiKey_GamepadDpadDown}, {"left", ImGuiKey_GamepadDpadLeft},
      {"right", ImGuiKey_GamepadDpadRight}, {"a", ImGuiKey_GamepadFaceDown},    {"b", ImGuiKey_GamepadFaceRight},
      {"x", ImGuiKey_GamepadFaceLeft},      {"y", ImGuiKey_GamepadFaceUp},      {"lb", ImGuiKey_GamepadL1},
      {"rb", ImGuiKey_GamepadR1},           {"start", ImGuiKey_GamepadStart},   {"back", ImGuiKey_GamepadBack}};
  for (const auto& button : shot_->pad)
    if (!kPadKeys.contains(button)) return std::fprintf(stderr, "Unknown pad button: %s\n", button.c_str()), 2;
  if (!shot_->pad.empty()) {
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  }
  // A few frames, so that the layout settles (combo widths, collapsing headers, wrapped text);
  // then each pad button is pressed for one frame and released for three.
  const int frames = 4 + 4 * static_cast<int>(shot_->pad.size()) + 4;
  for (int frame = 0; frame < frames; ++frame) {
    const int step = frame - 4;
    if (step >= 0 && step / 4 < static_cast<int>(shot_->pad.size()) && step % 4 < 2)
      io.AddKeyEvent(kPadKeys.at(shot_->pad[step / 4]), step % 4 == 0);
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    Frame();
    ImGui::Render();
    const float clear[4] = {kBackground.x, kBackground.y, kBackground.z, 1};
    context_->OMSetRenderTargets(1, &target_, nullptr);
    context_->ClearRenderTargetView(target_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  }
  context_->CopyResource(staging, texture);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  bool written = false;
  if (context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped) == S_OK) {
    written = WritePng(shot_->out, shot_->width, shot_->height, static_cast<const uint8_t*>(mapped.pData),
                       static_cast<int>(mapped.RowPitch));
    context_->Unmap(staging, 0);
  }
  ImGui_ImplDX11_Shutdown();
  ImGui::DestroyContext();
  ReleaseTarget();
  staging->Release();
  texture->Release();
  context_->Release();
  device_->Release();
  if (!written) return std::fprintf(stderr, "Could not write %s\n", Utf8(shot_->out).c_str()), 1;
  return 0;
}

void App::FitWindow(HWND window) {
  // The window and its frame inside the monitor's work area, centred there: on the ROG Xbox Ally X
  // at 150% (1280x720 points) the taskbar would otherwise cover the bar of pad prompts.
  MONITORINFO info{sizeof(info)};
  RECT outer{}, client{};
  if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &info) || !GetWindowRect(window, &outer) ||
      !GetClientRect(window, &client))
    return;
  const RECT& work = info.rcWork;
  const int frame_w = (outer.right - outer.left) - client.right, frame_h = (outer.bottom - outer.top) - client.bottom;
  const int width = std::min<int>(client.right, work.right - work.left - frame_w);
  const int height = std::min<int>(client.bottom, work.bottom - work.top - frame_h);
  SetWindowPos(window, nullptr, work.left + (work.right - work.left - width - frame_w) / 2,
               work.top + (work.bottom - work.top - height - frame_h) / 2, width + frame_w, height + frame_h,
               SWP_NOZORDER | SWP_NOACTIVATE);
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
  if (!body_font_) body_font_ = io.Fonts->AddFontDefault();
  // The icons merged into the body font.
  ImFontConfig icons;
  icons.MergeMode = true;
  const wchar_t* icon_file = fs::exists(fonts / L"SegoeIcons.ttf") ? L"SegoeIcons.ttf" : L"segmdl2.ttf";
  if (fs::exists(fonts / icon_file)) io.Fonts->AddFontFromFileTTF(Utf8(fonts / icon_file).c_str(), 17, &icons);
  heading_font_ = load(L"seguisb.ttf", 17);
  din_font_ = load(L"bahnschrift.ttf", 17);
  title_font_ = load(L"seguibli.ttf", 17);
  if (!heading_font_) heading_font_ = body_font_;
  if (!din_font_) din_font_ = heading_font_;
  if (!title_font_) title_font_ = heading_font_;  io.FontDefault = body_font_;
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
  style.SelectableTextAlign = ImVec2(0, 0.5f);
  if (touch_) {
    // Targets of about 9 mm on the ROG Xbox Ally X's 7-inch 1080p screen at 150%.
    style.FramePadding = ImVec2(14, 12);
    style.ItemSpacing = ImVec2(12, 12);
    style.ScrollbarSize = 22;
    style.GrabMinSize = 28;
  }
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
            banner_ = ExitText(code, seconds);
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
        if (wheel_ && (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) &&
            event.window.windowID == SDL_GetWindowID(window_))
          wheel_->SetFocus(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
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
  {
    // A pass still running ends with the launcher; a fetch finishes first.
    std::lock_guard<std::mutex> lock(shader_mutex_);
    if (shader_process_) TerminateProcess(shader_process_, 1);
  }
  if (shader_task_.joinable()) shader_task_.join();
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
  shaders_ready_ = ShadersReady();
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
  // The game opens the wheel itself.
  StopWheelTest();
  Refresh(false);
  const auto fail = [&](const std::string& message) {
    banner_ = message;
    banner_error_ = true;
  };
  if (!disc1_.error.empty()) return fail(disc1_.error), void(page_ = kGameData);
  if (!disc2_.error.empty()) return fail(disc2_.error), void(page_ = kGameData);
  if (!content_.error.empty()) return fail(content_.error), void(page_ = kGameData);
  for (const auto& bind : kBinds) {
    const std::string error = BindError(BindValue(bind));
    if (!error.empty()) return fail(std::string(bind.label) + ": " + error + "."), void(page_ = kControls);
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
  // The pad navigates from the first frame on. The pages and the profiles keep their rows' own
  // focus; ImGui's navigation serves the other popups.
  if (ImGui::GetFrameCount() < 3) ImGui::SetWindowFocus();
  ImGuiIO& io = ImGui::GetIO();
  const bool own_focus = ImGui::IsPopupOpen("Profiles") ||
                         !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
  if (own_focus) io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
  else io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
  PadShortcuts();
  Backdrop();
  TopBar();
  Banner();
  help_.clear();
  help_title_.clear();
  focus_id_.clear();
  focus_changed_ = false;
  reset_label_ = "Reset";
  const float prompts = 58 * scale_;
  const float body = ImGui::GetContentRegionAvail().y - prompts;
  // The page in a centred column: lists stay readable on a wide screen.
  const float width = ImGui::GetContentRegionAvail().x;
  const float column = std::min(width - 48 * scale_, 1500 * scale_);
  // No child window for the page: ImGui's flattened navigation crosses one level of child windows,
  // and the pages' lists are children of their own.
  const ImVec2 page_origin(ImGui::GetCursorScreenPos().x + (width - column) / 2, ImGui::GetCursorScreenPos().y);
  ImGui::SetCursorScreenPos(page_origin);
  ImGui::PushClipRect(page_origin, page_origin + ImVec2(column, body), true);
  page_size_ = ImVec2(column, body);
  ImGui::BeginGroup();
  switch (page_) {
    case kHome: HomeMenu(); break;
    case kDisplay: DisplayRows(); break;
    case kLook: LookRows(); break;
    case kControls: ControlsRows(); break;
    case kGameData: GameDataRows(); break;
    case kShaders: ShadersRows(); break;
    case kAdvanced: AdvancedRows(); break;
    default: AboutRows(); break;
  }
  ImGui::EndGroup();
  ImGui::PopClipRect();
  PromptBar();
  ProfilePopup();
  FocusRing();
  ImGui::End();
}
// The pad's focus in the wordmark's red (the owner's choice of 29 October 2026), and the
// translucent panels over the backdrop.
const ImVec4 kFocusColor = kAccentBright;
const ImVec4 kGlass = ImVec4(0.180f, 0.192f, 0.212f, 0.86f);
const ImVec4 kGlassLight = ImVec4(0.282f, 0.298f, 0.322f, 0.90f);

void App::Slant(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color, float skew) {
  draw->AddQuadFilled(ImVec2(min.x + skew, min.y), ImVec2(max.x, min.y), ImVec2(max.x - skew, max.y),
                      ImVec2(min.x, max.y), color);
}

// The project's own emblem: three bars leaning into the wind, red and two silvers, rising to the
// right. Drawn, so it needs no image file.
void App::Emblem(ImDrawList* draw, ImVec2 origin, float height) {
  const float skew = height * 0.5f, bar = height * 0.42f, gap = height * 0.12f;
  const ImU32 red = ImGui::GetColorU32(kAccent), red_dark = ImGui::GetColorU32(ImVec4(0.62f, 0.07f, 0.05f, 1));
  const ImU32 silver = ImGui::GetColorU32(ImVec4(0.86f, 0.87f, 0.89f, 1));
  const ImU32 steel = ImGui::GetColorU32(ImVec4(0.42f, 0.44f, 0.47f, 1));
  for (int i = 0; i < 3; ++i) {
    const float h = height * (0.55f + 0.225f * i);
    const float x = origin.x + i * (bar + gap) - (height - h) * 0.5f;
    const ImVec2 min(x + (height - h) * 0.5f, origin.y + height - h), max(x + bar + skew * h / height, origin.y + height);
    const float s = skew * h / height;
    const ImU32 top = i == 0 ? red : silver, bottom = i == 0 ? red_dark : steel;
    // A lit upper half and a darker lower half, like brushed metal.
    const float mid = (min.y + max.y) / 2;
    draw->AddQuadFilled(ImVec2(min.x + s, min.y), ImVec2(max.x, min.y), ImVec2(max.x - s / 2, mid),
                        ImVec2(min.x + s / 2, mid), top);
    draw->AddQuadFilled(ImVec2(min.x + s / 2, mid), ImVec2(max.x - s / 2, mid), ImVec2(max.x - s, max.y),
                        ImVec2(min.x, max.y), bottom);
  }
}

// Red slashes in a corner, the theme's motif.
void App::Slashes(ImDrawList* draw, ImVec2 max, float size) {
  for (int i = 0; i < 3; ++i) {
    const float w = size * (0.10f - 0.025f * i), x = max.x - size * (0.18f + 0.22f * i);
    const ImU32 color = ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.85f - 0.25f * i));
    Slant(draw, ImVec2(x - size * 0.5f, max.y - size * (0.9f - 0.2f * i)), ImVec2(x + w, max.y), color, size * 0.5f);
  }
}

void App::PanelBg(ImVec2 min, ImVec2 max) {
  ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(kGlass), 3 * scale_);
}

// The red frame of the focused entry, with its corner notch.
void App::Focus(ImDrawList* draw, ImVec2 min, ImVec2 max) {
  const ImU32 color = ImGui::GetColorU32(kFocusColor);
  draw->AddRect(min, max, color, 2 * scale_, 0, 2.5f * scale_);
  const float notch = 11 * scale_;
  draw->AddTriangleFilled(ImVec2(max.x - notch, min.y), ImVec2(max.x, min.y), ImVec2(max.x, min.y + notch), color);
}

bool App::LoadBackdrop(const fs::path& path) {
  std::vector<uint8_t> pixels;
  int width = 0, height = 0;
  if (!ReadImage(path, 640, pixels, width, height)) return false;
  // A soft blur, as a camera's depth of field: box passes on the small picture; the GPU's filter
  // upscales it.
  std::vector<uint8_t> work(pixels.size());
  for (int pass = 0; pass < 4; ++pass) {
    const bool horizontal = pass % 2 == 0;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        for (int c = 0; c < 4; ++c) {
          int sum = 0, count = 0;
          for (int k = -2; k <= 2; ++k) {
            const int sx = horizontal ? std::clamp(x + k, 0, width - 1) : x;
            const int sy = horizontal ? y : std::clamp(y + k, 0, height - 1);
            sum += pixels[(static_cast<size_t>(sy) * width + sx) * 4 + c];
            ++count;
          }
          work[(static_cast<size_t>(y) * width + x) * 4 + c] = static_cast<uint8_t>(sum / count);
        }
    pixels.swap(work);
  }
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA data{pixels.data(), static_cast<UINT>(width * 4), 0};
  ID3D11Texture2D* texture = nullptr;
  if (device_->CreateTexture2D(&desc, &data, &texture) != S_OK) return false;
  device_->CreateShaderResourceView(texture, nullptr, &backdrop_);
  texture->Release();
  backdrop_size_ = ImVec2(static_cast<float>(width), static_cast<float>(height));
  return backdrop_ != nullptr;
}

void App::Backdrop() {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 min = ImGui::GetWindowPos(), size = ImGui::GetWindowSize(), max = min + size;
  if (backdrop_) {
    // Cover the window, cropped to keep the picture's aspect.
    const float scale = std::max(size.x / backdrop_size_.x, size.y / backdrop_size_.y);
    const ImVec2 shown = backdrop_size_ * scale, offset = (size - shown) * 0.5f;
    draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(backdrop_)), min + offset, min + offset + shown);
    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(0.04f, 0.045f, 0.05f, 0.30f)));
  } else {
    draw->AddRectFilledMultiColor(min, max, ImGui::GetColorU32(ImVec4(0.17f, 0.18f, 0.20f, 1)),
                                  ImGui::GetColorU32(ImVec4(0.10f, 0.105f, 0.12f, 1)),
                                  ImGui::GetColorU32(ImVec4(0.035f, 0.04f, 0.045f, 1)),
                                  ImGui::GetColorU32(ImVec4(0.06f, 0.065f, 0.075f, 1)));
    // Light streaks across the dark, as from a garage's ceiling lights.
    for (int i = 0; i < 5; ++i) {
      const float x = min.x + size.x * (0.35f + 0.14f * i);
      Slant(draw, ImVec2(x, min.y), ImVec2(x + size.x * 0.05f, max.y),
            ImGui::GetColorU32(ImVec4(1, 1, 1, 0.018f + 0.006f * (i % 3))), -size.y * 0.35f);
    }
  }
  // Darker at the top and the bottom, where the bars sit.
  draw->AddRectFilledMultiColor(min, ImVec2(max.x, min.y + 150 * scale_), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.55f)),
                                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.55f)), ImGui::GetColorU32(ImVec4(0, 0, 0, 0)),
                                ImGui::GetColorU32(ImVec4(0, 0, 0, 0)));
  draw->AddRectFilledMultiColor(ImVec2(min.x, max.y - 120 * scale_), max, ImGui::GetColorU32(ImVec4(0, 0, 0, 0)),
                                ImGui::GetColorU32(ImVec4(0, 0, 0, 0)), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.6f)),
                                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.6f)));
}

void App::TopBar() {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x, height = 86 * scale_;
  const float left = origin.x + 28 * scale_, middle = origin.y + height / 2;
  Emblem(draw, ImVec2(left, middle - 17 * scale_), 34 * scale_);
  ImGui::PushFont(din_font_, 30);
  const float word_x = left + 74 * scale_;
  const float text_y = middle - ImGui::GetFontSize() / 2;
  // Drawn twice a pixel apart: Bahnschrift's regular weight, made heavier.
  for (const float dx : {0.0f, 0.9f * scale_}) draw->AddText(ImVec2(word_x + dx, text_y), ImGui::GetColorU32(kText), "MOTORSPORT");
  const float word = ImGui::CalcTextSize("MOTORSPORT ").x;
  for (const float dx : {0.0f, 0.9f * scale_}) draw->AddText(ImVec2(word_x + word + dx, text_y), ImGui::GetColorU32(kAccent), "DX");
  const float brand_end = word_x + word + ImGui::CalcTextSize("DX").x;
  ImGui::PopFont();
  // The page's badge, as a game names the menu it is in.
  if (page_ != kHome) {
    static const char32_t kIcons[kPageCount] = {kIconHome, kIconDisplay, kIconLook, kIconPad,
                                                kIconDisc, kIconShaders, kIconSettings, kIconInfo};
    ImGui::PushFont(din_font_, 22);
    std::string name = kPageNames[page_];
    for (auto& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const std::string icon = Icon(kIcons[page_]);
    const float w = ImGui::CalcTextSize(name.c_str()).x + 74 * scale_, h = 42 * scale_;
    const ImVec2 min(brand_end + 34 * scale_, middle - h / 2);
    draw->AddRectFilled(min, min + ImVec2(w, h), ImGui::GetColorU32(ImVec4(0.10f, 0.11f, 0.12f, 0.85f)), h / 2);
    draw->AddRect(min, min + ImVec2(w, h), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.35f)), h / 2, 0, 1.5f * scale_);
    draw->AddText(body_font_, ImGui::GetFontSize() * 0.9f, min + ImVec2(16 * scale_, (h - ImGui::GetFontSize() * 0.9f) / 2),
                  ImGui::GetColorU32(kText), icon.c_str());
    draw->AddText(min + ImVec2(50 * scale_, (h - ImGui::GetFontSize()) / 2), ImGui::GetColorU32(kText), name.c_str());
    ImGui::PopFont();
  }
  // The profile and the build, as a game's player card.
  const std::string release = (disc1_.release.empty() ? std::string("Disc 1 needed") : disc1_.release + " release") +
                              (build_date_.empty() ? "" : "  \xC2\xB7  build " + build_date_);
  ImGui::PushFont(din_font_, 19);
  const float card_w = std::max(ImGui::CalcTextSize(settings_.active.c_str()).x, ImGui::CalcTextSize(release.c_str()).x * 0.8f) +
                       90 * scale_;
  const float card_h = 50 * scale_;
  const ImVec2 card(origin.x + width - 28 * scale_ - card_w, middle - card_h / 2);
  draw->AddRectFilled(card, card + ImVec2(card_w, card_h), ImGui::GetColorU32(ImVec4(0.10f, 0.11f, 0.12f, 0.85f)), 3 * scale_);
  draw->AddRectFilled(card, card + ImVec2(card_h, card_h), ImGui::GetColorU32(ImVec4(0.24f, 0.25f, 0.27f, 0.95f)), 3 * scale_);
  const std::string pad = Icon(kIconPad);
  draw->AddText(body_font_, 22 * scale_, card + ImVec2((card_h - 22 * scale_) / 2, (card_h - 22 * scale_) / 2),
                ImGui::GetColorU32(kText), pad.c_str());
  draw->AddText(card + ImVec2(card_h + 14 * scale_, 5 * scale_), ImGui::GetColorU32(kText), settings_.active.c_str());
  draw->AddText(body_font_, 14 * scale_, card + ImVec2(card_h + 14 * scale_, 28 * scale_),
                ImGui::GetColorU32(disc1_.error.empty() ? kSecondary : kWarning), release.c_str());
  ImGui::PopFont();
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
  ImGui::Dummy(ImVec2(0, 0));
}

void App::TabBar(int& tab, const std::vector<const char*>& tabs) {
  const int count = static_cast<int>(tabs.size());
  if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) tab = (tab + count - 1) % count, row_focus_ = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) tab = (tab + 1) % count, row_focus_ = 0;
  }
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = page_size_.x, height = 46 * scale_;
  draw->AddRectFilled(origin, origin + ImVec2(width, height), ImGui::GetColorU32(ImVec4(0.12f, 0.13f, 0.14f, 0.88f)));
  ImGui::PushFont(din_font_, 19);
  const float y = origin.y + (height - ImGui::GetFontSize()) / 2;
  draw->AddText(ImVec2(origin.x + 18 * scale_, y), ImGui::GetColorU32(kMuted), "< LB");
  const float rb = ImGui::CalcTextSize("RB >").x;
  draw->AddText(ImVec2(origin.x + width - 18 * scale_ - rb, y), ImGui::GetColorU32(kMuted), "RB >");
  const float inner = origin.x + 90 * scale_, span = width - 180 * scale_;
  // Equal slots, or slots by the labels' widths when a label would not fit its equal slot.
  float widest = 0, total = 0;
  for (const char* label : tabs) {
    widest = std::max(widest, ImGui::CalcTextSize(label).x);
    total += ImGui::CalcTextSize(label).x;
  }
  const bool equal = widest + 24 * scale_ <= span / count;
  float slot_x = inner;
  for (int i = 0; i < count; ++i) {
    const float w = ImGui::CalcTextSize(tabs[i]).x;
    const float slot = equal ? span / count : w + (span - total) / count;
    const float x = slot_x + (slot - w) / 2;
    const bool selected = i == tab;
    draw->AddText(ImVec2(x, y), ImGui::GetColorU32(selected ? kFocusColor : kSecondary), tabs[i]);
    if (selected)
      draw->AddRectFilled(ImVec2(x - 6 * scale_, origin.y + height - 3 * scale_), ImVec2(x + w + 6 * scale_, origin.y + height),
                          ImGui::GetColorU32(kFocusColor));
    ImGui::SetCursorScreenPos(ImVec2(slot_x, origin.y));
    ImGui::PushID(i);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    if (ImGui::InvisibleButton("tab", ImVec2(slot, height))) tab = i, row_focus_ = 0;
    ImGui::PopItemFlag();
    ImGui::PopID();
    slot_x += slot;
  }
  ImGui::PopFont();
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height + 10 * scale_));
  ImGui::Dummy(ImVec2(0, 0));
}

void App::PadGlyph(ImDrawList* draw, ImVec2 center, const char* label) {
  const float radius = 13 * scale_;
  draw->AddCircle(center, radius, ImGui::GetColorU32(kText), 0, 1.6f * scale_);
  // The Menu button's glyph is an icon; the letters are Bahnschrift.
  const bool icon = static_cast<unsigned char>(label[0]) >= 0xE0;
  ImGui::PushFont(icon ? body_font_ : din_font_, icon ? 12 : 15);
  const ImVec2 size = ImGui::CalcTextSize(label);
  draw->AddText(center - size * 0.5f, ImGui::GetColorU32(kText), label);
  ImGui::PopFont();
}

void App::PromptBar() {
  // The A and X prompts clicked in the last frame were taken by this frame's page.
  prompt_accept_ = prompt_reset_ = false;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 window = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
  const float bar = 58 * scale_, y = window.y + size.y - 30 * scale_;
  enum Action { kPlay, kProfile, kReset, kBack, kSelect };
  struct Prompt {
    const char* glyph;
    const char* text;
    Action action;
  };
  std::vector<Prompt> prompts;
  const std::string not_ready = NotReady();
  const std::string menu = Icon(0xE700);
  if (not_ready.empty()) prompts.push_back({menu.c_str(), "Play", kPlay});
  prompts.push_back({"Y", "Profile", kProfile});
  if (focus_changed_) prompts.push_back({"X", reset_label_, kReset});
  if (page_ != kHome) prompts.push_back({"B", "Back", kBack});
  prompts.push_back({"A", "Select", kSelect});
  ImGui::PushFont(din_font_, 18);
  float x = window.x + size.x - 32 * scale_;
  for (auto it = prompts.rbegin(); it != prompts.rend(); ++it) {
    const float w = ImGui::CalcTextSize(it->text).x;
    const float right = x + 12 * scale_;
    x -= w;
    const float text_x = x;
    x -= 22 * scale_;
    const float glyph_x = x;
    // The mouse and touch press a prompt as the pad presses its button: the glyph, the word and a
    // margin.
    const ImVec2 min(glyph_x - 26 * scale_, window.y + size.y - bar), max(right, window.y + size.y);
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(it->text);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool pressed = ImGui::InvisibleButton("prompt", max - min);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopItemFlag();
    ImGui::PopID();
    if (hovered)
      draw->AddRectFilled(min + ImVec2(0, 8 * scale_), max - ImVec2(0, 8 * scale_),
                          ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f)), 3 * scale_);
    if (pressed) {
      switch (it->action) {
        case kPlay:
          if (!shot_) Play();
          break;
        case kProfile: ImGui::OpenPopup("Profiles"); break;
        case kReset: prompt_reset_ = true; break;
        case kBack:
          page_ = kHome;
          tab_ = 0;
          break;
        case kSelect: prompt_accept_ = true; break;
      }
    }
    draw->AddText(ImVec2(text_x, y - ImGui::GetFontSize() / 2), ImGui::GetColorU32(hovered ? kFocusColor : kText),
                  it->text);
    PadGlyph(draw, ImVec2(glyph_x, y), it->glyph);
    x -= 40 * scale_;
  }
  // Why Play waits, at the left.
  if (!not_ready.empty()) {
    const std::string text = Icon(kIconWarning) + "  " + not_ready;
    draw->AddText(body_font_, 17 * scale_, ImVec2(window.x + 32 * scale_, y - 9 * scale_), ImGui::GetColorU32(kWarning),
                  text.c_str());
  }
  ImGui::PopFont();
}

int App::OptionRow(const char* id, const char* label, const std::string& value, const char* help, bool enabled,
                   const ImVec4* value_color, float* fraction, bool changed) {
  const float width = ImGui::GetContentRegionAvail().x, height = 50 * scale_;
  const ImVec2 min = ImGui::GetCursorScreenPos(), max = min + ImVec2(width, height);
  ImGui::PushID(id);
  const int index = rows_seen_++;
  ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
  const bool pressed = ImGui::InvisibleButton("row", ImVec2(width, height)) && enabled;
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive(), activated = ImGui::IsItemActivated();
  ImGui::PopItemFlag();
  if (hovered && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) row_focus_ = index;
  const bool focused = index == row_focus_;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, ImGui::GetColorU32(focused || hovered ? kGlassLight : kGlass), 2 * scale_);
  // A changed value: a red edge at the left, and the label in red.
  if (changed) draw->AddRectFilled(min, ImVec2(min.x + 4 * scale_, max.y), ImGui::GetColorU32(kAccent));
  if (focused) Focus(draw, min, max);
  const ImVec4 text = !enabled ? kMuted : changed ? kAccentBright : kText;
  draw->AddText(body_font_, 18 * scale_, ImVec2(min.x + 18 * scale_, min.y + (height - 18 * scale_) / 2),
                ImGui::GetColorU32(text), label);
  // The value at the right, between chevrons while the row is focused.
  ImGui::PushFont(din_font_, 19);
  const ImVec2 value_size = ImGui::CalcTextSize(value.c_str());
  const float chevron = 26 * scale_;
  const float value_x = max.x - 18 * scale_ - chevron - value_size.x;
  draw->AddText(ImVec2(value_x, min.y + (height - value_size.y) / 2),
                ImGui::GetColorU32(value_color ? *value_color : enabled ? (focused ? kFocusColor : kText) : kMuted),
                value.c_str());
  // A slider's bar between the label and the value, at a fixed place unless a wide value needs room.
  const float bar_min = min.x + width * 0.42f;
  const float bar_max = std::min(max.x - 18 * scale_ - chevron * 2 - 64 * scale_, value_x - chevron - 14 * scale_);
  const bool bar = fraction && bar_max - bar_min > 40 * scale_;
  if (bar) {
    const float f = std::clamp(*fraction, 0.0f, 1.0f), y = min.y + height / 2, h = 4 * scale_;
    draw->AddRectFilled(ImVec2(bar_min, y - h / 2), ImVec2(bar_max, y + h / 2), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.16f)),
                        h / 2);
    const float x = bar_min + (bar_max - bar_min) * f;
    draw->AddRectFilled(ImVec2(bar_min, y - h / 2), ImVec2(x, y + h / 2),
                        ImGui::GetColorU32(focused ? kFocusColor : ImVec4(kText.x, kText.y, kText.z, 0.75f)), h / 2);
    if (focused || hovered) draw->AddCircleFilled(ImVec2(x, y), 7 * scale_, ImGui::GetColorU32(focused ? kFocusColor : kText));
  }
  ImGui::PopFont();
  int result = 0;
  if (enabled && (focused || hovered) && !value.empty()) {
    const std::string left = Icon(kIconLeft), right = Icon(kIconRight);
    const float icon = 14 * scale_;
    draw->AddText(body_font_, icon, ImVec2(value_x - chevron, min.y + (height - icon) / 2), ImGui::GetColorU32(kSecondary),
                  left.c_str());
    draw->AddText(body_font_, icon, ImVec2(max.x - 18 * scale_ - icon, min.y + (height - icon) / 2),
                  ImGui::GetColorU32(kSecondary), right.c_str());
  }
  if (enabled) {
    const float mouse = ImGui::GetIO().MousePos.x;
    if (bar && activated && mouse >= bar_min - 8 * scale_ && mouse <= bar_max + 8 * scale_) row_dragging_ = index;
    if (row_dragging_ == index) {
      if (!active) {
        row_dragging_ = -1;
      } else {
        const float f = std::clamp((mouse - bar_min) / (bar_max - bar_min), 0.0f, 1.0f);
        if (f != *fraction) *fraction = f, result = 3;
      }
    }
    if (focused && !fraction && MenuAccept()) result = 2;
    if (focused && !ImGui::GetIO().WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft) ||
          ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        result = -1;
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight) ||
          ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        result = 1;
      if (changed && (prompt_reset_ || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
        result = 4;
    }
    if (pressed && row_dragging_ < 0 && result != 3) {
      // A click on the left chevron steps back, on the right one steps on; anywhere else on a
      // row that is not a slider steps on, as A does.
      if (hovered && mouse < value_x && mouse > value_x - chevron * 1.5f) result = -1;
      else if (hovered && mouse > max.x - 18 * scale_ - chevron) result = 1;
      else if (!fraction) result = 2;
    }
  }
  // The focused row explains itself in the detail panel.
  if (focused) {
    help_title_ = label;
    help_ = help ? help : "";
    focus_changed_ = changed && enabled;
    focus_id_ = id;
    // The pad or the keys moved the focus here: keep the row in the list's view.
    if (row_moved_) {
      const float top = ImGui::GetWindowPos().y, bottom = top + ImGui::GetWindowHeight();
      if (min.y < top) ImGui::SetScrollFromPosY(min.y - top, 0);
      else if (max.y > bottom) ImGui::SetScrollFromPosY(max.y - top, 1);
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + 6 * scale_));
  ImGui::Dummy(ImVec2(0, 0));
  ImGui::PopID();
  return result;
}

void App::RowsPage(const std::vector<const char*>& tabs, const std::function<void()>& rows,
                   const std::function<void()>& extra) {
  const float top = ImGui::GetCursorScreenPos().y;
  const int tab = tab_;
  if (!tabs.empty()) TabBar(tab_, tabs);
  const float height = page_size_.y - (ImGui::GetCursorScreenPos().y - top);
  const float list_w = page_size_.x * 0.56f;
  const int move = row_count_ > 0 ? MenuMove() : 0;
  row_moved_ = move != 0 || tab != tab_;
  if (row_count_ > 0) row_focus_ = std::clamp(row_focus_ + move, 0, row_count_ - 1);
  rows_seen_ = 0;
  // A list a tab, so each keeps its own scroll.
  ImGui::PushID(tab_);
  ImGui::BeginChild("rows", ImVec2(list_w, height), ImGuiChildFlags_NavFlattened);
  rows();
  if (touch_ && ImGui::IsWindowHovered() && row_dragging_ < 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
  row_count_ = rows_seen_;
  row_focus_ = std::max(0, std::min(row_focus_, row_count_ - 1));
  ImGui::EndChild();
  ImGui::PopID();
  ImGui::SameLine(0, 18 * scale_);
  DetailPanel(ImVec2(page_size_.x - list_w - 18 * scale_, height), extra);
}

void App::DetailPanel(ImVec2 size, const std::function<void()>& extra) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kGlass);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26 * scale_, 22 * scale_));
  ImGui::BeginChild("detail", size, ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 min = ImGui::GetWindowPos(), max = min + ImGui::GetWindowSize();
  Slashes(draw, max, std::min(ImGui::GetWindowSize().y * 0.45f, 220 * scale_));
  std::string title = help_title_;
  for (auto& c : title) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  ImGui::PushFont(din_font_, 28);
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(title.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopFont();
  draw->AddRectFilled(ImVec2(min.x, ImGui::GetCursorScreenPos().y), ImVec2(min.x + 4 * scale_, ImGui::GetCursorScreenPos().y + 0),
                      ImGui::GetColorU32(kAccent));
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x * 0.92f);
  ImGui::PushStyleColor(ImGuiCol_Text, kSecondary);
  ImGui::TextWrapped("%s", help_.c_str());
  ImGui::PopStyleColor();
  ImGui::PopTextWrapPos();
  if (extra) extra();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void App::HomeMenu() {
  struct Entry {
    char32_t icon;
    const char* label;
    int page;  // -1: Play
  };
  static const Entry kEntries[] = {{kIconPlay, "Play", -1},          {kIconDisplay, "Display", kDisplay},
                                   {kIconLook, "Look", kLook},       {kIconPad, "Controls", kControls},
                                   {kIconDisc, "Game data", kGameData}, {kIconShaders, "Shaders", kShaders},
                                   {kIconSettings, "Advanced", kAdvanced}, {kIconInfo, "About", kAbout}};
  const float height = page_size_.y;
  const float list_w = std::min(420 * scale_, page_size_.x * 0.36f);
  const std::string not_ready = NotReady();
  const Status disc = DiscStatus(), shaders = ShaderStatus();
  const int entries = static_cast<int>(std::size(kEntries));
  home_focus_ = (home_focus_ + MenuMove() + entries) % entries;
  const bool accept = MenuAccept();

  // The menu: an icon rail and the entries.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kGlass);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 10 * scale_));
  ImGui::BeginChild("menu", ImVec2(list_w, height), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float rail = 58 * scale_;
  draw->AddRectFilled(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImVec2(rail, ImGui::GetWindowHeight()),
                      ImGui::GetColorU32(ImVec4(0.10f, 0.11f, 0.12f, 0.6f)));
  for (int i = 0; i < static_cast<int>(std::size(kEntries)); ++i) {
    const Entry& entry = kEntries[i];
    const float row = 54 * scale_;
    const ImVec2 min = ImGui::GetCursorScreenPos() + ImVec2(rail + 8 * scale_, 0);
    const ImVec2 max(ImGui::GetWindowPos().x + list_w - 12 * scale_, min.y + row);
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(i);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    bool pressed = ImGui::InvisibleButton("entry", max - min);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopItemFlag();
    if (hovered && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) home_focus_ = i;
    const bool current = home_focus_ == i;
    if (current && accept) pressed = true;
    if (current) {
      draw->AddRectFilled(min, max, ImGui::GetColorU32(kGlassLight), 2 * scale_);
      Focus(draw, min, max);
    }
    const std::string icon = Icon(entry.icon);
    draw->AddText(body_font_, 20 * scale_, ImVec2(ImGui::GetWindowPos().x + (rail - 20 * scale_) / 2, min.y + (row - 20 * scale_) / 2),
                  ImGui::GetColorU32(current ? kFocusColor : kMuted), icon.c_str());
    std::string label = entry.page < 0 && !disc1_.error.empty() ? "Choose Disc 1" : entry.label;
    for (auto& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    ImGui::PushFont(din_font_, 21);
    const bool disabled = entry.page < 0 && !not_ready.empty() && disc1_.error.empty();
    draw->AddText(ImVec2(min.x + 16 * scale_, min.y + (row - ImGui::GetFontSize()) / 2),
                  ImGui::GetColorU32(disabled ? kMuted : current ? kText : kSecondary), label.c_str());
    ImGui::PopFont();
    const Status status = entry.page == kGameData ? disc : entry.page == kShaders ? shaders : Status::kDone;
    if (status == Status::kNeeded || status == Status::kBusy) {
      const std::string mark = Icon(status == Status::kNeeded ? kIconWarning : kIconShaders);
      draw->AddText(body_font_, 17 * scale_, ImVec2(max.x - 32 * scale_, min.y + (row - 17 * scale_) / 2),
                    ImGui::GetColorU32(status == Status::kNeeded ? kWarning : kFocusColor), mark.c_str());
    }
    if (pressed) {
      if (entry.page >= 0) {
        page_ = entry.page;
        tab_ = 0;
        row_focus_ = 0;
      } else if (not_ready.empty() && !shot_) {
        Play();
      } else if (!disc1_.error.empty()) {
        OpenDialog(kDisc1, true);
      }
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x, max.y + 4 * scale_));
  }
  ImGui::Dummy(ImVec2(0, 0));
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::SameLine(0, 18 * scale_);

  // The detail of the focused entry.
  const Entry& entry = kEntries[home_focus_];
  std::string summary;
  std::string error;
  const auto plan = CurrentPlan(error);
  const Options& o = settings_.Active().options;
  if (plan) {
    static const char* const kQualities[] = {"Low", "Medium", "High", "Extra High", "Ultra"};
    static const char* const kQualityIds[] = {"low", "medium", "high", "extrahigh", "ultra"};
    std::string quality = "High";
    for (int i = 0; i < 5; ++i)
      if (o.quality == kQualityIds[i] || (i == 1 && o.quality == "xbox")) quality = kQualities[i];
    summary = "Render " + RenderScaleText(plan->cvars) + "  \xC2\xB7  " + quality + " quality  \xC2\xB7  " +
              std::to_string(o.frame_rate ? o.frame_rate : 60) + " fps races";
  }
  switch (entry.page) {
    case -1:
      help_title_ = not_ready.empty() ? "Ready to race" : "Almost ready";
      help_ = not_ready.empty() ? "Everything the game needs is in place. Press A, or Start from any page, to race with "
                                  "the profile " + settings_.active + "."
                                : "Point the launcher at your extracted Disc 1 once; A opens the folder picker. Disc 2 "
                                  "and downloadable content are optional.";
      break;
    case kDisplay:
      help_title_ = "Display";
      help_ = "The screen, the render scale and the upscaler, the race frame rate and the race quality.";
      break;
    case kLook:
      help_title_ = "Look";
      help_ = "How the races look: a preset, the tone curve, the anti-aliasing and fine adjustments of the picture, "
              "the lighting, the cars, the sky and the road.";
      break;
    case kControls:
      help_title_ = "Controls";
      help_ = "The controller's rumble and sticks, the racing wheel and its test, the keyboard and its keys.";
      break;
    case kGameData:
      help_title_ = "Game data";
      help_ = "Your extracted discs, the downloadable content and a texture pack.";
      break;
    case kShaders:
      help_title_ = "Shaders";
      help_ = "Compiles the game's shaders before you race, once a build, so races do not stutter while they compile.";
      break;
    case kAdvanced:
      help_title_ = "Advanced";
      help_ = "Where your profile, saves and logs are kept, and the developer options.";
      break;
    default:
      help_title_ = "About";
      help_ = "Who made Motorsport DX possible, and the licences of the code it uses.";
      break;
  }
  DetailPanel(ImVec2(page_size_.x - list_w - 18 * scale_, height), [&] {
    ImGui::Dummy(ImVec2(0, 14 * scale_));
    const auto line = [&](Status status, const std::string& text) {
      const ImVec4 color = status == Status::kDone ? kSuccess : status == Status::kNeeded ? kWarning
                           : status == Status::kBusy ? kFocusColor : kMuted;
      const std::string icon = Icon(status == Status::kDone ? kIconCheck : status == Status::kNeeded ? kIconWarning
                                    : status == Status::kBusy ? kIconShaders : kIconInfo);
      ImGui::TextColored(color, "%s", icon.c_str());
      ImGui::SameLine(0, 12 * scale_);
      ImGui::TextUnformatted(text.c_str());
    };
    if (entry.page == -1 || entry.page == kGameData) {
      line(disc, disc1_.error.empty() ? "Disc 1: " + disc1_.release + " release, verified" : "Disc 1: " + disc1_.error);
      const Status content = ContentStatus();
      line(content, content == Status::kOptional ? "Disc 2 and downloadable content: optional"
                    : content == Status::kDone ? "Disc 2 and content: " + std::to_string(content_.packages.size()) + " packages"
                                               : "Disc 2 and content: " + (disc2_.error.empty() ? content_.error : disc2_.error));
    }
    if (entry.page == -1 || entry.page == kShaders)
      line(shaders, shaders == Status::kDone ? "Shaders compiled for this build"
                    : shaders == Status::kBusy ? "Shaders compiling" : "Shaders not compiled yet (they also compile while you play)");
    if (entry.page == -1 || entry.page == kControls) {
      int count = 0;
      SDL_JoystickID* pads = SDL_GetGamepads(&count);
      std::string name = count > 0 && SDL_GetGamepadNameForID(pads[0]) ? SDL_GetGamepadNameForID(pads[0]) : "";
      SDL_free(pads);
      line(count > 0 ? Status::kDone : Status::kOptional, count > 0 ? name + " connected" : "No controller: the keyboard drives");
    }
    if ((entry.page == -1 || entry.page == kDisplay) && !summary.empty()) line(Status::kOptional, summary);
  });
}

void App::DisplayRows() {
  Options& o = settings_.Active().options;
  const auto step = [](int value, int delta, int count) { return (value + (delta == 2 ? 1 : delta) + count) % count; };
  RowsPage({"SCREEN", "PERFORMANCE", "EXTRAS"}, [&] {
    if (tab_ == 0) {
      static const char* const kDisplays[] = {"native", "720p", "1080p", "1440p", "4k"};
      int display = 0;
      for (int i = 0; i < 5; ++i)
        if (o.display == kDisplays[i]) display = i;
      const std::string native = "Native, " + std::to_string(monitor_.width) + "x" + std::to_string(monitor_.height);
      const std::string screens[] = {native, "1280x720 window", "1920x1080 window", "2560x1440 window", "3840x2160 window"};
      if (const int d = OptionRow("screen", "Screen", screens[display],
                                  "Native fills the screen, borderless, at its own resolution. A window size runs in a "
                                  "window of that size, or fullscreen with the option below."))
        o.display = (display = step(display, d, 5)) ? kDisplays[display] : "";
      if (const int d = OptionRow("fullscreen", "Fullscreen", display == 0 || o.fullscreen ? "On" : "Off",
                                  "A window size shown fullscreen. Native is always fullscreen.", display != 0))
        o.fullscreen = !o.fullscreen;
      std::string auto_text = "1x", unused;
      Options automatic = o;
      automatic.resolution_scale = 0;
      if (const auto plan = PlanOf(automatic, unused)) auto_text = RenderScaleText(plan->cvars);
      const std::string scales[] = {"Auto (" + auto_text + ")", "Screen's own", "1x (1280x720)", "2x (2560x1440)",
                                    "3x (3840x2160)", "4x (5120x2880)"};
      int item = o.resolution_scale == kPanelScale ? 1 : o.resolution_scale ? o.resolution_scale + 1 : 0;
      if (const int d = OptionRow("scale", "Render scale", scales[item],
                                  "How sharp the picture is. Auto covers the screen; the screen's own resolution renders "
                                  "the largest 16:9 picture that fits; 1x to 4x render at that multiple of 1280x720.")) {
        item = step(item, d, 6);
        o.resolution_scale = item == 1 ? kPanelScale : item ? item - 1 : 0;
      }
      if (OptionRow("supersample", "Supersample", o.supersample ? "On" : "Off",
                    "Renders one scale above the screen, then averages it down: the smoothest edges, for a strong GPU.",
                    o.resolution_scale == 0))
        o.supersample = !o.supersample;
    } else if (tab_ == 1) {
      static const char* const kUpscales[] = {"native", "quality", "balanced", "performance"};
      static const char* const kUpscaleNames[] = {"Off", "Quality", "Balanced", "Performance"};
      int upscale = 0;
      for (int i = 1; i < 4; ++i)
        if (o.upscale == kUpscales[i]) upscale = i;
      if (const int d = OptionRow("upscale", "Upscaler", kUpscaleNames[upscale],
                                  "FSR 3.1 or TAAU renders the race at fewer lines and rebuilds the picture: more frames "
                                  "for a little sharpness. Quality 1.5x, Balanced 1.7x, Performance 2x fewer lines.")) {
        upscale = step(upscale, d, 4);
        o.upscale = upscale ? kUpscales[upscale] : "";
      }
      const std::string look = o.look.empty() ? data_.Look().at("default").get<std::string>() : o.look;
      std::string aa = "taa";
      try {
        aa = LookKnobs(data_, look).Get("aa", "taa");
      } catch (const std::exception&) {
      }
      if (upscale && aa != "fsr" && aa != "taau") {
        if (OptionRow("use_fsr", "Use FSR 3.1 for the upscaler", "Press A", "The upscaler needs FSR 3.1 or TAAU as the "
                      "look's anti-aliasing; this sets FSR 3.1 in the look.", true, &kWarning)) {
          LookParts parts = LookParts::Parse(look);
          parts.knobs.Set("aa", "fsr");
          parts.knobs.Set("taa", "1");
          o.look = parts.Value();
        }
      }
      // Steps, not a slider of single frames (the owner's choice of 29 October 2026). A rate saved
      // before keeps showing until it is changed, then moves to its neighbour in the list.
      static const int kRates[] = {30, 60, 90, 120, 144, 240};
      constexpr int kRateCount = static_cast<int>(std::size(kRates));
      const int rate = o.frame_rate ? o.frame_rate : 60;
      int below = 0, above = kRateCount - 1;
      for (int i = 0; i < kRateCount; ++i) {
        if (kRates[i] <= rate) below = i;
        if (kRates[kRateCount - 1 - i] >= rate) above = kRateCount - 1 - i;
      }
      if (const int d = OptionRow("rate", "Race frame rate", std::to_string(rate) + " fps",
                                  "Races, replays and the countdown. Menus, post-race and photo mode keep their own timing. "
                                  "Pick the screen's refresh rate or a rate that divides it.")) {
        const int index = d == 2 ? (above == below ? below + 1 : above) % kRateCount
                          : d < 0 ? (above == below ? std::max(below - 1, 0) : below)
                                  : (above == below ? std::min(above + 1, kRateCount - 1) : above);
        o.frame_rate = kRates[index] == 60 ? 0 : kRates[index];
      }
      static const char* const kVsyncs[] = {"auto", "on", "off"};
      static const char* const kVsyncNames[] = {"Auto", "On", "Off"};
      int vsync = o.vsync == "on" ? 1 : o.vsync == "off" ? 2 : 0;
      if (const int d = OptionRow("vsync", "VSync", kVsyncNames[vsync],
                                  "Auto: on a variable refresh rate screen, frames as they come but at most one a refresh; "
                                  "otherwise On. On: no tearing. Off: at once, tearing allowed.")) {
        vsync = step(vsync, d, 3);
        o.vsync = vsync ? kVsyncs[vsync] : "";
      }
      static const char* const kQualities[] = {"low", "medium", "high", "extrahigh", "ultra"};
      static const char* const kQualityNames[] = {"Low", "Medium", "High", "Extra High", "Ultra"};
      static const char* const kQualityHelp[] = {
          "The game's own, fewer cars in the mirror, lighter ambient occlusion.",
          "The game's own race settings with soft particles.",
          "Object shadows, full detail wheels, car detail twice as far.",
          "High, every car at top detail, finer ambient occlusion.",
          "Extra High, the mirror every frame, 4x MSAA where the scale allows."};
      int quality = 2;
      for (int i = 0; i < 5; ++i)
        if (o.quality == kQualities[i] || (i == 1 && o.quality == "xbox")) quality = i;
      const std::string quality_help = std::string("Race detail. ") + kQualityHelp[quality] +
                                       " The render scale and the upscaler change the GPU time far more.";
      if (const int d = OptionRow("quality", "Race quality", kQualityNames[quality], quality_help.c_str())) {
        quality = d == 2 ? (quality + 1) % 5 : std::clamp(quality + d, 0, 4);
        o.quality = quality == 2 ? "" : kQualities[quality];
      }
    } else {
      if (OptionRow("mirror", "Rear-view mirror every frame", o.full_rate_mirror ? "On" : "Off",
                    "The mirror at the race's frame rate instead of every other frame."))
        o.full_rate_mirror = !o.full_rate_mirror;
      if (OptionRow("overlay", "Settings overlay at start", o.info ? "On" : "Off",
                    "Shows the run's settings over the game when it starts; F2 shows and hides it in the game."))
        o.info = !o.info;
      bool entry0 = o.entry0 != "k";
      if (OptionRow("menus", "Menus at the render scale", entry0 ? "On" : "Off",
                    "The menus and photo mode at the render scale with 4x MSAA. Off: at the whole scale below it, "
                    "for less GPU time."))
        o.entry0 = entry0 ? "k" : "";
      // A developer option (Advanced): the RTV path's MSAA tiles instead of the native path's one
      // pass, for comparisons.
      if (settings_.developer) {
        bool taa = true;
        try {
          taa = TaaEnabled(LookKnobs(data_, o.look.empty() ? data_.Look().at("default").get<std::string>() : o.look));
        } catch (const std::exception&) {
        }
        if (OptionRow("tiles", "Race rendering", o.msaa ? "MSAA tiles" : "One tile with TAA",
                      "Developer option. One tile: TAA anti-aliases the races. MSAA tiles: the tiles and MSAA of the "
                      "quality, on the RTV path. Needs a look with TAA.",
                      taa))
          o.msaa = !o.msaa;
      }
    }
  }, [&] {
    std::string error;
    ImGui::Dummy(ImVec2(0, 14 * scale_));
    if (const auto plan = CurrentPlan(error)) {
      ImGui::PushFont(din_font_, 17);
      ImGui::TextColored(kMuted, "THIS PROFILE");
      ImGui::PopFont();
      ImGui::TextUnformatted(("Render " + RenderScaleText(plan->cvars)).c_str());
      const std::string upscale = UpscaleText(plan->cvars);
      if (!upscale.empty()) ImGui::TextUnformatted(("Upscaled " + upscale).c_str());
      // The developer options show the whole plan.
      if (settings_.developer) {
        ImGui::Dummy(ImVec2(0, 10 * scale_));
        ImGui::PushTextWrapPos(0);
        Muted(plan->description + ".");
        ImGui::PopTextWrapPos();
      }
    } else {
      Colored(kDanger, error);
    }
  });
}

void App::ProfilePopup() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(560 * scale_, 0), ImVec2(560 * scale_, FLT_MAX));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.13f, 0.14f, 0.97f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * scale_, 20 * scale_));
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3 * scale_);
  const bool open = ImGui::BeginPopupModal("Profiles", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  if (!open) return;
  ImGui::PushFont(din_font_, 28);
  ImGui::TextUnformatted(profile_delete_ ? "DELETE PROFILE" : "PROFILES");
  ImGui::PopFont();
  ImGui::PushTextWrapPos(0);
  Muted(profile_delete_ ? "Delete the profile " + settings_.active +
                              "? Its options go; the Discs, content, looks and key bindings stay."
                        : "A profile is one set of run options; the Discs, content, looks and keys are shared.");
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, 8 * scale_));
  // The popup's own focus, as the pages keep theirs.
  std::swap(row_focus_, popup_focus_);
  std::swap(row_count_, popup_count_);
  const int page_rows = rows_seen_;
  rows_seen_ = 0;
  in_popup_rows_ = true;
  row_moved_ = false;
  if (ImGui::IsWindowAppearing()) {
    row_focus_ = 0;
    for (size_t i = 0; i < settings_.profiles.size(); ++i)
      if (settings_.profiles[i].name == settings_.active) row_focus_ = static_cast<int>(i);
  } else if (row_count_ > 0) {
    row_focus_ = std::clamp(row_focus_ + MenuMove(), 0, row_count_ - 1);
  }
  bool close = false;
  if (!profile_delete_) {
    for (size_t i = 0; i < settings_.profiles.size(); ++i) {
      const std::string& name = settings_.profiles[i].name;
      const std::string id = "profile" + std::to_string(i);
      if (OptionRow(id.c_str(), name.c_str(), name == settings_.active ? "Active" : "", nullptr, true, &kSuccess) == 2) {
        settings_.active = name;
        close = true;
      }
    }
    // A new profile takes the name typed below, or the next free "Profile <n>".
    const auto save = [&] {
      std::string name = new_name_;
      for (int n = 2; name.empty(); ++n) {
        name = "Profile " + std::to_string(n);
        for (const auto& profile : settings_.profiles)
          if (profile.name == name) name.clear();
      }
      const Options options = settings_.Active().options;
      std::erase_if(settings_.profiles, [&](const Profile& profile) { return profile.name == name; });
      settings_.profiles.push_back({name, options});
      settings_.active = name;
      new_name_.clear();
    };
    if (OptionRow("save", "Save as a new profile", "Press A", nullptr) == 2) save();
    if (settings_.profiles.size() > 1 &&
        OptionRow("delete", ("Delete " + settings_.active).c_str(), "Press A", nullptr, true, &kDanger) == 2) {
      profile_delete_ = true;
      row_focus_ = 1;
    }
    if (OptionRow("close", "Close", "Press A", nullptr) == 2) close = true;
    ImGui::Dummy(ImVec2(0, 4 * scale_));
    ImGui::PushFont(din_font_, 17);
    ImGui::TextColored(kMuted, "NAME OF A NEW PROFILE");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##name", "Profile <n> when empty", &new_name_, ImGuiInputTextFlags_EnterReturnsTrue))
      save();
  } else {
    if (OptionRow("confirm", "Delete", "Press A", nullptr, true, &kDanger) == 2) {
      std::erase_if(settings_.profiles, [&](const Profile& profile) { return profile.name == settings_.active; });
      settings_.active = settings_.profiles.front().name;
      profile_delete_ = false;
      row_focus_ = 0;
    }
    if (OptionRow("cancel", "Cancel", "Press A", nullptr) == 2) profile_delete_ = false;
  }
  // B or Escape steps back, as Back does in a game's menus.
  if (!ImGui::GetIO().WantTextInput &&
      (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
    if (profile_delete_) profile_delete_ = false;
    else close = true;
  }
  row_count_ = rows_seen_;
  row_focus_ = std::max(0, std::min(row_focus_, row_count_ - 1));
  in_popup_rows_ = false;
  rows_seen_ = page_rows;
  std::swap(row_focus_, popup_focus_);
  std::swap(row_count_, popup_count_);
  if (close) {
    profile_delete_ = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

int App::MenuMove() {
  if ((!in_popup_rows_ && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ||
      ImGui::GetIO().WantTextInput)
    return 0;
  for (const ImGuiKey key : {ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, ImGuiKey_UpArrow})
    if (ImGui::IsKeyPressed(key)) return -1;
  for (const ImGuiKey key : {ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, ImGuiKey_DownArrow})
    if (ImGui::IsKeyPressed(key)) return 1;
  return 0;
}

bool App::MenuAccept() {
  if ((!in_popup_rows_ && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ||
      ImGui::GetIO().WantTextInput)
    return false;
  if (prompt_accept_) return true;
  return ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
         ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
}

void App::PadShortcuts() {
  // Not while a popup or a combo is open, nor while a text field takes the keys.
  if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
      ImGui::GetIO().WantTextInput)
    return;
  if (!capture_.empty()) {
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) capture_.clear();
    return;
  }
  // B goes back to Home, as in a game's menus.
  if (page_ != kHome && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
    page_ = kHome;
    tab_ = 0;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)) ImGui::OpenPopup("Profiles");
  // A screenshot never starts the game.
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) && NotReady().empty() && !shot_) Play();
}

// ImGui's own focus ring is 2 pixels at every scale: a thicker one in the accent colour, so the
// pad's focus shows from arm's length on the handheld.
void App::FocusRing() {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  if (!g.NavCursorVisible || !g.NavWindow || !g.NavId || !g.IO.NavActive) return;
  ImRect rect = ImGui::WindowRectRelToAbs(g.NavWindow, g.NavWindow->NavRectRel[g.NavLayer]);
  const float gap = 3 * scale_;
  rect.Expand(gap);
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  draw->PushClipRect(g.NavWindow->InnerClipRect.Min - ImVec2(gap, gap), g.NavWindow->InnerClipRect.Max + ImVec2(gap, gap));
  draw->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(kAccent), ImGui::GetStyle().FrameRounding + gap, 0,
                2.5f * scale_);
  draw->PopClipRect();
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

Status App::DiscStatus() const { return disc1_.error.empty() ? Status::kDone : Status::kNeeded; }

bool App::WheelKnobsOn() {
  try {
    const Pairs knobs = WheelKnobs(data_, settings_.Active().options.wheel);
    return knobs.Get("on", "0") == "1";
  } catch (const std::exception&) {
    return false;
  }
}

Status App::ContentStatus() const {
  if (settings_.disc2.empty() && settings_.content.empty()) return Status::kOptional;
  if (!disc2_.error.empty() || !content_.error.empty()) return Status::kNeeded;
  return Status::kDone;
}

Status App::ShaderStatus() {
  if (shader_busy_) return Status::kBusy;
  if (shader_was_busy_) shaders_ready_ = ShadersReady();
  shader_was_busy_ = false;
  return shaders_ready_ ? Status::kDone : Status::kOptional;
}

std::string App::NotReady() const {
  if (!disc1_.error.empty()) return "Choose Disc 1 first";
  if (!disc2_.error.empty() || !content_.error.empty()) return "Check Disc 2 and the content on Game data";
  if (running_) return "The game is running";
  return {};
}

void App::LookRows() {
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
                                {"GT7 style", "gt7", false},
                                {"FM2023 style", "fm2023", false},
                                {"original", "original", false},
                                {"original, no TAA", "original,taa=0", false}};
  for (const auto& [name, value] : settings_.looks) entries.push_back({name, value, true});
  int current = -1;
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].value == look) current = static_cast<int>(i);

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
  const auto apply = [&] {
    const std::string value_text = parts.Value();
    o.look = value_text == "aces" ? "" : value_text;
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
    apply();
  };
  const auto reset = [&](const std::string& name) {
    parts.knobs.Erase(name);
    apply();
  };
  const auto effective = [&](const std::string& name) -> std::optional<std::string> {
    if (const std::string* value = parts.knobs.Find(name)) return *value;
    return base_value(name);
  };
  // The help of a knob's row, with the preset's value.
  const auto knob_help = [&](const char* help, const std::string& name) {
    const auto original = base_value(name);
    return std::string(help) + (original ? "\n\nThe preset's value: " + *original + "." : "\n\nNot set: the game's own.");
  };
  const auto on_off = [&](const std::string& name) {
    try {
      return PyFloat(effective(name).value_or("0")) > 0;
    } catch (const std::exception&) {
      return false;
    }
  };

  std::vector<const char*> tabs = {"PRESET"};
  for (const auto& group : kKnobGroups) tabs.push_back(group.tab);
  RowsPage(tabs, [&] {
    if (tab_ == 0) {
      const std::string preview =
          current >= 0 ? entries[current].label : (parts.base.empty() ? "custom" : parts.base) + " (changed)";
      if (const int d = OptionRow("preset", "Look", preview,
                                  "The preset. aces, gt and neutral share the look mods with their own tone curve; "
                                  "GT7 style adds Gran Turismo 7's tone mapping and a deeper sky; FM2023 style, "
                                  "after Forza Motorsport (2023), is warmer and hazier, with richer shadows. "
                                  "original is the game's own picture with TAA, and without TAA unchanged. Your saved "
                                  "looks follow them.\n\nChanging the preset drops the changes: Save as look keeps "
                                  "them.")) {
        const int count = static_cast<int>(entries.size());
        int from = current;
        for (int i = 0; from < 0 && i < count; ++i)
          if (entries[i].value == parts.base) from = i;
        const int next = from < 0 ? 0 : (from + (d < 0 ? -1 : 1) + count) % count;
        o.look = entries[next].value == "aces" ? "" : entries[next].value;
      }
      const Json& tonemaps = data_.Look().at("names").at("tonemap");
      std::vector<std::string> names;
      for (const auto& [name, number] : tonemaps.items()) names.push_back(name);
      int tonemap = 0;
      const std::string value = effective("tonemap").value_or("filmic");
      for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == value || PyRepr(tonemaps.at(names[i]).get<double>()) == value) tonemap = static_cast<int>(i);
      const int count = static_cast<int>(names.size());
      if (const int d = OptionRow("tonemap", "Tone curve", names[tonemap],
                                  "How the picture's light is mapped to the screen. filmic is the game's own; aces, gt "
                                  "and neutral are the presets' curves (gt keeps midtone colours closest to the game's, "
                                  "neutral compresses only the brightest channel); gt7 is Gran Turismo 7's, with a "
                                  "darker toe and hues kept in the highlights.",
                                  true, nullptr, nullptr, parts.knobs.Has("tonemap"))) {
        if (d == 4) reset("tonemap");
        else set_knob("tonemap", names[(tonemap + (d < 0 ? -1 : 1) + count) % count]);
      }
      const bool taa = on_off("taa");
      if (const int d = OptionRow("taa", "Temporal anti-aliasing", taa ? "On" : "Off",
                                  "Smooths the edges from frame to frame, with the scene jittered, and keeps a still "
                                  "camera's picture still. Races then render one tile without MSAA.",
                                  true, nullptr, nullptr, parts.knobs.Has("taa"))) {
        if (d == 4) reset("taa");
        else set_knob("taa", taa ? "0" : "1");
      }
      // TAA, FSR 3.1's native anti-aliasing (aa=fsr) or TAA as the upscaler (aa=taau).
      if (taa) {
        static const char* const kMethods[] = {"taa", "fsr", "taau"};
        static const char* const kMethodNames[] = {"TAA", "FSR 3.1", "TAAU"};
        const std::string method_text = effective("aa").value_or("taa");
        const int method = method_text == "fsr" ? 1 : method_text == "taau" ? 2 : 0;
        if (const int d = OptionRow("aa", "Anti-aliasing method", kMethodNames[method],
                                    "TAA before the HUD; FSR 3.1, AMD's anti-aliasing, earlier in the frame with the "
                                    "look's sharpening as its own; TAAU, TAA earlier in the frame. FSR 3.1 and TAAU are "
                                    "also what the upscaler on the Display page needs.",
                                    true, nullptr, nullptr, parts.knobs.Has("aa"))) {
          if (d == 4) reset("aa");
          else set_knob("aa", kMethods[(method + (d < 0 ? -1 : 1) + 3) % 3]);
        }
      }
      const bool adaptation = on_off("adaptation");
      if (const int d = OptionRow("adaptation", "60 fps exposure speed", adaptation ? "On" : "Off",
                                  "The auto exposure adapts as fast as the game's does at 60 fps, at every frame rate. "
                                  "Off: it adapts by a share a frame, so faster at higher frame rates.",
                                  true, nullptr, nullptr, parts.knobs.Has("adaptation"))) {
        if (d == 4) reset("adaptation");
        else set_knob("adaptation", adaptation ? "0" : "1");
      }
      if (OptionRow("save", "Save as look", "Press A", "Keeps this look under a name, in the Look list of every profile.") ==
          2) {
        new_name_.clear();
        ImGui::OpenPopup("Save look");
      }
      if (current >= 5 && OptionRow("delete", "Delete this look", "Press A",
                                    "Removes this saved look from the list; profiles that use it go back to the "
                                    "default look.") == 2)
        ImGui::OpenPopup("Delete look");
      if (current < 0 && !parts.knobs.Empty() &&
          OptionRow("reset", "Reset all changes", "Press A", "Back to the preset's own values.") == 2) {
        o.look = parts.base == "aces" ? "" : parts.base;
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
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
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      if (ImGui::BeginPopupModal("Delete look", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete the look %s?", current >= 5 ? entries[current].label.c_str() : "");
        if (ImGui::Button("Delete") && current >= 5) {
          std::erase_if(settings_.looks, [&](const auto& item) { return item.first == entries[current].label; });
          o.look = "";
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      return;
    }
    for (const auto& knob : kKnobGroups[tab_ - 1].knobs) {
      if (!data_.Look().at("knobs").contains(knob.name) && !data_.Look().at("track_knobs").contains(knob.name) &&
          !kOtherKnobDefaults.contains(knob.name) && std::string(knob.name) != "sun")
        continue;
      const auto text = effective(knob.name);
      float value = 1;
      try {
        if (text) value = static_cast<float>(PyFloat(*text));
      } catch (const std::exception&) {
      }
      float fraction = (value - knob.min) / (knob.max - knob.min);
      char shown[32];
      std::snprintf(shown, sizeof(shown), knob.step >= 1 ? "%.0f" : "%.2f", value);
      const std::string help = knob_help(knob.help, knob.name);
      const int d = OptionRow(knob.name, knob.label, text ? shown : "Game", help.c_str(), true, nullptr, &fraction,
                              parts.knobs.Has(knob.name));
      if (d == 4) {
        reset(knob.name);
      } else if (d == 3) {
        set_knob(knob.name, FormatKnob(knob.min + fraction * (knob.max - knob.min), knob.step));
      } else if (d == 1 || d == -1) {
        const float step = PadStep(knob);
        const float next = std::clamp(std::round((value + d * step) / step) * step, knob.min, knob.max);
        set_knob(knob.name, FormatKnob(next, knob.step));
      }
    }
  }, [&] {
    ImGui::Dummy(ImVec2(0, 14 * scale_));
    try {
      Pairs knobs = LookKnobs(data_, look);
      LookDefines(data_, knobs);
    } catch (const std::exception& error) {
      Colored(kDanger, error.what());
    }
    ImGui::PushTextWrapPos(0);
    Muted("Changes apply when the game starts: the look mods are compiled then.");
    ImGui::PopTextWrapPos();
    if (settings_.developer) {
      // The field keeps its own text while it is edited and applies it when it is left.
      ImGui::Dummy(ImVec2(0, 8 * scale_));
      ImGui::PushFont(din_font_, 17);
      ImGui::TextColored(kMuted, "LOOK VALUE (BUILD.PS1 -LOOK)");
      ImGui::PopFont();
      if (!look_editing_) look_edit_ = look;
      ImGui::SetNextItemWidth(-1);
      ImGui::InputText("##value", &look_edit_);
      look_editing_ = ImGui::IsItemActive();
      if (ImGui::IsItemDeactivatedAfterEdit()) o.look = look_edit_ == "aces" ? "" : look_edit_;
    }
  });
}

int App::KnobRow(Pairs& knobs, std::string& option, const Json& table, const char* name, const char* label,
                 const char* help, KnobKind kind, float step, const char* unit,
                 const std::vector<std::pair<std::string, std::string>>* choices, bool enabled) {
  const Json& knob = table.at(name);
  const std::string fallback = knob.at("default").get<std::string>();
  const std::string* given = knobs.Find(name);
  const std::string value = given ? *given : fallback;
  const auto set = [&](const std::string& text) {
    if (SameValue(text, fallback))
      knobs.Erase(name);
    else
      knobs.Set(name, text);
    std::string joined;
    for (const auto& [key, item] : knobs.Items()) joined += (joined.empty() ? "" : ",") + key + "=" + item;
    option = joined;
  };
  double number = 0;
  try {
    number = PyFloat(value);
  } catch (const std::exception&) {
  }
  const double low = knob.contains("min") ? knob.at("min").get<double>() : 0;
  const double high = knob.contains("max") ? knob.at("max").get<double>() : 1;
  std::string shown = value;
  float fraction = 0;
  float* bar = nullptr;
  if (kind == KnobKind::kToggle) {
    shown = value == "1" ? "On" : "Off";
  } else if (kind == KnobKind::kChoice) {
    for (const auto& [id, text] : *choices)
      if (id == value) shown = text;
  } else if (kind == KnobKind::kWhole) {
    // Axis and button numbers: -1 is none.
    shown = step == 1 && number < 0 ? "None" : std::to_string(static_cast<int>(number));
  } else {
    char text[48];
    std::snprintf(text, sizeof(text), step >= 1 ? "%.0f%s" : "%.2f%s", number, unit);
    shown = text;
    fraction = static_cast<float>((number - low) / (high - low));
    bar = &fraction;
  }
  const int d = OptionRow(name, label, shown, help, enabled, nullptr, bar, given && !SameValue(*given, fallback));
  if (d == 4) {
    set(fallback);
  } else if (kind == KnobKind::kToggle && d) {
    set(value == "1" ? "0" : "1");
  } else if (kind == KnobKind::kChoice && d) {
    const int count = static_cast<int>(choices->size());
    int index = 0;
    for (int i = 0; i < count; ++i)
      if ((*choices)[i].first == value) index = i;
    set((*choices)[(index + (d < 0 ? -1 : 1) + count) % count].first);
  } else if (kind == KnobKind::kWhole && (d == 1 || d == -1)) {
    set(std::to_string(static_cast<int>(std::clamp(number + d * step, low, high))));
  } else if (kind == KnobKind::kSlider && d == 3) {
    set(FormatKnob(low + fraction * (high - low), step));
  } else if (kind == KnobKind::kSlider && (d == 1 || d == -1)) {
    const float pad = PadStep(Knob{name, label, static_cast<float>(low), static_cast<float>(high), step, help});
    set(FormatKnob(std::clamp(std::round((number + d * pad) / pad) * pad, low, high), step));
  }
  return d;
}

void App::RowsTitle(const char* text) {
  ImGui::Dummy(ImVec2(0, 8 * scale_));
  ImGui::PushFont(din_font_, 17);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(at + ImVec2(0, 2 * scale_), at + ImVec2(3 * scale_, ImGui::GetFontSize()),
                                            ImGui::GetColorU32(kAccent));
  ImGui::SetCursorScreenPos(at + ImVec2(12 * scale_, 0));
  ImGui::TextColored(kSecondary, "%s", text);
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, 2 * scale_));
}

void App::ControlsRows() {
  namespace wheel = rex::input::wheel;
  Options& o = settings_.Active().options;
  // The knobs of each device (pipeline.py's PAD_KNOBS, KEYBOARD_KNOBS and WHEEL_KNOBS): a knob at
  // its default is left out of the option.
  std::string error;
  Pairs pad, keyboard, wheel_knobs;
  try {
    pad = PadKnobs(data_, o.pad);
    PadCvars(data_, o.pad);
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  try {
    keyboard = KeyboardKnobs(data_, o.keyboard);
    KeyboardCvars(data_, o.keyboard);
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  try {
    wheel_knobs = WheelKnobs(data_, o.wheel);
    WheelCvars(data_, o.wheel);
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  const Json& pad_table = data_.json.at("pad").at("knobs");
  const Json& keyboard_table = data_.json.at("keyboard").at("knobs");
  const Json& wheel_table = data_.json.at("wheel").at("knobs");
  const bool live = wheel_ != nullptr;
  const wheel::Snapshot snap = live ? wheel_->GetSnapshot() : wheel::Snapshot{};
  const auto raw_axis = [&](int axis, int16_t* out) {
    if (!snap.connected || axis < 0 || static_cast<size_t>(axis) >= snap.axes.size()) return false;
    *out = snap.axes[static_cast<size_t>(axis)];
    return true;
  };
  const auto wheel_number = [&](const std::string& name) {
    const std::string* value = wheel_knobs.Find(name);
    try {
      return PyFloat(value ? *value : wheel_table.at(name).at("default").get<std::string>());
    } catch (const std::exception&) {
      return 0.0;
    }
  };
  // The racing wheel's section that has the focus: its live value shows in the detail panel.
  std::string focus_section;
  int section_start = 0;
  const auto section = [&](const char* title) {
    RowsTitle(title);
    section_start = rows_seen_;
  };
  const auto section_end = [&](const char* id) {
    if (row_focus_ >= section_start && row_focus_ < rows_seen_) focus_section = id;
  };
  // Assign: the next wheel button pressed after A, while the test runs.
  if (!wheel_assign_.empty() && snap.connected) {
    for (size_t i = 0; i < snap.buttons.size(); ++i) {
      const bool was = i < wheel_assign_buttons_.size() && wheel_assign_buttons_[i];
      if (snap.buttons[i] && !was) {
        wheel_knobs.Set(wheel_assign_, std::to_string(i));
        std::string joined;
        for (const auto& [key, item] : wheel_knobs.Items()) joined += (joined.empty() ? "" : ",") + key + "=" + item;
        o.wheel = joined;
        wheel_assign_.clear();
        break;
      }
    }
    wheel_assign_buttons_ = snap.buttons;
  }
  std::string focus_bind;

  RowsPage({"CONTROLLER", "RACING WHEEL", "WHEEL TEST", "KEYBOARD", "KEY BINDINGS"}, [&] {
    if (tab_ == 0) {
      static const std::vector<std::pair<std::string, std::string>> kDrivers = {
          {"default", "Default"}, {"rawinput", "Raw Input"}, {"wgi", "WGI"}, {"gameinput", "GameInput"}};
      KnobRow(pad, o.pad, pad_table, "driver", "Pad driver",
              "Default: SDL's own (XInput for Xbox pads, HIDAPI for PlayStation pads). Raw Input: adds trigger rumble "
              "on Xbox One and Series pads (through Windows.Gaming.Input). WGI: Windows.Gaming.Input with XInput off, a "
              "fallback if Raw Input misbehaves. GameInput: Microsoft's GameInput first, for trigger rumble and the "
              "Share button where it has them (the ROG Xbox Ally X's triggers); without the GameInput runtime the "
              "others take the pads.",
              KnobKind::kChoice, 1, "", &kDrivers);
      KnobRow(pad, o.pad, pad_table, "rumble", "Rumble strength",
              "Multiplies the game's two rumble motors. 1: as the game sends them; 1.5 by default, since the game's "
              "driving rumble is only 0.05 to 0.2 of full.",
              KnobKind::kSlider);
      KnobRow(pad, o.pad, pad_table, "trigger_rumble", "Trigger rumble",
              "Throttle trigger from wheelspin, brake trigger from locking: derived from the game's motors and how far "
              "each trigger is pressed. Needs a pad with trigger motors (Raw Input, WGI or GameInput on Xbox pads).",
              KnobKind::kToggle);
      KnobRow(pad, o.pad, pad_table, "trigger_rumble_strength", "Trigger rumble strength",
              "Multiplies the derived trigger rumble.", KnobKind::kSlider);
      KnobRow(pad, o.pad, pad_table, "anti_deadzone", "Steering anti-deadzone",
              "Left stick: steers from the first movement. The game ignores the first 24% of the stick and cannot be "
              "set lower; this maps the stick into the game's own deadzone as the game reports it. Pair it with a "
              "stick deadzone of 0.05 so a resting stick does not steer.",
              KnobKind::kToggle);
      KnobRow(pad, o.pad, pad_table, "thread", "Input thread",
              "Reads the pad every millisecond off the UI thread: fresher input when the UI thread is busy (240 fps "
              "races).",
              KnobKind::kToggle);
      RowsTitle("STICKS AND TRIGGERS");
      KnobRow(pad, o.pad, pad_table, "stick_deadzone", "Stick deadzone",
              "Both sticks: read as centred up to this fraction (for a drifting stick).", KnobKind::kSlider);
      KnobRow(pad, o.pad, pad_table, "stick_outer", "Stick outer edge", "Both sticks: the deflection that gives full output.",
              KnobKind::kSlider);
      KnobRow(pad, o.pad, pad_table, "stick_curve", "Stick curve", "Both sticks: 1 is linear, above 1 finer near the centre.",
              KnobKind::kSlider);
      KnobRow(pad, o.pad, pad_table, "trigger_deadzone", "Trigger deadzone", "Triggers read 0 up to this fraction.",
              KnobKind::kSlider);
      KnobRow(pad, o.pad, pad_table, "trigger_outer", "Trigger outer edge", "The trigger travel that gives full output.",
              KnobKind::kSlider);
      RowsTitle("EXTRA BUTTONS");
      static const std::pair<const char*, const char*> kExtras[] = {
          {"misc1", "Share / microphone"},      {"right_paddle1", "Paddle P1 (upper right)"},
          {"right_paddle2", "Paddle P2 (lower right)"}, {"left_paddle1", "Paddle P3 (upper left)"},
          {"left_paddle2", "Paddle P4 (lower left)"},   {"touchpad", "Touchpad click"}};
      for (const auto& [name, label] : kExtras) {
        std::vector<std::pair<std::string, std::string>> choices;
        for (const auto& choice : pad_table.at(name).at("choices")) {
          std::string text = choice.get<std::string>();
          std::string upper = text;
          for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          choices.emplace_back(text, text.size() <= 2 ? upper : std::string(1, upper[0]) + text.substr(1));
        }
        KnobRow(pad, o.pad, pad_table, name, label,
                "The Xbox 360 button it presses. Guide works only with the guide_button cvar. On Windows, Xbox pads "
                "report neither paddles nor Share: an Elite's own profile can map its paddles to other buttons. These "
                "apply to PlayStation and other pads that report them.",
                KnobKind::kChoice, 1, "", &choices);
      }
    } else if (tab_ == 1) {
      KnobRow(wheel_knobs, o.wheel, wheel_table, "on", "Use a racing wheel",
              "A racing wheel drives the car beside the controller: its steering and pedals at the wheel's full "
              "resolution, its buttons for the shifts, the clutch and the handbrake, and force feedback from the "
              "game's steering torque. Off by default: no real wheel has been tested yet. Run the Wheel test tab "
              "first to find the axis and button numbers and the force's direction, and set the wheel's rotation "
              "to what Pit House (or the wheel's own software) uses.",
              KnobKind::kToggle);
      if (wheel_number("on") != 1) return;
      KnobRow(wheel_knobs, o.wheel, wheel_table, "input", "Drive with the wheel",
              "Off: the wheel is only opened, for its force feedback.", KnobKind::kToggle);
      section("STEERING");
      KnobRow(wheel_knobs, o.wheel, wheel_table, "steer_axis", "Steering axis",
              "The axis number the wheel test shows moving when you turn the wheel.", KnobKind::kWhole, 1);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "steer_invert", "Invert steering", "If turning right steers left.",
              KnobKind::kToggle);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "steer_range", "Wheel rotation",
              "The wheel's rotation lock to lock, as set in Pit House: the wheel reports it over its whole axis.",
              KnobKind::kSlider, 10, "\xC2\xB0");
      KnobRow(wheel_knobs, o.wheel, wheel_table, "car_lock", "Car steering lock",
              "Each car's steering lock, lock to lock, comes from its road-wheel lock in the game: this is the lock "
              "of most road cars (a 42\xC2\xB0 road-wheel lock); GT racing cars get 6/7 of it and prototypes 5/7 "
              "(at 900: 771 and 643). Past the car's lock the end stop holds the wheel. Never more than the wheel's "
              "rotation.",
              KnobKind::kSlider, 10, "\xC2\xB0");
      KnobRow(wheel_knobs, o.wheel, wheel_table, "steer_lock", "Fixed steering lock",
              "One lock for every car, lock to lock, in place of each car's own. 0: each car's own lock.",
              KnobKind::kSlider, 10, "\xC2\xB0");
      KnobRow(wheel_knobs, o.wheel, wheel_table, "end_stop", "End stop",
              "The force that holds the wheel past the car's steering lock, a fraction of the wheel's full force "
              "reached 6\xC2\xB0 past the lock; it never exceeds the force cap. 0: off, the wheel turns on to its "
              "own rotation.",
              KnobKind::kSlider);
      section_end("steering");
      static const std::pair<const char*, const char*> kPedals[] = {
          {"throttle", "THROTTLE PEDAL"}, {"brake", "BRAKE PEDAL"}, {"clutch", "CLUTCH PEDAL"}};
      for (const auto& [prefix, title] : kPedals) {
        const std::string p = prefix;
        const std::string axis = p + "_axis", rest = p + "_rest", pressed = p + "_pressed";
        int16_t raw = 0;
        const bool has_raw = raw_axis(static_cast<int>(wheel_number(axis)), &raw);
        section(title);
        KnobRow(wheel_knobs, o.wheel, wheel_table, axis.c_str(), "Axis",
                "The axis number the wheel test shows moving with this pedal; None: no pedal.", KnobKind::kWhole, 1);
        const char* set_help = "The raw axis value with the pedal released (rest) or fully pressed. While the wheel "
                               "test runs, A sets it from the pedal's position now; left and right step it.";
        for (const auto& [name, label] : {std::pair{rest, "Rest"}, std::pair{pressed, "Pressed"}})
          if (KnobRow(wheel_knobs, o.wheel, wheel_table, name.c_str(), label, set_help, KnobKind::kWhole, 1024) == 2 &&
              has_raw) {
            wheel_knobs.Set(name, std::to_string(raw));
            std::string joined;
            for (const auto& [key, item] : wheel_knobs.Items()) joined += (joined.empty() ? "" : ",") + key + "=" + item;
            o.wheel = joined;
          }
        KnobRow(wheel_knobs, o.wheel, wheel_table, (p + "_deadzone").c_str(), "Deadzone",
                "The share of the travel from rest that still reads 0.", KnobKind::kSlider);
        KnobRow(wheel_knobs, o.wheel, wheel_table, (p + "_gamma").c_str(), "Response curve",
                "1 is linear; above 1 finer at the start of the travel.", KnobKind::kSlider);
        section_end(prefix);
      }
      section("BUTTONS");
      static const std::pair<const char*, const char*> kButtons[] = {
          {"shift_up", "Shift up"}, {"shift_down", "Shift down"}, {"clutch_button", "Clutch, held"},
          {"handbrake_button", "Handbrake, held"}};
      for (const auto& [name, label] : kButtons) {
        const bool assigning = wheel_assign_ == name;
        const std::string help = std::string("The button number the wheel test shows; None: no button. While the wheel "
                                             "test runs, A waits for the next wheel button pressed.") +
                                 (assigning ? "\n\nPress a wheel button now; A again cancels." : "");
        if (KnobRow(wheel_knobs, o.wheel, wheel_table, name, label, help.c_str(), KnobKind::kWhole, 1) == 2 &&
            snap.connected) {
          wheel_assign_ = assigning ? "" : name;
          wheel_assign_buttons_ = snap.buttons;
        }
      }
      section_end("buttons");
      section("FORCE FEEDBACK");
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_cap", "Force cap",
              "The hard upper limit of every force together (the game's force, the spring, the damper, the "
              "vibrations and the end stop), a fraction of the wheel's full force, whatever the gain. 0.25 by "
              "default: raise it only once the forces feel right and safe on your wheel.",
              KnobKind::kSlider);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_gain", "Force gain", "Multiplies the game's force before the cap.",
              KnobKind::kSlider);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_min_force", "Minimum force",
              "The smallest force sent while the game asks for any, a fraction of the wheel's full force: lifts small "
              "forces above the wheel's own friction. 0 for a direct-drive base.",
              KnobKind::kSlider);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_smoothing", "Smoothing",
              "Smooths the game's force. 10 ms by default; 0: none; more is softer and later.",
              KnobKind::kSlider, 1, " ms");
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_spring", "Centring spring",
              "The game's centring spring, which grows with speed and fades when the rear slides. 1: as the game "
              "scales it; 0: off. The spring and the damper take at most half of the cap.",
              KnobKind::kSlider);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_damper", "Damper",
              "The game's damper. 1: as the game scales it; 0: off. The spring and the damper take at most half of the "
              "cap.",
              KnobKind::kSlider);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_road", "Kerbs and surface",
              "The vibration of kerbs, rumble strips, grass and gravel, a fraction of the wheel's full force at the "
              "game's strongest surface rumble. 0: off. The kerbs and impacts take at most a quarter of the cap.",
              KnobKind::kSlider);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_impact", "Impacts",
              "The jolt of hitting walls, cars and objects, a fraction of the wheel's full force at the game's "
              "strongest impact. 0: off. The kerbs and impacts take at most a quarter of the cap.",
              KnobKind::kSlider);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_physics", "Physics-rate force",
              "Follow the game's steering torque at its physics rate (about 360 times a second) rather than once a "
              "frame.",
              KnobKind::kToggle);
      KnobRow(wheel_knobs, o.wheel, wheel_table, "ffb_invert", "Invert force",
              "If the wheel test's direction pulse turns the wheel to the left.", KnobKind::kToggle);
    } else if (tab_ == 2) {
      if (OptionRow("test", "Wheel test", live ? "Running" : "Off",
                    "Reads a racing wheel and sends test forces to it, without the game: the device, its axes and "
                    "buttons, the force's direction, a centring spring and a slow sine. Every force goes through the "
                    "safety limits: it stops when this window loses focus, ramps in over 1.5 s and never exceeds the "
                    "force cap. Start with the wheel centred and your hands on it.")) {
        // A screenshot never starts the test.
        if (live) StopWheelTest();
        else if (!shot_) StartWheelTest();
      }
      char cap[16];
      std::snprintf(cap, sizeof(cap), "%.2f", wheel_cap_);
      float fraction = (wheel_cap_ - 0.05f) / 0.45f;
      if (const int d = OptionRow("cap", "Force cap", cap,
                                  "The hard upper limit of every test force, a fraction of the wheel's full force. Set "
                                  "it before the test starts.",
                                  !live, nullptr, &fraction, wheel_cap_ != 0.25f)) {
        if (d == 4) wheel_cap_ = 0.25f;
        else if (d == 3) wheel_cap_ = std::round((0.05f + fraction * 0.45f) * 100) / 100;
        else if (d == 1 || d == -1) wheel_cap_ = std::clamp(std::round((wheel_cap_ + d * 0.01f) * 100) / 100, 0.05f, 0.5f);
      }
      if (OptionRow("invert", "Invert force", wheel_invert_ ? "On" : "Off",
                    "Flips the force's direction, if the direction pulse turns the wheel to the left.", !live))
        wheel_invert_ = !wheel_invert_;
      if (OptionRow("record", "Record forces", wheel_record_ ? "On" : "Off",
                    "Writes every force sent to logs\\wheel-test-forces.csv.", !live))
        wheel_record_ = !wheel_record_;
      static const char* const kTests[] = {"Stop force", "Direction pulse 5%", "Spring 10%", "Spring 25%", "Sine 10%",
                                           "Sine 25%"};
      const bool spring = wheel_test_choice_ == 2 || wheel_test_choice_ == 3;
      if (const int d = OptionRow("force", "Test force", kTests[wheel_test_choice_],
                                  "Left and right pick a test force, A sends it. The direction pulse turns the wheel "
                                  "to the right; the spring centres it; the sine rocks it slowly.",
                                  live && snap.haptic && (!spring || snap.spring))) {
        if (d == 2) {
          static const std::pair<wheel::TestEffect, double> kEffects[] = {
              {wheel::TestEffect::kNone, 0.0},    {wheel::TestEffect::kPulse, 0.05}, {wheel::TestEffect::kSpring, 0.10},
              {wheel::TestEffect::kSpring, 0.25}, {wheel::TestEffect::kSine, 0.10},  {wheel::TestEffect::kSine, 0.25}};
          wheel_->SetTest(kEffects[wheel_test_choice_].first, kEffects[wheel_test_choice_].second);
        } else if (d == 1 || d == -1) {
          wheel_test_choice_ = (wheel_test_choice_ + d + 6) % 6;
        }
      }
    } else if (tab_ == 3) {
      KnobRow(keyboard, o.keyboard, keyboard_table, "smooth", "Smooth steering and pedals",
              "The keys move the steering, throttle and brake at limited rates instead of jumping, mapped into the "
              "game's own deadzones so they act from the first press. Off: the keys are digital, full lock and full "
              "pedal at once.",
              KnobKind::kToggle);
      KnobRow(keyboard, o.keyboard, keyboard_table, "steer_attack", "Steering attack",
              "Seconds from centre to full lock while a steering key is held.", KnobKind::kSlider, 0.01f, " s");
      KnobRow(keyboard, o.keyboard, keyboard_table, "steer_release", "Steering release",
              "Seconds from full lock back to centre after the key is released.", KnobKind::kSlider, 0.01f, " s");
      KnobRow(keyboard, o.keyboard, keyboard_table, "throttle_attack", "Throttle attack", "Seconds from 0 to full throttle.",
              KnobKind::kSlider, 0.01f, " s");
      KnobRow(keyboard, o.keyboard, keyboard_table, "throttle_release", "Throttle release",
              "Seconds from full throttle to 0.", KnobKind::kSlider, 0.01f, " s");
      KnobRow(keyboard, o.keyboard, keyboard_table, "brake_attack", "Brake attack", "Seconds from 0 to full brake.",
              KnobKind::kSlider, 0.01f, " s");
      KnobRow(keyboard, o.keyboard, keyboard_table, "brake_release", "Brake release", "Seconds from full brake to 0.",
              KnobKind::kSlider, 0.01f, " s");
      KnobRow(keyboard, o.keyboard, keyboard_table, "precision", "Precision share",
              "Share of full lock and full pedal while the precision key (Ctrl) is held.", KnobKind::kSlider);
      RowsTitle("MOUSE AND DEADZONES");
      if (OptionRow("right_stick", "Mouse moves the right stick", settings_.mouse_right_stick ? "On" : "Off",
                    "The mouse's motion moves the right stick (the camera), as the runtime does by default."))
        settings_.mouse_right_stick = !settings_.mouse_right_stick;
      KnobRow(keyboard, o.keyboard, keyboard_table, "mouse_steering", "Mouse steering",
              "The mouse's horizontal motion steers; the right stick then gets no mouse motion.", KnobKind::kToggle);
      KnobRow(keyboard, o.keyboard, keyboard_table, "mouse_steer_sensitivity", "Mouse steering sensitivity",
              "Thousandths of full lock per count of mouse motion.", KnobKind::kSlider);
      KnobRow(keyboard, o.keyboard, keyboard_table, "mouse_steer_centering", "Mouse steering centring",
              "Time constant of the return to centre when the mouse rests; 0 turns it off.", KnobKind::kSlider, 0.01f, " s");
      KnobRow(keyboard, o.keyboard, keyboard_table, "title_deadzones", "Into the game's deadzones",
              "Off: the keys' values go to the game as they are, and its deadzones apply on top (for comparisons).",
              KnobKind::kToggle);
    } else {
      for (const auto& bind : kBinds) {
        const std::string value = BindValue(bind);
        const bool capturing = capture_ == bind.cvar;
        const bool bad = !BindError(value).empty();
        if (rows_seen_ == row_focus_) focus_bind = bind.cvar;
        const int d = OptionRow(bind.cvar, bind.label, capturing ? "Press a key" : value.empty() ? "None" : value,
                                "The keys of this function, by the game's DEFAULT LAYOUT 1 (the pad input in brackets). "
                                "A waits for a key, which replaces the keys; Esc cancels. Several keys are typed "
                                "below, separated by commas, with modifiers as Shift+Up. Ctrl is the precision key and "
                                "also works while the other keys are held.",
                                true, capturing ? &kWarning : bad ? &kDanger : nullptr, nullptr,
                                settings_.binds.Has(bind.cvar));
        if (d == 4) settings_.binds.Erase(bind.cvar);
        else if (d == 2) capture_ = capturing ? "" : bind.cvar;
      }
    }
  }, [&] {
    ImGui::Dummy(ImVec2(0, 14 * scale_));
    ImGui::PushTextWrapPos(0);
    const auto developer_value = [&](const char* title, std::string& value, std::string& edit, bool& editing) {
      if (!settings_.developer) return;
      ImGui::Dummy(ImVec2(0, 8 * scale_));
      ImGui::PushFont(din_font_, 17);
      ImGui::TextColored(kMuted, "%s", title);
      ImGui::PopFont();
      // The field keeps its own text while it is edited and applies it when it is left.
      if (!editing) edit = value;
      ImGui::SetNextItemWidth(-1);
      ImGui::InputText("##value", &edit);
      editing = ImGui::IsItemActive();
      if (ImGui::IsItemDeactivatedAfterEdit()) value = edit;
    };
    if (tab_ == 0) {
      int count = 0;
      SDL_JoystickID* pads = SDL_GetGamepads(&count);
      for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetGamepadNameForID(pads[i]);
        ImGui::TextColored(kSuccess, "%s", Icon(kIconCheck).c_str());
        ImGui::SameLine(0, 12 * scale_);
        ImGui::Text("%s connected", name ? name : "A controller");
      }
      SDL_free(pads);
      if (count == 0) Muted("No controller connected. A connected controller works without setup.");
      developer_value("CONTROLLER VALUE (BUILD.PS1 -PAD)", o.pad, pad_edit_, pad_editing_);
    } else if (tab_ == 1) {
      if (focus_section == "steering") {
        int16_t raw = 0;
        if (raw_axis(static_cast<int>(wheel_number("steer_axis")), &raw)) {
          wheel::SteeringMap map;
          map.invert = wheel_number("steer_invert") == 1;
          map.range_deg = wheel_number("steer_range");
          map.lock_deg = wheel_number("steer_lock");
          map.car_lock_deg = wheel_number("car_lock");
          // A road car's lock: the game sets each car's own.
          const double steering = wheel::MapSteering(map, raw, map.reference_road_deg);
          char text[48];
          std::snprintf(text, sizeof(text), "%+.4f", steering);
          ImGui::TextColored(kMuted, "Steering now, -1 full left to 1 full right");
          ImGui::ProgressBar(static_cast<float>(steering * 0.5 + 0.5), ImVec2(-1, 0), text);
        }
      } else if (focus_section == "throttle" || focus_section == "brake" || focus_section == "clutch") {
        int16_t raw = 0;
        if (raw_axis(static_cast<int>(wheel_number(focus_section + "_axis")), &raw)) {
          wheel::PedalMap map;
          map.axis = static_cast<int>(wheel_number(focus_section + "_axis"));
          map.released = wheel_number(focus_section + "_rest");
          map.pressed = wheel_number(focus_section + "_pressed");
          map.deadzone = wheel_number(focus_section + "_deadzone");
          map.gamma = wheel_number(focus_section + "_gamma");
          const double value = wheel::MapPedal(map, raw);
          char text[48];
          std::snprintf(text, sizeof(text), "%.4f (raw %d)", value, raw);
          ImGui::TextColored(kMuted, "The pedal now, 0 to 1");
          ImGui::ProgressBar(static_cast<float>(value), ImVec2(-1, 0), text);
        }
      }
      if (!live && wheel_number("on") == 1)
        Muted("Start the wheel test on the Wheel test tab to see the values and to set the pedals and buttons from "
              "the wheel.");
      developer_value("WHEEL VALUE (BUILD.PS1 -WHEEL)", o.wheel, wheel_edit_, wheel_editing_);
    } else if (tab_ == 2) {
      if (!live) {
        Muted("The test is not running.");
      } else if (!snap.connected) {
        Colored(kWarning, "No wheel found yet. Connect it and check that Pit House sees it; this checks again every second.");
      } else {
        ImGui::Text("%s", snap.name.c_str());
        ImGui::TextColored(kMuted, "Vendor %04x, product %04x, %s", snap.vendor, snap.product,
                           snap.is_wheel_type ? "SDL wheel" : "joystick");
        ImGui::TextColored(snap.haptic ? kSecondary : kWarning, "Force feedback: %s%s",
                           snap.haptic ? "yes" : "none (SDL found no haptic device for it)",
                           snap.haptic && !snap.spring ? ", no spring effect" : "");
        static const char* kTests[] = {"none", "direction pulse", "spring", "sine"};
        const char* state = "on";
        if (snap.blocked & wheel::kBlockFocus) state = "zero: this window has no focus";
        else if (snap.blocked) state = "zero";
        else if (snap.flags & wheel::kFlagWatchdog) state = "zero: no force requested";
        else if (snap.flags & wheel::kFlagRamping) state = "ramping in";
        ImGui::Text("Test: %s   Force: %+.3f   Spring: %.3f   Ramp: %.0f%%   (%s)", kTests[static_cast<int>(snap.test)],
                    snap.output, snap.spring_level, snap.envelope * 100.0, state);
        ImGui::TextColored(kMuted, "Device updates: %llu, mean %.0f us, max %.0f us; force thread tick mean %.3f ms, "
                           "max %.3f ms",
                           static_cast<unsigned long long>(snap.sends), snap.send_us_mean, snap.send_us_max,
                           snap.tick_ms_mean, snap.tick_ms_max);
        for (size_t i = 0; i < snap.axes.size(); ++i) {
          const float fraction = (static_cast<float>(snap.axes[i]) + 32768.0f) / 65535.0f;
          char text[32];
          std::snprintf(text, sizeof(text), "%d", snap.axes[i]);
          ImGui::TextColored(kSecondary, "Axis %zu", i);
          ImGui::SameLine(80 * scale_);
          ImGui::ProgressBar(fraction, ImVec2(-1, 0), text);
        }
        std::string pressed;
        for (size_t i = 0; i < snap.buttons.size(); ++i)
          if (snap.buttons[i]) pressed += (pressed.empty() ? "" : " ") + std::to_string(i);
        ImGui::TextColored(kSecondary, "Buttons (%zu):", snap.buttons.size());
        ImGui::SameLine();
        ImGui::TextUnformatted(pressed.empty() ? "none pressed" : pressed.c_str());
        for (size_t i = 0; i < snap.hats.size(); ++i)
          ImGui::TextColored(kSecondary, "Hat %zu: %u", i, static_cast<unsigned>(snap.hats[i]));
      }
      std::lock_guard lock(wheel_log_mutex_);
      if (!wheel_log_.empty()) {
        ImGui::Dummy(ImVec2(0, 4 * scale_));
        for (const auto& line : wheel_log_) Muted(line);
      }
    } else if (tab_ == 3) {
      developer_value("KEYBOARD VALUE (BUILD.PS1 -KEYBOARD)", o.keyboard, keyboard_edit_, keyboard_editing_);
    } else {
      for (const auto& bind : kBinds) {
        if (focus_bind != bind.cvar) continue;
        ImGui::PushFont(din_font_, 17);
        ImGui::TextColored(kMuted, "KEYS");
        ImGui::PopFont();
        std::string value = BindValue(bind);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##keys", &value)) settings_.binds.Set(bind.cvar, value);
        const std::string bind_error = BindError(value);
        if (!bind_error.empty()) Colored(kDanger, bind_error);
      }
    }
    if (!error.empty()) Colored(kDanger, error);
    ImGui::PopTextWrapPos();
  });
}

void App::WheelLog(std::string_view text) {
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char stamp[16];
  std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
  const std::string line = std::string(stamp) + " " + std::string(text);
  std::lock_guard lock(wheel_log_mutex_);
  wheel_log_.push_back(line);
  while (wheel_log_.size() > 12) wheel_log_.pop_front();
  std::error_code error;
  fs::create_directories(root_ / "logs", error);
  std::ofstream file(root_ / "logs" / "wheel-test.log", std::ios::binary | std::ios::app);
  file << line << '\n';
}

void App::StartWheelTest() {
  namespace wheel = rex::input::wheel;
  if (wheel_) return;
  if (!wheel_haptic_) {
    // The joystick subsystem runs already (SDL_INIT_GAMEPAD); without haptics the wheel is still read.
    if (!SDL_InitSubSystem(SDL_INIT_HAPTIC))
      WheelLog(std::string("Wheel: SDL's haptic subsystem did not start: ") + SDL_GetError());
    wheel_haptic_ = true;
  }
  wheel::HostOptions options;
  options.governor.cap = wheel_cap_;
  options.invert = wheel_invert_;
  if (wheel_record_) {
    std::error_code error;
    fs::create_directories(root_ / "logs", error);
    options.record_path = Utf8(root_ / "logs" / "wheel-test-forces.csv");
  }
  wheel_ = std::make_unique<wheel::WheelHost>(options,
                                              [this](wheel::LogLevel, std::string_view text) { WheelLog(text); });
  wheel_->SetFocus((SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0);
  wheel_->Start();
}

void App::StopWheelTest() {
  if (!wheel_) return;
  wheel_->Stop();
  wheel_.reset();
}

std::string App::ShortPath(const std::string& path, float width) {
  if (ImGui::CalcTextSize(path.c_str()).x <= width) return path;
  // The end of the path, where the folder's own name is.
  for (size_t start = 1; start < path.size(); ++start) {
    const std::string text = "..." + path.substr(start);
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
  }
  return "...";
}

bool App::PathOptionRow(const char* id, const char* label, std::string& value, DialogTarget target, const char* help,
                        bool clearable, const ImVec4* color) {
  ImGui::PushFont(din_font_, 19);
  const std::string shown = value.empty() ? "Not set" : ShortPath(value, ImGui::GetContentRegionAvail().x * 0.5f);
  ImGui::PopFont();
  const int d = OptionRow(id, label, shown, help, true, value.empty() ? &kMuted : color);
  // A screenshot never opens a dialog.
  if (d == 2 && !shot_) OpenDialog(target, true);
  // X clears a folder that may be empty; the row is not marked as changed.
  if (!clearable || value.empty() || focus_id_ != id) return false;
  focus_changed_ = true;
  reset_label_ = "Clear";
  if (prompt_reset_ || (!ImGui::GetIO().WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) ||
                                                          ImGui::IsKeyPressed(ImGuiKey_Delete, false)))) {
    value.clear();
    return true;
  }
  return false;
}

bool App::PathField(std::string& value) {
  ImGui::PushFont(din_font_, 17);
  ImGui::TextColored(kMuted, "FOLDER");
  ImGui::PopFont();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##path", "Type a folder, or press A to browse", &value);
  return ImGui::IsItemDeactivatedAfterEdit();
}

void App::GameDataRows() {
  Options& o = settings_.Active().options;
  std::uintmax_t pending = 0;
  for (size_t i = 0; i < content_.packages.size(); ++i)
    if (i >= imported_.size() || !imported_[i]) pending += content_.packages[i].size;
  RowsPage({"DISCS", "CONTENT"}, [&] {
    if (tab_ == 0) {
      if (PathOptionRow("disc1", "Disc 1 (Play Disc)", settings_.disc1, kDisc1,
                        "The folder of your extracted Disc 1, the one Motorsport DX needs. A opens the folder picker.",
                        false, disc1_.error.empty() ? &kSuccess : &kDanger))
        Refresh(false);
      if (PathOptionRow("disc2", "Disc 2 (Content Disc)", settings_.disc2, kDisc2,
                        "The folder of your extracted Disc 2: its cars import on the first Play. Optional. A opens the "
                        "folder picker, X clears it.",
                        true, disc2_.error.empty() ? &kSuccess : &kDanger))
        Refresh(false);
      PathOptionRow("replace", "Texture pack", o.texture_replace, kTextureReplace,
                    "A folder of <hash>.dds files that replace the game's textures. Optional. A opens the folder "
                    "picker, X clears it.",
                    true, nullptr);
      return;
    }
    if (OptionRow("add_folder", "Add a content folder", "Press A",
                  "A folder searched for content packages, such as the console's Content folder.") == 2 &&
        !shot_)
      OpenDialog(kContentFolder, true);
    if (OptionRow("add_files", "Add content files", "Press A", "Content package files, picked one or several at a time.") ==
            2 &&
        !shot_)
      OpenDialog(kContentFiles, false);
    if (!settings_.content.empty()) RowsTitle("SEARCHED");
    for (size_t i = 0; i < settings_.content.size(); ++i) {
      const std::string id = "content" + std::to_string(i);
      ImGui::PushFont(din_font_, 19);
      const std::string name = ShortPath(settings_.content[i], ImGui::GetContentRegionAvail().x * 0.6f);
      ImGui::PopFont();
      if (OptionRow(id.c_str(), name.c_str(), "Remove",
                    "A folder or file searched for content packages. A removes it from the list; the files stay.",
                    true, &kSecondary) == 2) {
        settings_.content.erase(settings_.content.begin() + static_cast<long>(i));
        Refresh(false);
        break;
      }
    }
    RowsTitle("PACKAGES");
    if (content_.packages.empty())
      OptionRow("none", "No content packages", "", "The base game runs without them.", false);
    for (size_t i = 0; i < content_.packages.size(); ++i) {
      const Package& package = content_.packages[i];
      const bool imported = i < imported_.size() && imported_[i];
      const std::string id = "package" + std::to_string(i);
      const std::string help = Utf8(package.path) + "\n\n" + FormatSize(package.size) +
                               (imported ? ". Imported into the user data: A removes the import; the package file "
                                           "stays and imports again on the next Play while it is listed."
                                         : ". Imports into the user data on the next Play.");
      if (OptionRow(id.c_str(), Utf8(package.path.filename()).c_str(), imported ? "Imported" : "On the next Play",
                    help.c_str(), true, imported ? &kSuccess : &kWarning) == 2 &&
          imported) {
        remove_candidate_ = package;
        ImGui::OpenPopup("Remove import");
      }
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Remove import", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Remove the imported %s from the user data?", Utf8(remove_candidate_->path.filename()).c_str());
      Muted("The package file itself stays. It imports again on the next Play while it is listed.");
      if (ImGui::Button("Remove")) {
        RemoveImport(*remove_candidate_);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
          ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }, [&] {
    ImGui::Dummy(ImVec2(0, 14 * scale_));
    ImGui::PushTextWrapPos(0);
    if (tab_ == 0) {
      if (focus_id_ == "disc1") {
        if (disc1_.error.empty()) Colored(kSuccess, disc1_.release + " release, the three executables verified.");
        else Colored(kDanger, disc1_.error);
        ImGui::Dummy(ImVec2(0, 6 * scale_));
        if (PathField(settings_.disc1)) Refresh(false);
      } else if (focus_id_ == "disc2") {
        if (settings_.disc2.empty()) Muted("Not used: the cars of Disc 2 are not imported.");
        else if (disc2_.error.empty()) Colored(kSuccess, "The four content packages found.");
        else Colored(kDanger, disc2_.error);
        ImGui::Dummy(ImVec2(0, 6 * scale_));
        if (PathField(settings_.disc2)) Refresh(false);
      } else if (focus_id_ == "replace") {
        PathField(o.texture_replace);
      }
    } else {
      if (!content_.error.empty()) Colored(kDanger, content_.error);
      if (pending) {
        std::error_code error;
        fs::path drive = UserData();
        while (!drive.empty() && !fs::exists(drive)) drive = drive.parent_path();
        const auto space = fs::space(drive.empty() ? root_ : drive, error);
        Muted("The next Play imports " + FormatSize(pending) + " into the user data" +
              (error ? "." : " (" + FormatSize(space.available) + " free)."));
      }
      if (!content_.skipped.empty()) {
        ImGui::Dummy(ImVec2(0, 6 * scale_));
        ImGui::PushFont(din_font_, 17);
        ImGui::TextColored(kMuted, "SKIPPED FILES (%d)", static_cast<int>(content_.skipped.size()));
        ImGui::PopFont();
        for (const auto& line : content_.skipped) Muted(line);
      }
    }
    ImGui::PopTextWrapPos();
  });
}

bool App::ShadersReady() {
  if (!fs::exists(root_ / "game-build" / "fm4_rex.exe")) return false;
  std::ifstream marker(root_ / "logs" / "shader-pass.done", std::ios::binary);
  std::string done((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
  return done == ShaderPassMarker();
}

// The build the pass last finished for: the game's executable as build-result.json names it,
// and whether DXC was beside it.
std::string App::ShaderPassMarker() {
  try {
    std::ifstream stream(root_ / "build-result.json", std::ios::binary);
    const Json report = Json::parse(stream);
    return report.at("artifacts").at("fm4_rex.exe").get<std::string>() +
           (DxcPresent(root_ / "game-build") ? " dxil" : " fxc");
  } catch (const std::exception&) {
    return {};
  }
}

void App::CompileShaders() {
  std::string error;
  const auto plan = CurrentPlan(error);
  if (!plan || ProcessRunning(L"fm4_rex.exe")) {
    std::lock_guard<std::mutex> lock(shader_mutex_);
    shader_message_ = plan ? "The game is running (fm4_rex.exe). Close it first." : error;
    return;
  }
  std::vector<std::string> args = GameArgs(*plan);
  const fs::path progress_file = root_ / "logs" / "shader-pass.jsonl";
  args.push_back("--shader_offline_pass=true");
  args.push_back("--shader_offline_pass_progress=" + Utf8(progress_file));
  const Pairs env = plan->env;
  StartShaderTask([this, args, env, progress_file] {
    const fs::path game = root_ / "game-build";
    std::string fetch_error;
    if (!DxcPresent(game)) fetch_error = FetchDxc(game, root_ / "downloads" / "dxc");
    std::error_code ignored;
    fs::remove(progress_file, ignored);
    void* process = StartProcess(args, env, root_, root_ / "logs" / "shader-pass.log");
    {
      std::lock_guard<std::mutex> lock(shader_mutex_);
      shader_process_ = process;
    }
    const unsigned long code = WaitProcess(process);
    {
      std::lock_guard<std::mutex> lock(shader_mutex_);
      shader_process_ = nullptr;
    }
    PassProgress progress;
    const bool finished = ReadPassProgress(progress_file, progress) && progress.finished && code == 0;
    std::string message;
    if (finished) {
      std::ofstream(root_ / "logs" / "shader-pass.done", std::ios::binary | std::ios::trunc)
          << ShaderPassMarker();
      message = "The shaders are compiled: " + std::to_string(progress.done) + " translations, " +
                std::to_string(progress.errors) + " errors.";
    } else {
      message = "The shader pass stopped (exit " + std::to_string(code) +
                "); what is left compiles while you play.";
    }
    return fetch_error.empty() ? message : fetch_error + "\n" + message;
  });
}

void App::StartShaderTask(std::function<std::string()> task) {
  if (shader_busy_.exchange(true)) return;
  if (shader_task_.joinable()) shader_task_.join();
  shader_task_ = std::thread([this, task = std::move(task)] {
    std::string message;
    try {
      message = task();
    } catch (const std::exception& exception) {
      message = exception.what();
    }
    {
      std::lock_guard<std::mutex> lock(shader_mutex_);
      shader_message_ = message;
    }
    shader_busy_ = false;
  });
}

// The offline shader pass (docs/analysis/d3d-hle-n6.md 19.3.9, the owner's answer 5 of 19.10):
// the game's executable with the plan's settings translates and compiles its shader storage and
// exits before the title runs, so the shaders are not compiled while the game plays. With
// native_shader_dxil it compiles with the DirectX Shader Compiler, which this page fetches from
// Microsoft's release on this computer (never shipped with the launcher).
void App::ShadersRows() {
  const fs::path game = root_ / "game-build";
  const bool busy = shader_busy_;
  const bool dxc = DxcPresent(game);
  const Status status = ShaderStatus();
  RowsPage({}, [&] {
    OptionRow("state", "Shaders", status == Status::kDone ? "Compiled" : status == Status::kBusy ? "Compiling" : "Not compiled",
              "The shader pass compiles the game's shaders for the current settings before you play, so they are not "
              "compiled during a race. It runs once after an install or an update; Skip stops it, and what is left "
              "compiles while you play.",
              true, status == Status::kDone ? &kSuccess : status == Status::kBusy ? &kFocusColor : &kMuted);
    // A screenshot never starts a task.
    if (OptionRow("compile", "Compile shaders", "Press A",
                  "Runs the shader pass now with the active profile's settings: the game starts without a window, "
                  "compiles and exits.",
                  !busy) == 2 &&
        !shot_)
      CompileShaders();
    if (OptionRow("skip", "Skip", "Press A", "Stops the pass; what is left compiles while you play.", busy) == 2) {
      std::lock_guard<std::mutex> lock(shader_mutex_);
      if (shader_process_) TerminateProcess(shader_process_, 1);
    }
    if (OptionRow("dxc", "DirectX Shader Compiler", dxc ? "Beside the game" : "Not fetched",
                  "Microsoft's compiler for the native path's shaders. A fetches its release v1.9.2609 from GitHub, "
                  "checked by its SHA-256 (or from FM4_DXC_MIRROR), and puts its two DLLs and license files beside the "
                  "game. Without it the shaders compile with FXC.",
                  !busy && !dxc, dxc ? &kSuccess : nullptr) == 2 &&
        !shot_) {
      StartShaderTask([this, game] {
        const std::string error = FetchDxc(game, root_ / "downloads" / "dxc");
        return error.empty() ? std::string("The DirectX Shader Compiler is beside the game.") : error;
      });
    }
  }, [&] {
    ImGui::Dummy(ImVec2(0, 14 * scale_));
    PassProgress progress;
    if (busy && ReadPassProgress(root_ / "logs" / "shader-pass.jsonl", progress) && progress.total > 0) {
      ImGui::ProgressBar(static_cast<float>(progress.done) / static_cast<float>(progress.total), ImVec2(-1, 0),
                         (progress.stage + ": " + std::to_string(progress.done) + " of " + std::to_string(progress.total))
                             .c_str());
    } else if (busy) {
      Muted("Working...");
    }
    std::lock_guard<std::mutex> lock(shader_mutex_);
    ImGui::PushTextWrapPos(0);
    if (!shader_message_.empty()) ImGui::TextWrapped("%s", shader_message_.c_str());
    ImGui::PopTextWrapPos();
  });
}

void App::AdvancedRows() {
  Options& o = settings_.Active().options;
  std::vector<const char*> tabs = {"GENERAL"};
  if (settings_.developer) tabs.push_back("DEVELOPER");
  if (tab_ >= static_cast<int>(tabs.size())) tab_ = 0;
  const auto toggle = [&](const char* id, const char* label, bool& value, const char* help) {
    if (const int d = OptionRow(id, label, value ? "On" : "Off", help, true, nullptr, nullptr, value))
      value = d == 4 ? false : !value;
  };
  RowsPage(tabs, [&] {
    if (tab_ == 0) {
      const std::string fallback = Utf8(root_ / "user-data");
      if (PathOptionRow("userdata", "User data", settings_.user_data, kUserData,
                        "The folder of the profile, the saves and the imported content. Not set: user-data beside the "
                        "game. Moving it does not move them. X goes back to the default.",
                        true, nullptr))
        Refresh(false);
      // A screenshot never opens a folder.
      if (OptionRow("logs", "Open the logs folder", "Press A", "The game's and the launcher's logs.") == 2 && !shot_) {
        fs::create_directories(root_ / "logs");
        OpenPath(root_ / "logs");
      }
      if (OptionRow("open_userdata", "Open the user data", "Press A", "The profile, the saves and the imported content.") ==
              2 &&
          !shot_) {
        fs::create_directories(UserData());
        OpenPath(UserData());
      }
      toggle("developer", "Show developer options", settings_.developer,
             "The render paths, test switches and raw option values, on this page's Developer tab and on the others: "
             "for comparisons and bug reports.");
      return;
    }
    toggle("vblank", "Fixed 60 Hz vblank", o.fixed_vblank, "Holds every swap to the vblank grid.");
    toggle("shadow", "Shadow workaround", o.shadow_workaround, "The shadow map only with the cockpit camera.");
    toggle("single_tile", "One tile without MSAA without TAA", o.single_tile,
           "Races in one tile without MSAA also with a look without TAA (then FXAA): -SingleTile.");
    toggle("ao", "Ambient occlusion without the knob", o.ao, "The ambient occlusion pass also without the look's ao knob.");
    toggle("rov", "ROV render path", o.rov, "The uncorrected reference of the EDRAM emulation, slower: -Rov.");
    // The render path: -1 automatic (the native path unless ROV or the MSAA tiles), 0 RTV (-Rtv),
    // 1 the native path (-Native).
    static const char* const kPaths[] = {"Automatic", "RTV", "Native path"};
    const int path = o.native < 0 ? 0 : o.native == 0 ? 1 : 2;
    if (const int d = OptionRow("path", "Render path", kPaths[path],
                                "Automatic is the native path unless ROV or the MSAA tiles are on. RTV: the PM4 path on "
                                "host render targets (-Rtv). Native path: races in one pass with MSAA, the native port's "
                                "stages (-Native).",
                                true, nullptr, nullptr, o.native >= 0)) {
      const int next = d == 4 ? 0 : (path + (d < 0 ? -1 : 1) + 3) % 3;
      o.native = next == 0 ? -1 : next == 1 ? 0 : 1;
    }
    const bool native = o.native < 0 ? !o.rov && !o.msaa : o.native == 1;
    // -MsaaSamples: -1 (unset) is one pass with 2x, the default.
    static const char* const kMsaa[] = {"One pass, 2x", "One pass, 4x", "One tile, none"};
    const int msaa = o.msaa_samples == 4 ? 1 : o.msaa_samples == 0 ? 2 : 0;
    if (const int d = OptionRow("msaa", "Native MSAA", kMsaa[msaa],
                                "The native path's races: one pass with 2x MSAA (the default), one pass with 4x "
                                "(-MsaaSamples 4), or one tile without MSAA (-MsaaSamples 0).",
                                native, nullptr, nullptr, o.msaa_samples >= 0)) {
      const int next = d == 4 ? 0 : (msaa + (d < 0 ? -1 : 1) + 3) % 3;
      o.msaa_samples = next == 1 ? 4 : next == 2 ? 0 : -1;
    }
    toggle("perf", "Performance trace", o.perf_trace,
           "Writes the perf trace, one JSON line a second, to logs\\perf-<profile>-<time>.jsonl.");
    if (OptionRow("cvars", "Extra cvars", o.extra_cvars.empty() ? "None" : "Set",
                  "Runtime settings written as --name=value, separated by spaces, applied after all the others: for "
                  "A/B runs (docs/running.md, Ally X run set). Type them in the field at the right.",
                  true, nullptr, nullptr, !o.extra_cvars.empty()) == 4)
      o.extra_cvars.clear();
    PathOptionRow("dump", "Texture dump", o.texture_dump, kTextureDump,
                  "Writes each new game texture once as <hash>.dds into this folder. A opens the folder picker, X "
                  "clears it.",
                  true, nullptr);
    if (OptionRow("command", "Copy the command line", "Press A",
                  "Copies the environment and the game's command line of the active profile, for a run without the "
                  "launcher.") == 2) {
      std::string error;
      if (const auto plan = CurrentPlan(error)) {
        std::string text;
        for (const auto& [name, value] : plan->env.Items()) text += "set \"" + name + "=" + value + "\"\r\n";
        text += CommandLine(GameArgs(*plan)) + "\r\n";
        if (!shot_) SDL_SetClipboardText(text.c_str());
      }
    }
  }, [&] {
    ImGui::Dummy(ImVec2(0, 14 * scale_));
    ImGui::PushTextWrapPos(0);
    if (focus_id_ == "userdata") {
      Muted(settings_.user_data.empty() ? "Default: " + Utf8(root_ / "user-data") : "Now: " + settings_.user_data);
      ImGui::Dummy(ImVec2(0, 6 * scale_));
      if (PathField(settings_.user_data)) Refresh(false);
    } else if (focus_id_ == "dump") {
      PathField(o.texture_dump);
    } else if (focus_id_ == "cvars") {
      ImGui::PushFont(din_font_, 17);
      ImGui::TextColored(kMuted, "EXTRA CVARS");
      ImGui::PopFont();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputTextWithHint("##cvars", "--name=value --name=value", &o.extra_cvars);
    }
    ImGui::PopTextWrapPos();
  });
}

// Who made this possible, in short; CREDITS.txt beside the launcher (outputs/FM4-build/CREDITS.txt,
// copied by build and export) says it in full, and third-party-notices holds the licenses.
void App::AboutRows() {
  const fs::path credits = root_ / "CREDITS.txt";
  const fs::path notices = root_ / "third-party-notices";
  // A short name for the list, the full one and what each gave.
  static const char* const kThanks[][3] = {
      {"ReXGlue SDK", "The ReXGlue SDK", "Tom (crack), its founder, and the ReXGlue contributors: the recompiler and runtime."},
      {"Xenia", "Xenia", "Ben Vanik and the Xenia project contributors: the Xbox 360 emulation it all builds on."},
      {"Xenia Canary", "Xenia Canary", "Its game patches and arithmetic, which parts of the FM4 fixes follow."},
      {"XenonRecomp", "XenonRecomp", "hedge-dev and contributors: the modern Xbox 360 static recompiler."},
      {"rexdex's recompiler", "rexdex's recompiler", "The original Xbox 360 static recompiler."},
      {"XenosRecomp, Unleashed Recompiled", "XenosRecomp and Unleashed Recompiled",
       "hedge-dev: ideas for the shader mods and the native path."},
      {"Xbox 360 communities", "The Xbox 360 homebrew, modding and reverse-engineering communities",
       "Their shared research."},
      {"Build tools", "LLVM and Clang, Python, the Windows SDK and the DirectX compilers", "The tools that build it."},
      {"Turn 10 Studios", "Turn 10 Studios", "Who made Forza Motorsport 4."},
  };
  RowsPage({}, [&] {
    OptionRow("about", "Motorsport DX", build_date_.empty() ? "" : "Build " + build_date_,
              "Runs Forza Motorsport 4 (Xbox 360), recompiled for Windows from your own discs.\n\nMotorsport DX is not "
              "affiliated with or endorsed by Microsoft, Xbox Game Studios or Turn 10 Studios. Forza Motorsport and "
              "Xbox are trademarks of the Microsoft group of companies.");
    RowsTitle("MADE POSSIBLE BY");
    for (const auto& [name, full, what] : kThanks) {
      const std::string help = std::string(full) + ". " + what;
      OptionRow(name, name, "", help.c_str());
    }
    RowsTitle("LICENSES");
    // A screenshot never opens a file.
    if (OptionRow("credits", "Open the credits", "Press A",
                  "CREDITS.txt: the projects that made Motorsport DX possible, in full.", fs::exists(credits)) == 2 &&
        !shot_)
      OpenPath(credits);
    if (OptionRow("notices", "Open the third-party notices", "Press A",
                  fs::exists(notices) ? "The licenses of the libraries in the program files."
                                      : "Run build (or export) to copy CREDITS.txt and third-party-notices here.",
                  fs::exists(notices)) == 2 &&
        !shot_)
      OpenPath(notices);
  }, [&] {
    if (focus_id_ != "notices" || !fs::exists(notices)) return;
    ImGui::Dummy(ImVec2(0, 14 * scale_));
    ImGui::PushTextWrapPos(0);
    Muted(Utf8(notices));
    ImGui::PopTextWrapPos();
  });
}

}  // namespace

int main(int argc, char** argv) {
  App app;
  if (!app.Init(argc, argv)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Motorsport DX", SDL_GetError(), nullptr);
    return 1;
  }
  if (app.Headless()) return app.Screenshot();
  app.Run();
  return 0;
}
