// Tests of the racing wheel's host (rex/input/wheel/wheel_host.h,
// docs/analysis/racing-wheels.md) without hardware: an SDL virtual wheel for
// the input path, and the recording backend for the force path. SDL's real
// joystick drivers are off, and the haptic subsystem never starts, so no force
// reaches a device connected to the computer. Checks that the host finds the
// virtual wheel, reads its axes at full 16-bit resolution with its buttons and
// hat, that every recorded force stays within the cap, ramps in, is zero while
// the window has no focus and after an emergency stop, that the title's forces
// (stage 3) arrive with their spring and damper inside the cap and stop at once
// outside a driven race and through the watchdog when they stop coming, and
// that a disconnect is noticed. Argument: a directory for the recordings.
// Returns 1 on failure.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
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
  double send_us = 0.0;
  double damper = 0.0;
  double spring_coeff = 0.0;
  double damper_coeff = 0.0;
  int driving = -1;
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
      stream >> comma >> row.send_us >> comma >> row.damper >> comma >> row.spring_coeff >>
          comma >> row.damper_coeff >> comma >> row.driving;
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
    // The driving controls (stage 2): pedal 2 rests at +32767, shift up on button 17.
    options.mapping.brake.released = 32767.0;
    options.mapping.brake.pressed = -32768.0;
    options.mapping.shift_up_button = 17;
    std::mutex controls_mutex;
    wheel::Controls published;
    uint64_t publications = 0;
    options.on_controls = [&](const wheel::Controls& controls) {
      std::lock_guard lock(controls_mutex);
      published = controls;
      ++publications;
    };
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
    {
      std::lock_guard lock(controls_mutex);
      Check(published.connected && publications > 10, "the controls are published every tick");
      Check(std::fabs(published.steering - 12345.0 / 32767.0) < 1e-12,
            "the steering is published at full resolution");
      Check(published.throttle == 0.0 && std::fabs(published.brake - 32766.0 / 65535.0) < 1e-12,
            "the pedals are published with their calibration");
      Check(published.shift_up && published.shift_up_presses == 1, "a shift press is counted");
      Check(std::fabs(input.controls.steering - published.steering) < 1e-12,
            "the snapshot shows the controls");
    }

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

  // The title's forces (stage 3): a frame-rate source with a spring and a damper.
  {
    const auto record = directory / "wheel-host-test-title.csv";
    std::mutex source_mutex;
    wheel::ForceSample source;
    uint64_t sequence = 0;
    const auto publish = [&](const wheel::ForceSample& sample) {
      std::lock_guard lock(source_mutex);
      source = sample;
      ++sequence;
    };
    wheel::HostOptions options;
    options.sink = wheel::Sink::kRecord;
    options.record_path = record.string();
    options.governor.cap = 0.25;
    options.governor.ramp_s = 0.2;
    options.governor.smoothing_s = 0.01;
    options.force_source = [&](wheel::ForceSample* out) {
      std::lock_guard lock(source_mutex);
      *out = source;
      return sequence;
    };
    wheel::WheelHost host(options, Printer());
    host.SetFocus(true);
    host.Start();
    Check(WaitFor(host, true, 2000), "the fourth host finds the virtual wheel");
    // About 240 samples a second: the title's mix at full scale, a sine of the torque.
    const auto drive = [&](bool driving, int ms) {
      for (int elapsed = 0; elapsed < ms; elapsed += 4) {
        wheel::ForceSample sample;
        sample.driving = driving;
        sample.torque = std::sin(elapsed * 0.004);
        sample.constant = driving ? sample.torque : 0.0;
        sample.spring = {0.5, 0.5};
        sample.damper = {0.1, 0.1};
        publish(sample);
        Sleep(4);
      }
    };
    drive(true, 1200);
    const wheel::Snapshot driving = host.GetSnapshot();
    Check(driving.title_driving && driving.title_samples > 100, "the title's samples arrive");
    Check(driving.spring_level > 0.0 && driving.damper_level > 0.0,
          "the spring and the damper run");
    drive(false, 200);
    const wheel::Snapshot stopped = host.GetSnapshot();
    Check(stopped.output == 0.0 && stopped.spring_level == 0.0 &&
              (stopped.blocked & wheel::kBlockPaused) != 0,
          "outside a driven race every force is zero");
    drive(true, 600);
    Sleep(300);  // no samples: the watchdog
    Check(host.GetSnapshot().output == 0.0 && host.GetSnapshot().spring_level == 0.0,
          "the watchdog zeroes the forces when the samples stop");
    host.Stop();

    const std::vector<Row> rows = ReadRecording(record);
    double worst_sum = 0.0;
    bool zero_when_stopped = true;
    bool saw_stopped = false;
    bool followed_sign = true;
    int driven_rows = 0;
    for (const Row& row : rows) {
      worst_sum = std::fmax(worst_sum, std::fabs(row.output) + row.spring + row.damper);
      if (row.driving == 0) {
        saw_stopped = true;
        zero_when_stopped = zero_when_stopped && row.output == 0.0 && row.spring == 0.0 &&
                            row.damper == 0.0;
      }
      if (row.driving == 1 && row.envelope >= 1.0 && std::fabs(row.target) > 0.3) {
        ++driven_rows;
        followed_sign = followed_sign && (row.output > 0.0) == (row.target > 0.0);
      }
    }
    Check(rows.size() > 300, "the title's forces are recorded");
    Check(worst_sum <= 0.25 + 2.0 / 32767.0, "the force, spring and damper stay within the cap");
    Check(saw_stopped && zero_when_stopped, "the recording is zero outside a driven race");
    Check(driven_rows > 50 && followed_sign, "the force follows the torque's sign");
    Check(!rows.empty() && rows.back().output == 0.0 && rows.back().spring == 0.0,
          "the recording ends at zero");
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
