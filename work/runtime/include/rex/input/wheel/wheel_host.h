/**
 * @file        input/wheel/wheel_host.h
 * @brief       The racing wheel's device and force feedback host
 *
 * Reads a racing wheel through SDL's joystick API (every axis and button at
 * device resolution, no gamepad mapping) and sends force feedback through
 * SDL's haptic API from a thread of its own, through the safety governor
 * (ffb_governor.h). Free of runtime dependencies: the runtime's SDL input
 * driver and the GUI launcher's wheel test both use it, with their own log.
 * See docs/analysis/racing-wheels.md, stage 0.
 *
 * The caller initialises SDL's joystick and haptic subsystems before Start
 * and pumps SDL's events (which update the joysticks). All haptic calls run
 * on the host's own thread.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <rex/input/wheel/ffb_governor.h>

namespace rex::input::wheel {

enum class LogLevel { kInfo, kWarning, kError };
using LogFunction = std::function<void(LogLevel, std::string_view)>;

enum class Sink {
  // SDL's haptic device of the wheel (DirectInput on Windows), and the recording
  // if a path is set.
  kDevice,
  // No force reaches any device; every force is only recorded (the recording
  // backend, for checks without hardware).
  kRecord,
};

// The launcher's test forces. Each is a source like the title's torque and goes
// through the governor.
enum class TestEffect {
  kNone,
  // A constant force towards positive (right, unless inverted) for one second:
  // shows the force's direction.
  kPulse,
  // A centring spring that the firmware runs, ramped in.
  kSpring,
  // A 1 Hz sine as a constant force from this thread.
  kSine,
};

struct HostOptions {
  GovernorConfig governor;
  Sink sink = Sink::kDevice;
  // A CSV file of every force sent, UTF-8 (empty: none).
  std::string record_path;
  // Flips the constant force's sign, for a base whose positive is to the left.
  bool invert = false;
  // The force thread's rate.
  int rate_hz = 1000;
  // Only joysticks whose name contains this (case-insensitive); empty: any.
  std::string name_filter;
};

struct Snapshot {
  bool connected = false;
  std::string name;
  uint16_t vendor = 0;
  uint16_t product = 0;
  bool is_wheel_type = false;
  bool haptic = false;
  bool spring = false;
  std::vector<int16_t> axes;
  std::vector<uint8_t> buttons;
  std::vector<uint8_t> hats;
  // The governor's last tick.
  double target = 0.0;
  double output = 0.0;
  double spring_level = 0.0;
  double envelope = 0.0;
  uint32_t blocked = 0;
  uint32_t flags = 0;
  TestEffect test = TestEffect::kNone;
  // Device updates sent, and their cost (SDL_UpdateHapticEffect), in microseconds.
  uint64_t sends = 0;
  double send_us_mean = 0.0;
  double send_us_max = 0.0;
  // The force thread's measured tick period, in milliseconds.
  double tick_ms_mean = 0.0;
  double tick_ms_max = 0.0;
};

class WheelHost {
 public:
  WheelHost(HostOptions options, LogFunction log);
  ~WheelHost();
  WheelHost(const WheelHost&) = delete;
  WheelHost& operator=(const WheelHost&) = delete;

  void Start();
  // Zero force, stop and close the device, end the thread. Idempotent.
  void Stop();

  // The window's focus: without it the force is zero.
  void SetFocus(bool focus) { SetBlocked(kBlockFocus, !focus); }
  void SetBlocked(uint32_t reason, bool blocked);
  // A new value from the force source (the title's torque from stage 3), -1..1.
  void SubmitForce(double force);
  void SetTest(TestEffect test, double amplitude);

  Snapshot GetSnapshot() const;

  // The crash and exit path: zero force on every running host at once, from any
  // thread. Installed as an unhandled exception filter, an atexit handler and a
  // SIGABRT handler by the first Start.
  static void EmergencyStopAll();

 private:
  struct Device;
  void Run();
  void Log(LogLevel level, std::string_view text) const;
  void Scan(double now_s);
  void CloseDevice();
  void SendForces(double now_s, double constant, double spring);
  void ZeroDevice();
  void Record(double now_s, double constant, double spring, double send_us);
  void EmergencyStop();

  HostOptions options_;
  LogFunction log_;
  Governor governor_;
  std::unique_ptr<Device> device_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::atomic<bool> emergency_{false};
  std::atomic<bool> emergency_done_{false};
  std::atomic<uint32_t> blocked_{0};
  // The open SDL_Haptic, for the emergency stop when the thread does not answer.
  std::atomic<void*> haptic_published_{nullptr};
  std::atomic<uint64_t> source_bits_{0};
  std::atomic<uint64_t> source_ns_{0};
  std::atomic<int> test_{0};
  std::atomic<uint64_t> test_amplitude_bits_{0};
  std::atomic<uint64_t> test_started_ns_{0};
  // The force thread's own state.
  int16_t last_sent_level_ = 0;
  double last_sent_spring_ = 0.0;
  uint32_t last_recorded_flags_ = ~0u;
  uint32_t last_recorded_blocked_ = ~0u;
  uint64_t send_count_ = 0;
  double send_us_sum_ = 0.0;
  double send_us_max_ = 0.0;
  mutable std::mutex snapshot_mutex_;
  Snapshot snapshot_;
  std::FILE* record_ = nullptr;
};

}  // namespace rex::input::wheel
