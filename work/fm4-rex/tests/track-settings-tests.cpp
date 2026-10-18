// CPU tests of the track lighting file edits (work/fm4-rex/src/fm4-track-settings.h):
// parsing FM4_TRACK_SETTINGS and applying it to a synthetic TrackSettings.xml.
// No game data; returns 1 on failure.

#include <cstdio>
#include <string>

#include "fm4-track-settings.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

bool Throws(const char* spec) {
  try {
    fm4::ParseTrackSettings(spec);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

// The layout of the title's files: one element per line, CRLF, six decimals.
const char kXml[] =
    "<TrackSettings>\r\n"
    "  <skyScale value=\"1.000000\"/>\r\n"
    "  <skyScaleTwo value=\"2.000000\"/>\r\n"
    "  <RoadSpecular RoadSpecular=\"-0.041000\" RoadSpecular2=\"0.843990\" RoadSkid=\"-0.003996\"/>\r\n"
    "  <RoadSpecularX RoadSpecular2=\"9.000000\"/>\r\n"
    "  <Fog><Settings0 Near=\"10.000000\"/><Settings1 Near=\"20.000000\"/></Fog>\r\n"
    "</TrackSettings>\r\n";

}  // namespace

int main() {
  auto edits = fm4::ParseTrackSettings(
      "skyScale.value*=0.8; Alps:RoadSpecular.RoadSpecular2+=-0.1 ;Settings1.Near=5;");
  Check(edits.size() == 3, "three items");
  Check(edits[0].track.empty() && edits[0].element == "skyScale" &&
            edits[0].attribute == "value" && edits[0].op == '*' && edits[0].value == 0.8,
        "multiply item");
  Check(edits[1].track == "Alps" && edits[1].element == "RoadSpecular" &&
            edits[1].attribute == "RoadSpecular2" && edits[1].op == '+' && edits[1].value == -0.1,
        "track item that adds");
  Check(edits[2].op == '=' && edits[2].value == 5.0, "set item");
  Check(fm4::ParseTrackSettings("").empty() && fm4::ParseTrackSettings(" ; ").empty(), "empty");
  Check(Throws("skyScale*=1"), "no attribute");
  Check(Throws("skyScale.value*=x"), "not a number");
  Check(Throws("skyScale.value*=1x"), "trailing text");
  Check(Throws("skyScale.value-=1"), "unknown operator");
  Check(Throws(":skyScale.value=1"), "empty track");
  Check(Throws("sky Scale.value=1"), "invalid name");

  // The pc-like knobs: every track, road specular halved, sky at 0.8.
  std::string xml = kXml;
  unsigned changed = fm4::ApplyTrackSettings(
      xml, "Alps",
      fm4::ParseTrackSettings(
          "RoadSpecular.RoadSpecular*=0.5;RoadSpecular.RoadSpecular2*=0.5;skyScale.value*=0.8"));
  Check(changed == 3, "three values changed");
  Check(xml.find("<RoadSpecular RoadSpecular=\"-0.020500\" RoadSpecular2=\"0.421995\" "
                 "RoadSkid=\"-0.003996\"/>\r\n") != std::string::npos,
        "road specular halved, skid kept");
  Check(xml.find("<skyScale value=\"0.800000\"/>") != std::string::npos, "sky scaled");
  Check(xml.find("<skyScaleTwo value=\"2.000000\"/>") != std::string::npos,
        "longer element name kept");
  Check(xml.find("<RoadSpecularX RoadSpecular2=\"9.000000\"/>") != std::string::npos,
        "longer element name kept for an attribute");
  Check(xml.size() == sizeof(kXml) - 1, "same layout and length");

  // Track names match in any case; other tracks are left alone.
  xml = kXml;
  const auto sebring = fm4::ParseTrackSettings("Sebring:skyScale.value=3");
  Check(fm4::ApplyTrackSettings(xml, "Alps", sebring) == 0 && xml == kXml, "other track kept");
  Check(fm4::ApplyTrackSettings(xml, "SEBRING", sebring) == 1 &&
            xml.find("<skyScale value=\"3.000000\"/>") != std::string::npos,
        "track in any case");

  // Repeated elements each change; an element without the attribute is skipped.
  xml = kXml;
  Check(fm4::ApplyTrackSettings(xml, "Alps", fm4::ParseTrackSettings("Settings1.Near*=2;"
                                                                     "skyScale.missing=1")) == 1 &&
            xml.find("<Settings1 Near=\"40.000000\"/>") != std::string::npos &&
            xml.find("<Settings0 Near=\"10.000000\"/>") != std::string::npos,
        "nested element and missing attribute");

  // A value that is not a number is an error, not a silent skip.
  std::string bad = "<skyScale value=\"high\"/>";
  bool threw = false;
  try {
    fm4::ApplyTrackSettings(bad, "Alps", fm4::ParseTrackSettings("skyScale.value*=2"));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  Check(threw, "value that is not a number");

  std::printf(failures ? "track settings tests: %d failed\n" : "track settings tests: passed\n",
              failures);
  return failures ? 1 : 0;
}
