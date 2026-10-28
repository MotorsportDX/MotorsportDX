# Native racing wheel support

Status: research and design of 27 October 2026; stage 0 (the device and safety shell) and stage 1 (the control path) done on 29 October 2026 ([stage 0](#stage-0-done-29-october-2026), [stage 1](#stage-1-findings-29-october-2026)). The project runs the base Disc 1 executable without a title update, so every address below is from the recompiled base `default.xex` (USA; Japan/Asia share the code).

The goal is native PC wheel support, not gamepad emulation:

- the wheel is read through a PC API at full resolution, and its steering and pedals reach the title's driving controls as floats, without the 8-bit triggers of `X_INPUT_STATE` and without the pad's steering assists;
- steering is 1:1 with the car, with a soft lock per car;
- force feedback comes from the title's own physics and goes to the wheel as a constant force plus firmware-side effects, with gain, minimum force and smoothing settings;
- force feedback is safe on a direct-drive base.

The title's Xbox 360 wheel path is documented here as the reference, not as the target.

## Target hardware

The owner's set, confirmed 27 October 2026, is the design and test target:

| Device | Use |
|---|---|
| MOZA Racing R9 wheelbase (direct drive) | steering axis, force feedback |
| MOZA ESX steering wheel | paddles for sequential shifts; buttons for clutch (the title's manual-with-clutch mode), handbrake and the other actions |
| MOZA SR-P pedals, two pedals | throttle and brake only, no clutch pedal |

There is no H-shifter. An analog clutch and an H-shifter stay out of scope and are noted as later options only. Force feedback can only be judged by the owner at the wheel, so every stage is verified first without hardware and then with a short test script for the owner.

## Summary

- **The title has a complete wheel path.** It detects an Xbox 360 wheel per poll, and a 900-degree wheel through a capabilities field. It has its own wheel layouts, options and rumble table. Its physics computes a steering torque for force feedback. It drives five force feedback effects through the Xbox 360 kernel force feedback API (`XInputdFF*`), with parameters from `Media/ControllerFFB.ini`.
- **The runtime stubs all of it.** `XInputdFFGetDeviceInfo` through `XInputdFFSetRumble` and `XamInputRawState` are `REX_EXPORT_STUB`s ([xboxkrnl_hid.cpp](../../work/runtime/src/kernel/xboxkrnl/xboxkrnl_hid.cpp), [xam_input.cpp](../../work/runtime/src/kernel/xam/xam_input.cpp)). The SDL driver reports every device as a gamepad (subtype 1), so the title never enters its wheel path.
- **The base build carries the wheel bugs that title update 2 fixed:** the zero credit bug, the anti-cheat rule that smooth wheel input triggered, and countersteering that treated 900 degrees as 270. Entering the title's wheel path as it is would bring those bugs with it. Smooth, high-resolution steering could also trip the anti-cheat rule through the pad path. Stage 1 found the rule: it counts frames with a fast steering change and withholds a race's rewards when there are fewer than 10 per completed lap; it does not depend on the device class, and an integration fix keeps it from firing for the wheel ([stage 1](#stage-1-findings-29-october-2026)).
- **The title's force feedback source is usable.** The steering torque is a float in `CCarDynamics`. The title reads it once per frame, together with the speed and the rear tyre slip. A hook at that point gives a native force feedback thread its input without the title's effect objects. The car physics runs at 359 Hz (`CarPhysicsUpdateHz`), so a later stage can sample the tyres at physics rate.
- **Recommendation:**
  - Read the wheel with SDL3's joystick and haptic API, which the runtime already ships (SDL 3.5.0). On Windows its haptic backend is DirectInput.
  - Keep the force feedback in a host thread with a safety governor.
  - Inject the steering, pedal and button values at the title's control level, once stage 1 has traced where that is.
  - Use the MOZA SDK only for the soft lock (its "limit angle"), and only once its licence terms are known.
- **Verdict: feasible.** Stage 1 found the injection point of the control floats: the pad object's sources after the input conversion, or the 28-byte control record that `sub_826D5E78` hands to `CCarDynamics` each frame. Everything around it can be built and verified without the wheel.

## The title's input and wheel path

### Reading the device

| Function | What it does |
|---|---|
| `sub_82C778C8` `Input::ReadController` | the title's `XInputGetState`: `XamInputGetState(user, 1, state)` |
| `sub_822B5E60` | the input poll, once per frame, for each of four users (5,632 bytes per user from the input owner + 652): capabilities check `sub_822B5E10`, `XInputGetState`, the conversion `sub_822B4130`, the edge tracker `sub_82379300`, the rumble and force feedback update `sub_823A42A0`, then the motors ([modern gamepads](modern-gamepads.md#the-titles-input-code)) |
| `sub_822B4130` `Input::ConvertButtons` | `X_INPUT_STATE` to the title's pad object, with ten deadzone floats from the profile: the left stick (steering) through the radial deadzone `sub_822B3F18`, the triggers through `sub_82394A60` (8 bits in, 0 to 255 out) |
| `sub_828C4078` | the device class, per poll (called from the poll and from `sub_828C5B60`, `sub_828C8358`); see below |
| `sub_82CA5218` | a raw-state reader: `XamInputRawState` for raw devices, `XamInputGetState` otherwise, with device setup through `XamInputGetCapabilitiesEx` in `sub_82CA5060` |

`sub_828C4078` reads `XamInputGetCapabilities(user, 1)` and keeps a class byte at +6 of the per-user block:

- **0:** a pad.
- **1:** a wheel, `SubType == 2` (`XINPUT_DEVSUBTYPE_WHEEL`).
- **2:** a Microsoft device with vendor ID `0x045E` and product ID `0x02B2`, read from the raw state. The title has a third set of layouts and options named "Bristol" (`BristolLayouts`, `BristolOptions/…`, without a force feedback scale). Class 2 is probably that device. The name and the device are not confirmed.

For a wheel it records the capabilities fields in the same block:
- the `wButtons` bits `0x40` and `0x80`;
- whether `sThumbLY` and `sThumbRY` are `0xFFFF`;
- whether both motor fields are `0xFF00`;
- `Flags` bit 0;
- `sThumbRX >> 8` when `sThumbRX` is neither 0 nor 1;
- **`sThumbLX == 900`, the 900-degree flag (+0x10).**

So an Xbox 360 wheel reports its rotation range through the capabilities. `sub_828C3448` then queries the force feedback device information.

### Layouts and options

The database `Media/db/gamedb.slt` has one layout table per class: `ControllerLayouts` (13), `WheelLayouts` (5) and `BristolLayouts` (5). Wheel layout 0 maps:

| Wheel control | Action |
|---|---|
| right and left triggers | gas and brake |
| right and left shoulders (the paddles) | shift up and down |
| A, B | clutch, handbrake |
| right stick up/down, left stick up/down | clutch, handbrake (analog) |

An Xbox 360 wheel sends its pedals as the 8-bit triggers and its clutch and handbrake axes on the stick Y axes. The profile keeps separate `WheelOptions/…` and `WheelAdvancedOptions/…` with the same deadzones as the pad, plus `WheelAdvancedOptions/ForceFeedbackScale` (`Media/ProfileSchema/ForzaProfile.sch`).

### Steering mapping and assists

`sub_82894B20` registers the physics tunables. The steering ones are:

- **The wheel mapping, in two sets:** `WheelMapping` (the 270-degree wheel) and `WheelMapping900Degree`, each with `WheelMapSteerSpeed0/1`, `WheelMapSteerScale0/1`, `WheelMapNormSteerInput1`, `WheelMapNormSteerOutput1` and `WheelMapCounterSteerScale`. The countersteer scale is the title update 2 bug: the base code uses the 270-degree countersteer for a 900-degree wheel.
- **The pad's steering filters and assists:** `GraphicsSteeringFilter\…` (the drawn steering wheel), the speed-sensitive steering in `Data_Car` (`SteerSpeedSensitive…`, `SteerMaxAngVelTurning` 105 deg/s and similar), `AutoSteerController\…` and `AutoSteerKinect\…`, and the `FrictionTorqueMod\WithAssists0/1`, `NoAssists0/1` and `SimulationSteering0/1` sets in `Media/physics/PhysicsSettings.ini`. The profile's steering setting (`IDS_Steering_Assisted`, `Normal`, `Simulation`) picks between them.
- **The command line options of `sub_82400E18`** (`CForzaCommandLineParameters`): `force900degreefilters`, `forcecarphysicsupdatehz` and `forcephysicsminupdatehz`.

Not traced yet: the code from the pad object (+36 onwards, the floats the conversion writes) to the car's control inputs, and which of these filters each device class goes through. This is stage 1's task.

### Steering lock per car

The database has no lock-to-lock angle for the steering wheel:

| Source | Value |
|---|---|
| `Data_Car.SteerMaxAngle` | 42 for the cars checked |
| `Data_Car.SteerMaxAngleFiltered` | the road-wheel angle the title uses, 12 to 35 degrees, average 27.2, over 503 cars |
| the shared `GlobalCarAttribs.xml` (driver) | `SteeringWheelAngleScale` 0.75 |
| per-car `carattribs.xml` | `SteeringWheelAngleOverride`, 0 for the cars read in the FM3 car converter work |
| driver animations | `SteerLock2LockSmall` and `SteerLock2LockLarge` in two variants |

The angle the cockpit's steering wheel shows reaches the presentation through `CCarPresentationDeferred::SetSteeringWheelAngle` (`sub_822E6B30`, executed by `sub_822C5728`). A hook there can measure, per car, the drawn wheel's angle at full lock. A per-car soft lock can then match what the driver sees in the cockpit.

## The title's force feedback

### Data

`Media/ControllerFFB.ini` (482 bytes):

| Effect | Settings |
|---|---|
| Inertia | `InertiaScale`, `InertiaMaxForce`, `InertiaDeadband`: 0, 0, 0 |
| Friction | `FrictionScale`, `FrictionMaxForce`, `FrictionDeadband`: 0, 0, 0 |
| Damper | `DamperScale`, `DamperMaxForce`, `DamperDeadband`: 0.1, 0.1, 0 |
| Spring | `SpringScale`, `SpringMaxForce`, `SpringDeadband`: 0.5, 0.5, 0 |
| Dynamic force | `DynFFBScale` 1, `DynFFBMaxForce` 1 |

The spring's scale also depends on two curves:
- speed: `SpringScaleSpeed0/1` 1 and 10, scales 0 and 1;
- rear slip: `SpringScaleRearSlip0/1` 0.25 and 0.5, scales 1 and 0.

`Media/ControllerRumble.xml` has a `<Wheel>` table beside the pad's, at about three times the pad's strength ([modern gamepads](modern-gamepads.md#the-titles-rumble)).

The steering torque model is in the physics tunables of `sub_82894B20`, among them:
- `SteerTorque`, `SteerTorqueClamp`, `SteerTorqueShouldSmooth`, `SteerTorqueSmoothSpringK`;
- `SteerTorquePeakNoise…` (by normalized slip angle);
- `SteerTorqueWheelPos…` and `SteerTorqueWheelPosScaleRearSlip…`;
- `SteerTorqueSameDirDamping…`;
- `SteerTorqueNormSlipAnglePredictTime`;
- the torque steer set `TorqueSteer…`.

The tyre model has aligning torque curves (`AffectCurveLoadAffectAlignTorqueID`, `…NormSlipRatio…`, `…NormSlipAngle…` in `List_TireFrictionMultiCurve`, read by `sub_828A2350`).

### Flow

1. **The source, once per frame in a race.** `sub_826D5E78` reads, per human car:
   - the steering torque from `CCarDynamics` vtable slot 254 (`+0x3F8`, `CCarDynamics::get_f32_48` `sub_828154B0`, a stored float);
   - the speed;
   - a load-weighted rear tyre slip from the per-tyre slots `+0x2B0`, `+0xCC` and `+0x268`.

   It zeroes the torque when the car's flag at +0x38F8 is clear and `sub_822B7130` returns false. It passes the three floats to `sub_8239E600`.
2. **Scale and clamp.** `sub_8239E600` calls `sub_828C3330` on the parameter block at +0x1090. The dynamic force is `clamp(DynFFBScale × torque, ±min(DynFFBMaxForce, 1))`, stored at +0x538. The spring scale is the speed curve × the rear slip curve, stored at +0x540.
3. **Setup, a state machine in `sub_823A3F58`.** The device must be a wheel (`SubType == 2`) with `Flags` bit 0, and `XInputdFFGetDeviceInfo` must report at least 5 effects. Then:
   - `XInputdFFDeviceControl` resets the device;
   - `sub_828C34E0` builds five 0x74-byte effects (types 1 to 5) from the ini values × `ForceFeedbackScale` (+0x53C);
   - `XInputdFFSetEffect` uploads each;
   - `XInputdFFEffectOperation` starts them;
   - the ready flag is set.
4. **Update, every poll.** `sub_823A42A0` writes the dynamic force into its effect and sends it with `XInputdFFUpdateEffect` (flags `0x400`), double-buffered over two overlapped requests. It resends the spring effect (flags `0x100`) when its scale changes.

All calls go through pointers that static initializers fill with `XexGetProcedureAddress` on `xboxkrnl.exe` (wrappers `sub_82C9F240` to `sub_82C9F4A0`):

| Kernel ordinal | Function | Pointer |
|---|---|---|
| 642 | `XInputdFFGetDeviceInfo` | `0x82E74944` |
| 643 | `XInputdFFSetEffect` | `0x82E74938` |
| 644 | `XInputdFFUpdateEffect` | `0x82E7494C` |
| 645 | `XInputdFFEffectOperation` | `0x82E7493C` |
| 646 | `XInputdFFDeviceControl` | `0x82E74930` |
| 647 | `XInputdFFSetDeviceGain` | `0x82E7492C` |
| 648 | `XInputdFFCancelIo` | `0x82E74934` |
| 649 | `XInputdFFSetRumble` | `0x82E74928` |

`XamInputGetUserVibrationLevel` (xam ordinal 442) is at `0x82E74940`. The pad's overlapped rumble (`sub_828C50E8`) is `XInputdFFSetRumble`.

`UploadForcesToWheel` is a script command (callback `sub_828C5AC8`) that `sub_828C85C8` registers. `sub_828C85C8` is the controller manager's initialization, which also loads `ControllerFFB.ini` (`sub_828C2CD8`) and `ControllerRumble.xml`. It is a debug command that re-sends the parameters, not the per-frame path.

### What this means for native force feedback

- The title's torque is already the full steering torque model, scaled to −1..1 and clamped by `DynFFBMaxForce`. It is updated once per input poll (each frame), not at the physics rate.
- The title's own effects are only one constant-like effect and four conditions. A native path keeps the idea (dynamic force plus spring and damper) but runs the constant force on the host at the device's rate, interpolated, and leaves the conditions to the firmware.
- Implementing the eight `XInputdFF*` exports in the runtime and reporting a wheel would reproduce the Xbox 360 behaviour. It is a useful reference for comparing the forces, but it is limited to the frame rate and to the title's effect set, and it needs the wheel class with its title update 2 bugs. It is not the target.

## The runtime's hook mechanism

- **`REX_HOOK_RAW(sub_X)`** ([hook.h](../../work/runtime/include/rex/hook.h)) replaces a recompiled function. The original stays callable as `__imp__sub_X`. The hook gets the PowerPC context (`ctx.r3`, `ctx.f1`, …) and the guest memory base, and reads guest memory big-endian (as `UIRead` does in [fm4-ui-diagnostics.cpp](../../work/fm4-rex/src/fm4-ui-diagnostics.cpp)).
- **A guest address can have only one hook definition.** Shared addresses are collected in [fm4-guest-hooks.cpp](../../work/fm4-rex/src/fm4-guest-hooks.cpp), which calls each feature in a fixed order.
- **Kernel exports** are `REX_EXPORT`s in the runtime ([xam_input.cpp](../../work/runtime/src/kernel/xam/xam_input.cpp)). The `XInputdFF*` exports would be implemented there.
- **Existing pattern:** the hook of `sub_822B4130` publishes the title's live input ranges to the runtime ([title_input_ranges.h](../../work/runtime/include/rex/input/title_input_ranges.h), [keyboard controls](keyboard-controls.md)). The wheel uses the same pattern: guest-side hooks only read or write a few fields and publish through atomics, and a host thread does the device work.
- **Cost and threading:**
  - A hook runs natively on the guest thread that called the function, and costs a function call.
  - It must not block: no device I/O and no locks shared with the input driver's SDL lock, which already caused a lock-order deadlock (see [modern gamepads](modern-gamepads.md#the-titles-input-code)).
  - Guest threads are host threads. The force feedback thread reads the published values without touching guest memory.

## Host device API

| API | For the MOZA set | Notes |
|---|---|---|
| SDL3 joystick + haptic | **recommended** | Already in the runtime (3.5.0). The joystick API reads any HID wheel with every axis and button at device resolution, without a gamepad mapping (the runtime now opens only `SDL_OpenGamepad` devices, [sdl_input_driver.cpp](../../work/runtime/src/input/sdl/sdl_input_driver.cpp)). The haptic API's Windows backend is DirectInput: constant force, spring, damper, friction, inertia, sine, gain and autocentre. Each update is one `SDL_UpdateHapticEffect`, which becomes one DirectInput parameter update. |
| DirectInput 8 directly | fallback | The same device path without SDL's layer. Use it only if SDL's update cost or effect handling falls short (stage 0 measures this). Needs a cooperative level on the game window. |
| GameInput | later candidate | Force feedback on PC for racing wheels through `IGameInputDevice::CreateForceFeedbackEffect` ([Microsoft: force feedback](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/advanced/input-forcefeedback)). The runtime has its own GameInput driver for pads since 25 October 2026. Whether GameInput lists the R9 as a racing wheel is unknown; stage 0 checks it. |
| MOZA SDK | soft lock only | Native C++ (MSVC 2022, 32 and 64 bit) and C#, through Pit House's SDK service. It offers device parameter get and set, including a "limit angle" and torque limits, six force feedback effects, HID capture of the steering angle, velocity and acceleration, and pedal calibration ([MOZA SDK](https://mozaracing.com/pages/sdk)). The page names no LED control and no licence, registration or redistribution terms. Until the terms are known, nothing from it goes into the repository or an export: the runtime would load it at run time, if present, behind an option, with a [third-party notice](../sources-and-licensing.md). |

**Rates and latency.**
- The title's torque changes once per frame.
- The host force feedback thread runs at 500 to 1,000 Hz. It interpolates between frames, applies the governor and sends a constant force.
- The firmware runs its own loop and the condition effects (spring, damper) by itself.
- The added latency is at most one frame plus one host tick. Stage 4's physics-rate source removes most of the frame.

**Pit House.** The base applies its own settings on top of what the game sends: maximum angle, force feedback strength, road sensitivity, damper, friction, inertia and similar. The test script fixes one Pit House profile, so results are comparable and the game's soft lock is not fighting the base's angle limit.

## Force feedback safety

On a direct-drive base, safety is a requirement, not a setting. A governor sits between every force source and the device:

- **Zero force** on: pause, menus, loading, replays, photo mode, a non-human car, window focus loss, exit, a guest crash and a runtime crash. The title already zeroes its torque by the condition in `sub_826D5E78`. The host does not rely on that: it also checks focus and the runtime's state, and runs a watchdog that ramps to zero when no new value arrives within 50 ms.
- **Ramp-in** over 1 to 2 s on every start and after every zero state.
- **Torque cap:** a hard upper limit in the runtime, independent of the gain, default well below the R9's peak, plus a slew-rate limit.
- **Bad values:** NaN, infinity and out-of-range values become zero. A step larger than the slew limit is clamped, so no single frame produces a full-force spike.
- **Exit and crash:** the effects stop in the normal shutdown, in an `atexit` handler and in the crash handler ([fm4-crash-diagnostics.cpp](../../work/fm4-rex/src/fm4-crash-diagnostics.cpp)). Stage 0 also checks what the base does when the process is killed (DirectInput releases a process's effects; the R9's behaviour is confirmed in the owner's test).

## Staged plan

Each stage lands after one short check (the owner's process since 29 October 2026): the build, the unit tests and one career at 3/2 with the default pad, to show that nothing regressed ([e2e harness](../e2e/README.md)).

### Stage 0: device and safety shell (no title change)

Delivers:
- an SDL joystick and haptic wheel driver beside the pad driver, which opens joysticks that are not gamepads;
- logs of the device's axes, buttons, hats and haptic capabilities;
- a force feedback sink with the governor;
- a recording backend that writes every force sent;
- a launcher test mode with no game, which applies a slow ramped centre spring and a low-amplitude sine.

Verified without hardware:
- unit tests of the governor on synthetic streams: NaN, spikes, focus loss, watchdog, ramp;
- SDL's virtual joystick for the input path.

Owner's test script:
1. Open Pit House with the fixed profile.
2. Run the test mode.
3. Confirm the axis ranges (steering lock to lock, both pedals) and the button and paddle numbers.
4. Feel the spring and sine at 10% and 25%.
5. Alt-tab: the force must stop.
6. Kill the process: the force must stop.

#### Stage 0 done, 29 October 2026

- **The host** ([wheel_host.h](../../work/runtime/include/rex/input/wheel/wheel_host.h), [wheel_host.cpp](../../work/runtime/src/input/wheel/wheel_host.cpp)) is free of runtime dependencies, so the runtime and the GUI launcher share it. It takes SDL's first joystick of the wheel type (SDL lists the MOZA R3 to R21 bases, the R9 as `346e:0002`), else the first joystick that is not a gamepad, and checks every second while none is open. It logs one `Wheel report:` line: name, type, vendor and product, axes, buttons, hats, and the haptic device's axes, effect counts and features. Its own thread runs at 1,000 Hz with high priority and does every haptic call:
  - a constant force on the steering axis (`SDL_HAPTIC_STEERING_AXIS`), updated only when its 16-bit level changes;
  - a spring for the test, which the firmware runs, with its coefficient and saturation set to the governed level, so the cap holds at any wheel position;
  - each device update timed, and the tick period measured.
- **The governor** ([ffb_governor.h](../../work/runtime/include/rex/input/wheel/ffb_governor.h), header only) implements [Force feedback safety](#force-feedback-safety): zero at once on any block reason (focus, pause, exit, no device, the user's stop), a linear ramp-in over 1.5 s after every start and zero state, a watchdog (50 ms without a new source value: the force falls at the slew limit and ramps in again later), a slew limit of 4 full scales per second with each tick counted at most 10 ms (a stalled thread cannot jump), NaN, infinities and values outside -1..1 as zero, the gain, and a cap independent of the gain (default 0.25). Condition effects take the same block, ramp and cap.
- **Exit and crash.** The first host installs an unhandled exception filter (chained to the previous one), a SIGABRT handler and an `atexit` handler that zero every running host: the force thread answers within a tick, else the caller stops the effects itself. The runtime ends with `std::_Exit` after `ReXApp::OnClosing` (no `atexit`, no driver teardown), so the SDL driver's `StopVibration` stops the host synchronously: zero, stop, destroy the effects, close the wheel.
- **The recording backend.** `Sink::kRecord` opens no haptic device and records every force; with a path, the device sink records too. One CSV line per force sent or state change: time, target, output, spring, ramp, block reasons, governor flags, update cost. The file is flushed every 250 ms and on an emergency stop, so a killed process keeps it, and other programs can read it while it is written.
- **SDL with haptics.** The runtime's SDL was built with `SDL_HAPTIC` off (its `thirdparty/CMakeLists.txt`), so the first in-game check found no haptic subsystem. It is on now; only the wheel host starts the subsystem, and the joystick and haptic subsystems start separately, so the wheel is still read without haptics.
- **The runtime.** The SDL input driver starts the host with `wheel=true` (default off), with `wheel_name`, `wheel_ffb_cap`, `wheel_ffb_gain`, `wheel_ffb_invert`, `wheel_ffb_sink` and `wheel_ffb_record` ([running.md](../running.md#racing-wheel)), and passes the window's focus. No source is connected yet, so the watchdog keeps the force at zero. Nothing reaches the title.
- **The launcher's test mode** (Controls page, `--wheel-test`): the live device view and the test forces (direction pulse, spring and sine at 10% and 25%), focus loss as a block, a log and a recording in the export's `logs` folder. The owner's checklist is in [running.md](../running.md#racing-wheel-test-checklist).
- **Verified without hardware** (`pipeline.py build` runs both):
  - `fm4_wheel_ffb_tests`: the governor on synthetic streams (the ramp, the cap at gain 4, six kinds of bad values, a full-scale spike, five block reasons and two at once, the watchdog and the ramp after it, a one-second stall, the condition level, the configuration's sanitizing);
  - `fm4_wheel_host_tests`: the host with SDL's real joystick drivers off and no haptic subsystem, against an SDL virtual wheel and the recording backend: the wheel found and typed, its three axes read exactly (12,345, -32,768 and 1), button 17 and the hat; a sine of 0.4 under a cap of 0.15 recorded within the cap, zero while unfocused and at the end; the emergency stop ending a pulse at zero; the disconnect noticed; the recording readable while the host runs; SDL's haptic subsystem starting. The tick period was 1.00 ms on average.
- **Not verified:** a real wheel, the cost of a DirectInput update, and what the R9 does when the process is killed. These are the owner's checklist.

### Stage 1: trace the title's control path

Delivers an update of this note with:
- the chain from the pad object (+36 onwards) to the car's control inputs, and the struct and fields for steering, throttle, brake, clutch, handbrake and gear;
- the point in the frame where a write takes effect;
- which assists and filters each device class and steering setting goes through;
- the writer of `CCarDynamics` +48 and the physics substep function (359 Hz);
- the drawn steering wheel's full-lock angle per car, from a hook on `sub_822E6B30`;
- whether the anti-cheat rule and the credit bug depend on the device class or on the input's smoothness.

Method:
- static reading of the poll's consumers;
- one traced run with `FM4_TEST_INPUT_FILE` steering sweeps;
- a career race with smooth synthetic steering, checking the credits.

#### Stage 1 findings, 29 October 2026

Static analysis of the base `default.xex` (the catalog, Ghidra's `Export.java` and the recompiler's generated code); no game run was needed for the findings below. Addresses are guest addresses.

**The control path.** `sub_826D5E78`, once per frame from the race update `sub_826DCFB8` (its first call, before the lap logic `sub_823446C0`), loops over the human cars. For each it reads the player's pad object and builds a 28-byte control record on the stack:

| Offset | Field | Source |
|---|---|---|
| +0x00 | throttle, 0..1 | layout action 0, `sub_8238A468` (the largest positive value of the action's bindings) |
| +0x04 | brake, 0..1 | action 1, `sub_8238A468` |
| +0x08 | steering, -1..1 | action 2, `sub_822B35C0` (the binding of largest magnitude, signed) |
| +0x0C | clutch, 0..1 | action 6, `sub_8238A468` |
| +0x10 | handbrake, 0..1 | action 3, `sub_8238A468` |
| +0x14, +0x15 | shift up, shift down | buttons 4 and 5 (`sub_822D9630`), zero when the race forbids them |
| +0x16 | button 8 | `sub_822D9630`, when the car's slot 0x9C returns 0 |
| +0x17 | wheel mode | 0 for a pad; for device class 1 or 2: 2 if the 900-degree flag is set (`sub_828C4050`: class 1 and +0x10) or `force900degreefilters` (`CForzaCommandLineParameters` +0xCB1), else 1 |
| +0x18 | gear | 10 (sequential) by default; with a gear selector (`sub_828C4028`: class 1 and +0x0F) the H-shifter's gear from `sub_828C5FD0`: -1 to 0, 0 to 9, 1..8 as is |

The action readers take each binding's value from the pad object at +0x40 (`sub_828C5858` unipolar, `sub_828C5978` bipolar): these are the floats `Input::ConvertButtons` (`sub_822B4130`) writes. The record then goes to the car's `CCarDynamics` slot 14 (`sub_823651D8`: `memcpy(this + 0xE90, record, 28)`); slot 35 (`sub_8235B8E0`) returns `this + 0xE90`. The steering is the only signed action, which identifies it; the names of the unsigned ones follow the wheel layout's order and their readers (`CSkidMarks` reads the brake and the handbrake).

So the floats are floats from the pad object onwards: the 8-bit limit of the triggers and the 16-bit sticks are only in `X_INPUT_STATE` and its conversion. Writing the wheel's values either into the pad object's sources after `sub_822B4130` (which the runtime already hooks for the live input ranges) or into the control record keeps the title's own layouts, menus and pause logic.

**Where a write takes effect.** The record is set at the start of the race update, before the lap logic, and the car's physics reads `this + 0xE90` in its own steps afterwards. A write in the same frame, before `sub_826D5E78` returns, takes effect in that frame.

**Device class and filters.** The device class reaches the car only as the wheel-mode byte +0x17. No reader of that byte was found: not at `CCarDynamics + 0xEA7`, not through a word or doubleword load of its neighbours, not through slot 35 or a stack copy of the record. The `WheelMapping` tunables sit in the physics tunables at +1000 (the 270-degree set) and +1028 (the 900-degree set), seven floats each, but their readers address them indirectly too. Which steering filters each mode goes through is therefore left to a traced run in stage 2: the same steering sweep injected with modes 0, 1 and 2, comparing the road-wheel angle.

**The anti-cheat rule and the credits.** The title update 2 bugs are one rule in `GameSettings` (`Media/GameTunableSettings`, section `CheatDetection`), whose settings object is the global at `0x82DC372C` (constructor `sub_823E8870`, registration `sub_823EBB40`):

| Field | Offset | Default in code | `GameTunableSettings.ini` |
|---|---|---|---|
| `WorldCollisionsPerLapThreshold` | +0x10 | 900 | 900 |
| `MinInputsPerLap` | +0x14 | 10 | not set |
| `FrameAnalogInputThreshold` | +0x18 | 0.0075 | not set |

- **Counting**, `sub_823B1560`, from `sub_826D5E78` once per frame for each human car that the AI does not drive (+0x38F8 clear) and that is not a replay (`sub_823623E0`): a frame whose steering (record +0x08) differs from the car's previous steering by more than 0.0075 counts as one input (+0x3938), capped at `MinInputsPerLap` times the laps begun.
- **The check**, `sub_823B1648`, in the lap logic `sub_823446C0` when a lap completes: fewer than `MinInputsPerLap` times the completed laps sets the car's sticky flag +0x393C.
- **The consequence**, `CRaceRewardsComponent` `sub_8249BAB0`: with the flag set, the race's rewards block is skipped: no credits and no experience for that race (unless the mode flag `get_u8_338` is set). The same function also ends without rewards when the world collisions per lap reach 900.

So the rule depends on the input's smoothness per frame, not on the device class: the pad class with a smooth wheel would trip it as well. A lap needs 10 frames in which the steering moves more than 0.0075 of its half range in one frame. That is a fast turn of the wheel: on a 900-degree wheel 3.4 degrees in one frame, 203 degrees per second at 60 fps and 810 degrees per second at 240 fps; at a 540-degree soft lock 2.0 degrees in one frame, 122 and 486 degrees per second. Smooth driving can complete a lap without 10 such frames, and the higher the frame rate, the likelier that is. This matches the update's notes (a zero credit bug and an anti-cheat rule that smooth wheel input triggered): they read as one rule seen from two sides. A pad's stick and the keyboard's rate limiter (0.014 of the half range per frame at 240 fps) clear the threshold easily.

**Decision for stage 2.** The device class does not decide the rule, so stage 2 can choose the class on the steering filters alone. Whatever the class, the native wheel needs an integration fix for the rule while it drives: either set `MinInputsPerLap` to 0 in the settings object (the input rule off, the collision rule kept), or scale the threshold to the frame time (the rule at its 60 fps meaning). The first is simpler and the game is offline; the choice is for the main session.

**The force feedback source, for stage 3.** The torque getter `CCarDynamics` slot 254 (`sub_828154B0`) returns the float at +0x30. Three `CCarDynamics` functions write it: `fn_82363A78` (151 instructions, a candidate for the torque smoothing of `SteerTorqueShouldSmooth`), `fn_8230A938` and `fn_8230C350` (physics step candidates). Which one runs at 359 Hz is stage 3's to confirm with a counter hook.

**Not done in stage 1:** the drawn steering wheel's full-lock angle per car (a hook on `sub_822E6B30` and a game run; it belongs with stage 2's traced runs, where the injection hook exists), and the readers of the wheel-mode byte and the `WheelMapping` sets (above).

### Stage 2: native input

Delivers:
- the runtime driver publishes the steering (−1..1 of the soft-lock range), throttle and brake as floats, and the buttons;
- an integration hook writes them at stage 1's injection point;
- the pedals at device resolution, with calibration in the launcher (range, deadzone, gamma) on top of Pit House's;
- the paddles as sequential shifts;
- clutch and handbrake on buttons;
- the title's device class chosen by stage 1's findings: the wheel class only if its title update 2 bugs can be avoided, otherwise the pad class with its assists bypassed at the injection point.

Verified:
- traced runs with the virtual joystick show the values reaching the car with more than 8-bit steps;
- the steering angle follows the input 1:1 without rate limiting;
- the career credits are correct after a race.

Owner's script: a lap in a known car, in cockpit view, checking that the drawn wheel matches the R9's angle, plus the pedals, the paddles and the buttons.

### Stage 3: force feedback from the title's torque

Delivers:
- a hook on `sub_828C3330` publishes the torque, speed and rear slip, and the human-car flag;
- the host thread turns them into a constant force at 500 to 1,000 Hz;
- spring and damper as firmware effects, scaled the way the title scales them;
- launcher options for gain, minimum force, smoothing and the torque cap.

Verified without hardware: recorded force streams from e2e laps (Quick Race, the same car and track):
- no spikes past the slew limit;
- zero in menus and pauses;
- the force follows the steering load in corners;
- stable against the frame rate.

Owner's script:
1. Gain 25%, then 50%.
2. Straight, corners and a spin.
3. Pause during a corner: the force stops.
4. Alt-tab: the force stops.
5. Rate the centre feel and the loss of grip at the limit.

### Stage 4: physics-rate force feedback and effects

Delivers:
- the torque, or the front tyres' aligning torque, sampled in the physics substep at 359 Hz;
- kerb, surface and impact effects derived from the title's rumble events and surface data, mixed into the constant force or sent as periodic effects.

Verified: the stage 3 and stage 4 streams compared over the same lap (latency, detail, no new spikes), then the owner's script.

### Stage 5: soft lock and lights

Delivers:
- **Soft lock per car:** through the MOZA SDK's limit angle, if its terms allow use; otherwise an end stop built from the force feedback: a steep spring beyond the car's lock, inside the governor's cap.
- **RPM and shift lights:** only if the SDK or HID reports turn out to offer them; the SDK page names no LED control.

Verified: the end-stop force traced at the lock in the recording backend, and the owner's lock-to-lock check per car class.

### Stage 6: options, docs and close gate

Delivers:
- launcher options and defaults;
- `docs/running.md` (controls and options);
- `docs/known-issues.md` and `docs/bug-reports.md` (tested hardware: the MOZA set);
- `docs/status/current.md`;
- the close gate of the increment.

### Later options

- Analog clutch and H-shifter: through the same driver, mapped to the control fields of stage 1.
- The `XInputdFF*` exports as a reference implementation to compare forces against the Xbox 360 behaviour.

## Risks

- **Title update 2 bugs in the base build.** Settled by stage 1: the anti-cheat rule is input-based (steering changes per frame), with no rewards as its consequence, in any device class. The wheel needs an integration fix of the rule while it drives (`MinInputsPerLap` 0, or a threshold scaled to the frame time). The countersteer bug of the 900-degree set remains a reason to measure the wheel modes' filters before choosing one.
- **The filters per wheel mode are not known yet.** No reader of the control record's wheel-mode byte was found statically; stage 2 measures the modes' steering response with a traced run.
- **Frame-rate torque.** The title's torque changes at frame rate; interpolation adds up to a frame of latency until stage 4.
- **MOZA SDK terms are unknown.** Without them, soft lock falls back to a force feedback end stop, and no LEDs.
- **Pit House interaction.** The base's own angle limit, damper and filters can mask or double what the game sends.
- **Hardware verification needs the owner.** Every force feedback stage ends with the owner's script; nothing in the repository can judge the feel.

## Open questions for the owner

1. **Menus and layouts.** Should the game show its wheel options and layouts, if stage 1 allows the wheel class, or keep the controller screens?
2. **Soft lock rule.** Per car, matching the cockpit's drawn wheel; a fixed angle (for example 900 or 540 degrees); or Pit House's setting?
3. **MOZA SDK.** Download it and check its terms, or ask MOZA support whether it may be loaded by a free, non-commercial project and whether it controls the wheel's LEDs?
4. **Pit House profile.** Which settings do you run on the R9 (maximum angle, force feedback strength, road sensitivity, damper, friction, inertia), and may the test script fix one profile?
5. **ESX on PC.** What do Windows' game controller panel and Pit House show for the ESX: button count, paddles, rotary knobs, LEDs?
6. **Torque cap.** What starting limit feels safe (for example 40% of the R9's peak) before tuning upwards?
7. **Clutch button.** Is manual with clutch on a wheel button wanted, or automatic clutch only?

## Evidence

- Strings, functions, callers and decompiled code: the catalog and Ghidra project under `I:\FM4-re` ([function catalog](function-catalog.md)), exported with `Export.java` for the functions named above.
- Generated code: `<BuildRoot>\project\generated\default` (the XamInput call sites, the ordinal initializers `sub_82D38EA8` to `sub_82D390A0`).
- Game data: `Media/ControllerFFB.ini`, `Media/ControllerRumble.xml`, `Media/physics/PhysicsSettings.ini`, `Media/GameTunableSettings/GameTunableSettings.ini`, and the tables `ControllerLayouts`, `WheelLayouts`, `BristolLayouts` and `Data_Car` in `Media/db/gamedb.slt` of the USA Disc 1.
- Title update 2 notes: [title updates](title-updates.md).
- External: [MOZA SDK](https://mozaracing.com/pages/sdk), [GameInput force feedback](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/advanced/input-forcefeedback) (both read 27 October 2026).
