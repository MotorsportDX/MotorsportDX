/**
 * @file        graphics/d3d12/native_shader_contract.cpp
 * @brief       The native path's shader contract: shaders.toml
 */

#include <rex/graphics/d3d12/native_shader_contract.h>

#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>

#include <toml++/toml.hpp>

namespace rex::graphics::d3d12::native_shader_contract {

namespace {

// The pieces as one string (no fmt: the CPU tests build this file without it).
std::string Text(std::initializer_list<std::string_view> pieces) {
  std::string text;
  for (std::string_view piece : pieces) {
    text += piece;
  }
  return text;
}

// A lower-case name of letters, digits and '_' that does not start with a digit: it becomes
// part of a define (XE_NATIVE_PASS_<NAME>) or of a function (xe_srv_<name>).
bool IsPassName(std::string_view name) {
  if (name.empty() || (name[0] >= '0' && name[0] <= '9')) {
    return false;
  }
  for (char c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

// A define name: letters, digits and '_', not starting with a digit.
bool IsDefineName(std::string_view name) {
  if (name.empty() || (name[0] >= '0' && name[0] <= '9')) {
    return false;
  }
  for (char c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

// The first key of table not in allowed, or an empty view.
std::string_view UnknownKey(const toml::table& table,
                            std::initializer_list<std::string_view> allowed) {
  for (auto&& [key, node] : table) {
    bool known = false;
    for (std::string_view name : allowed) {
      known |= key.str() == name;
    }
    if (!known) {
      return key.str();
    }
  }
  return std::string_view();
}

// A list of strings: an array of them, or one string.
bool StringList(const toml::node* node, std::vector<std::string>& out) {
  out.clear();
  if (!node) {
    return true;
  }
  if (auto text = node->value<std::string>()) {
    out.push_back(*text);
    return true;
  }
  const toml::array* array = node->as_array();
  if (!array) {
    return false;
  }
  for (const toml::node& item : *array) {
    auto text = item.value<std::string>();
    if (!text) {
      return false;
    }
    out.push_back(*text);
  }
  return true;
}

}  // namespace

bool ParseManifest(std::string_view text, const std::string& file_name, Manifest& manifest_out,
                   std::string& error_out) {
  Manifest manifest;
  auto fail = [&error_out](std::string message) {
    error_out = std::move(message);
    return false;
  };
  toml::table root;
  try {
    root = toml::parse(text, file_name);
  } catch (const toml::parse_error& error) {
    return fail(
        Text({"line ", std::to_string(error.source().begin.line), ": ", error.description()}));
  }
  if (std::string_view key = UnknownKey(root, {"pass", "pixel"}); !key.empty()) {
    return fail(Text({"unknown key '", key, "'"}));
  }

  // [[pass]]: the title's pass classes, after the built-in ones.
  if (const toml::node* node = root.get("pass")) {
    const toml::array* passes = node->as_array();
    if (!passes) {
      return fail("'pass' must be an array of tables ([[pass]])");
    }
    for (const toml::node& item : *passes) {
      const toml::table* fields = item.as_table();
      if (!fields) {
        return fail("'pass' must be an array of tables ([[pass]])");
      }
      if (std::string_view key = UnknownKey(
              *fields, {"name", "depth_base", "min_viewport_width", "max_viewport_width"});
          !key.empty()) {
        return fail(Text({"[[pass]]: unknown key '", key, "'"}));
      }
      PassRule& rule = manifest.passes.emplace_back();
      rule.name = (*fields)["name"].value_or(std::string());
      if (!IsPassName(rule.name)) {
        return fail(
            Text({"[[pass]]: name '", rule.name, "' is not lower case letters, digits and _"}));
      }
      if (manifest.FindPass(rule.name) != manifest.PassCount() - 1) {
        return fail(Text({"[[pass]]: the pass '", rule.name, "' is defined twice or is built in"}));
      }
      if (manifest.PassCount() > kPassCount) {
        return fail(Text({"more than ", std::to_string(kPassCount - kFirstTitlePass), " passes"}));
      }
      const toml::node* bases = fields->get("depth_base");
      auto add_base = [&rule](const toml::node& base) {
        auto value = base.value<int64_t>();
        if (!value || *value < 0 || *value >= 4096) {
          return false;
        }
        rule.depth_bases.push_back(uint32_t(*value));
        return true;
      };
      bool bases_valid = bases != nullptr;
      if (const toml::array* array = bases ? bases->as_array() : nullptr) {
        for (const toml::node& base : *array) {
          bases_valid &= add_base(base);
        }
      } else if (bases) {
        bases_valid = add_base(*bases);
      }
      if (!bases_valid || rule.depth_bases.empty()) {
        return fail(
            Text({"[[pass]] '", rule.name, "': depth_base must be EDRAM tiles, 0 to 4095"}));
      }
      for (auto [key, width] : {std::pair{"min_viewport_width", &rule.min_viewport_width},
                                std::pair{"max_viewport_width", &rule.max_viewport_width}}) {
        if (const toml::node* value = fields->get(key)) {
          auto number = value->value<int64_t>();
          if (!number || *number < 0 || *number > 8192) {
            return fail(
                Text({"[[pass]] '", rule.name, "': ", key, " must be guest pixels, 0 to 8192"}));
          }
          *width = uint32_t(*number);
        }
      }
      if (rule.min_viewport_width > rule.max_viewport_width) {
        return fail(
            Text({"[[pass]] '", rule.name, "': min_viewport_width is above max_viewport_width"}));
      }
    }
  }

  // [[pixel]]: the entries.
  if (const toml::node* node = root.get("pixel")) {
    const toml::array* pixels = node->as_array();
    if (!pixels) {
      return fail("'pixel' must be an array of tables ([[pixel]])");
    }
    for (const toml::node& item : *pixels) {
      const toml::table* fields = item.as_table();
      if (!fields) {
        return fail("'pixel' must be an array of tables ([[pixel]])");
      }
      std::string hash_text = (*fields)["hash"].value_or(std::string());
      if (fields->contains("shader")) {
        return fail(Text({"[[pixel]] ", hash_text,
                          ": replacement shaders (shader) are not supported yet, only hooks"}));
      }
      if (std::string_view key =
              UnknownKey(*fields, {"hash", "hooks", "passes", "params", "srv"});
          !key.empty()) {
        return fail(Text({"[[pixel]] ", hash_text, ": unknown key '", key, "'"}));
      }
      PixelEntry entry;
      char* end = nullptr;
      entry.hash = std::strtoull(hash_text.c_str(), &end, 16);
      if (hash_text.size() != 16 || end != hash_text.c_str() + 16) {
        return fail(Text({"[[pixel]]: hash '", hash_text, "' is not 16 hexadecimal digits"}));
      }
      if (manifest.FindPixel(entry.hash)) {
        return fail(Text({"[[pixel]] ", hash_text, ": the hash has two entries"}));
      }
      entry.hooks = (*fields)["hooks"].value_or(std::string());
      if (entry.hooks.empty() || entry.hooks.find_first_of("/\\:") != std::string::npos) {
        return fail(
            Text({"[[pixel]] ", hash_text, ": hooks must name a file of the look directory"}));
      }
      std::vector<std::string> names;
      if (!StringList(fields->get("passes"), names)) {
        return fail(Text({"[[pixel]] ", hash_text, ": passes must be pass names"}));
      }
      if (fields->contains("passes")) {
        entry.pass_mask = 0;
        for (const std::string& name : names) {
          uint32_t pass = manifest.FindPass(name);
          if (pass == UINT32_MAX) {
            return fail(Text({"[[pixel]] ", hash_text, ": no pass '", name, "'"}));
          }
          entry.pass_mask |= uint32_t(1) << pass;
        }
      }
      if (!StringList(fields->get("params"), entry.params)) {
        return fail(Text({"[[pixel]] ", hash_text, ": params must be define names"}));
      }
      for (const std::string& name : entry.params) {
        if (!IsDefineName(name)) {
          return fail(Text({"[[pixel]] ", hash_text, ": param '", name, "' is not a define name"}));
        }
      }
      std::vector<std::string> resources;
      if (!StringList(fields->get("srv"), resources)) {
        return fail(Text({"[[pixel]] ", hash_text, ": srv must be resource names"}));
      }
      for (const std::string& resource : resources) {
        // A kind and a lower-case name (part of xe_srv_<name>).
        size_t colon = resource.find(':');
        std::string_view kind = colon == std::string::npos
                                    ? std::string_view()
                                    : std::string_view(resource).substr(0, colon);
        std::string name = colon == std::string::npos ? std::string() : resource.substr(colon + 1);
        if (kind != "post") {
          return fail(Text({"[[pixel]] ", hash_text, ": srv '", resource,
                            "' is not post:<texture> (the only kind so far)"}));
        }
        if (!IsPassName(name)) {
          return fail(Text({"[[pixel]] ", hash_text, ": srv '", resource,
                            "': the name must be lower-case letters, digits and '_'"}));
        }
        for (const Resource& other : entry.srv) {
          if (other.name == name) {
            return fail(Text({"[[pixel]] ", hash_text, ": srv '", name, "' twice"}));
          }
        }
        entry.srv.push_back({Resource::Kind::kPost, std::move(name)});
      }
      manifest.pixel.push_back(std::move(entry));
    }
  }
  manifest_out = std::move(manifest);
  return true;
}

}  // namespace rex::graphics::d3d12::native_shader_contract
