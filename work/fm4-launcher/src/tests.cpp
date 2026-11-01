// MotorsportDX.exe --test <cases.json> <result.json>: the launcher tests of pipeline.py
// (launcher_tests). The cases hold pipeline.py's launch plans of a set of options on a set of
// monitors and its check of the Discs and content of local.json; the result lists every
// difference of this port.
#include <fstream>
#include <map>
#include <set>

#include "discs.h"
#include "iso.h"
#include "settings.h"

namespace fm4 {

namespace {

void Compare(const std::string& what, const Json& expected, const Pairs& actual,
             std::vector<std::string>& failures) {
  std::map<std::string, std::string> got(actual.Items().begin(), actual.Items().end());
  std::set<std::string> keys;
  for (const auto& [key, value] : expected.items()) keys.insert(key);
  for (const auto& [key, value] : got) keys.insert(key);
  for (const auto& key : keys) {
    const std::string want = expected.contains(key) ? expected.at(key).get<std::string>() : "<none>";
    const std::string have = got.contains(key) ? got.at(key) : "<none>";
    if (want != have) failures.push_back(what + " " + key + ": expected '" + want + "', got '" + have + "'");
  }
}

}  // namespace

int RunTests(const std::filesystem::path& cases_path, const std::filesystem::path& result_path) {
  std::ifstream stream(cases_path, std::ios::binary);
  const Json cases = Json::parse(stream);
  const std::string root = cases.at("root").get<std::string>();
  const LauncherData data = LauncherData::Load(PathFromUtf8(root) / "launcher.json");
  std::vector<std::string> failures;
  int plans = 0;
  for (const auto& test : cases.at("plans")) {
    const Options options = OptionsFromJson(test.at("options"));
    const Json& screen = test.at("monitor");
    const Monitor monitor{screen.at(0).get<int>(), screen.at(1).get<int>(), screen.at(2).get<double>(),
                          screen.at(3).get<int>(),   screen.at(4).get<int>(), screen.at(5).get<int>(),
                          screen.size() > 6 && screen.at(6).get<bool>()};
    const std::string what = test.at("options").dump() + " on " + test.at("monitor").dump();
    ++plans;
    try {
      const Plan plan = LaunchPlan(data, root, cases.at("disc1").get<std::string>(), options, monitor,
                                   cases.at("manifest").get<std::string>());
      const Json& expected = test.at("expected");
      Compare(what + " cvar", expected.at("cvars"), plan.cvars, failures);
      Compare(what + " env", expected.at("env"), plan.env, failures);
      if (plan.description != expected.at("description").get<std::string>())
        failures.push_back(what + " description: expected '" +
                           expected.at("description").get<std::string>() + "', got '" +
                           plan.description + "'");
    } catch (const std::exception& error) {
      failures.push_back(what + ": " + error.what());
    }
  }
  const Json& discs = cases.at("discs");
  const DiscCheck disc1 = CheckDisc1(data, discs.at("disc1").get<std::string>());
  if (!disc1.error.empty() || disc1.release != discs.at("release").get<std::string>())
    failures.push_back("Disc 1: expected release " + discs.at("release").get<std::string>() +
                       ", got '" + disc1.release + "' " + disc1.error);
  const std::string disc2 = discs.at("disc2").is_string() ? discs.at("disc2").get<std::string>() : "";
  const DiscCheck disc2_check = CheckDisc2(data, disc2);
  if (!disc2_check.error.empty()) failures.push_back("Disc 2: " + disc2_check.error);
  std::vector<std::string> content;
  for (const auto& item : discs.at("content")) content.push_back(item.get<std::string>());
  const ContentCheck check = CheckContent(data, disc2, content);
  if (!check.error.empty()) failures.push_back("Content: " + check.error);
  std::vector<std::string> expected_packages, packages;
  for (const auto& item : discs.at("packages")) expected_packages.push_back(item.get<std::string>());
  for (const auto& package : check.packages) packages.push_back(Utf8(package.path));
  if (packages != expected_packages)
    failures.push_back("Content packages: expected " + Json(expected_packages).dump() + ", got " +
                       Json(packages).dump());
  const Json& image = cases.at("image");
  const DiscImage read = ReadDiscImage(PathFromUtf8(image.at("path").get<std::string>()));
  Json entries = Json::array();
  for (const auto& entry : read.entries)
    entries.push_back(entry.directory ? Json{entry.path, nullptr, 0} : Json{entry.path, entry.offset, entry.size});
  if (!read.error.empty() || entries != image.at("entries"))
    failures.push_back("Disc image: expected " + image.at("entries").dump() + ", got " + entries.dump() + " " +
                       read.error);
  const std::filesystem::path out = PathFromUtf8(image.at("out").get<std::string>());
  std::atomic<std::uint64_t> done{0};
  const std::atomic<bool> cancel{false};
  const std::string unpacked = UnpackDiscImage(PathFromUtf8(image.at("path").get<std::string>()), read.entries, out, done, cancel);
  if (!unpacked.empty()) failures.push_back("Disc image unpack: " + unpacked);
  for (const auto& [path, hash] : image.at("files").items()) {
    std::string have;
    try {
      have = Sha256File(out / PathFromUtf8(path));
    } catch (const std::exception& error) {
      have = error.what();
    }
    if (have != hash.get<std::string>()) failures.push_back("Disc image file " + path + ": " + have);
  }
  if (!std::filesystem::is_directory(out / "Media" / "Empty")) failures.push_back("Disc image: no Media/Empty");
  if (ReadDiscImage(PathFromUtf8(image.at("other").get<std::string>())).error.empty())
    failures.push_back("Disc image: a text file was read as one");
  const std::filesystem::path unpack_root = PathFromUtf8(image.at("discs").get<std::string>());
  std::atomic<std::uint64_t> total{0};
  std::string label;
  const auto unpack = [&](const std::string& key) {
    label.clear();
    return UnpackDisc(data, unpack_root, PathFromUtf8(image.at(key).get<std::string>()), done, total, cancel,
                      [&](const std::string& name) { label = name; });
  };
  DiscUnpack unpacked_disc = unpack("disc2");
  if (unpacked_disc.disc1 || label != "Disc 2" || unpacked_disc.folder != Utf8(unpack_root / "disc2") ||
      !CheckDisc2(data, unpacked_disc.folder).error.empty() || done != total)
    failures.push_back("Disc image as Disc 2: " + unpacked_disc.message);
  unpacked_disc = unpack("disc2");
  if (!label.empty() || unpacked_disc.folder != Utf8(unpack_root / "disc2"))
    failures.push_back("Disc image as Disc 2 again: " + unpacked_disc.message);
  unpacked_disc = unpack("disc1");
  if (!unpacked_disc.folder.empty() || !unpacked_disc.disc1 || std::filesystem::exists(unpack_root / "disc1") ||
      std::filesystem::exists(unpack_root / "disc1.partial"))
    failures.push_back("Disc image of no release: " + unpacked_disc.message);
  unpacked_disc = unpack("path");
  if (!unpacked_disc.folder.empty() || unpacked_disc.message.find("neither") == std::string::npos)
    failures.push_back("Disc image of neither Disc: " + unpacked_disc.message);
  std::ofstream result(result_path, std::ios::binary | std::ios::trunc);
  result << Json{{"plans", plans}, {"failures", failures}}.dump(1);
  return result ? 0 : 1;
}

}  // namespace fm4
