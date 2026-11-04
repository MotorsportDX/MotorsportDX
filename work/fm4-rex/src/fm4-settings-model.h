// The model of the in-game settings overlay (fm4-settings-overlay.h,
// docs/analysis/ingame-settings.md): a small JSON reader and writer, the rows of the player's
// settings in launcher.json ("settings", outputs/FM4-build/player_settings.py), the steps of a
// value, the knobs of a look, and overlay-changes.json, which the GUI launcher folds into the
// profile (work/fm4-launcher/src/overlay.h). Header only, without the runtime, for its tests
// (work/fm4-rex/tests/settings-model-tests.cpp).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fm4settings {

// A JSON value. Objects keep their keys in order.
struct Json {
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };
  Type type = Type::kNull;
  bool boolean = false;
  double number = 0.0;
  std::string text;
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> members;

  bool IsObject() const { return type == Type::kObject; }
  bool IsString() const { return type == Type::kString; }
  const Json* Find(std::string_view key) const {
    for (const auto& [name, value] : members)
      if (name == key) return &value;
    return nullptr;
  }
  // The text of a string member, or fallback.
  std::string Text(std::string_view key, std::string fallback = {}) const {
    const Json* value = Find(key);
    return value && value->IsString() ? value->text : fallback;
  }
  Json& Set(std::string key, Json value) {
    for (auto& [name, member] : members)
      if (name == key) return member = std::move(value);
    type = Type::kObject;
    members.emplace_back(std::move(key), std::move(value));
    return members.back().second;
  }
  static Json String(std::string value) {
    Json json;
    json.type = Type::kString;
    json.text = std::move(value);
    return json;
  }
  static Json Object() {
    Json json;
    json.type = Type::kObject;
    return json;
  }
};

namespace detail {

struct Parser {
  std::string_view text;
  size_t at = 0;
  std::string error;

