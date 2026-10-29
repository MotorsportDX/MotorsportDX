#pragma once
#include <atomic>
#include <cstdint>
inline std::atomic<uint16_t> fm4_diagnostic_buttons{0};
// State polls of the diagnostic controller (FM4TestInput), about one per guest frame in a race.
inline std::atomic<uint32_t> fm4_diagnostic_polls{0};
// D3DDevice_Swap calls of the title (fm4-guest-hooks.cpp): its presented frames.
inline std::atomic<uint32_t> fm4_diagnostic_swaps{0};
// Session updates of the title (fm4-race-clock.cpp): one per main loop pass in a session.
inline std::atomic<uint32_t> fm4_diagnostic_updates{0};
// The session time (seconds) after the last session update.
inline std::atomic<double> fm4_diagnostic_session_time{0.0};
// The session the title is updating (sub_8240DB08's this; 0 outside an update) and its time
// before the update, so the race code that runs inside it can read the frame's time step
// (fm4-wheel-input.cpp); and the step of the last completed update.
inline std::atomic<uint32_t> fm4_session_updating{0};
inline std::atomic<double> fm4_session_time_before{0.0};
inline std::atomic<double> fm4_session_last_step{0.0};
// The value of fm4_diagnostic_updates when the title pushed the race render scenario that is
// on (fm4-vblank-rate.cpp); 0 outside a race (the pre-race already updates the session).
// FM4TestInput counts race frames from it.
inline std::atomic<uint32_t> fm4_diagnostic_race_update{0};
// Screen events for the e2e harness (fm4-test-events.cpp): one line in FM4_TEST_EVENTS_FILE,
// nothing without the variable.
bool FM4TestEventsOn();
void FM4TestEvent(const char* format, ...);
