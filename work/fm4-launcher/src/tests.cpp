// MotorsportDX.exe --test <cases.json> <result.json>: the launcher tests of pipeline.py
// (launcher_tests). The cases hold pipeline.py's launch plans of a set of options on a set of
// monitors and its check of the Discs and content of local.json; the result lists every
// difference of this port.
#include <algorithm>
#include <tuple>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include "builder.h"
#include "discs.h"
#include "iso.h"
#include "mods.h"
#include "overlay.h"
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

// An update (builder.h, update_test_case in builder.py): a later package into an earlier install
// replaces its source and package files and keeps its folders; the files an earlier package left
// in the source go; the backup holds the saves and the settings, not the imported content.
void UpdateTests(const Json& update, std::vector<std::string>& failures) {
  namespace fs = std::filesystem;
  const fs::path install = PathFromUtf8(update.at("install").get<std::string>()),
                 package = PathFromUtf8(update.at("package").get<std::string>());
  const auto files = [](const fs::path& folder) {
    std::vector<std::string> list;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(folder, error); !error && it != fs::recursive_directory_iterator();
         it.increment(error))
      if (it->is_regular_file()) list.push_back(Utf8(fs::path(it->path().lexically_relative(folder).generic_wstring())));
    std::sort(list.begin(), list.end());
    return Json(list);
  };
  const auto text = [](const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  };
  if (UpdateInstall(package, package / "source").empty()) failures.push_back("Builder update: took a folder of no install");
  if (UpdateInstall(package, package).empty()) failures.push_back("Builder update: took the package's own folder");
  if (const std::string error = UpdateInstall(package, install); !error.empty())
    failures.push_back("Builder update: " + error);
  if (files(install / "source") != update.at("source"))
    failures.push_back("Builder update, the source: " + files(install / "source").dump());
  for (const char* name : {"MotorsportDX.exe", "launcher.json", "toolchain.json", "README.txt", "third-party-notices/A.txt"})
    if (text(install / name) != "new") failures.push_back(std::string("Builder update, not copied: ") + name);
  for (const char* name : {"source.new", "source.old", "update.new"})
    if (fs::exists(install / name)) failures.push_back(std::string("Builder update, left: ") + name);
  if (text(install / "build" / "build-result.json") == "{}" || text(install / "logs" / "builder.log").starts_with("package") ||
      !fs::is_regular_file(install / "downloads" / "tool.zip"))
    failures.push_back("Builder update: changed the install's build, logs or downloads");
  // An earlier package unpacked into the same folder: its files and Python's caches go.
  for (const char* name : {"work/old-only.cpp", "stale/__pycache__/x.pyc"}) {
    fs::create_directories((install / "source" / name).parent_path());
    std::ofstream(install / "source" / name) << "old";
  }
  const size_t removed = PruneSource(install / "source");
  if (removed != 2 || files(install / "source") != update.at("source") || fs::exists(install / "source" / "stale"))
    failures.push_back("Builder prune: " + std::to_string(removed) + " removed, " + files(install / "source").dump());
  BuildInputs inputs;
  inputs.user_data = install / "build" / "user-data";
  inputs.settings = install / "build" / "launcher-settings.json";
  const fs::path backup = install / "backups" / "test";
  if (const std::string error = BackUpSaves(inputs, backup); !error.empty()) failures.push_back("Builder backup: " + error);
  if (files(backup) != update.at("backup")) failures.push_back("Builder backup: " + files(backup).dump());
}

