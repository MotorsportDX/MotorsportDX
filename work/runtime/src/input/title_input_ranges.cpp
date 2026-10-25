/**
 * @file        input/title_input_ranges.cpp
 * @brief       The pad ranges a title applies to the axes the keyboard drives
 */
#include <rex/input/title_input_ranges.h>

#include <array>
#include <mutex>

#include <rex/input/device.h>

namespace rex::input {

namespace {

struct Store {
  std::mutex mutex;
  keyboard::TitleRanges defaults;
  std::array<keyboard::TitleRanges, kMaxGuestUsers> users;
  std::array<bool, kMaxGuestUsers> published = {};
};

Store& GetStore() {
  static Store store;
  return store;
}

}  // namespace

void SetTitleInputRanges(uint32_t user_index, const keyboard::TitleRanges& ranges) {
  if (user_index >= kMaxGuestUsers) {
    return;
  }
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  store.users[user_index] = ranges;
  store.published[user_index] = true;
}

void SetDefaultTitleInputRanges(const keyboard::TitleRanges& ranges) {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  store.defaults = ranges;
}

keyboard::TitleRanges GetTitleInputRanges(uint32_t user_index) {
  Store& store = GetStore();
  std::lock_guard lock(store.mutex);
  if (user_index < kMaxGuestUsers && store.published[user_index]) {
    return store.users[user_index];
  }
  return store.defaults;
}

}  // namespace rex::input
