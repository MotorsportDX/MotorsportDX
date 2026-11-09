# Analysis notes

Research, design and decision notes, grouped by topic. Each note keeps the dates and measurements of its time; the present state is in [current status](../status/current.md).

Status:

- **Active**: the work goes on, or the note is the design that the next steps follow.
- **Done**: the work is finished or the research answered its question; the note is the record and the reference.
- **Superseded**: a later note or decision replaced it; it is kept for its measurements.

Updated on 9 November 2026.

## Native port and render paths

| Note | Status | Contents |
| --- | --- | --- |
| [d3d-hle.md](d3d-hle.md) | Active | Emulating the GPU at the D3D API level instead of PM4 and EDRAM: the title's D3D layer, three designs, the chosen native port's architecture, stages and gate tiers (sections 1 to 15) |
| [d3d-hle-n3.md](d3d-hle-n3.md) | Done | Stage N3 (section 16): resolves and readbacks on demand, host render targets, one pass with 2x MSAA |
| [d3d-hle-n4.md](d3d-hle-n4.md) | Done | Stage N4 (section 17): the render thread's CPU time, deferred constant buffers, the draw back end on the replay side, the ROG Xbox Ally X runs |
| [d3d-hle-n5.md](d3d-hle-n5.md) | Done | Stage N5 (section 18): the screen's own resolution and aspect, the menus and photo mode at the screen's scale |
| [d3d-hle-n6.md](d3d-hle-n6.md) | Active | Stage N6 (section 19): no shader or pipeline compiles in play, the DXIL world, the pipeline library, the Level 1 final gate; the ROG Xbox Ally X runs of Level 1 are pending |
| [shader-contract.md](shader-contract.md) | Active | The native shader contract: look mods declared per pixel shader in a manifest, pass classes, knobs as constants, replacement shaders and vertex shader hooks (increments 1 to 6, off by default) |
| [native-default.md](native-default.md) | Done | The readiness report for the native path as the default on every computer; the flip landed on 26 October 2026 |
| [cp-draw-offload.md](cp-draw-offload.md) | Superseded | Moving draw work off RTV's command processor thread; replaced by the native port's stage N4 |
| [ghidra-render-path.md](ghidra-render-path.md) | Done | Static analysis of `default.xex`'s render path and the draws a game patch could cut |
| [function-catalog.md](function-catalog.md) | Done | How the catalog of the title's functions is built and used (the catalog stays outside the repository) |

## Post passes, anti-aliasing and upscaling

| Note | Status | Contents |
| --- | --- | --- |
| [post-pass-framework.md](post-pass-framework.md) | Done | The runtime's post pass framework: screen passes from HLSL files and a manifest, injection points, history, object motion |
| [gtao-design.md](gtao-design.md) | Done | The GTAO ambient occlusion pass, from frame graph observations |
| [motion-vectors.md](motion-vectors.md) | Done | Per-pixel motion vectors: the game's motion blur reprojection, its stencil object IDs, the host-built per-car pass |
| [taa.md](taa.md) | Active | TAA in every look preset: injection points, jitter, the scene camera, the resolve and knobs; step 8 (a reactive mask) waits for a case that ghosts |
| [fsr-plan.md](fsr-plan.md) | Done | AMD FSR 3.1 as native anti-aliasing and as an upscaler (stages 0 to 3), TAAU, and the windshield at the input scale |
| [windshield.md](windshield.md) | Done | The cockpit windshield and side window glass that the title does not draw |
| [hdr-output.md](hdr-output.md) | Active | The HDR10 output design: where the frame loses its range and the recommended design; deferred until an HDR display is available |

## Look and shader mods

| Note | Status | Contents |
| --- | --- | --- |
| [b4-shader-research.md](b4-shader-research.md) | Done | The editable shader path: HLSL through FXC, with overrides per hash |
| [shader-mod-iteration.md](shader-mod-iteration.md) | Done | The DXBC cache, hot reload and live look knobs |
| [look-mod-coverage.md](look-mod-coverage.md) | Done | Which final combine, motion blur, car paint, glass, road and nature hashes each track and mode uses, and which have no look mod |
| [look-presets-gt7-fm2023.md](look-presets-gt7-fm2023.md) | Done | The `gt7` and `fm2023` presets: the references, the GT7 tone mapping port, the tuning rounds; both locked on 29 October 2026 |
| [cockpit-brightness.md](cockpit-brightness.md) | Done | Why the cockpit interior is dark on sunny tracks, and the knobs that became `cockpit_fill` |
| [quality-presets.md](quality-presets.md) | Done | The five race quality presets (`-Quality low` to `ultra`) and the measurements behind them |
| [texture-replacement.md](texture-replacement.md) | Done | Texture dumps and replacements keyed by a content hash, and the offline extractor; no replacement art yet |
| [visual-style.md](visual-style.md) | Done | Where FM4's visual style comes from: game code, game assets or the runtime |
| [material-atlas.md](material-atlas.md) | Done | Names and registers of the game's material constant tables |

