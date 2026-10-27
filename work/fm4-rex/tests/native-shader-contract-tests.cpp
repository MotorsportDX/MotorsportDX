// CPU tests of the native shader contract's manifest and pass classes
// (rex/graphics/d3d12/native_shader_contract.h, docs/analysis/shader-contract.md):
// the look's shaders.toml (the first argument) parses with its passes and entries,
// synthetic manifests with mistakes are refused, and draws get their pass class by
// their depth surface and the final point. Returns 1 on failure.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include <rex/graphics/d3d12/native_shader_contract.h>

namespace {

namespace nsc = rex::graphics::d3d12::native_shader_contract;

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

bool Parses(const char* text, nsc::Manifest& manifest) {
  std::string error;
  bool parsed = nsc::ParseManifest(text, "test.toml", manifest, error);
  if (!parsed) {
    std::printf("  refused: %s\n", error.c_str());
  }
  return parsed;
}

bool Refused(const char* text) {
  nsc::Manifest manifest;
  std::string error;
  if (nsc::ParseManifest(text, "test.toml", manifest, error)) {
    return false;
  }
  std::printf("  refused as expected: %s\n", error.c_str());
  return !error.empty();
}

}  // namespace

int main(int argc, char** argv) {
  // The look's manifest.
  if (argc < 2) {
    std::printf("usage: fm4_native_shader_contract_tests <look directory>/shaders.toml\n");
    return 1;
  }
  std::ifstream file(argv[1], std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  Check(!text.empty(), "the look's shaders.toml is read");
  nsc::Manifest look;
  Check(Parses(text.c_str(), look), "the look's shaders.toml parses");
  Check(look.FindPass("scene") == nsc::kFirstTitlePass, "scene is the first title pass");
  Check(look.FindPass("mirror") == nsc::kFirstTitlePass + 1, "mirror is the second");
  const nsc::PixelEntry* glass = look.FindPixel(0xE62CFDF09D758B6Cull);
  Check(glass != nullptr, "the car windows have an entry");
  if (glass) {
    Check(glass->hooks == "shader_E62CFDF09D758B6C.mod.hlsli", "the windows' hooks");
    Check(glass->pass_mask == nsc::kAllPasses, "the windows apply in every pass");
    Check(glass->params.size() == 3 && glass->params[0] == "FM4_LOOK_GLASS", "the windows' params");
  }
  // The other car materials in the mirror pass: lamp covers, paint, chrome and rims.
  int car_entries = 0;
  for (uint64_t hash : {0x00AF2783DCAB9C75ull, 0xAD10B610621DEFE5ull, 0xB85EF04779ABD438ull,
                        0xAF6AA26032BC613Dull, 0xF8CDBAFAD23FD784ull, 0x36DFBBD4669AF5E9ull,
                        0x75B334519A1568F6ull, 0x0412EE51F7B00389ull}) {
    const nsc::PixelEntry* entry = look.FindPixel(hash);
    car_entries += entry && entry->pass_mask == nsc::kAllPasses && !entry->params.empty() &&
                   entry->params[0].rfind("FM4_LOOK_", 0) == 0;
  }
  Check(car_entries == 8, "the car materials have entries with their knobs");
  const nsc::PixelEntry* combine = look.FindPixel(0xE97AA211B556A9B3ull);
  Check(combine && combine->params.empty() && combine->srv.size() == 1 &&
            combine->srv[0].kind == nsc::Resource::Kind::kPost &&
            combine->srv[0].name == "gtao_occlusion",
        "the final combine reads the ambient occlusion by name");
  // The look's classes by the depth surface and the viewport: the scene at 1024 as wide as the
  // screen, the mirror at 80.
  Check(look.Classify(true, 1024, 1280, false) == look.FindPass("scene"), "scene draw");
  Check(look.Classify(true, 1024, 1024, false) == look.FindPass("scene"), "scene at 1024 wide");
  Check(look.Classify(true, 1024, 480, false) == nsc::kPassOther, "a narrow draw on the scene");
  Check(look.Classify(true, 80, 384, false) == look.FindPass("mirror"), "mirror draw");
  Check(look.Classify(true, 80, 384, true) == look.FindPass("mirror"),
        "mirror after the final point");
  Check(look.Classify(true, 224, 1280, false) == nsc::kPassOther, "unknown depth surface");
  Check(look.Classify(false, 1024, 1280, false) == nsc::kPassPost,
        "untested draw before the final");
  Check(look.Classify(false, 1024, 1280, true) == nsc::kPassHud, "untested draw after the final");

  // A manifest with passes and a narrowed entry.
  nsc::Manifest manifest;
  Check(Parses(R"(
[[pass]]
name = "scene"
depth_base = [1024, 720]
[[pass]]
name = "shadow"
depth_base = 0

[[pixel]]
hash = "0123456789abcdef"
hooks = "shader_0123456789ABCDEF.mod.hlsli"
passes = ["scene", "hud"]
params = "FM4_LOOK_X"
srv = ["post:gtao_occlusion", "post:motion_vectors"]
)",
               manifest),
        "a manifest with two passes and an entry");
  Check(manifest.PassCount() == nsc::kFirstTitlePass + 2, "two title passes");
  Check(manifest.PassName(nsc::kPassOther) == "other" &&
            manifest.PassName(nsc::kPassPost) == "post" &&
            manifest.PassName(nsc::kPassHud) == "hud" &&
            manifest.PassName(nsc::kFirstTitlePass + 1) == "shadow",
        "pass names");
  Check(manifest.Classify(true, 720, 0, false) == nsc::kFirstTitlePass, "the second scene base");
  Check(manifest.Classify(true, 0, 512, false) == nsc::kFirstTitlePass + 1, "base 0");
  const nsc::PixelEntry* entry = manifest.FindPixel(0x0123456789ABCDEFull);
  Check(entry && entry->pass_mask == ((1u << nsc::kFirstTitlePass) | (1u << nsc::kPassHud)),
        "the entry's passes");
  Check(entry && entry->params.size() == 1 && entry->params[0] == "FM4_LOOK_X",
        "one param as a string");
  Check(entry && entry->srv.size() == 2 &&
            entry->srv[0].kind == nsc::Resource::Kind::kPost &&
            entry->srv[0].name == "gtao_occlusion" && entry->srv[1].name == "motion_vectors",
        "two post textures by name");
  Check(!manifest.FindPixel(0x0123456789ABCDEEull), "no entry for another hash");
  nsc::Manifest empty;
  Check(Parses("", empty) && empty.pixel.empty() && empty.PassCount() == nsc::kFirstTitlePass,
        "an empty manifest");

  // Mistakes.
  Check(Refused("[scene]\n"), "unknown top-level key");
  Check(Refused("[[pass]]\nname = \"Scene\"\ndepth_base = 1\n"), "upper case pass name");
  Check(Refused("[[pass]]\nname = \"hud\"\ndepth_base = 1\n"), "built-in pass name");
  Check(Refused("[[pass]]\nname = \"a\"\ndepth_base = 1\n[[pass]]\nname = \"a\"\ndepth_base = 2\n"),
        "pass defined twice");
  Check(Refused("[[pass]]\nname = \"a\"\n"), "pass without depth_base");
  Check(Refused("[[pass]]\nname = \"a\"\ndepth_base = 4096\n"), "depth_base out of range");
  Check(Refused("[[pass]]\nname = \"a\"\ndepth_base = 1\nlayer = 2\n"), "unknown pass key");
  Check(Refused("[[pass]]\nname = \"a\"\ndepth_base = 1\nmax_viewport_width = -1\n"),
        "negative viewport width");
  Check(Refused("[[pass]]\nname = \"a\"\ndepth_base = 1\nmin_viewport_width = 600\n"
                "max_viewport_width = 500\n"),
        "viewport widths crossed");
  Check(Refused("[[pixel]]\nhash = \"0123\"\nhooks = \"a.hlsli\"\n"), "short hash");
  Check(Refused("[[pixel]]\nhash = \"0123456789ABCDEF\"\n"), "entry without hooks");
  Check(Refused("[[pixel]]\nhash = \"0123456789ABCDEF\"\nhooks = \"../a.hlsli\"\n"),
        "hooks outside the look directory");
  Check(Refused("[[pixel]]\nhash = \"0123456789ABCDEF\"\nhooks = \"a.hlsli\"\npasses = [\"x\"]\n"),
        "unknown pass in an entry");
  Check(Refused("[[pixel]]\nhash = \"0123456789ABCDEF\"\nhooks = \"a.hlsli\"\nparams = [\"1X\"]\n"),
        "bad param name");
  const std::string with_entry = "[[pixel]]\nhash = \"0123456789ABCDEF\"\nhooks = \"a.hlsli\"\n";
  Check(Refused((with_entry + "srv = [\"gtao_occlusion\"]\n").c_str()), "srv without a kind");
  Check(Refused((with_entry + "srv = [\"scene:depth\"]\n").c_str()), "unknown srv kind");
  Check(Refused((with_entry + "srv = [\"post:\"]\n").c_str()), "srv without a name");
  Check(Refused((with_entry + "srv = [\"post:Motion\"]\n").c_str()), "upper case srv name");
  Check(Refused((with_entry + "srv = [\"post:a\", \"post:a\"]\n").c_str()), "srv twice");
  Check(Refused((with_entry + "srv = [1]\n").c_str()), "srv not a name");
  Check(Refused("[[pixel]]\nhash = \"0123456789ABCDEF\"\nshader = \"a.hlsl\"\n"),
        "replacement shaders are not supported yet");
  Check(Refused("[[pixel]]\nhash = \"0123456789ABCDEF\"\nhooks = \"a.hlsli\"\n[[pixel]]\nhash = "
                "\"0123456789abcdef\"\nhooks = \"b.hlsli\"\n"),
        "two entries of one hash");
  Check(Refused("[[pixel]]\nhash = \"0123456789ABCDEF\"\nhooks = \"a.hlsli\"\nvertex = 1\n"),
        "unknown entry key");
  Check(Refused("[[pass]\n"), "syntax error");

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("native shader contract tests passed\n");
  return 0;
}
