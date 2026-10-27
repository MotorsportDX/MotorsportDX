# Native racing wheel support

Status: research and design, 27 October 2026. Static analysis only: no runtime code changes and no game run. The project runs the base Disc 1 executable without a title update, so every address below is from the recompiled base `default.xex` (USA; Japan/Asia share the code).

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
- **The base build carries the wheel bugs that title update 2 fixed:** the zero credit bug, the anti-cheat rule that smooth wheel input triggered, and countersteering that treated 900 degrees as 270. Entering the title's wheel path as it is would bring those bugs with it. Smooth, high-resolution steering could also trip the anti-cheat rule through the pad path. This is the main design risk (see [Risks](#risks)).
- **The title's force feedback source is usable.** The steering torque is a float in `CCarDynamics`. The title reads it once per frame, together with the speed and the rear tyre slip. A hook at that point gives a native force feedback thread its input without the title's effect objects. The car physics runs at 359 Hz (`CarPhysicsUpdateHz`), so a later stage can sample the tyres at physics rate.
- **Recommendation:**
  - Read the wheel with SDL3's joystick and haptic API, which the runtime already ships (SDL 3.5.0). On Windows its haptic backend is DirectInput.
  - Keep the force feedback in a host thread with a safety governor.
  - Inject the steering, pedal and button values at the title's control level, once stage 1 has traced where that is.
  - Use the MOZA SDK only for the soft lock (its "limit angle"), and only once its licence terms are known.
- **Verdict: feasible.** The unknown that decides the effort is the injection point of the control floats (stage 1). Everything around it can be built and verified without the wheel.

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

Each stage ends with its own step gate ([e2e harness](../e2e/README.md)). Input stages still compare one native capture with the reference at 1:1, as every gate does.

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

- **Title update 2 bugs in the base build.** The wheel class may lose race credits and trip the anti-cheat rule. Smooth input in the pad class may trip the rule too. Stage 1 must settle this before stage 2 picks the device class. If the rule is input-based, the injection has to happen past the check, or the fix of title update 2 has to be reproduced as an integration fix.
- **The injection point is not known yet.** If the control floats are rebuilt from the pad object deep inside the physics, the hook moves there, which costs more reading.
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