// The player's settings of launcher.json and the folding of the in-game overlay's changes
// (overlay.h): a change lands in the profile it names, a value equal to the launchers' default
// leaves the knob out, and an unknown profile, option, knob or value is dropped with a log line.
void OverlayTests(const LauncherData& data, const Json& cases, const std::filesystem::path& work,
                  std::vector<std::string>& failures) {
  for (const auto& problem : CheckPlayerSettings(data)) failures.push_back(problem);
  Settings settings;
  settings.profiles = {{"Default", Options{}}, {"Race", Options{}}};
  settings.profiles[1].options.pad = "driver=rawinput,rumble=1.2";
  settings.active = "Default";
  const auto fold = [&](const Json& changes, const std::string& profile = "Race") {
    std::vector<std::string> log;
    const bool changed = FoldOverlayChanges(data, Json{{"profile", profile}, {"changes", changes}}, settings, log);
    return std::make_pair(changed, log);
  };
  const auto expect = [&](const std::string& what, const std::string& have, const std::string& want) {
    if (have != want) failures.push_back("Overlay fold " + what + ": expected '" + want + "', got '" + have + "'");
  };
  const auto logged = [](const std::vector<std::string>& log, const std::string& text) {
    return std::any_of(log.begin(), log.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
  };
  auto [changed, log] = fold(Json{{"pad", {{"rumble", "0.8"}, {"stick_deadzone", "0.1"}}},
                                  {"keyboard", {{"steer_attack", "0.5"}}},
                                  {"look", {{"sharpen", "0.6"}}},
                                  {"quality", "ultra"},
                                  {"fullscreen", "1"},
                                  {"frame_rate", "120"},
                                  {"resolution_scale", "-1"}});
  const Options& race = settings.profiles[1].options;
  if (!changed) failures.push_back("Overlay fold: no change");
  expect("pad", race.pad, "driver=rawinput,rumble=0.8,stick_deadzone=0.1");
  expect("keyboard", race.keyboard, "steer_attack=0.5");
  expect("look", race.look, "aces,sharpen=0.6");
  expect("quality", race.quality, "ultra");
  expect("fullscreen", race.fullscreen ? "1" : "0", "1");
  expect("frame rate", std::to_string(race.frame_rate), "120");
  expect("render scale", std::to_string(race.resolution_scale), std::to_string(kPanelScale));
  expect("other profile", OptionsToJson(settings.profiles[0].options).dump(), OptionsToJson(Options{}).dump());
  // The launchers' default leaves the knob out; the preset first, then the knobs against it.
  std::tie(changed, log) = fold(Json{{"pad", {{"rumble", "1.5"}}}, {"look", {{"sharpen", "0.4"}, {"preset", "gt7"}}}});
  expect("pad at its default", race.pad, "driver=rawinput,stick_deadzone=0.1");
  expect("look preset", race.look, "gt7");
  std::tie(changed, log) = fold(Json{{"look", {{"windshield", "0"}, {"taa", "0"}}}, {"upscale", "quality"}});
  expect("look knobs", race.look, "gt7,windshield=0,taa=0");
  expect("upscale", race.upscale, "quality");
  // Dropped with a log line, the rest folded.
  const std::string before = OptionsToJson(race).dump();
  std::tie(changed, log) = fold(Json{{"pad", {{"bogus", "1"}, {"rumble", "9"}}},
                                     {"frobnicate", "1"},
                                     {"quality", "best"},
                                     {"look", {{"preset", "nope"}, {"bogus", "1"}}},
                                     {"vsync", "on"}});
  expect("partly dropped", race.vsync, "on");
  for (const char* text : {"pad bogus=1: dropped", "pad rumble=9: dropped", "frobnicate=1: dropped",
                           "quality=best: dropped", "look preset=nope: dropped", "look bogus=1: dropped"})
    if (!logged(log, text)) failures.push_back(std::string("Overlay fold: no log line '") + text + "'");
  if (OptionsToJson(race).dump() == before) failures.push_back("Overlay fold: the valid change was dropped");
  std::tie(changed, log) = fold(Json{{"quality", "low"}}, "Gone");
  if (changed || !logged(log, "'Gone', which does not exist") || race.quality != "ultra")
    failures.push_back("Overlay fold of an unknown profile");
  // The file at Play, its folding and removal; a damaged file is dropped with a log line.
  const std::filesystem::path path = work / kOverlayChangesFile;
  Json start = OverlayStart(settings.profiles[1]);
  if (OptionsToJson(OptionsFromJson(start.at("options"))) != OptionsToJson(race))
    failures.push_back("Overlay start: options do not round-trip");
  start["changes"] = Json{{"frame_rate", "240"}};
  std::ofstream(path, std::ios::binary | std::ios::trunc) << start.dump(1);
  log.clear();
  if (!FoldOverlayFile(data, path, settings, log) || race.frame_rate != 240 || std::filesystem::exists(path))
    failures.push_back("Overlay file: not folded and removed");
  std::ofstream(path, std::ios::binary | std::ios::trunc) << "{\"profile\": ";
  log.clear();
  if (FoldOverlayFile(data, path, settings, log) || !logged(log, "not JSON") || std::filesystem::exists(path))
    failures.push_back("Overlay file: a damaged file was not dropped");
  log.clear();
  if (FoldOverlayFile(data, path, settings, log) || !log.empty()) failures.push_back("Overlay file: none, yet a change");
  // The shader pass runs again for a look changed in the game: the plans' looks differ.
  const std::string root = cases.at("root").get<std::string>(), disc1 = cases.at("disc1").get<std::string>(),
                    manifest = cases.at("manifest").get<std::string>();
  const Monitor monitor{1920, 1080, 1.0, 60, 0, 0, false};
  Options plain, gt7;
  plain.native = gt7.native = 1;
  gt7.look = "gt7";
  try {
    if (ShaderLook(LaunchPlan(data, root, disc1, plain, monitor, manifest)) !=
            ShaderLook(LaunchPlan(data, root, disc1, Options(plain), monitor, manifest)) ||
        ShaderLook(LaunchPlan(data, root, disc1, plain, monitor, manifest)) ==
            ShaderLook(LaunchPlan(data, root, disc1, gt7, monitor, manifest)))
      failures.push_back("Shader look: a changed look does not change the pass's marker");
  } catch (const std::exception& error) {
    failures.push_back(std::string("Shader look: ") + error.what());
  }
  // A file the game wrote (cases "overlay": its path and the options expected after folding it
  // into a default profile of its name).
  if (cases.contains("overlay")) {
    const Json& overlay = cases.at("overlay");
    std::ifstream stream(PathFromUtf8(overlay.at("file").get<std::string>()), std::ios::binary);
    Json file;
    try {
      file = Json::parse(stream);
    } catch (const std::exception& error) {
      failures.push_back(std::string("Overlay file of the game: ") + error.what());
      return;
    }
    Settings game;
    game.profiles = {{file.value("profile", std::string()), OptionsFromJson(file.at("options"))}};
    log.clear();
    const Options start = game.profiles[0].options;
    if (!FoldOverlayChanges(data, file, game, log)) failures.push_back("Overlay file of the game: no change");
    const Json options = OptionsToJson(game.profiles[0].options);
    for (const auto& [name, want] : overlay.at("expect").items())
      if (options.at(name) != want)
        failures.push_back("Overlay file of the game: " + name + " is " + options.at(name).dump() + ", expected " +
                           want.dump());
    for (const auto& line : log)
      if (line.find("dropped") != std::string::npos) failures.push_back("Overlay file of the game: " + line);
    if (ShaderLook(LaunchPlan(data, root, disc1, start, monitor, manifest)) ==
        ShaderLook(LaunchPlan(data, root, disc1, game.profiles[0].options, monitor, manifest)))
      failures.push_back("Overlay file of the game: its look change does not rerun the shader pass");
  }
}

// The car mods (mods.h, launcher_test_case in mods.py): the mod Disc pipeline.py prepared is
// ready here; the one prepared here holds the same files; the mod lists as playable.
void ModTests(const Json& mods, std::vector<std::string>& failures) {
  namespace fs = std::filesystem;
  const fs::path python = PathFromUtf8(mods.at("python").get<std::string>()),
                 cpp = PathFromUtf8(mods.at("cpp").get<std::string>());
  const std::string disc1 = mods.at("disc1").get<std::string>(), id = mods.at("id").get<std::string>();
  if (!ModDiscReady(python, disc1, id)) failures.push_back("Mods: the mod Disc of pipeline.py is not ready here");
  if (ModDiscReady(cpp, disc1, id)) failures.push_back("Mods: a mod Disc is ready before it was prepared");
  std::atomic<std::uint64_t> done{0}, total{0};
  const std::atomic<bool> cancel{false};
  if (const std::string error = PrepareModDisc(cpp, disc1, id, done, total, cancel); !error.empty())
    failures.push_back("Mods: " + error);
  std::map<std::string, std::string> files, want;
  for (const auto& [path, hash] : mods.at("files").items()) want[path] = hash.get<std::string>();
  std::error_code error;
  const fs::path disc = ModDisc(cpp);
  for (auto it = fs::recursive_directory_iterator(disc, error); !error && it != fs::recursive_directory_iterator();
       it.increment(error))
    if (it->is_regular_file() && it->path().filename() != ".fm4-mod-disc.json")
      files[Utf8(fs::path(it->path().lexically_relative(disc).generic_wstring()))] = Sha256File(it->path());
  if (files != want) failures.push_back("Mods: the mod Disc's files differ (" + std::to_string(files.size()) + " files)");
  if (!ModDiscReady(cpp, disc1, id)) failures.push_back("Mods: the prepared mod Disc is not ready");
  const std::vector<ModInfo> listed = ListMods(cpp, disc1);
  if (listed.size() != 1 || listed[0].id != id || !listed[0].error.empty() || listed[0].packaged != 1 ||
      listed[0].skipped != 1)
    failures.push_back("Mods: the list: " + (listed.empty() ? std::string("none") : listed[0].error));
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
  // Builder mode (builder.h): launcher.json's downloads and steps, the progress lines of
  // pipeline.py's progress(), Ninja's progress, and the plain words of the pipeline's errors.
  const Json& builder = cases.at("builder");
  try {
    const std::vector<Download> downloads = ReadDownloads(data.json.at("builder"));
    std::vector<std::string> ids;
    for (const auto& item : downloads) {
      ids.push_back(item.id);
      if (item.files.empty() || item.size == 0 || item.host.empty() || item.license_link.empty())
        failures.push_back("Builder download " + item.id + ": incomplete");
    }
    if (Json(ids) != builder.at("items")) failures.push_back("Builder downloads: " + Json(ids).dump());
    std::vector<std::string> steps;
    for (const auto& step : data.json.at("builder").at("steps")) steps.push_back(step.at("id"));
    if (Json(steps) != builder.at("steps")) failures.push_back("Builder steps: " + Json(steps).dump());
  } catch (const std::exception& error) {
    failures.push_back(std::string("Builder data: ") + error.what());
  }
  const Json& lines = builder.at("progress");
  const Json& expected_lines = builder.at("expected");
  if (lines.size() != expected_lines.size()) failures.push_back("Builder progress: " + lines.dump());
  for (size_t i = 0; i < std::min(lines.size(), expected_lines.size()); ++i) {
    ProgressLine line;
    const Json& want = expected_lines.at(i);
    const std::string event = want.at(0).get<std::string>();
    if (!ParseProgressLine(lines.at(i).get<std::string>(), line) || line.event != event ||
        (event == "stage" ? line.stage : line.step) != want.at(1).get<std::string>() ||
        line.state != want.at(2).get<std::string>() || line.error.empty() == want.at(3).get<bool>())
      failures.push_back("Builder progress line: " + lines.at(i).get<std::string>());
  }
  int compiled = 0, compiles = 0;
  if (!NinjaProgress("[1/9] Building CXX\n[47/1187] Linking x\nwarning: [3/4]\n", compiled, compiles) ||
      compiled != 47 || compiles != 1187 || NinjaProgress("no progress\n", compiled, compiles))
    failures.push_back("Builder Ninja progress: " + std::to_string(compiled) + "/" + std::to_string(compiles));
  for (const auto& error : builder.at("errors")) {
    const PlainError plain = Explain(error.at(0).get<std::string>(), error.at(1).get<std::string>());
    if (plain.text.empty() || plain.action.empty() || plain.screen != error.at(2).get<std::string>())
      failures.push_back("Builder error '" + error.at(0).get<std::string>() + "': '" + plain.text + "' on '" +
                         plain.screen + "'");
  }
  UpdateTests(builder.at("update"), failures);
  OverlayTests(data, cases, std::filesystem::path(result_path).parent_path(), failures);
  if (cases.contains("mods")) ModTests(cases.at("mods"), failures);
  std::ofstream result(result_path, std::ios::binary | std::ios::trunc);
  result << Json{{"plans", plans}, {"failures", failures}}.dump(1);
  return result ? 0 : 1;
}

}  // namespace fm4
