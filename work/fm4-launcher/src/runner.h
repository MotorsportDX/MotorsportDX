// Windows side of the launcher: the primary monitor, starting the game with its log and
// environment, and opening folders and files.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "plan.h"

namespace fm4 {

// Width and height in pixels and the DPI scale of the primary monitor, as primary_monitor in
// pipeline.py measures them.
Monitor PrimaryMonitor();

// subprocess.list2cmdline: the command line of an argument list.
std::string CommandLine(const std::vector<std::string>& args);

// Starts args[0] in directory with the launcher's environment minus its FM4_ variables plus
// env, stdout and stderr into log. Returns the process handle; throws on failure.
void* StartProcess(const std::vector<std::string>& args, const Pairs& env,
                   const std::filesystem::path& directory, const std::filesystem::path& log);
// Waits for a process from StartProcess and returns its exit code; closes the handle.
unsigned long WaitProcess(void* process);

void OpenPath(const std::filesystem::path& path);

// Whether a process of this executable name (fm4_rex.exe) is running.
bool ProcessRunning(const wchar_t* name);

}  // namespace fm4