## Input and the launcher

| Note | Status | Contents |
| --- | --- | --- |
| [racing-wheels.md](racing-wheels.md) | Active | Native racing wheel support, stages 0 to 6 done; off by default until the owner's hardware test |
| [modern-gamepads.md](modern-gamepads.md) | Active | Modern pads, SDL's drivers, the title's deadzones, rumble and latency; the Xbox Series controller test is pending |
| [keyboard-controls.md](keyboard-controls.md) | Active | The keyboard's steering and pedal model and racing layout; the test on the laptop is pending |
| [ingame-settings.md](ingame-settings.md) | Active | The in-game PC settings overlay: which settings apply live, at the next load or at a restart, one source of truth with the launcher, stages S0 to S6 and the owner's open questions; S0 to S4 implemented on 4 November 2026, S5 dropped (restart only), S6 open |
| [launcher-ux.md](launcher-ux.md) | Done | The GUI launcher's racing theme, increments T1 to T6; not yet seen on the ROG Xbox Ally X |

## Game content, modes and testing

| Note | Status | Contents |
| --- | --- | --- |
| [career-season-soak.md](career-season-soak.md) | Active | The first 2-hour World Tour soak: memory, handles and GPU memory over time, a performance survey and the career's stubs; the reward screen hitches (the title's own work); the soak that revisits tracks and cars (a host cache, and a slow handle leak); the title render thread's frame loop, which spins between frames and recomputes the gamma ramp on every turn, and the wait that replaces the spin (off by default) |
| [photo-autovista-replay.md](photo-autovista-replay.md) | Active | How photo mode, Autovista and replays render, what the gates cover, and the plan (M5); research of 18 October 2026 on RTV |
| [direct-race-launch.md](direct-race-launch.md) | Superseded | A Quick Race launch without the menus (not built); the e2e fast route through the menus replaced it |
| [releases.md](releases.md) | Done | How the Japan/Asia and USA releases differ, and why one build serves both |
| [title-updates.md](title-updates.md) | Done | What the title updates change, and what supporting one would take |
| [dlc-structure.md](dlc-structure.md) | Done | How content packages are laid out, mounted and licensed, and what a mod package would need |
| [career-modes.md](career-modes.md) | Active | FM3 and FM2 career modes in FM4: the launcher's career choice and a save slot per career, the profile save's layout, FM4's Anark frontend and its C++ screen contracts, FM3's and FM2's career screens and the ways to show them, layers and estimates (research, 9 November 2026) |
| [rain-feasibility.md](rain-feasibility.md) | Done | Whether the physics still has a wet grip modifier, and how a wet race could lower grip |

## Release

| Note | Status | Contents |
| --- | --- | --- |
| [preview.md](preview.md) | Active | What a preview can be (source only), the internal names, and the checklist for a closed and a public preview |
| [builder-installer.md](builder-installer.md) | Active | A free download without the game that builds Motorsport DX on the player's computer from their discs: what it ships (two options, their licences), a toolchain the player does not install by hand, the flow, the launcher's builder mode, increments, risks and the owner's decisions; B1 to B3, B5 to B7 and B9 done (B4 replaced), B8 the owner's run |
| [mingw-toolchain.md](mingw-toolchain.md) | Active | Route (c) of the builder: building with llvm-mingw instead of Microsoft's libraries and the Windows SDK; the MSVC-specific code by class, mingw-w64's headers, third-party libraries, tooling, one toolchain or two, the pin, licences, estimate, prototype plan and risks; the prototype's results (31 October 2026: the player build and its tests pass with llvm-mingw, the generated code is the same, the WARP ABI probe matches, the MSVC build's instructions are unchanged; on the GPU the setjmp crash and its fix, the smoke A/B passing, and the frozen-frame parity that still fails on draws); the switch (M1, 31 October 2026): llvm-mingw is the default build, with `-Abi msvc` kept for the A/B; its verification (M2, 1 November 2026): the full gate, career, parity and perf tiers against the MSVC build of the same commit, 2x captures byte-identical where the moment does not depend on the race, a short soak, the export for the ROG Xbox Ally X |
