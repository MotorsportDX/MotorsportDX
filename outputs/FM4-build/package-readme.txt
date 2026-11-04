Motorsport DX: the builder package

Motorsport DX runs Forza Motorsport 4 (Xbox 360) on Windows, recompiled from your own discs.
This package holds no part of the game. MotorsportDX.exe builds the game on this computer from
your discs, and then starts it.

What you need
  - Windows 10 or 11, 64-bit, with a graphics card that supports Direct3D 12.
  - Your own Forza Motorsport 4 discs, USA or Japan/Asia release: Disc 1 (Play Disc), extracted
    to a folder or as an ISO; Disc 2 (Content Install Disc) is optional and brings its cars.
  - About 20 GB of free disk space and an internet connection for the build tools.

How to start
  1. Unpack this zip into a short folder that no cloud service syncs, for example
     C:\MotorsportDX (not the Desktop, Documents or OneDrive, and no letters outside A to Z).
  2. Start MotorsportDX.exe. It shows "Build the game".
  3. Follow its four screens: Welcome, Your discs, Downloads and Build. The Downloads screen
     lists every file it fetches, where from, its size and its licence; nothing is fetched until
     you choose Download. Nothing is installed for all users and no system setting changes:
     everything stays in this folder.
  4. The build takes about 10 minutes on a fast computer and longer on a slower one. Keep the
     computer plugged in. When it is done, the launcher starts the game's own launcher.

Afterwards start MotorsportDX.exe in this folder as before: it starts the game's launcher.

Updating to a new version
  1. Unpack the new version's zip into a new folder, and start its MotorsportDX.exe.
  2. On its Welcome screen, choose "Update an earlier install" and then the folder of your
     earlier version (the one with MotorsportDX.exe, source and build in it).
  3. The new version moves into that folder and builds there. Your profiles, saves, settings,
     discs, imported cars and build tools stay. Before it builds, your saves are copied into
     backups in that folder. Afterwards the new folder can be deleted.

The folders it makes here
  source\       the project's source code at this package's version
  toolchain\    the build tools (Python, CMake, Ninja, llvm-mingw)
  downloads\    the tools' downloaded archives
  build\        the built game, your profile and saves (build\user-data), and its logs
  logs\         the builder's own log (builder.log)
  backups\      after an update, a copy of your saves from before it

When something fails, the Build screen says what happened and offers Retry and Open log. Its
Copy details button copies a short report for a bug report (docs\bug-reports.md in the source
folder says which files help).

Motorsport DX is not affiliated with or endorsed by Microsoft, Xbox Game Studios or Turn 10
Studios. Forza Motorsport and Xbox are trademarks of the Microsoft group of companies.
LICENSE.txt is the project's licence; CREDITS.txt thanks the projects it builds on, and
third-party-notices holds the licences of the code in MotorsportDX.exe and of the downloads.