  void Space() {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n'))
      ++at;
  }
  bool Fail(const char* what) {
    if (error.empty()) error = std::string(what) + " at byte " + std::to_string(at);
    return false;
  }
  bool Literal(std::string_view word) {
    if (text.substr(at, word.size()) != word) return false;
    at += word.size();
    return true;
  }
  static void Utf8(std::string& out, unsigned code) {
    if (code < 0x80) {
      out += char(code);
    } else if (code < 0x800) {
      out += char(0xC0 | (code >> 6));
      out += char(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
      out += char(0xE0 | (code >> 12));
      out += char(0x80 | ((code >> 6) & 0x3F));
      out += char(0x80 | (code & 0x3F));
    } else {
      out += char(0xF0 | (code >> 18));
      out += char(0x80 | ((code >> 12) & 0x3F));
      out += char(0x80 | ((code >> 6) & 0x3F));
      out += char(0x80 | (code & 0x3F));
    }
  }
  bool Hex4(unsigned& code) {
    if (at + 4 > text.size()) return Fail("short \\u escape");
    code = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text[at++];
      code = code * 16 + (c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                 : 99);
      if (code >= 0x10000) return Fail("bad \\u escape");
    }
    return true;
  }
  bool String(std::string& out) {
    ++at;  // "
    while (at < text.size() && text[at] != '"') {
      const char c = text[at++];
      if (c != '\\') {
        out += c;
        continue;
      }
      if (at >= text.size()) return Fail("short escape");
      const char e = text[at++];
      switch (e) {
        case '"': case '\\': case '/': out += e; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned code;
          if (!Hex4(code)) return false;
          if (code >= 0xD800 && code < 0xDC00 && text.substr(at, 2) == "\\u") {
            at += 2;
            unsigned low;
            if (!Hex4(low)) return false;
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          }
          Utf8(out, code);
          break;
        }
        default: return Fail("bad escape");
      }
    }
    if (at >= text.size()) return Fail("unterminated string");
    ++at;
    return true;
  }
  bool Value(Json& out, int depth) {
    if (depth > 64) return Fail("too deep");
    Space();
    if (at >= text.size()) return Fail("missing value");
    const char c = text[at];
    if (c == '{') {
      out.type = Json::Type::kObject;
      ++at;
      Space();
      if (at < text.size() && text[at] == '}') return ++at, true;
      for (;;) {
        Space();
        if (at >= text.size() || text[at] != '"') return Fail("expected a key");
        std::string key;
        if (!String(key)) return false;
        Space();
        if (at >= text.size() || text[at] != ':') return Fail("expected ':'");
        ++at;
        Json value;
        if (!Value(value, depth + 1)) return false;
        out.members.emplace_back(std::move(key), std::move(value));
        Space();
        if (at < text.size() && text[at] == ',') {
          ++at;
          continue;
        }
        if (at < text.size() && text[at] == '}') return ++at, true;
        return Fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      out.type = Json::Type::kArray;
      ++at;
      Space();
      if (at < text.size() && text[at] == ']') return ++at, true;
      for (;;) {
        Json value;
        if (!Value(value, depth + 1)) return false;
        out.items.push_back(std::move(value));
        Space();
        if (at < text.size() && text[at] == ',') {
          ++at;
          continue;
        }
        if (at < text.size() && text[at] == ']') return ++at, true;
        return Fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      out.type = Json::Type::kString;
      return String(out.text);
    }
    if (Literal("true")) return out.type = Json::Type::kBool, out.boolean = true, true;
    if (Literal("false")) return out.type = Json::Type::kBool, true;
    if (Literal("null")) return out.type = Json::Type::kNull, true;
    const size_t start = at;
    while (at < text.size() && std::string_view("+-0123456789.eE").find(text[at]) != std::string_view::npos) ++at;
    if (at == start) return Fail("unexpected character");
    const std::string number(text.substr(start, at - start));
    char* end = nullptr;
    out.type = Json::Type::kNumber;
    out.number = std::strtod(number.c_str(), &end);
    return end == number.c_str() + number.size() || Fail("bad number");
  }
};

inline void Quote(std::string& out, std::string_view text) {
  out += '"';
  for (const char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escape[8];
          std::snprintf(escape, sizeof(escape), "\\u%04x", c);
          out += escape;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

inline void Dump(std::string& out, const Json& json, int indent) {
  const std::string pad(size_t(indent + 1), ' '), close(size_t(indent), ' ');
  switch (json.type) {
    case Json::Type::kNull: out += "null"; break;
    case Json::Type::kBool: out += json.boolean ? "true" : "false"; break;
    case Json::Type::kNumber: {
      char number[32];
      if (json.number == std::floor(json.number) && std::fabs(json.number) < 1e15)
        std::snprintf(number, sizeof(number), "%.0f", json.number);
      else
        std::snprintf(number, sizeof(number), "%.17g", json.number);
      out += number;
      break;
    }
    case Json::Type::kString: Quote(out, json.text); break;
    case Json::Type::kArray:
      if (json.items.empty()) {
        out += "[]";
        break;
      }
      out += "[\n";
      for (size_t i = 0; i < json.items.size(); ++i) {
        out += pad;
        Dump(out, json.items[i], indent + 1);
        out += i + 1 < json.items.size() ? ",\n" : "\n";
      }
      out += close + "]";
      break;
    case Json::Type::kObject:
      if (json.members.empty()) {
        out += "{}";
        break;
      }
      out += "{\n";
      for (size_t i = 0; i < json.members.size(); ++i) {
        out += pad;
        Quote(out, json.members[i].first);
        out += ": ";
        Dump(out, json.members[i].second, indent + 1);
        out += i + 1 < json.members.size() ? ",\n" : "\n";
      }
      out += close + "}";
      break;
  }
}

}  // namespace detail

inline bool ParseJson(std::string_view text, Json& out, std::string& error) {
  detail::Parser parser{text, 0, {}};
  out = Json();
  if (!parser.Value(out, 0)) return error = parser.error, false;
  parser.Space();
  if (parser.at != text.size()) return error = "text after the value at byte " + std::to_string(parser.at), false;
  return true;
}

// Indented by one space a level, as the launcher writes its files.
inline std::string DumpJson(const Json& json) {
  std::string out;
  detail::Dump(out, json, 0);
  return out;
}

// A row of the player's settings.
struct Choice {
  std::string value, name;
};

struct Row {
  std::string page, group, option, knob, label;
  bool live = false;
  std::string cvar, param, fallback;
  std::vector<Choice> choices;
  double min = 0.0, max = 0.0;
  bool integer = false;

  // A choice row's name of a value; a number row's value as shown.
  std::string Name(const std::string& value) const {
    for (const auto& choice : choices)
      if (choice.value == value) return choice.name;
    return value;
  }
  bool Boolean() const {
    return choices.size() == 2 && choices[0].value == "0" && choices[1].value == "1";
  }
};

// launcher.json's "settings"; none when it has none.
inline std::vector<Row> ReadRows(const Json& launcher) {
  std::vector<Row> rows;
  const Json* settings = launcher.Find("settings");
  if (!settings || settings->type != Json::Type::kArray) return rows;
  for (const Json& item : settings->items) {
    if (!item.IsObject()) continue;
    Row row;
    row.page = item.Text("page");
    row.group = item.Text("group");
    row.option = item.Text("option");
    row.knob = item.Text("knob");
    row.label = item.Text("label");
    row.live = item.Text("class") == "live";
    row.cvar = item.Text("cvar");
    row.param = item.Text("param");
    row.fallback = item.Text("default");
    if (const Json* choices = item.Find("choices"); choices && choices->type == Json::Type::kArray) {
      for (const Json& choice : choices->items)
        if (choice.items.size() == 2 && choice.items[0].IsString() && choice.items[1].IsString())
          row.choices.push_back({choice.items[0].text, choice.items[1].text});
    }
    if (const Json* low = item.Find("min")) row.min = low->number;
    if (const Json* high = item.Find("max")) row.max = high->number;
    if (const Json* whole = item.Find("integer")) row.integer = whole->boolean;
    rows.push_back(std::move(row));
  }
  return rows;
}

// A number as the launcher stores it: at most the given decimals, without trailing zeros.
inline std::string NumberText(double value, int decimals = 3) {
  char text[48];
  std::snprintf(text, sizeof(text), "%.*f", decimals, value);
  std::string result(text);
  if (result.find('.') != std::string::npos) {
    result.erase(result.find_last_not_of('0') + 1);
    if (result.back() == '.') result.pop_back();
  }
  if (result == "-0") result = "0";
  return result;
}

// The step of a number row: about a twentieth of its range, rounded to 1, 2 or 5 times a power
// of ten; whole rows step by whole numbers.
inline double StepSize(const Row& row) {
  const double span = row.max - row.min;
  if (span <= 0.0) return 1.0;
  const double rough = span / 20.0;
  const double power = std::pow(10.0, std::floor(std::log10(rough)));
  const double step = rough / power < 1.5 ? power : rough / power < 3.5 ? 2.0 * power : 5.0 * power;
  return row.integer ? std::max(1.0, std::round(step)) : step;
}

inline int StepDecimals(const Row& row) {
  const double step = StepSize(row);
  int decimals = 0;
  while (decimals < 6 && std::fabs(step * std::pow(10.0, decimals) - std::round(step * std::pow(10.0, decimals))) > 1e-9)
    ++decimals;
  return decimals;
}

// The next value of a row from value: the neighbouring choice (wrapping when wrap), or the value
// moved by delta steps on the step grid and held in the range.
inline std::string Step(const Row& row, const std::string& value, int delta, bool wrap = false) {
  if (!row.choices.empty()) {
    const int count = int(row.choices.size());
    int index = 0;
    for (int i = 0; i < count; ++i)
      if (row.choices[size_t(i)].value == value) index = i;
    index += delta;
    index = wrap ? (index % count + count) % count : std::clamp(index, 0, count - 1);
    return row.choices[size_t(index)].value;
  }
  const double step = StepSize(row);
  double number = std::strtod(value.c_str(), nullptr);
  number = std::round((number - row.min) / step + delta) * step + row.min;
  number = std::clamp(number, row.min, row.max);
  return NumberText(number, std::max(StepDecimals(row), row.integer ? 0 : 1));
}

// A cvar's value as the row's knob ("true" as "1") and back.
inline std::string KnobOfCvar(const Row& row, const std::string& value) {
  if (row.Boolean()) return value == "true" || value == "1" ? "1" : "0";
  if (!row.choices.empty()) return value;
  return NumberText(std::strtod(value.c_str(), nullptr), 6);
}

inline std::string CvarOfKnob(const Row& row, const std::string& value) {
  if (row.Boolean()) return value == "1" ? "true" : "false";
  return value;
}

// "NAME=VALUE;NAME=VALUE" (shader_mod_defines), later items win.
inline std::map<std::string, std::string> ParseDefines(std::string_view text) {
  std::map<std::string, std::string> defines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find(';', start);
    if (end == std::string_view::npos) end = text.size();
    const std::string_view item = text.substr(start, end - start);
    const size_t equals = item.find('=');
    if (equals != std::string_view::npos && equals > 0)
      defines[std::string(item.substr(0, equals))] = std::string(item.substr(equals + 1));
    start = end + 1;
  }
  return defines;
}

// The knobs of a look value ("gt7,sharpen=0.5": presets from launcher.json's "look", later items
// win), as LookKnobs of the launcher.
inline std::vector<std::pair<std::string, std::string>> LookKnobs(const Json& launcher, std::string_view look) {
  std::vector<std::pair<std::string, std::string>> knobs;
  const auto set = [&knobs](std::string name, std::string value) {
    for (auto& [knob, current] : knobs)
      if (knob == name) return void(current = std::move(value));
    knobs.emplace_back(std::move(name), std::move(value));
  };
  const Json* tables = launcher.Find("look");
  const auto items = [](std::string_view text) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
      size_t end = text.find(',', start);
      if (end == std::string_view::npos) end = text.size();
      std::string item(text.substr(start, end - start));
      item.erase(0, item.find_first_not_of(" \t"));
      item.erase(item.find_last_not_of(" \t") + 1);
      if (!item.empty()) parts.push_back(item);
      start = end + 1;
    }
    return parts;
  };
  for (std::string item : items(look)) {
    const size_t equals = item.find('=');
    if (equals != std::string::npos) {
      set(item.substr(0, equals), item.substr(equals + 1));
      continue;
    }
    if (!tables) continue;
    if (const Json* aliases = tables->Find("aliases"); aliases && aliases->Find(item))
      item = aliases->Text(item);
    const Json* presets = tables->Find("presets");
    if (!presets || !presets->Find(item)) continue;
    if (item == "original") knobs.clear();
    for (const std::string& part : items(presets->Text(item))) {
      const size_t at = part.find('=');
      if (at != std::string::npos) set(part.substr(0, at), part.substr(at + 1));
    }
  }
  return knobs;
}

