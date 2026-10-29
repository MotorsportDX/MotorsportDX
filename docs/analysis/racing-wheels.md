# Native racing wheel support

Status: research and design of 27 October 2026; stage 0 (the device and safety shell), stage 1 (the control path), stage 2 (native input), stage 3 (force feedback from the title's torque), stage 4 (the physics-rate force and the kerb and impact effects) and stage 5 (each car's steering lock with an end stop, and the drawn wheel) done on 29 October 2026 ([stage 0](#stage-0-done-29-october-2026), [stage 1](#stage-1-findings-29-october-2026), [stage 2](#stage-2-done-29-october-2026), [stage 3](#stage-3-done-29-october-2026), [stage 4](#stage-4-done-29-october-2026), [stage 5](#stage-5-done-29-october-2026)). The project runs the base Disc 1 executable without a title update, so every address below is from the recompiled base `default.xex` (USA; Japan/Asia share the code).

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
- **The base build carries the wheel bugs that title update 2 fixed:** the zero credit bug, the anti-cheat rule that smooth wheel input triggered, and countersteering that treated 900 degrees as 270. Entering the title's wheel path as it is would bring those bugs with it. Smooth, high-resolution steering could also trip the anti-cheat rule through the pad path. Stage 1 found the rule: it counts frames with a fast steering change and withholds a race's rewards when there are fewer than 10 per completed lap; it does not depend on the device class. Stage 2 scales its threshold to the frame time for every device, so it counts as at 60 fps ([stage 2](#stage-2-done-29-october-2026)).
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
| `Data_Car.SteerMaxAngle` | the road-wheel lock at full steering in wheel mode 2 (`CCarDynamics` +0x54, radians): 42 degrees for 441 of the 503 cars, 36 for 37 GT racing cars, 30 for 25 prototypes ([stage 5](#stage-5-done-29-october-2026)) |
| `Data_Car.SteerMaxAngleFiltered` | 12 to 35 degrees, average 27.2, over 503 cars (`CCarDynamics` +0x58); not the lock in wheel mode 2 |
| the shared `GlobalCarAttribs.xml` (driver) | `SteeringWheelAngleScale` 0.75 |
| per-car `carattribs.xml` | `SteeringWheelAngleOverride`, 0 for the cars read in the FM3 car converter work |
| driver animations | `SteerLock2LockSmall` and `SteerLock2LockLarge` in two variants |

The angle the cockpit's steering wheel shows reaches the presentation through `CCarPresentationDeferred::SetSteeringWheelAngle` (`sub_822E6B30`, executed by `sub_822C5728`), and the model turns by 0.75 of it ([stage 5](#stage-5-done-29-october-2026)). The title has no steering ratio, so stage 5 sets each car's lock from its road-wheel lock at one steering ratio.

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
2. **Scale and clamp.** `sub_8239E600` calls `sub_828C3330` on the parameter block at +0x1090. The dynamic force is `clamp(DynFFBScale × torque, ±min(DynFFBMaxForce, 1))`, stored at +0x538. The spring scale is the speed curve × the rear slip curve, stored at +0x540. Then `sub_823921F8` stores at the block's +5 whether the player drives the car (the same condition that keeps the torque); without it the device update sends a zero force and the spring at full strength (the menus' centring).
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
- **Torque cap:** a hard upper limit in the runtime, independent of the gain, default well below the R9's peak, plus a slew-rate limit. It bounds the constant force and the firmware's spring and damper together (stage 3).
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

**The control path.** `sub_826D5E78`, once per frame from the race update `sub_826DCFB8` (its first call, before the lap logic `sub_823446C0`), loops over the race's cars (all 8 in stage 2's trace). For a player's car it reads the player's pad object and builds a 28-byte control record on the stack:

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

- **Counting**, `sub_823B1560`, from `sub_826D5E78` once per frame for each car whose flag +0x38F8 is clear (all 8 cars, the AI's opponents too, in stage 2's trace) and that is not a replay (`sub_823623E0`): a frame whose steering (record +0x08) differs from the car's previous steering by more than 0.0075 counts as one input (+0x3938), capped at `MinInputsPerLap` times the laps begun.
- **The check**, `sub_823B1648`, in the lap logic `sub_823446C0` when a lap completes: fewer than `MinInputsPerLap` times the completed laps sets the car's sticky flag +0x393C.
- **The consequence**, `CRaceRewardsComponent` `sub_8249BAB0`: with the flag set, the race's rewards block is skipped: no credits and no experience for that race (unless the mode flag `get_u8_338` is set). The same function also ends without rewards when the world collisions per lap reach 900.

So the rule depends on the input's smoothness per frame, not on the device class: the pad class with a smooth wheel would trip it as well. A lap needs 10 frames in which the steering moves more than 0.0075 of its half range in one frame. That is a fast turn of the wheel: on a 900-degree wheel 3.4 degrees in one frame, 203 degrees per second at 60 fps and 810 degrees per second at 240 fps; at a 540-degree soft lock 2.0 degrees in one frame, 122 and 486 degrees per second. Smooth driving can complete a lap without 10 such frames, and the higher the frame rate, the likelier that is. This matches the update's notes (a zero credit bug and an anti-cheat rule that smooth wheel input triggered): they read as one rule seen from two sides. A pad's stick and the keyboard's rate limiter (0.014 of the half range per frame at 240 fps) clear the threshold easily.

**Decision for stage 2.** The device class does not decide the rule, so stage 2 can choose the class on the steering filters alone. Whatever the class, the native wheel needs an integration fix for the rule while it drives: either set `MinInputsPerLap` to 0 in the settings object (the input rule off, the collision rule kept), or scale the threshold to the frame time (the rule at its 60 fps meaning). The first is simpler and the game is offline; the choice is for the main session.

**The force feedback source, for stage 3.** The torque getter `CCarDynamics` slot 254 (`sub_828154B0`) returns the float at +0x30. A search for stores to +48 in `CCarDynamics` functions named `fn_82363A78`, `fn_8230A938` and `fn_8230C350`; stage 4 found that none of them writes the torque (they store to a stats copy and to per-tyre arrays) and traced its writer at run time ([stage 4](#stage-4-done-29-october-2026)).

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

#### Stage 2 done, 29 October 2026

**The input path.**
- **The host** maps the wheel on its 1,000 Hz thread ([wheel_mapping.h](../../work/runtime/include/rex/input/wheel/wheel_mapping.h), header only, so the CPU tests check the same code):
  - the steering axis to -1..1 of the lock: the wheel's rotation (`wheel_steer_range`, Pit House's maximum angle) over the steering lock (`wheel_steer_lock`, 0 for 1:1), with an inversion;
  - each pedal from its raw values at rest and fully pressed (either order) through a deadzone at the rest end and a gamma;
  - the buttons to the shifts, the clutch and the handbrake, the shifts as press counters, so a press shorter than a guest frame still shifts once.
- **The runtime** publishes the controls through a store with its own lock ([wheel_controls.h](../../work/runtime/include/rex/input/wheel/wheel_controls.h)), never the SDL driver's.
- **The integration** ([fm4-wheel-input.cpp](../../work/fm4-rex/src/fm4-wheel-input.cpp)) hooks the three action readers that `sub_826D5E78` calls for the record and acts only on its own call sites (return addresses `0x826D61AC` throttle, `0x826D61C8` brake, `0x826D61DC` steering, `0x826D6248` clutch, `0x826D625C` handbrake, `0x826D62C0` and `0x826D62D8` the shifts), for the pad whose user index (the pad object's first word, low byte) is 0. It merges the wheel as the title merges two bindings of an action: the larger pedal, the steering of larger magnitude, a shift on a new press. So the title's layouts, menus, pause, rewind, replays and AI driving are untouched, the pad and the keyboard still drive, and the anti-cheat count, which runs after the record is handed over and reads it from the stack, sees the wheel's steering.
- **The launcher and `-Wheel`.** Every mapping value is a knob of `-Wheel` (`WHEEL_KNOBS` in `pipeline.py`, launcher.json's `wheel`, the launcher's port in `plan.cpp`; integer knobs for the axis and button numbers). The Controls page's **Racing wheel** tab sets them; with the wheel test running, it shows the steering and each pedal's value as the car will get it, takes a pedal's rest and pressed values from the live axis, and assigns a button by pressing it ([running.md](../running.md#racing-wheel-in-the-game)).
- **A virtual wheel for checks.** `wheel_virtual=<script>` attaches an SDL virtual wheel that a CSV script drives (raw axis values interpolated between rows, a button mask; it repeats), and lets SDL read joysticks without window focus, so an e2e run can drive the car through the whole runtime path.

**Verified without hardware** (snapshot `whl3`, `4c65e50`, at 3/2 and 240 Hz on the native path; the runs `whl3f` and `whl3v`):
- **The values reach the car at the wheel's resolution.** A career race driven by the virtual wheel's 12-second script (steps of 0.25, a 0.5 Hz sine, a ramp of single raw steps, full lock both ways, pedals, shift presses), traced at the hand-over to `CCarDynamics`: 10,821 records driven by the wheel, the steering in steps down to 0.00003 (one raw step of the 16-bit axis; an 8-bit step would be 0.0078), 303 distinct steering values within the first 0.01 of the ramp, the throttle in steps down to 0.000015 (the pad's 8-bit triggers: 0.0039), the brake at 0.5000, each shift press one record with the shift bit set, and only the first player's record driven (user 0; every other car's record untouched).
- **The steering filters per wheel mode.** The record's wheel mode byte (+0x17) was cycled through 0, 1 and 2 every 12 seconds of session time, and the first 8 KB of the car's `CCarDynamics` was dumped each frame. Two fields follow the input: +0x24, a steering target, at once and at 0.733 of the input at full lock (probably the Ford Ka's road-wheel lock; no other car was traced), and +0x04, the steering itself, which follows the target:

  | Mode | Target (+0x24) against the input | Steering (+0x04): a full-lock step to 90% | A 0.25 step | Slow input (the sine): steering minus target |
  |---|---|---|---|---|
  | 0, the pad's | linear | 360 ms | 90 ms | up to ±0.005 |
  | 1, the 270-degree wheel's | a curve: 0.25 gives 0.18 of the lock | 50 ms | 9 to 16 ms | 0 |
  | 2, the 900-degree wheel's | linear | 50 ms | 12 ms | 0 |

  In mode 2 the steering equals the target in every steady frame and during the whole sine, and the target is 0.733 times the input in every frame (1,750 frames with an input above 0.05), full lock and slides included: no countersteer deviation showed in this run.
- **The drawn steering wheel** (`SetSteeringWheelAngle`, `sub_822E6B30`, one call per frame for the cockpit car): in mode 2 it turns about 7.6 radians per unit of input, close to 2.5π (450 degrees each way, a 900-degree wheel), and stops at ±π/2 (90 degrees); in mode 0 it turns 0.39 radians at 0.25, in mode 1 0.50 radians. So in mode 2 the cockpit's wheel matches a wheel set to 900 degrees 1:1 up to 90 degrees either way, and holds there beyond (stage 5 found that the model turns 0.75 of this angle, so it stops at 67.5 degrees). Whether the limit is the title's animation or a per-car value is open; only the Ford Ka was traced.
- **Unit tests** (`pipeline.py build`): `fm4_wheel_mapping_tests` (the steering's resolution and lock, the pedals' calibration in either order, deadzone, gamma, missing axes and buttons, the press counters) and `fm4_wheel_host_tests` (the mapped controls published every tick from an SDL virtual wheel: the steering at full resolution, a pedal calibrated in reverse, a shift press counted once).
- **The career's rewards**: see the anti-cheat rule below.

**The device class (stage 2's choice).** The pad class stays, and the record of a frame whose steering the wheel won gets wheel mode 2, the title's 900-degree wheel filters: linear and fast (the table above). Mode 0 rate-limits the steering, which a wheel must not have; mode 1 bends the response for a 270-degree wheel. The wheel class itself (reporting `XINPUT_DEVSUBTYPE_WHEEL`) is not needed for these filters, since the mode reaches the car only as the record's byte, and it would bring the title's wheel layouts and menus and its frame-rate force feedback with the title update 2 bugs. A frame whose steering the pad wins keeps mode 0, so a pad player keeps the pad's filters while a wheel is connected. No player-facing trade-off between the classes was found, so the choice is the main session's to review, not the owner's.

**The anti-cheat rule: the main session's decision of 29 October 2026.** The threshold is scaled to the frame time, so it counts as it would at 60 fps: a frame counts when its steering change exceeds `FrameAnalogInputThreshold` × (dt × 60), dt being the session time the title advanced in that frame (the session update `sub_8240DB08` runs the race update inside it, after adding its step; `fm4-race-clock.cpp` publishes the session and its time before the update). The hook of the count (`sub_823B1560`) divides the frame's change by dt × 60 (clamped to 0.05..4) before the title compares it, so the title's own rule, cap and lap check run unchanged. It applies to every device, pads included; `FM4_INPUT_THRESHOLD_UNSCALED=1` keeps the title's rule for comparisons. `MinInputsPerLap` 0, the fallback, was not needed.

**Does a pad already lose its rewards at 240 Hz?** No, as far as a run without a person can show. The e2e harness cannot drive a career race with the pad and finish it, so the measurement used the AI driver, whose steering is smoother than a pad's stick (`whl3f`, `career_finish` at 240 Hz, the count's trace raw and scaled):
- The count runs for all 8 cars of the race each frame, the player's car among them (its flag +0x38F8 is 0 for every car, so it is not an AI flag).
- Each car changed its steering by more than 0.0075 in one frame 330 to 440 times a lap at 240 Hz (with the scaled threshold, 700 to 1,140 times); the title's count reached its cap of 10 a lap and no lap check failed.
- The race paid its rewards: race winnings +3,000 CR, difficulty +750 CR, race feats +1,520 CR, the driver and Ford affinity level 2 with its 2,000 CR bonus, EVENT COMPLETE.

A pad player's stick changes faster than the AI's steering, so the rule does not withhold a pad player's rewards at 240 Hz, and there was no live player bug to land first. A wheel turned slowly and smoothly is the case the scaling is for: unscaled at 240 Hz a counted frame needs the wheel to move 810 degrees a second on a 900-degree wheel, scaled 203, as at 60 fps.

**Open:**
- The readers of the wheel mode byte and the `WheelMapping` sets are still not found statically; the modes were measured instead. Whether mode 2 uses the 270-degree countersteer scale (the title update 2 bug) somewhere this run did not reach (a held countersteer in a long slide) is not known; nothing in the trace showed it.
- The per-car drawn steering angle: one car traced (±90 degrees at most in mode 2). Stage 5's soft lock needs the road-wheel lock per car (+0x24's 0.733 for the Ford Ka) and the drawn limit per car.
- The menus are not driven by the wheel's buttons (the controller or the keyboard drive them).
- A real wheel: the owner's checklist ([running.md](../running.md#racing-wheel-test-checklist), steps 9 to 14).

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

#### Stage 3 done, 29 October 2026

**The source.** `sub_8239E600` is a tail call: `sub_828C3330(pad + 0x1090, &{torque, speed, rear slip})`, its only caller the control step `sub_826D5E78`, once per frame for each car with a player's pad. The block (pad + 0x1090) holds `ControllerFFB.ini` as `sub_828C2CD8` loads it, three floats per effect (scale, maximum force, deadband) from +0x08: inertia, friction, damper (+0x20), spring (+0x2C); then `DynFFBScale` (+0x38), `DynFFBMaxForce` (+0x3C), the speed curve (+0x40 to +0x4C) and the rear slip curve (+0x50 to +0x5C). `ForceFeedbackScale` (+0x53C) scales every effect when `sub_828C34E0` builds them (coefficient = scale × it, saturation = maximum force × it); the device update `sub_823A42A0` sends the dynamic force × full scale and sets the spring effect's gain to the spring scale (+0x540). The torque is zeroed unless the player drives the car (`+0x38F8` clear and `sub_822B7130` false: not the AI, not a replay), and the same flag goes to the block's +5 (`sub_823921F8`, return address `0x826D6788`). A race update that skips the control step, or whose control step finds no player's pad, resets every block (`sub_828C5B18`, from `0x826DD040` and `0x826D6A54`).

**The integration** ([fm4-wheel-input.cpp](../../work/fm4-rex/src/fm4-wheel-input.cpp)):
- the hook of `sub_828C3330` reads the block's results after the title's code: the constant force is +0x538 × `ForceFeedbackScale`, the spring's coefficient and saturation are `SpringScale` and `SpringMaxForce` × `ForceFeedbackScale` × the spring scale (+0x540, as the title's effect gain), the damper's are `DamperScale` and `DamperMaxForce` × `ForceFeedbackScale`; inertia and friction are 0 in the ini and left out;
- the hook of `sub_823921F8` at the control step's call publishes that sample with the flag, for the first player's pad (user 0) only, so the 8 cars of the race never drive the force;
- the hook of `sub_828C5B18` publishes a sample outside a driven race.

So the force is zero at once whenever the title itself would send none (the AI drives, the race ended, a replay, a race update without the player's control), and the title's menu spring is not reproduced: outside a driven race the wheel is free. When the race update stops altogether (pause, photo mode, the rewind, loading, the menus), nothing is published and the governor's watchdog takes the force to zero.

**The host.** The force thread polls the latest sample each tick through `HostOptions::force_source` (the store in [wheel_controls.h](../../work/runtime/include/rex/input/wheel/wheel_controls.h); the launcher's test has none). A sample outside a driven race sets the pause block. The host runs a damper effect beside the constant force and the spring, and records the title's sample (whether it drives, its sequence number, the torque, the speed and the rear slip) with every force.

**The governor** ([ffb_governor.h](../../work/runtime/include/rex/input/wheel/ffb_governor.h)) now takes the spring and the damper with the constant force:
- the cap bounds their sum (the constant force plus both saturations, the most the firmware can apply at once); the conditions take at most half of the cap (`condition_share`), both scaled by one factor beyond it, and the constant force is clipped to what they leave;
- a condition's level moves at the slew limit, and is passed on only in steps of at least 0.002 while it moves (exactly once it settles), so the device gets few condition updates and the bound holds for the levels the device actually has;
- a minimum force (`min_force`; linear below 2% of full scale, so continuous through zero) and a low-pass smoothing of the constant force (`smoothing_s`, 10 ms in the runtime; it starts from zero after every zero state).

With the title's defaults (`ForceFeedbackScale` 1, so the spring at 0.5, the damper at 0.1 and the force up to 1) and the default cap of 0.25 at gain 1, the spring and damper together take 0.125 at speed and the constant force gets 0.125 to 0.25.

**Options** (cvars, `-Wheel` knobs and the launcher's Force feedback section, [running.md](../running.md#racing-wheel-force-feedback)): `ffb_min_force` (0), `ffb_smoothing` (10 ms), `ffb_spring` and `ffb_damper` (1, the title's scale), with `ffb_gain` and `ffb_cap` from stage 0. With the recording backend (`wheel_ffb_sink=record`) the window's focus does not block the force, since none reaches a device, so an e2e run records the forces without focus.

**Verified without hardware** (snapshot `whl6`, `129989b`, at 3/2 and 240 Hz on the native path; the force recordings with the virtual wheel's 16-second script: a straight, a steering sine of ±0.15 and held steps of ±0.2 at a part throttle):
- **The unit tests:** `fm4_wheel_ffb_tests` (the conditions ramped, scaled, zeroed by a block and the watchdog, bad values; the title's mix at full scale within a cap of 0.25 with the conditions at half of it in their proportion; the sum within the cap while the mix changes; a moving condition passed on in steps of at least 0.002; the minimum force; the smoothing's 63% after one time constant, no overshoot, a restart from zero after a block) and `fm4_wheel_host_tests` (a 240 Hz title source with a spring and a damper: within the cap, the force's sign following the torque, zero at once outside a driven race, the watchdog when the samples stop).
- **The title's values:** `ForceFeedbackScale` is 1 with the BuildRoot's profile and the pad class, so the force equals the torque (`DynFFBScale` 1); the spring scale runs from 0 at rest to 1 at speed and fades with the rear slip, its effect up to 0.5 and the damper 0.1 before the governor; the torque reached -0.90 to 0.33 in these runs. At speed the torque opposes the steering (correlation -0.46): with the wheel's positive force to the right (the launcher's direction pulse), the force centres the wheel, so `ffb_invert` stays off by default.
- **Career, the virtual wheel drives** (`whl6v`): 18,898 samples, 9,826 of them driven. The constant force plus both saturations never exceeded 0.250015 (the cap and one 16-bit step), no step exceeded the slew limit, the force's sign matched the torque's in 11,874 of 11,889 rows with a torque above 0.05 (correlation 0.81; unclipped, the force was 0.996 of its target), and the cap clipped 58% of the driven rows (the spring and damper hold up to 0.125 at speed). The pause menu (QUIT RACE) stopped the samples and the force reached zero 50 ms after the last one; the quit, the replay and the menus published samples outside a driven race, with zero force, spring and damper in all 9,072 rows.
- **Photo mode and the rewind** (`whl6p`, `whl6r`, the fast route): after driving, the pause menu (photo mode follows it) and the rewind stopped the samples; the force reached zero 50 and 81 ms after the last one, at the slew limit from -0.125 in the rewind, and stayed there. Within the cap, no step above the slew limit, the sign matched the torque's in every row above 0.05.
- **The AI's car** (`whl5a`, the first build `e7432a7`, whose game-side code is the same): a fast career with `engageaionstart` and the wheel on published 9,973 samples, none driven, so the force stays zero while the AI drives the player's car.
- **The landing check** (`whl6`): the career at 3/2 and 240 Hz with the defaults, exit 0, main's four log errors, a sane capture (it ran beside a second cell, so its frame rate is not comparable).
- **A fix found on the way:** the e2e game window has no focus, so the first runs recorded zero force throughout (the focus block); with the recording backend the window's focus no longer blocks.

**Not verified:** a real wheel (the owner's checklist steps 15 to 21), a race the player finishes (not run; the end of a race was covered by QUIT RACE), the force at 60 fps, and how often DirectInput accepts the constant force's updates (one per tick while it changes; the conditions change rarely).

### Stage 4: physics-rate force feedback and effects

Delivers:
- the torque, or the front tyres' aligning torque, sampled in the physics substep at 359 Hz;
- kerb, surface and impact effects derived from the title's rumble events and surface data, mixed into the constant force or sent as periodic effects.

Verified: the stage 3 and stage 4 streams compared over the same lap (latency, detail, no new spikes), then the owner's script.

#### Stage 4 done, 29 October 2026

**Where the torque comes from.** A data write watch (`fm4-write-watch.cpp`: a hardware write breakpoint on the torque's address, on every thread, recording each write with its host call stack, which the linker map turns into the guest chain) found one writer of `CCarDynamics` +0x30 in two careers at 240 and 60 Hz (`whl7`): `sub_823021F0`, the steering torque model, from `sub_82302A18` at the end of the car's step `sub_82822BD0`, from the world step `sub_8282A0A8` and the physics update `sub_8282A470` in the race update. It wrote once per frame (6,861 writes at about 200 fps, 2,434 at 60 and 120 fps), never more. The structure:

- `sub_8282A470` divides the frame's time into world steps by a rate it reads from a global; it ran one world step per frame in both runs.
- The car's step `sub_82822BD0` integrates its physics in m steps of dt/m (`sub_82304E80`, `sub_82307448`, `sub_823071B0`, `sub_82304ED8`), m from the frame's time and the physics rate, then calls `sub_82302A18`, whose first call is the torque model with the frame's time.
- The torque model (`sub_823021F0` on the physics object, which is `CCarDynamics` - 0x960, so the torque is its +2448) reads the front tyres' state (tyres 0 and 1, 1,056 bytes each from +6160: +480, +528, +552, +712 and its own state at +800 to +828) and the car's steering position (+5456), evaluates the tyre curves (`sub_8230E910`), smooths the result (a spring at +2440 and +2444, the steering rate's three-entry ring at +2464 to +2480) and stores the torque. Its callees store nothing outside the stack.

So the title's torque is a per-frame value, and the physics steps run at about 359 Hz within the car's step. Stage 1's three candidates (`fn_82363A78` copies 21 getter values into a stats record; `fn_8230A938` and `fn_8230C350` store to per-tyre and 56-byte arrays) do not write it.

**The physics-rate force** ([fm4-wheel-input.cpp](../../work/fm4-rex/src/fm4-wheel-input.cpp)):
- After each physics step of the first player's car (`sub_82304ED8` returns), the hook runs the title's torque model on that step's state, on a copy of the registers, with the time since the car's step began, and puts back every byte the model writes (+2440 to +2484 and the front tyres' +800 to +832). The title's state is unchanged, and the last step's value is the title's own torque: in the check the two were equal in 7,199, 5,451 and 2,740 car steps at 240, 120 and 60 Hz (largest difference 0.000001).
- Each value is scaled as the force feedback block scales the frame's torque (`DynFFBScale`, `DynFFBMaxForce`, `ForceFeedbackScale`) and published as a point at its physics time, only while the player drives the car (the frame sample's flag). `FM4_WHEEL_PHYSICS=0` or `wheel_ffb_physics=false` publishes none.
- The host plays the points back on its own clock ([ffb_stream.h](../../work/runtime/include/rex/input/wheel/ffb_stream.h)): the offset between a point's physics time and its arrival, and the delay that lets every point of a burst arrive in time, are measured over half a second (the delay at most 50 ms); a playback clock steered towards that target interpolates between the steps. While the points keep coming the played force replaces the frame sample's constant force; when they stop, the frame force drives it again (the watchdog still sees them stop). The spring, the damper and the effects stay with the frame sample.

**The effects.** The control step hands the rumble update (`sub_8239C970`, return `0x826D66F4`) ten floats per player's car: the engine and transmission speed, the three collision impulses (cars +8, the world +12, objects +16), the tyres' traction, air friction and the surface bumpiness (+36), which the rumble mixer (`sub_822A0018`, the curve evaluator `sub_828C42A8`) turns into the two motors through `ControllerRumble.xml`. The pad's rumble object (pad +688) holds the `<Controller>` table at +32 and the `<Wheel>` table at +1792, each effect a left and a right curve of three points. The hook evaluates the `<Wheel>` table's curves (the stronger motor) for the surface and the collisions:
- **the road's vibration**: the surface bumpiness's level (0.35 at a bumpiness of 0.02, 0.8 at 0.4), 0 on asphalt and rising off the track with speed; a 40 Hz sine effect that the firmware runs, held until the next sample;
- **impacts**: the strongest collision's level, and its length from the table's time curves (0.08 to 0.3 s); a new impact when it exceeds 1.25 times what is left of the last one, so a scrape along a wall does not restart it every frame; a 17 Hz sine burst that the firmware fades out over that length.

The governor ([ffb_governor.h](../../work/runtime/include/rex/input/wheel/ffb_governor.h)) counts both in the cap's sum by their magnitude, within `effect_share` (a quarter of the cap): an impact keeps its magnitude for its length, the road's vibration gets what it leaves and moves like a condition's level, and the constant force gives way at once. Every zero state stops both. Their gains are `wheel_ffb_road` (0.25) and `wheel_ffb_impact` (0.5), fractions of full scale at the title's strongest level; a wheel without a sine effect gets neither (logged).

**Verified without hardware** (snapshot `whl8`, `1b4533d`, at 3/2 on the native path; the career driven by the virtual wheel's 27-second script: the stage 3 script, then a full-throttle straight and a held right turn into the barrier):

| Rate | Physics steps per car step | Points per second driven | Rows driven by the points | Playback delay, median and largest | Distinct force values per second, frame and points | Frame force behind the points |
|---|---|---|---|---|---|---|
| 240 Hz (about 140 fps beside a second cell) | mostly 2 to 4 | 361 | 91.0% | 10.0 and 21.7 ms | 143 and 533 | 0 ms (correlation 0.998) |
| 120 Hz | mostly 3 and 4 | 338 | 90.2% | 10.0 and 22.0 ms | 114 and 486 | 0 ms (1.000) |
| 60 Hz | mostly 6 | 305 | 91.9% | 18.3 and 33.7 ms | 58 and 494 | 0 ms (0.998) |

- **The same detail at every rate:** the points carry about 500 distinct values a second at 60, 120 and 240 Hz, where the frame force carries one a frame; the played force follows the physics steps at the cost of the delay (about one frame at 60 fps, 10 ms above it), and lines up with the frame force (no lag at the best correlation): the stage 3 force had the same delay in its frame steps and its 10 ms smoothing.
- **No new spikes:** the force plus the spring, the damper and both effects never exceeded 0.250016 (the cap and one 16-bit step), no rise exceeded the slew limit, and every row outside a driven race (8,663, 9,873 and 5,784) has zero force, spring, damper and effects.
- **The effects:** the barrier gave 83 to 276 impacts (world impulses up to 31) with their bursts at the share's 0.0625, and the grass after it the road's vibration (bumpiness up to 0.03, 0 on asphalt). These runs read the `<Controller>` table by mistake (its surface curve ends at 0.25); with the `<Wheel>` table (`whl9v`, `111adac`, 240 Hz) the trace shows its curve (0.8 at its end), the road's level reaches 0.35 at a bumpiness of 0.022 and 0.36 at most, and its vibration and 175 impacts run at the share's 0.0625; the last step equals the title in all 7,410 car steps, 363 points a second, the sum within 0.250015, no rise above the slew limit, zero in all 9,335 rows outside a driven race.
- **Unit tests:** `fm4_wheel_ffb_tests` (the effects within their share, an impact for its length with the road giving way, the effects stopped by a block, the watchdog and a stale impact, bad levels; the stream at 60, 120 and 240 fps following a 359 Hz sine at one lag to within 0.03, a millisecond of its steepest slope, where holding each frame's newest step would be off by up to 0.42; a rewind, a late burst, the delay's limit) and `fm4_wheel_host_tests` (points in bursts driving the force behind them, the frame force taking over when they stop, the effects inside the cap and zero outside a driven race).
- **The landing check:** the career at 3/2 and 240 Hz with the defaults (`whl8` from `1b4533d`, `whl9` from `111adac`): exit 0, the same four log errors as main's, compare PASS against `whl6` (both ran beside a second cell).

**Not verified:** a real wheel (the owner's [checklist](../running.md#racing-wheel-test-checklist) steps 22 to 27), how the bursts and the road's sine feel on the R9 and how often DirectInput accepts their updates, photo mode and the rewind with the points (the points stop with the race update, so the watchdog applies as in stage 3), and the frame rate alone (each cell ran beside a second one).

### Stage 5: soft lock and lights

Delivers:
- **Soft lock per car:** through the MOZA SDK's limit angle, if its terms allow use; otherwise an end stop built from the force feedback: a steep spring beyond the car's lock, inside the governor's cap.
- **RPM and shift lights:** only if the SDK or HID reports turn out to offer them; the SDK page names no LED control.

Verified: the end-stop force traced at the lock in the recording backend, and the owner's lock-to-lock check per car class.

#### Stage 5 done, 29 October 2026

Route (a), the main session's decision of 29 October 2026: the soft lock is an end stop built from the force feedback. The MOZA SDK waits for the owner's decision after the R9 test, and RPM and shift lights stay out (they would need the SDK).

**The car's lock.** The title has no steering ratio and no lock-to-lock angle. What it has per car is the road-wheel lock at full steering, `Data_Car.SteerMaxAngle`, which the car's `CCarDynamics` holds at +0x54 in radians (stage 2's dump of the Ford Ka: 0.7330, 42 degrees; +0x58 holds `SteerMaxAngleFiltered`, 29 degrees). In wheel mode 2 the steering target (+0x24) is the record's steering times it. Over the 503 cars of the database it takes three values: 42 degrees for 441 cars, 36 for 37 GT racing cars (the DBR9, the Corvette C5-R and C6-R, the Viper GTS-R and others) and 30 for 25 prototypes (the R10, the 908, the 333 SP and others).

- **The integration** ([fm4-wheel-input.cpp](../../work/fm4-rex/src/fm4-wheel-input.cpp)) publishes that lock with each record of the first player's car.
- **The host** ([wheel_mapping.h](../../work/runtime/include/rex/input/wheel/wheel_mapping.h)) makes the car's steering lock `wheel_car_lock` x the road-wheel lock / 42, with `wheel_car_lock` 900 degrees by default (the title's own 900-degree wheel for a 42-degree car):
  - 771 degrees for the GT cars and 643 for the prototypes, so every car has the same steering ratio (10.7:1 at 900);
  - a fixed `wheel_steer_lock` replaces it for every car;
  - the lock is never more than the wheel's rotation (`wheel_steer_range`, 900 by default).
  - `wheel_steer_lock` 0 used to mean the wheel's rotation; it now means the car's own lock, which is the same 900 degrees for a 42-degree car at the defaults.

**The input.** The steering is the wheel's angle over half the car's lock: linear and 1:1, full at the lock, held at full beyond it, in wheel mode 2 as before (no rate limit). A 540-degree car on a 900-degree wheel reaches full steering at 270 degrees either way.

**The end stop.** `EndStop` in [ffb_governor.h](../../work/runtime/include/rex/input/wheel/ffb_governor.h), computed by the host at 1,000 Hz from the wheel's angle:
- zero inside the lock;
- past it, a parabola over the first 2 degrees (so the force and its slope start at zero), then linear up to its strength 6 degrees past the lock, held beyond;
- a damper on the wheel's speed (smoothed over 10 ms): turning outwards at v degrees a second adds what the spring adds over v x 0.02 s, and turning back takes as much away. It fades in over the first 2 degrees like the spring, and it never pushes outwards.

The governor adds it to the constant force after the title's gain, minimum force and smoothing, and before the ramp-in, the cap and the slew limit. It runs only while the title drives the player's car, and it is zero whenever a block or the watchdog zeroes the force. Its strength is `wheel_end_stop` (1, a fraction of full scale), so at the defaults the cap bounds it: it gets what the spring, the damper and the vibrations leave of the cap. The title's spring, at its saturation that far from the centre, pushes the same way.

**The drawn wheel.** The lap logic `sub_823446C0` sets each car's drawn steering wheel through the car's presentation:
- the call: car +11604, slot 32, `SetSteeringWheelAngle`, from `0x82344A20`, with car +15024 plus +15032;
- in wheel mode 2 the angle is the record's steering x 2.5 pi, held at +-pi/2, the title's 900-degree mapping;
- the presentation (`sub_823143F8`) turns the wheel's model by a global (`0x82D92B44`: 2.5 in the image, 0.75 at run time, `GlobalCarAttribs.xml`'s `SteeringWheelAngleScale`) times that angle.

So the title's drawn wheel turns 0.75 of a 900-degree wheel's angle and stops at 67.5 degrees; stage 2's reading of 1:1 up to 90 degrees took the angle passed for the model's.

For the first player's car while the wheel steers it, the hook passes the wheel's own angle, up to half the car's lock, divided by that global, and keeps the title's other term. The model then turns 1:1 with the wheel. The car is the one the anti-cheat count takes right after the wheel's record; the lap logic passes the same car. `FM4_WHEEL_DRAWN=title` keeps the title's angle. The driver's hands follow the title's hand animation of the same angle: past about 90 degrees they keep the full-turn pose, one hand over the top of the rim, and no gap or distortion showed in the captures.

**Verified without hardware.** Snapshots `whl10` (`04e4680`, the lock and the end stop) and `whl12` (`7946238`, the drawn wheel), with `whl11` between them (a first version of the drawn wheel, without the model's scale); native path, 240 Hz.

- **The input** (`whl10vk`, the career's Ford Ka, `steer_range=1080` with the car's lock; `whl10va`, the Quick Race's 1980 131 Abarth, `steer_range=900` with `car_lock=540`):
  - the virtual wheel swept lock to lock to the axis' ends, 90 and 180 degrees past the lock, then turned into the stop in 0.3 s;
  - the lock published was 42 degrees in every record of the player's car, so the locks were 900 and 540;
  - 7,565 and 12,010 records were driven by the wheel, all in mode 2;
  - inside the lock the steering equalled the angle over half the lock to within 0.00002;
  - every record past the lock (2,141 and 5,511) had full steering.
- **The end stop**, in the same recordings:
  - it was zero in all 25,440 and 16,703 driven rows inside the lock, and present in the 4,998 and 6,735 rows past it, reaching its strength;
  - the force pushed back towards the lock in 4,565 of 4,759 and 6,502 of 6,502 rows more than 6 degrees past it; the others came during the fast turns back to the lock, where the damper takes the push away and the title's force or the slew limit remains;
  - the force, spring, damper and vibrations never exceeded 0.250016, no rise exceeded the slew limit, and every row outside a driven race (8,930 and 384) was zero.
- **The drawn wheel** (`whl12vd` at 2x, the Abarth in the cockpit view, the virtual wheel held at 0, 45, 90, 135, 180, 270, 360 and 420 degrees, then -135 and -270):
  - the angle passed was the wheel's over 0.75 (the global read at run time) at every hold;
  - in the 1:1 crops of the full captures the model at 360 degrees matched 0, the labelled spoke stood at 9 o'clock at 90 degrees and at 12 at 180, and mirrored at -270.
  - `whl11vt` ran with the title's angle for comparison.
- **Different locks.** All the cars the e2e scenarios reach have a 42-degree road-wheel lock, so the in-game runs covered the lock through the options (900 and 540 degrees). The unit tests cover 36 and 30 degrees (771 and 643), and a host test covers a 30-degree car with its end stop.
- **The landing checks:** the career at 3/2 and 240 Hz with the defaults, exit 0, main's four log errors, no grey or washed-out captures.
  - `whl10` ran beside a wheel cell, and its replay was 17% below `whl9`'s;
  - `whl11` and `whl12` alone passed against `whl9` (race 234 and 237 fps).
- **Unit tests:**
  - `fm4_wheel_mapping_tests`: the car's lock for 42, 36 and 30 degrees, full steering at 270 degrees for a 540-degree car, a fixed lock, the range's limit, bad values;
  - `fm4_wheel_ffb_tests`: the end stop's onset, knee, strength, direction, continuity, damper, bad values; through the governor within the cap and the slew limit, not scaled by the gain, zero when blocked or stale;
  - `fm4_wheel_host_tests`: a 30-degree car's lock from the published road-wheel lock, the end stop both ways within the cap and zero outside a driven race;
  - the launcher's tests (531 plans) and `--screenshot` of the Racing wheel tab.

**Not verified:**
- a real wheel ([checklist](../running.md#racing-wheel-test-checklist) steps 28 to 32), in particular whether the stop holds without oscillating on the R9. The virtual wheel follows its script, so it never pushes back;
- a car with a 36- or 30-degree lock in the game;
- the drawn wheel and the hands in other cars.

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

- **Title update 2 bugs in the base build.** The anti-cheat rule (stage 1) is input-based, with no rewards as its consequence, in any device class; stage 2 scales its threshold to the frame time for every device (the main session's decision of 29 October 2026). The countersteer bug of the 900-degree set, whose filters (wheel mode 2) the wheel now uses, did not show in stage 2's trace, but a long held slide was not traced.
- **The filters per wheel mode** were measured in stage 2 (mode 2: linear, a full-lock step in 50 ms); their readers are still not found statically.
- **Frame-rate torque.** The title computes its torque once a frame; stage 4 evaluates the title's torque model after each physics step and plays the points back about a frame behind (10 ms above 60 fps). A change of the model's state between the steps (the restored bytes) would show as a difference between the last step's value and the title's, which the trace's `phys` lines check.
- **MOZA SDK terms are unknown.** Stage 5 built the soft lock as a force feedback end stop (the main session's decision); the SDK's limit angle and any LEDs wait for the owner's decision after the R9 test. Whether the end stop holds firmly and without oscillation on a direct-drive base is open until then.
- **Pit House interaction.** The base's own angle limit, damper and filters can mask or double what the game sends.
- **Hardware verification needs the owner.** Every force feedback stage ends with the owner's script; nothing in the repository can judge the feel.

## Open questions for the owner

1. **Menus and layouts.** Should the game show its wheel options and layouts, if stage 1 allows the wheel class, or keep the controller screens?
2. **Soft lock rule.** Per car, matching the cockpit's drawn wheel; a fixed angle (for example 900 or 540 degrees); or Pit House's setting? Stage 5 sets each car's own (900 degrees for most road cars, from the game's road-wheel lock) with a fixed lock as an option; the owner may still choose otherwise.
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
