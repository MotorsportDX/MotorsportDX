// CPU tests of the uniform draw resolution scale as a fraction
// (rex/graphics/util/draw_scale.h, docs/analysis/d3d-hle-n5.md section 18):
// parsing, the whole scales' integer results, and the host edges of the
// fractional ones. Returns 1 on failure.

#include <cstdint>
#include <cstdio>

#include <rex/graphics/util/draw_scale.h>

namespace {

using rex::graphics::DrawScale;

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

bool Is(const char* text, uint32_t numerator, uint32_t denominator) {
  auto scale = DrawScale::Parse(text);
  return scale && scale->numerator == numerator && scale->denominator == denominator;
}

}  // namespace

int main() {
  Check(Is("2", 2, 1), "whole");
  Check(Is("3/2", 3, 2), "fraction");
  Check(Is("1.5", 3, 2), "decimal");
  Check(Is("1080/720", 3, 2), "panel lines over 720, reduced");
  Check(Is("1600/720", 20, 9), "1600 lines");
  Check(Is("2160/720", 3, 1), "2160 lines");
  Check(Is("7", 7, 1), "the largest");
  Check(!DrawScale::Parse(""), "empty");
  Check(!DrawScale::Parse("0.5"), "below 1");
  Check(!DrawScale::Parse("8"), "above 7");
  Check(!DrawScale::Parse("3/0"), "zero denominator");
  Check(!DrawScale::Parse("3/2/1") && !DrawScale::Parse("1.5.0") && !DrawScale::Parse("1.5/2"),
        "two separators");
  Check(!DrawScale::Parse("/2") && !DrawScale::Parse("3/") && !DrawScale::Parse(".5"),
        "a missing number");
  Check(!DrawScale::Parse("1.5x") && !DrawScale::Parse(" 2") && !DrawScale::Parse("-2"),
        "other characters");
  Check(!DrawScale::Parse("1000/721"), "a denominator above 720");

  // Whole scales give the integer results.
  for (uint32_t s = 1; s <= DrawScale::kMax; ++s) {
    DrawScale scale = DrawScale::Whole(s);
    bool same = scale.IsWhole() && scale.Floor() == s && scale.ToFloat() == float(s);
    for (uint32_t guest = 0; guest <= 2048; ++guest) {
      same &= scale.ScaleDown(guest) == guest * s && scale.ScaleUp(guest) == guest * s &&
              scale.ScaleEdge(guest) == guest * s;
    }
    Check(same, "whole scales are the integer multiples");
  }

  // 3/2: the panel of 1920x1080 from 1280x720.
  DrawScale three_halves = *DrawScale::Parse("3/2");
  Check(!three_halves.IsWhole() && three_halves.Floor() == 1, "3/2 is above 1");
  Check(three_halves.ScaleDown(1280) == 1920 && three_halves.ScaleDown(720) == 1080,
        "1280x720 to 1920x1080");
  Check(three_halves.ScaleDown(1) == 1 && three_halves.ScaleUp(1) == 2 &&
            three_halves.ScaleEdge(1) == 2,
        "one guest pixel");
  Check(three_halves.ScaleEdge(80) == 120 && three_halves.ScaleEdge(16) == 24,
        "an EDRAM tile is whole at 3/2");
  // 20/9: 2560x1600 from 1152x720.
  DrawScale twenty_ninths = *DrawScale::Parse("20/9");
  Check(twenty_ninths.Floor() == 2, "20/9 is above 2");
  Check(twenty_ninths.ScaleDown(1152) == 2560 && twenty_ninths.ScaleDown(720) == 1600,
        "1152x720 to 2560x1600");
  Check(twenty_ninths.ScaleEdge(16) == 36 && twenty_ninths.ScaleUp(16) == 36 &&
            twenty_ninths.ScaleDown(16) == 35,
        "an EDRAM tile's height is not whole at 20/9");

  // Neighbouring rectangles share their host edge, and every guest pixel gets
  // at least one host pixel above 1.
  for (const char* text : {"3/2", "20/9", "9/4", "5/2", "7/3"}) {
    DrawScale scale = *DrawScale::Parse(text);
    bool ordered = true;
    for (uint32_t guest = 0; guest < 4096; ++guest) {
      uint32_t width = scale.ScaleEdge(guest + 1) - scale.ScaleEdge(guest);
      ordered &= width >= scale.Floor() && width <= scale.Floor() + 1;
      ordered &= scale.ScaleDown(guest) <= scale.ScaleEdge(guest) &&
                 scale.ScaleEdge(guest) <= scale.ScaleUp(guest);
    }
    Check(ordered, "edges of fractional scales");
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("draw scale tests passed\n");
  return 0;
}
