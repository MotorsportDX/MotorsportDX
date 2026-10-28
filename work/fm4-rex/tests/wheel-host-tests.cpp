// Tests of the racing wheel's host (rex/input/wheel/wheel_host.h,
// docs/analysis/racing-wheels.md) without hardware: an SDL virtual wheel for
// the input path, and the recording backend for the force path. SDL's real
// joystick drivers are off, and the haptic subsystem never starts, so no force
// reaches a device connected to the computer. Checks that the host finds the
// virtual wheel, reads its axes at full 16-bit resolution with its buttons and
// hat, that every recorded force stays within the cap, ramps in, is zero while
// the window has no focus and after an emergency stop, and that a disconnect is
// noticed. Argument: a directory for the recordings. Returns 1 on failure.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include <rex/input/wheel/wheel_host.h>

namespace {

namespace wheel = rex::input::wheel;

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Row {
  double time = 0.0;
  double target = 0.0;
  double output = 0.0;
  double spring = 0.0;
  double envelope = 0.0;
  uint32_t blocked = 0;
  uint32_t flags = 0;
};

std::vector<Row> ReadRecording(const std::filesystem::path& path) {
  std::vector<Row> rows;
  std::ifstream file(path);
  std::string line;
  std::getline(file, line);  // the header
  while (std::getline(file, line)) {
    Row row;
    char comma;
    std::istringstream stream(line);
    stream >> row.time >> comma >> row.target >> comma >> row.output >> comma >> row.spring >>
        comma >> row.envelope >> comma >> row.blocked >> comma >> row.flags;
    if (stream) {
      rows.push_back(row);
    }
  }
  return rows;
}

wheel::LogFunction Printer() {
  return [](wheel::LogLevel, std::string_view text) {
    std::printf("  %.*s\n", int(text.size()), text.data());
  };
}

bool WaitFor(wheel::WheelHost& host, bool connected, int timeout_ms) {
  for (int waited = 0; waited < timeout_ms; waited += 10) {
    SDL_UpdateJoysticks();
    if (host.GetSnapshot().connected == connected) {
      return true;
    }
    Sleep(10);
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path directory = argc > 1 ? argv[1] : ".";
  // Only SDL's virtual joysticks.
  for (const char* hint :
       {SDL_HINT_JOYSTICK_RAWINPUT, SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_DIRECTINPUT,
        SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT,
        SDL_HINT_JOYSTICK_MFI}) {
    SDL_SetHint(hint, "0");
  }
  if (!SDL_Init(SDL_INIT_JOYSTICK)) {
    std::printf("FAIL SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  // The runtime's SDL must be built with haptics (SDL_HAPTIC). Starting the subsystem sends no
  // force: the hosts below use the recording backend and open no haptic device.
  if (!SDL_InitSubSystem(SDL_INIT_HAPTIC)) {
    std::printf("FAIL SDL's haptic subsystem: %s\n", SDL_GetError());
    ++failures;
  }

  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_WHEEL;
  desc.naxes = 3;
  desc.nbuttons = 24;
  desc.nhats = 1;
  desc.name = "FM4 virtual wheel";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  Check(id != 0, "the virtual wheel attaches");
  SDL_Joystick* virtual_wheel = SDL_OpenJoystick(id);
  Check(virtual_wheel != nullptr, "the virtual wheel opens");

  // The input path and the force path with a sine, the cap and focus loss.
  {
    const auto record = directory / "wheel-host-test-forces.csv";
    wheel::HostOptions options;
    options.sink = wheel::Sink::kRecord;
    options.record_path = record.string();
    options.governor.cap = 0.15;
    options.governor.ramp_s = 0.5;
    wheel::WheelHost host(options, Printer());
    host.SetFocus(true);
    host.Start();
    Check(WaitFor(host, true, 2000), "the host finds the virtual wheel");
    const wheel::Snapshot first = host.GetSnapshot();
    Check(first.name == "FM4 virtual wheel", "the host takes the virtual wheel");
    Check(first.is_wheel_type, "SDL reports the wheel type");
    Check(!first.haptic, "the recording backend opens no haptic device");

    SDL_SetJoystickVirtualAxis(virtual_wheel, 0, 12345);
    SDL_SetJoystickVirtualAxis(virtual_wheel, 1, -32768);
    SDL_SetJoystickVirtualAxis(virtual_wheel, 2, 1);
    SDL_SetJoystickVirtualButton(virtual_wheel, 17, true);
    SDL_SetJoystickVirtualHat(virtual_wheel, 0, SDL_HAT_RIGHT);
    SDL_UpdateJoysticks();
    Sleep(40);
    const wheel::Snapshot input = host.GetSnapshot();
    Check(input.axes.size() == 3 && input.axes[0] == 12345 && input.axes[1] == -32768 &&
              input.axes[2] == 1,
          "the axes arrive at full resolution");
    Check(input.buttons.size() == 24 && input.buttons[17] == 1 && input.buttons[16] == 0,
          "the buttons arrive");
    Check(input.hats.size() == 1 && input.hats[0] == SDL_HAT_RIGHT, "the hat arrives");

    host.SetTest(wheel::TestEffect::kSine, 0.4);
    Sleep(1200);
    // Flushed while running, so a killed process keeps its recording.
    Check(ReadRecording(record).size() > 100, "the recording is flushed while the host runs");
    host.SetFocus(false);
    Sleep(150);
    host.SetFocus(true);
    Sleep(300);
    host.SetTest(wheel::TestEffect::kNone, 0.0);
    Sleep(200);
    host.Stop();

    const std::vector<Row> rows = ReadRecording(record);
    Check(rows.size() > 500, "the recording holds the forces sent");
    double max_abs = 0.0;
    bool zero_while_unfocused = true;
    bool saw_unfocused = false;
    bool ramped = false;
    for (const Row& row : rows) {
      max_abs = std::fmax(max_abs, std::fabs(row.output));
      if (row.blocked & wheel::kBlockFocus) {
        saw_unfocused = true;
        zero_while_unfocused = zero_while_unfocused && row.output == 0.0;
      }
    }
    for (const Row& row : rows) {
      if (row.output != 0.0) {
        ramped = std::fabs(row.output) < 0.01;
        break;
      }
    }
    Check(max_abs <= 0.15 + 1.0 / 32767.0, "every recorded force is within the cap");
    Check(max_abs >= 0.14, "the sine reaches the cap");
    Check(saw_unfocused && zero_while_unfocused, "the force is zero while unfocused");
    Check(ramped, "the first force is small (the ramp-in)");
    Check(!rows.empty() && rows.back().output == 0.0, "the recording ends at zero");
  }

  // The emergency stop (the crash and exit path) zeroes a running force.
  {
    const auto record = directory / "wheel-host-test-emergency.csv";
    wheel::HostOptions options;
    options.sink = wheel::Sink::kRecord;
    options.record_path = record.string();
    options.governor.cap = 0.25;
    options.governor.ramp_s = 0.0;
    wheel::WheelHost host(options, Printer());
    host.SetFocus(true);
    host.Start();
    Check(WaitFor(host, true, 2000), "the second host finds the virtual wheel");
    host.SetTest(wheel::TestEffect::kPulse, 0.2);
    Sleep(300);
    Check(std::fabs(host.GetSnapshot().output) > 0.1, "the pulse is running before the stop");
    wheel::WheelHost::EmergencyStopAll();
    host.Stop();
    const std::vector<Row> rows = ReadRecording(record);
    Check(!rows.empty() && rows.back().output == 0.0 &&
              (rows.back().blocked & wheel::kBlockExit) != 0,
          "the emergency stop ends at zero");
  }

  // A disconnect is noticed.
  {
    wheel::HostOptions options;
    options.sink = wheel::Sink::kRecord;
    wheel::WheelHost host(options, Printer());
    host.Start();
    Check(WaitFor(host, true, 2000), "the third host finds the virtual wheel");
    SDL_CloseJoystick(virtual_wheel);
    SDL_DetachVirtualJoystick(id);
    Check(WaitFor(host, false, 2000), "the host notices the disconnect");
    host.Stop();
  }

  SDL_Quit();
  std::printf("%s\n", failures ? "wheel host tests FAILED" : "wheel host tests passed");
  return failures ? 1 : 0;
}
