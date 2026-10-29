/**
 * @file        input/wheel/wheel_controls.cpp
 * @brief       The racing wheel's exchange with a title integration
 */
#include <rex/input/wheel/wheel_controls.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace rex::input::wheel {

namespace {

struct Store {
  std::mutex mutex;
  Controls controls;
  double road_lock_deg = 0.0;
  ForceSample forces;
  uint64_t forces_sequence = 0;
  std::array<ForcePoint, 512> points;
  uint64_t points_count = 0;
};

Store& GetStore() {
  static Store store;
  return store;
}

}  // namespace

void PublishControls(const Controls& controls) {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  store.controls = controls;
}

bool GetControls(Controls* out) {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  *out = store.controls;
  return out->connected;
}

void PublishRoadLock(double degrees) {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  store.road_lock_deg = std::isfinite(degrees) && degrees > 0.0 ? degrees : 0.0;
}

double GetRoadLock() {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  return store.road_lock_deg;
}

void PublishForces(const ForceSample& sample) {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  store.forces = sample;
  ++store.forces_sequence;
}

uint64_t GetForces(ForceSample* out) {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  *out = store.forces;
  return store.forces_sequence;
}

void PublishForcePoint(const ForcePoint& point) {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  store.points[store.points_count % store.points.size()] = point;
  ++store.points_count;
}

uint64_t GetForcePoints(uint64_t after, ForcePoint* out, size_t max, size_t* got) {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  const uint64_t oldest =
      store.points_count > store.points.size() ? store.points_count - store.points.size() : 0;
  uint64_t first = std::max(after, oldest);
  if (store.points_count - std::min(first, store.points_count) > max) {
    first = store.points_count - max;
  }
  size_t n = 0;
  for (uint64_t i = first; i < store.points_count; ++i) {
    out[n++] = store.points[i % store.points.size()];
  }
  *got = n;
  return store.points_count;
}

}  // namespace rex::input::wheel
