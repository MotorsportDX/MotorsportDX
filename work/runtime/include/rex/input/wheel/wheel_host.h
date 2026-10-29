/**
 * @file        input/wheel/wheel_host.h
 * @brief       The racing wheel's device and force feedback host
 *
 * Reads a racing wheel through SDL's joystick API (every axis and button at
 * device resolution, no gamepad mapping) and sends force feedback through
 * SDL's haptic API from a thread of its own, through the safety governor
 * (ffb_governor.h). Free of runtime dependencies: the runtime's SDL input
 * driver and the GUI launcher's wheel test both use it, with their own log.
 * See docs/analysis/racing-wheels.md, stages 0 and 2 to 5.
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
#include <rex/input/wheel/ffb_stream.h>
#include <rex/input/wheel/wheel_controls.h>
#include <rex/input/wheel/wheel_mapping.h>

namespace rex::input::wheel {

enum class LogLevel { kInfo, kWarning, kError };
using LogFunction = std::function<void(LogLevel, std::string_view)>;
// The driving controls, from the force thread at its rate (stage 2); `connected` is false while
// no wheel is open.
using ControlsFunction = std::function<void(const Controls&)>;
// The title's force feedback (stage 3), polled by the force thread every tick: fills the latest
// sample and returns its sequence number (0: none yet). See wheel_controls.h.
using ForceSourceFunction = std::function<uint64_t(ForceSample*)>;
// The title's force at its physics rate (stage 4), polled every tick: copies the points after
// the first `after` into `out` (at most `max`), sets `got`, and returns the count published.
using ForcePointsFunction =
    std::function<uint64_t(uint64_t after, ForcePoint* out, size_t max, size_t* got)>;
// The road-wheel lock of the car the wheel drives (stage 5), in degrees, polled every tick for
// the car's own steering lock (wheel_mapping.h); 0: none.
using RoadLockFunction = std::function<double()>;

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
  // The axes and buttons of the driving controls.
  MappingConfig mapping;
  // Called with the mapped controls every tick while a wheel is open, and once when it closes.
  ControlsFunction on_controls;
  // The title's forces; without it only SubmitForce and the test forces drive the wheel.
  ForceSourceFunction force_source;
  // The title's force at its physics rate; while its points arrive it replaces the frame
  // sample's constant force (which stays the fallback), played back by a ForceStream.
  ForcePointsFunction force_points;
  // Multiply the title's spring and damper before the gain (1: as the title scales them).
  double spring_scale = 1.0;
  double damper_scale = 1.0;
  // Multiply the title's road vibration and impacts (0..1 levels) into periodic effects'
  // magnitudes before the gain (0: off).
  double road_scale = 0.25;
  double impact_scale = 0.5;
  // The car's road-wheel lock; without it the steering lock is the fixed one or the range.
  RoadLockFunction road_lock;
  // The soft lock's end stop beyond the car's lock, while the title drives (strength 0: off).
  EndStopConfig end_stop;
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
  // The mapped driving controls.
  Controls controls;
  // The governor's last tick.
  double target = 0.0;
  double output = 0.0;
  double spring_level = 0.0;
  double damper_level = 0.0;
  double road_level = 0.0;
  double impact_level = 0.0;
  // The end stop's share of the wanted force on the last tick.
  double end_stop = 0.0;
  double envelope = 0.0;
  uint32_t blocked = 0;
  uint32_t flags = 0;
  TestEffect test = TestEffect::kNone;
  // The title's last sample: whether it drives, and how many samples arrived.
  bool title_driving = false;
  uint64_t title_samples = 0;
  // The physics-rate points that arrived, whether they drive the force now, and the playback's
  // delay behind them in milliseconds.
  uint64_t title_points = 0;
  bool physics_rate = false;
  double stream_delay_ms = 0.0;
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
  void SendForces(double now_s, double constant, const Condition& spring,
                  const Condition& damper, bool new_sample);
  void ZeroDevice();
  void Record(double now_s, double send_us);
  void EmergencyStop();
  void UpdateControls();

  HostOptions options_;
  LogFunction log_;
  Governor governor_;
  EndStop end_stop_;
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
  Condition last_sent_spring_;
  Condition last_sent_damper_;
  double last_sent_road_ = 0.0;
  double last_sent_impact_ = 0.0;
  uint64_t last_sent_impacts_ = 0;
  uint32_t title_impacts_ = 0;
  bool title_impacts_started_ = false;
  ForceSample title_sample_;
  uint64_t title_sequence_ = 0;
  ForceStream stream_;
  uint64_t points_seen_ = 0;
  // The physics-rate value submitted on this tick, and whether it drives the force.
  double stream_value_ = 0.0;
  bool stream_live_ = false;
  double last_torque_ = 0.0;
  uint32_t last_recorded_flags_ = ~0u;
  uint32_t last_recorded_blocked_ = ~0u;
  Controls controls_;
  std::vector<int16_t> axes_;
  std::vector<uint8_t> buttons_;
  uint64_t send_count_ = 0;
  double send_us_sum_ = 0.0;
  double send_us_max_ = 0.0;
  mutable std::mutex snapshot_mutex_;
  Snapshot snapshot_;
  std::FILE* record_ = nullptr;
};

}  // namespace rex::input::wheel
