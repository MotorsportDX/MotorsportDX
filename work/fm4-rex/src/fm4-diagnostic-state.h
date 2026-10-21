#pragma once
#include <atomic>
#include <cstdint>
inline std::atomic<uint16_t> fm4_diagnostic_buttons{0};
// State polls of the diagnostic controller (FM4TestInput), one per guest frame in a race.
inline std::atomic<uint32_t> fm4_diagnostic_polls{0};
// D3DDevice_Swap calls of the title (fm4-guest-hooks.cpp): its presented frames.
inline std::atomic<uint32_t> fm4_diagnostic_swaps{0};
// The value of fm4_diagnostic_polls when the title last pushed a race render scenario
// (fm4-vblank-rate.cpp); 0 before the first race. FM4TestInput counts race frames from it.
inline std::atomic<uint32_t> fm4_diagnostic_race_poll{0};
