// CPU tests of the in-game settings overlay's model (fm4-settings-model.h,
// docs/analysis/ingame-settings.md): the JSON reader and writer, the rows of launcher.json's
// "settings" (argument: a launcher.json that pipeline.py wrote), the steps of a value, the knobs
// of a look, and overlay-changes.json as the GUI launcher folds it. Returns 1 on failure.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "fm4-settings-model.h"

namespace {

namespace s = fm4settings;

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

void Equal(const std::string& have, const std::string& want, const char* what) {
  if (have != want) {
    std::printf("FAIL %s: expected '%s', got '%s'\n", what, want.c_str(), have.c_str());
    ++failures;
  }
}

const s::Row* Find(const std::vector<s::Row>& rows, const char* option, const char* knob) {
  for (const auto& row : rows)
    if (row.option == option && row.knob == knob) return &row;
  return nullptr;
}

void JsonTests() {
  s::Json json;
  std::string error;
  Check(s::ParseJson(R"( {"a": [1, 2.5, -3e2, true, false, null], "b": {"c": "x\"y\\z\né😀"}} )",
                     json, error),
        "parse");
  Check(json.Find("a") && json.Find("a")->items.size() == 6 && json.Find("a")->items[2].number == -300.0,
        "array of numbers");
  Equal(json.Find("b")->Text("c"), "x\"y\\z\n\xc3\xa9\xf0\x9f\x98\x80", "escapes and UTF-8");
  s::Json again;
  Check(s::ParseJson(s::DumpJson(json), again, error) && s::DumpJson(again) == s::DumpJson(json), "round trip");
  Equal(s::DumpJson(json.Find("a")->items[1]), "2.5", "a decimal");
  for (const char* bad : {"", "{", "{\"a\" 1}", "[1,]", "\"open", "{} x", "tru"})
    Check(!s::ParseJson(bad, json, error), bad);
}

void RowTests(const s::Json& launcher) {
  const std::vector<s::Row> rows = s::ReadRows(launcher);
  Check(rows.size() > 20, "the table's rows");
  const s::Row* rumble = Find(rows, "pad", "rumble");
  Check(rumble && rumble->live && rumble->cvar == "pad_rumble_strength" && rumble->min == 0.0 &&
            rumble->max == 2.0 && rumble->fallback == "1.5",
        "pad rumble");
  if (rumble) {
    Check(s::StepSize(*rumble) == 0.1, "rumble's step");
    Equal(s::Step(*rumble, "1.5", 1), "1.6", "a step up");
    Equal(s::Step(*rumble, "1.5", -2), "1.3", "two steps down");
    Equal(s::Step(*rumble, "1.98", 1), "2", "held at the top");
    Equal(s::Step(*rumble, "0", -1), "0", "held at the bottom");
    Equal(s::KnobOfCvar(*rumble, "1.500000"), "1.5", "a double cvar");
  }
  const s::Row* smooth = Find(rows, "keyboard", "smooth");
  Check(smooth && smooth->Boolean(), "a switch");
  if (smooth) {
    Equal(s::KnobOfCvar(*smooth, "true"), "1", "a bool cvar");
    Equal(s::CvarOfKnob(*smooth, "0"), "false", "a bool knob");
    Equal(s::Step(*smooth, "1", 1, true), "0", "a switch wraps with A");
    Equal(s::Step(*smooth, "1", 1, false), "1", "a switch stays at its end with right");
  }
  const s::Row* ffb = Find(rows, "wheel", "ffb_gain");
  Check(ffb && !ffb->live, "the wheel's force feedback is restart");
  const s::Row* sharpen = Find(rows, "look", "sharpen");
  Check(sharpen && sharpen->live && sharpen->param == "TAA_SHARPEN", "the look's sharpening");
  if (sharpen) Equal(s::Step(*sharpen, "0.4", 1), "0.45", "a step of sharpening");
  const s::Row* quality = Find(rows, "quality", "");
  Check(quality && !quality->live && quality->Name("") == "High", "quality");
  if (quality) Equal(s::Step(*quality, "", 1), "extrahigh", "the next quality");
  const s::Row* rate = Find(rows, "frame_rate", "");
  if (rate) Equal(rate->Name("0"), "60 fps", "60 fps is stored as 0");
  const s::Row* scale = Find(rows, "resolution_scale", "");
  Check(scale && scale->Name("-1") == "Screen's own", "the screen's own scale");
  // Looks: presets, aliases and knobs, later items win; original clears the preset's knobs.
  const auto knob = [&](const char* look, const char* name) {
    for (const auto& [key, value] : s::LookKnobs(launcher, look))
      if (key == name) return value;
    return std::string("<none>");
  };
  Equal(knob("gt7,sharpen=0.5", "sharpen"), "0.5", "a knob over a preset");
  Equal(knob("pc", "tonemap"), "aces", "an alias");
  Equal(knob("aces,original", "tonemap"), "<none>", "original clears");
  Equal(knob("original", "taa"), "1", "original's TAA");
  Equal(s::LookPreset(launcher, "pc,ao=0"), "aces", "the preset of an alias");
  Equal(s::LookPreset(launcher, "sharpen=0.5"), "aces", "the default preset");
  Equal(s::LookPreset(launcher, "gt7,original,taa=0"), "original", "the last preset");
  const auto defines = s::ParseDefines("TAA=1;TAA_SHARPEN=0.4;;FM4_LOOK_AO=0.6;TAA=0");
  Check(defines.size() == 3 && defines.at("TAA") == "0" && defines.at("TAA_SHARPEN") == "0.4", "defines");
}

