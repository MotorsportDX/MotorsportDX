/**
 * @file        input/wheel/wheel_controls.cpp
 * @brief       The racing wheel's exchange with a title integration
 */
#include <rex/input/wheel/wheel_controls.h>

#include <mutex>

namespace rex::input::wheel {

namespace {

struct Store {
  std::mutex mutex;
  Controls controls;
  ForceSample forces;
  uint64_t forces_sequence = 0;
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

}  // namespace rex::input::wheel
