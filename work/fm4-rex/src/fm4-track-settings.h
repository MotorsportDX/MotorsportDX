// FM4_TRACK_SETTINGS edits the title's per-track lighting files,
// Media\tracks\<Track>\TrackSettings.xml and TrackSettings1.xml, in copies made
// from the user's own disc (FM4MountTrackSettings in fm4-render-overrides.cpp
// mounts them). Items are separated by ';':
//   [<Track>:]<Element>.<attribute><op><number>
// with op *= (multiply), += (add) or = (set). An item changes that attribute
// of every element of that name in a file, in every race location unless
// <Track> (the track folder name, any case) limits it; the garage, showroom and
// thumbnail scenes keep theirs. pipeline.py builds the value from the look
// knobs, for example "skyScale.value*=0.8".
#pragma once

#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fm4 {

struct TrackSettingsEdit {
  std::string track;  // empty: every track
  std::string element;
  std::string attribute;
  char op = '=';  // '*', '+' or '='
  double value = 0.0;
};

inline bool TrackSettingsEqualCase(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i])))
      return false;
  }
  return true;
}

// A whole decimal number, or throws.
inline double TrackSettingsNumber(const std::string& text, const char* what) {
  size_t used = 0;
  double value = 0.0;
  try {
    value = std::stod(text, &used);
  } catch (const std::exception&) {
    used = 0;
  }
  if (text.empty() || used != text.size())
    throw std::runtime_error(std::string(what) + " is not a number: '" + text + "'");
  return value;
}

inline bool TrackSettingsName(std::string_view name) {
  if (name.empty()) return false;
  for (char c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

inline std::vector<TrackSettingsEdit> ParseTrackSettings(std::string_view spec) {
  std::vector<TrackSettingsEdit> edits;
  size_t start = 0;
  while (start < spec.size()) {
    size_t end = spec.find(';', start);
    if (end == std::string_view::npos) end = spec.size();
    std::string item(spec.substr(start, end - start));
    start = end + 1;
    item.erase(0, item.find_first_not_of(" \t"));
    item.erase(item.find_last_not_of(" \t") + 1);
    if (item.empty()) continue;
    const std::string error = "FM4_TRACK_SETTINGS item '" + item + "'";
    TrackSettingsEdit edit;
    const size_t colon = item.find(':');
    if (colon != std::string::npos) {
      edit.track = item.substr(0, colon);
      item = item.substr(colon + 1);
    }
    const size_t dot = item.find('.');
    const size_t op = item.find_first_of("*+=");
    if (dot == std::string::npos || op == std::string::npos || dot > op)
      throw std::runtime_error(error + " is not [Track:]Element.attribute*=|+=|=number");
    edit.element = item.substr(0, dot);
    edit.attribute = item.substr(dot + 1, op - dot - 1);
    size_t number = op + 1;
    if (item[op] == '=') {
      edit.op = '=';
    } else if (op + 1 < item.size() && item[op + 1] == '=') {
      edit.op = item[op];
      number = op + 2;
    } else {
      throw std::runtime_error(error + " needs *=, += or =");
    }
    if ((colon != std::string::npos && !TrackSettingsName(edit.track)) ||
        !TrackSettingsName(edit.element) || !TrackSettingsName(edit.attribute))
      throw std::runtime_error(error + " has an empty or invalid name");
    edit.value = TrackSettingsNumber(item.substr(number), error.c_str());
    edits.push_back(std::move(edit));
  }
  return edits;
}

// Applies the edits for one track to the text of one of its files and returns
// the number of values changed. The rest of the file stays byte for byte.
inline unsigned ApplyTrackSettings(std::string& xml, std::string_view track,
                                   const std::vector<TrackSettingsEdit>& edits) {
  unsigned changed = 0;
  for (const auto& edit : edits) {
    if (!edit.track.empty() && !TrackSettingsEqualCase(edit.track, track)) continue;
    const std::string open = "<" + edit.element;
    const std::string key = edit.attribute + "=\"";
    for (size_t at = xml.find(open); at != std::string::npos; at = xml.find(open, at + 1)) {
      const size_t name_end = at + open.size();
      if (name_end >= xml.size() || !std::isspace(static_cast<unsigned char>(xml[name_end])))
        continue;
      const size_t tag_end = xml.find('>', name_end);
      if (tag_end == std::string::npos) throw std::runtime_error("Unexpected track settings format");
      size_t attribute = xml.find(key, name_end);
      while (attribute != std::string::npos && attribute < tag_end &&
             !std::isspace(static_cast<unsigned char>(xml[attribute - 1])))
        attribute = xml.find(key, attribute + 1);
      if (attribute == std::string::npos || attribute > tag_end) continue;
      const size_t value = attribute + key.size();
      const size_t value_end = xml.find('"', value);
      if (value_end == std::string::npos || value_end > tag_end)
        throw std::runtime_error("Unexpected track settings format");
      const double old = TrackSettingsNumber(xml.substr(value, value_end - value),
                                             (edit.element + "." + edit.attribute).c_str());
      const double result = edit.op == '*'   ? old * edit.value
                            : edit.op == '+' ? old + edit.value
                                             : edit.value;
      // The title's own files write six decimals.
      char text[64];
      std::snprintf(text, sizeof(text), "%f", result);
      xml.replace(value, value_end - value, text);
      ++changed;
    }
  }
  return changed;
}

}  // namespace fm4
