/**
 * @file        graphics/d3d12/native_shader_contract.h
 * @brief       The native path's shader contract: the manifest and the pass classes
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rex::graphics::d3d12::native_shader_contract {

// The native shader contract (native_shader_contract, docs/analysis/shader-contract.md):
// <shader_mod_path>/shaders.toml declares, per guest pixel shader, the mod it takes on the
// native path, the pass classes it applies in, and the knobs it reads as constants. The pass
// class of a draw comes from its depth surface, its viewport's width and the post passes'
// final point: the manifest's [[pass]] rules first, then the built-in classes. Header only, so
// the CPU tests check the classification.

// Built-in pass classes; the manifest's come after them, in the file's order.
constexpr uint32_t kPassOther = 0;  // depth or stencil tested, no rule matches
constexpr uint32_t kPassPost = 1;   // neither tested, before the final point
constexpr uint32_t kPassHud = 2;    // neither tested, after the final point
constexpr uint32_t kFirstTitlePass = 3;
// Four bits in the draw back end's flags.
constexpr uint32_t kPassCount = 16;
constexpr uint32_t kAllPasses = (uint32_t(1) << kPassCount) - 1;

// A [[pass]] rule: draws with depth or stencil testing on a depth surface at one of the EDRAM
// bases (in tiles, as RB_DEPTH_INFO holds them), with a viewport from min_viewport_width to
// max_viewport_width guest pixels wide (the rear-view mirror shares the scene's depth surface
// on the native path, in a narrower viewport).
struct PassRule {
  std::string name;
  std::vector<uint32_t> depth_bases;
  uint32_t min_viewport_width = 0;
  uint32_t max_viewport_width = UINT32_MAX;
};

// The look files (srv = ["file:<name>"]) of a manifest: at most kMaxLookFiles names, each a
// PNG of at most kMaxLookFileBytes, kMaxLookFileSize pixels a side, 8 bits per channel, RGB
// or RGBA.
constexpr uint32_t kMaxLookFiles = 8;
constexpr size_t kMaxLookFileBytes = size_t(16) << 20;
constexpr uint32_t kMaxLookFileSize = 4096;

// A resource an entry reads by name (srv), as xe_srv_<name>(uv) in its mod.
struct Resource {
  enum class Kind {
    // A texture the post passes expose to mods (a slot in passes.toml), by its name there.
    kPost,
    // A texture of the look directory, <name>.png (8-bit RGBA, the project's own data such
    // as a LUT or noise), also read bilinear as xe_srv_<name>_linear(uv).
    kFile,
  };
  Kind kind = Kind::kPost;
  std::string name;
};

// A [[pixel]] entry.
struct PixelEntry {
  // The runtime's key of the guest pixel shader (XXH3 of its ucode).
  uint64_t hash = 0;
  // The mod in shader_mod_path that hooks into the shader's generated HLSL.
  std::string hooks;
  // Bit per pass class the entry applies in; the others take the plain translation.
  uint32_t pass_mask = kAllPasses;
  // Defines of the mod read as constants after the host values (xe_mod_knob), so that
  // shader_mod_set changes them at once and every look shares one binary.
  std::vector<std::string> params;
  // The resources the mod reads by name ("post:<texture>", "file:<name>"), each name once.
  std::vector<Resource> srv;
};

struct Manifest {
  std::vector<PassRule> passes;
  std::vector<PixelEntry> pixel;

  uint32_t PassCount() const { return kFirstTitlePass + uint32_t(passes.size()); }

  std::string_view PassName(uint32_t pass) const {
    switch (pass) {
      case kPassOther:
        return "other";
      case kPassPost:
        return "post";
      case kPassHud:
        return "hud";
      default:
        return pass - kFirstTitlePass < passes.size()
                   ? std::string_view(passes[pass - kFirstTitlePass].name)
                   : std::string_view();
    }
  }

  // The class of a name, or UINT32_MAX.
  uint32_t FindPass(std::string_view name) const {
    for (uint32_t pass = 0; pass < PassCount(); ++pass) {
      if (PassName(pass) == name) {
        return pass;
      }
    }
    return UINT32_MAX;
  }

  // Few entries: a scan is cheaper than a hash map at every draw.
  const PixelEntry* FindPixel(uint64_t hash) const {
    for (const PixelEntry& entry : pixel) {
      if (entry.hash == hash) {
        return &entry;
      }
    }
    return nullptr;
  }

  // The pass class of a draw: depth_tested if depth or stencil testing is on, depth_base the
  // depth surface's EDRAM base, viewport_width the viewport's width in guest pixels (0 without
  // viewport scaling), after_final whether the post passes' final point has run in the frame.
  // A rule wins over the final point (a mirror drawn after it); the first rule that matches.
  uint32_t Classify(bool depth_tested, uint32_t depth_base, uint32_t viewport_width,
                    bool after_final) const {
    if (depth_tested) {
      for (size_t i = 0; i < passes.size(); ++i) {
        const PassRule& rule = passes[i];
        if (viewport_width < rule.min_viewport_width || viewport_width > rule.max_viewport_width) {
          continue;
        }
        for (uint32_t base : rule.depth_bases) {
          if (base == depth_base) {
            return kFirstTitlePass + uint32_t(i);
          }
        }
      }
      return kPassOther;
    }
    return after_final ? kPassHud : kPassPost;
  }
};

// Parses shaders.toml (native_shader_contract.cpp): false with the reason in error_out on a
// syntax error, an unknown key, a bad value or a name used twice.
// Whether data is a look file the runtime takes (kMaxLookFileBytes, kMaxLookFileSize, an 8-bit
// RGB or RGBA PNG), from the PNG's signature and header chunk; its size, or why not.
bool CheckLookPng(std::string_view data, uint32_t& width_out, uint32_t& height_out,
                  std::string& error_out);

bool ParseManifest(std::string_view text, const std::string& file_name, Manifest& manifest_out,
                   std::string& error_out);

}  // namespace rex::graphics::d3d12::native_shader_contract