void ChangesTests() {
  s::Changes changes;
  std::string error;
  Check(changes.Read(R"({"profile": "Race", "options": {"look": "gt7", "frame_rate": 120, "fullscreen": true,
                         "resolution_scale": -1, "quality": ""}, "changes": {}})",
                     error),
        "read the launcher's file");
  Equal(changes.profile, "Race", "the profile");
  Equal(changes.Option("frame_rate"), "120", "a number option");
  Equal(changes.Option("fullscreen"), "1", "a bool option");
  Equal(changes.Option("resolution_scale"), "-1", "a negative option");
  Equal(changes.Option("look"), "gt7", "a text option");
  Check(!changes.Change("pad", "rumble"), "no change yet");
  changes.Set("pad", "rumble", "0.5");
  changes.Set("pad", "stick_deadzone", "0.1");
  changes.Set("quality", "", "ultra");
  changes.Set("look", "preset", "aces");
  changes.Set("pad", "rumble", "0.6");
  Check(changes.Change("pad", "rumble") && *changes.Change("pad", "rumble") == "0.6", "a change replaced");
  Check(changes.Change("quality", "") && *changes.Change("quality", "") == "ultra", "an option change");
  s::Changes again;
  Check(again.Read(changes.Write(), error) && again.Write() == changes.Write(), "the file round-trips");
  s::Changes small;
  small.profile = "Default";
  small.Set("pad", "rumble", "0.5");
  small.Set("vsync", "", "on");
  Equal(small.Write(),
        "{\n \"profile\": \"Default\",\n \"options\": {},\n \"changes\": {\n  \"pad\": {\n   \"rumble\": \"0.5\"\n  },\n"
        "  \"vsync\": \"on\"\n }\n}\n",
        "the written file");
  Check(!changes.Read("not json", error), "a damaged file");
}

}  // namespace

int main(int argc, char** argv) {
  JsonTests();
  ChangesTests();
  if (argc > 1) {
    std::ifstream stream(argv[1], std::ios::binary);
    const std::string text(std::istreambuf_iterator<char>(stream), {});
    s::Json launcher;
    std::string error;
    if (!s::ParseJson(text, launcher, error)) {
      std::printf("FAIL cannot read %s: %s\n", argv[1], error.c_str());
      ++failures;
    } else {
      RowTests(launcher);
    }
  } else {
    std::printf("FAIL no launcher.json given\n");
    ++failures;
  }
  std::printf(failures ? "%d failures\n" : "settings model tests passed\n", failures);
  return failures ? 1 : 0;
}