// The preset of a look value: its last item without '=' (the default look when it has none).
inline std::string LookPreset(const Json& launcher, std::string_view look) {
  std::string preset;
  size_t start = 0;
  while (start <= look.size()) {
    size_t end = look.find(',', start);
    if (end == std::string_view::npos) end = look.size();
    std::string item(look.substr(start, end - start));
    item.erase(0, item.find_first_not_of(" \t"));
    item.erase(item.find_last_not_of(" \t") + 1);
    if (!item.empty() && item.find('=') == std::string::npos) preset = item;
    start = end + 1;
  }
  if (const Json* tables = launcher.Find("look")) {
    if (const Json* aliases = tables->Find("aliases"); aliases && aliases->Find(preset))
      preset = aliases->Text(preset);
    if (preset.empty()) preset = tables->Text("default");
  }
  return preset;
}

// overlay-changes.json: the profile and options the run started with (as the launcher wrote
// them at Play) and the changes of this run, in the launcher's names: "changes" maps pad,
// keyboard, wheel and look to their changed knobs (look's "preset" sets its preset) and every
// other option to its new value.
struct Changes {
  std::string profile;
  Json options = Json::Object();
  Json changes = Json::Object();

  bool Read(std::string_view text, std::string& error) {
    Json json;
    if (!ParseJson(text, json, error)) return false;
    if (!json.IsObject()) return error = "not an object", false;
    profile = json.Text("profile");
    if (const Json* value = json.Find("options"); value && value->IsObject()) options = *value;
    if (const Json* value = json.Find("changes"); value && value->IsObject()) changes = *value;
    return true;
  }
  std::string Write() const {
    Json json = Json::Object();
    json.Set("profile", Json::String(profile));
    json.Set("options", options);
    json.Set("changes", changes);
    return DumpJson(json) + "\n";
  }
  // The option's value the run started with, as text ("" when the file has none).
  std::string Option(const std::string& option) const {
    const Json* value = options.Find(option);
    if (!value) return {};
    if (value->type == Json::Type::kBool) return value->boolean ? "1" : "0";
    if (value->type == Json::Type::kNumber) return NumberText(value->number, 0);
    return value->IsString() ? value->text : std::string();
  }
  // A change of this run, or nullptr.
  const std::string* Change(const std::string& option, const std::string& knob) const {
    const Json* value = changes.Find(option);
    if (!value) return nullptr;
    if (knob.empty()) return value->IsString() ? &value->text : nullptr;
    const Json* knob_value = value->IsObject() ? value->Find(knob) : nullptr;
    return knob_value && knob_value->IsString() ? &knob_value->text : nullptr;
  }
  void Set(const std::string& option, const std::string& knob, const std::string& value) {
    if (knob.empty()) {
      changes.Set(option, Json::String(value));
      return;
    }
    Json* group = nullptr;
    for (auto& [name, member] : changes.members)
      if (name == option) group = &member;
    if (!group || !group->IsObject()) group = &changes.Set(option, Json::Object());
    group->Set(knob, Json::String(value));
  }
};

}  // namespace fm4settings
