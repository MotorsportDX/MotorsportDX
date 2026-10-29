/**
 * @file        input/wheel/wheel_controls.cpp
 * @brief       The racing wheel's driving controls, published for a title integration
 */
#include <rex/input/wheel/wheel_controls.h>

#include <mutex>

namespace rex::input::wheel {

namespace {

struct Store {
  std::mutex mutex;
  Controls controls;
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

}  // namespace rex::input::wheel
