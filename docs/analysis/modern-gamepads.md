# Modern gamepads

Design note for better support of modern pads: Xbox One and Series controllers (with the Elite paddles), the PS5 DualSense and the DualShock 4, and the built-in controller of the ROG Xbox Ally X. Forza Motorsport 4 knows only the Xbox 360 pad: `XamInputGetState` and `XamInputSetState` carry two rumble motors, 8-bit triggers and no paddles. Stage 1 (25 October 2026) gathered evidence from the code and from logs and proposed options and defaults. Stage 2 (25 October 2026) implements them with today's behaviour as the default ([Stage 2](#stage-2)); the owner's hardware test through an export decides the final defaults. The owner's pads are an Xbox Series controller and the Ally X's built-in controller, so the PlayStation rows below come from the SDL source only.

## Summary

- **Drivers.** Xbox class pads (the Series controller, the Elite, the Ally X's built-in pad) go through SDL's XInput driver today: two motors, no trigger rumble, no Share button, no paddles. SDL's Windows HID backend skips every XInput device, so its HIDAPI Xbox drivers never see them, and its GameInput driver cannot start in this build (the Windows SDK's `gameinput.h` predates the API version SDL requires). Trigger rumble for Xbox One and Series pads is available through SDL's RAWINPUT driver with its WGI correlation, or the stand-alone WGI driver. PlayStation pads always go through HIDAPI, with rumble, light bar, gyro and touchpad. **Recommended:** keep SDL's defaults as the default; offer `rawinput` (trigger rumble) and `wgi` as options; no GameInput option until SDL is built with the GameInput SDK's header.
- **The title's own deadzones.** The title applies a radial deadzone with a linear, rescaled response to the sticks and a linear range to the triggers, from the player's profile: steering 0.24 to 0.95 of the stick, throttle 0.15 to 0.90 and brake 0.15 to 0.95 of the trigger. Measured through the diagnostic controller, the first 24% of the steering stick does nothing. The game's own steering deadzone setting is the right fix; a runtime anti-deadzone is offered as an option, off by default, because it adds up with a lowered in-game deadzone.
- **Rumble.** `Media/ControllerRumble.xml` maps the car's state to the motors: the right motor carries the engine near its limiter and front tyre slip, the left one rear tyre slip, gear changes and impacts, both the surface; the effects add up. Measured in normal driving: 0.05 to 0.2 of full strength. Trigger rumble can be derived (throttle from the left motor, brake from the right), off by default; slip-only trigger effects need a hook in the title's rumble update and are left for later. The title sends no rumble while the AI drives.
- **Latency.** The runtime adds about 1 ms at 60 and 120 Hz: SDL's update on the UI thread runs every millisecond, and the rest is the title polling once per frame. In 240 Hz races the UI thread is busy presenting and updates SDL only every 8.4 ms, so a new pad state waits 4 ms more on average. An SDL input thread of its own would fix that and is proposed as an opt-in, to be measured per driver (COM objects created on the UI thread may pull calls back to it); updating SDL on the guest's reading thread is allowed by SDL but runs driver code on guest threads, so it is not proposed.
- **Defaults.** Every stage 2 option defaults to today's behaviour. Candidates for the owner's test: `rawinput` with trigger rumble on the Series controller, rumble strength 1.5 to 2, the game's steering deadzone lowered, the input thread at 240 Hz; the Ally X with `default` and `rawinput` and the latency probe.

## Today's input path

The runtime's default backend is SDL (`input_backend=sdl`, [sdl_input_driver.cpp](../../work/runtime/src/input/sdl/sdl_input_driver.cpp)). SDL is 3.5.0, the SDK submodule at revision `8bf3b7215` (14 March 2026). The runtime sets no joystick hints, so SDL's Windows defaults apply.

- The driver opens every SDL gamepad and maps it to an `X_INPUT_STATE`: sticks as SDL reports them (the Y axes inverted), triggers shifted from SDL's 15 bits to 8, no deadzone and no response curve.
- Buttons: the Elite paddles map to Y, B, X and A; Misc1 (the Series Share button, the DualSense microphone button) and the DualSense touchpad click map to Guide, which is dropped unless `guide_button` is on.
- `SetDeviceVibration` forwards the two motors with `SDL_RumbleGamepad`, armed for SDL's maximum of about 65 s because XInput vibration holds until the next call.
- SDL's event watch buffers the pad events on the thread that posts them; `GetDeviceState` drains them under the driver's lock and returns the latest state (the buffering breaks a lock-order deadlock with SDL's joystick lock). `GetDeviceState` also queues an `SDL_PumpEvents` on the UI thread, but the UI thread already pumps on its own (see [Input latency](#input-latency)).
- The title polls all four users once per frame: `XamInputGetCapabilities`, `XamInputGetState` and `XamInputSetState` for each (see [The title's input code](#the-titles-input-code)).

## SDL drivers on Windows

SDL 3.5.0 tries its Windows joystick drivers in this order (`SDL_joystick.c`, `SDL_joystick_drivers[]`): HIDAPI, GameInput, RAWINPUT, Windows (XInput, then DirectInput), Windows.Gaming.Input (WGI), virtual. A driver skips a device that a driver earlier in the list reports as present (`SDL_JoystickHandledByAnotherDriver`, by vendor and product ID). The runtime's SDL build has all of them compiled in (`SDL_build_config.h`: `SDL_JOYSTICK_HIDAPI`, `_GAMEINPUT`, `_RAWINPUT`, `_XINPUT`, `_DINPUT`, `_WGI`; `HAVE_XINPUT_H`, `HAVE_WINDOWS_GAMING_INPUT_H`, `HAVE_GAMEINPUT_H`).

The hints that decide the driver, with their defaults on Windows desktop:

| Hint | Default | Note |
| --- | --- | --- |
| `SDL_JOYSTICK_HIDAPI` | on | HIDAPI drivers per pad family; the Xbox ones also default to on in the code (the header's "0 on Windows" is not implemented), but see below |
| `SDL_JOYSTICK_GAMEINPUT` | off | `SDL_GAMEINPUT_DEFAULT false` outside GDK |
| `SDL_JOYSTICK_RAWINPUT` | off | when on, RAWINPUT also uses WGI for correlation unless `SDL_JOYSTICK_WGI=0` |
| `SDL_JOYSTICK_WGI` | off | the stand-alone WGI driver |
| `SDL_XINPUT_ENABLED` | on | |
| `SDL_JOYSTICK_DIRECTINPUT` | on | |
| `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS` | off | pad events are dropped while no SDL window has keyboard focus |
| `SDL_JOYSTICK_ENHANCED_REPORTS` | on | PlayStation pads switch to their full report mode (rumble, effects, sensors) |

Three findings decide the matrix:

1. **Xbox pads never reach HIDAPI on Windows.** Every XInput device (wired Xbox 360, One and Series pads, Xbox pads over Bluetooth through `xinputhid`, and pads that present themselves as an Xbox 360 pad) has a HID interface whose path contains `&IG_`, and SDL's Windows HID backend skips those interfaces ("XInput devices don't get real HID reports and are better handled by the raw input driver", `src/hidapi/windows/hid.c`). So the HIDAPI Xbox drivers, the only SDL drivers that read the Elite paddles and the Share button of an Xbox pad, do not see them, whatever the hints.
2. **GameInput cannot start in this build.** `SDL_InitGameInput` loads `gameinput.dll` and calls `GameInputCreate`, then on Win32 requires GameInput API version 1 or newer (`#if GAMEINPUT_API_VERSION >= 1`, otherwise `hr = E_NOINTERFACE`, `SDL_gameinput.cpp`). `GAMEINPUT_API_VERSION` comes from `gameinput.h`, and the Windows SDK 10.0.26100 header that the build uses does not define it (that header is version 0; versions 1 and 2 ship in Microsoft's separate GameInput SDK package). With `SDL_JOYSTICK_GAMEINPUT=1` the driver fails its init, SDL keeps the other drivers, and the error stays in `SDL_GetError`. This PC has `GameInput.dll` and `GameInputRedist.dll` in System32, so only the header is missing.
3. **Trigger rumble on Windows comes only from WGI** (or GameInput). The XInput driver returns `SDL_Unsupported()` for trigger rumble (`WINDOWS_JoystickRumbleTriggers`); RAWINPUT drives the trigger motors through its WGI correlation (`RAWINPUT_JoystickRumbleTriggers`), and sets the trigger rumble capability only for pads whose IDs are of the Xbox One family (`ctx->is_xboxone`).

### The matrix

"Today" is SDL's defaults, which the runtime uses now. "RAWINPUT" is `SDL_JOYSTICK_RAWINPUT=1` (WGI correlation on by default). "WGI" is the stand-alone WGI driver, which takes XInput devices only with XInput and DirectInput off (`SDL_XINPUT_ENABLED=0`, `SDL_JOYSTICK_DIRECTINPUT=0`, `SDL_JOYSTICK_WGI=1`; with XInput on it skips them, and with DirectInput on, DirectInput would claim them first). "GameInput" is what SDL's GameInput driver would give if it started (it does not in this build).

| Pad | Today | RAWINPUT | WGI | GameInput (not in this build) |
| --- | --- | --- | --- | --- |
| Xbox Series, USB or Bluetooth | XInput driver: two motors, no trigger rumble, no Share, Guide through `XInputGetStateEx`, battery level | RAWINPUT driver: two motors (XInput correlation), trigger rumble through WGI once correlated, no Share | WGI driver: two motors and trigger rumble; axes read 0 without focus; no Share | two motors, trigger rumble, Share as Misc1; no LED, no sensors (compiled out) |
| Xbox Elite Series 2 | as Series; the paddles are not reported (the controller's own profile can map them to other buttons, which XInput then reports) | as Series, no paddles | as Series, no paddles | no paddles (SDL reads only GameInput's gamepad state) |
| DualSense, USB or Bluetooth | HIDAPI PS5 driver in every column: rumble emulated through the pad's effects report, light bar, player LEDs, gyro and accelerometer, touchpad, Misc1 (microphone button), touchpad click; no SDL trigger rumble; adaptive triggers only through raw effect bytes (`SDL_SendGamepadEffect`) | same | same | same (HIDAPI claims it first) |
| DualShock 4 | HIDAPI PS4 driver in every column: two motors, light bar, sensors, touchpad; no trigger motors | same | same | same |
| ROG Xbox Ally X built-in | XInput driver, as an Xbox 360 class pad: two motors, no trigger rumble | RAWINPUT driver; trigger rumble only if the pad reports Xbox One family IDs, which an Xbox 360 class pad does not | WGI driver, if WGI lists it as a gamepad | unknown |

Notes on the rows:

- **Duplicates.** None in any column: RAWINPUT stops the XInput driver from adding pads (`AddXInputDevice` returns early when RAWINPUT is on), DirectInput skips `IG_` paths and Xbox IDs while XInput or RAWINPUT is on, WGI skips what an earlier driver claims, and HIDAPI claims PlayStation pads before any other driver. Turning XInput off without turning DirectInput off would hand Xbox pads to DirectInput (one combined trigger axis, no rumble), which is why the WGI column needs both off.
- **RAWINPUT caveats.** It needs a pad input before the correlation exists ("Controller isn't correlated yet, try hitting a button first"), so the first rumble of a session can fail until a button or stick moves; in a race this is immediate. It is disabled in remote desktop sessions. Its source notes that XInput used together with raw input switched off a Series controller connected through the Xbox Wireless Adapter; the XInput correlation (`SDL_JOYSTICK_RAWINPUT_CORRELATE_XINPUT`) is on by default. The runtime's SDL driver comment already warns that RAWINPUT's correlation delays battery information.
- **Enhanced reports.** SDL switches Bluetooth PlayStation pads to their full report mode by default, which, per SDL's hint text, breaks DirectInput for other (non-SDL) applications until the pad is power-cycled. The runtime keeps SDL's default.
- **The Ally X.** SDL lists `0B05:1B4C` as the "ASUS ROG Ally X built-in controller", an Xbox 360 class pad (`controller_list.h`); this is the 2024 Ally X, and the owner's 2025 ROG Xbox Ally X reports the same IDs ([its test](#the-owners-rog-xbox-ally-x-test)). SDL also treats a Microsoft `045E:028E` pad on a system with the Ally's BMI320 accelerometer and gyroscope as the Ally (`IsROGAlly`) and fuses those sensors into the gamepad (`SDL_GAMECONTROLLER_SENSOR_FUSION`, on unless set to 0). Whether the Ally X's pad has trigger motors, and through which Windows API they are reachable, cannot be read from the source; the pad report logs what SDL sees, and on the owner's Ally X neither XInput nor RAWINPUT reports trigger rumble.
- **Which driver opened a pad.** SDL has no public call for it. Byte 14 of the joystick GUID is the driver's signature: `x` XInput, `r` RAWINPUT, `w` WGI, `g` GameInput, `h` HIDAPI, `v` virtual (`SDL_joystick.c`); DirectInput GUIDs have 0 there.

### Recommendation

- **Default: today's drivers** (SDL's defaults: XInput for Xbox class pads, HIDAPI for PlayStation pads). They are what the owner plays with now, and they need no correlation.
- **Option `rawinput`** for trigger rumble on Xbox One and Series pads: RAWINPUT with its WGI correlation. It is the candidate default for the owner's hardware test with the Series controller.
- **Option `wgi`** (WGI with XInput and DirectInput off) as a fallback if RAWINPUT's correlation misbehaves with a pad.
- **No `gameinput` option in stage 2.** It would need SDL rebuilt with the GameInput SDK's v1 or v2 header, which is a download (Microsoft's `Microsoft.GameInput` package) and needs the owner's approval; without it the option would silently fall back. With the header, GameInput would add the Share button; the system DLL is present on this PC, and an absent DLL falls back to the other drivers.
- Fallback in every case: a driver that does not start leaves SDL's others running, so a pad is never lost; the pad report logs the driver that took it.

## The title's input code

Addresses are those of the recompiled default.xex (USA and Japan/Asia share the code).

- `sub_82C778C8` is the title's `XInputGetState(user, state)`: it calls `XamInputGetState(user, 1, state)`. `sub_822A8F38` is its `XInputSetState(user, vibration)`, the XDK wrapper that swaps the two motors for an old wheel (kernel 2.0 before build 5611, device subtype 2) and calls `XamInputSetState`.
- `sub_822B5E60` is the input poll, once per frame. For each of the four users (5,632 bytes per user from the input owner + 652) it calls the capabilities check `sub_822B5E10`, `XInputGetState`, the conversion `sub_822B4130`, the edge tracker `sub_82379300` and the rumble update `sub_823A42A0`, then sends the user's motors: through `sub_822A8F38` on every poll, or through an overlapped set-state path (`sub_828C50E8`) when the user's rumble object has its flag set. The motors are sent as zero unless rumble is on in three places: a global flag (+28193 of the owner), the user's own flag (+40) and a per-user flag (+28194 + user). The e2e runs below saw only the first path.
- `sub_822B4130` converts the `XINPUT_STATE` into the title's pad object with ten deadzone floats in `f1` to `f10`, read from the user's block: left stick inner and outer, right stick inner and outer, left trigger inner and outer, right trigger inner and outer, and a second inner and outer pair for the right stick (used, through `sub_828C5410`, when a flag at +28 is set).
- **Sticks** (`sub_822B3F18`, once per stick): a radial, rescaled deadzone with a linear response. If x² + y² is at most inner², the stick is centred. Otherwise, with m = √(x² + y²), the output vector is (x, y) · (m − inner) / m / (outer − inner), each axis clamped to −1..1, and the magnitude (m − inner) / (outer − inner) clamped to 1. There is no response curve: the output grows linearly from the edge of the deadzone, without a jump.
- **Triggers** (`sub_82394A60`): t is the 8-bit trigger; below inner the output is 0, above it (t − inner) / (outer − inner), clamped to 0..1, times 255. A trigger above 0 also sets the title's digital trigger bit (1024 left, 2048 right) next to the buttons.

### The deadzones are the player's profile settings

The ten floats come from the profile, not from code constants. `Media/ProfileSchema/ForzaProfile.sch` defines `ControllerAdvancedOptions` (and the same set for `WheelAdvancedOptions` and `BristolAdvancedOptions`) with these defaults for the pad:

| Setting | Default | Raw value the conversion gets |
| --- | --- | --- |
| `SteeringAxisDeadzoneInside` | 0.24 | 7,864.08 (0.24 × 32,767) |
| `SteeringAxisDeadzoneOutside` | 0.95 | 31,128.7 |
| `ThrottleDeadzoneInside` / `Outside` | 0.15 / 0.90 | 38.25 / 229.5 (right trigger) |
| `BrakeDeadzoneInside` / `Outside` | 0.15 / 0.95 | 38.25 / 242.25 (left trigger) |
| `ClutchDeadzoneInside` / `Outside` | 0.10 / 1.00 | not on the default layout's axes |
| `HandbrakeDeadzoneInside` / `Outside` | 0.10 / 1.00 | not on the default layout's axes |
| `RumbleScale` | 1.00 | |
| `ControllerOptions/RumbleSetting` | 1 (on) | |

The game's own options screens change these per profile. Before a profile loads, the conversion gets XInput's constants instead: 8,689 inner and 32,767 outer for both sticks (`XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE`) and 30 and 254 for the triggers (`XINPUT_GAMEPAD_TRIGGER_THRESHOLD`). The right stick, which looks around, gets 17,378 inner (53%) and 31,128.7 outer once the profile loads.

### Measured

The `gp-sweep` run (see [Evidence](#evidence)) held each stick and trigger value for 0.5 s at a standstill in a Quick Race and logged the title's conversion (`FM4_INPUT_TRACE_FILE`). Selected rows, steering stick X with Y at 0 unless given:

| Raw X (Y) | Steering output |
| --- | --- |
| 0 to 7,849 | 0 (inside the 24% deadzone) |
| 8,000 | 0.0058 |
| 8,689 | 0.0355 |
| 10,000 | 0.0918 |
| 16,000 | 0.3497 |
| 24,000 | 0.6936 |
| 30,000 | 0.9515 |
| 32,000 and 32,767 | 1.0 |
| 6,000 (6,000) | x 0.0189 (magnitude 8,485 is past the deadzone) |
| 5,000 (5,000) | 0 |

Every row matches (m − 7,864.08) / (31,128.7 − 7,864.08) to four decimals. The triggers:

| Raw | 30 | 35 | 40 | 50 | 64 | 100 | 128 | 160 | 200 | 220 | 230 | 240 | 250 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Brake (left) | 0 | 0 | 2 | 14 | 32 | 77 | 112 | 152 | 202 | 227 | 239 | 252 | 255 |
| Throttle (right) | 0 | 0 | 2 | 15 | 34 | 82 | 119 | 162 | 215 | 242 | 255 | 255 | 255 |

### What this means for modern pads

- The first 24% of the steering stick's travel does nothing, and full lock comes at 95%. That is the title's design for the Xbox 360 pad: 24% is XInput's recommended left stick deadzone (7,849). On a pad whose stick centres precisely, the dead first quarter is the main reason small corrections do not steer.
- The right fix is the title's own setting: lower `SteeringAxisDeadzoneInside` in the game's controller options (for example to 0.05 or 0.10, above the pad's own drift). It is saved in the profile and applies to every pad, and the title keeps rescaling from the deadzone edge, so there is no jump.
- A runtime anti-deadzone (raw = inner + |x| · (32,767 − inner) / 32,767 along the stick's direction) would give the same result without touching the profile, but only while the profile keeps its 0.24: if the player also lowers the in-game deadzone, the two add up and the stick jumps at the centre. So it is an option for players who do not want to change the profile, off by default, with its inner value tied to the profile's (7,864 for the default 0.24).
- The runtime's own inner deadzone is for pads that drift: with the in-game deadzone lowered, a worn stick's rest value can steer. It defaults to 0, so today's behaviour is unchanged.
- The trigger ranges (15% to 90% throttle, 15% to 95% brake) are again profile settings. A runtime trigger range is only needed for pads whose triggers do not reach full travel (the 8-bit value stays below 230 for the throttle), and defaults to the identity.

## The title's rumble

### What the motors carry

The rumble is data: `Game:\Media\ControllerRumble.xml` (its path is used by `sub_828C7698` and `sub_828C7918`, its effect names by `sub_828C6A28` and `sub_828C4808`) defines, for a `<Controller>` and for a `<Wheel>`, the share of each effect on the left motor (the pad's large, low-frequency motor) and the right motor (the small, high-frequency one). Most effects are a curve of three points (x, y) over a quantity of the car; the `On...` effects are pulses. The pad's values (`<Controller>`):

| Effect | Left motor | Right motor |
| --- | --- | --- |
| `EngineNormRPM` (engine speed, 0 to 1 of the limiter) | none | 0 at 0.6, 0.125 at 0.8, 0.15 at 1.0 |
| `TireTractionRear` (rear tyre slip) | 0 at 0.9, 0.125 at 1.5, 0.175 at 3 | 0 at 1, 0.05 at 2, 0.075 at 3 |
| `TireTractionFront` (front tyre slip) | 0 at 1, 0.05 at 2, 0.075 at 3 | 0 at 0.7, 0.125 at 1.1, 0.175 at 3 |
| `SurfaceBumpiness` (kerbs, grass, gravel) | 0.175 at 0.028, 0.25 at 0.825 | 0.2 at 0.042, 0.401 at 0.825 |
| `CarToWorldCollision` (walls) | 0 at 20, 0.45 at 70, 1.0 at 350 | 0.3 at 10, 0.45 at 75 |
| `CarToCarCollision` | 0.5 at 1, 0.7 at 500 | 0.07 at 1, 0.175 at 500 |
| `CarToObjectCollision` (cones, signs) | 0.15 at 0.001, 0.25 at 10.75 | 0.225 at 0.001, 0.275 at 0.4 |
| `OnShift` (a gear change) | 0.3 | 0 |
| `OnEngineSputter` | 0.3 | 0.3 |
| `TransmissionRPM`, `TireTraction`, `AirFriction`, `OnBackfire` | 0 | 0 |

The three collision effects have a `...Time` curve as well (how long the collision rumble lasts). The wheel's table has the same effects at about three times the strength. So the right motor is the engine near the limiter and the front tyres; the left motor is the rear tyres, gear changes and impacts; both carry the surface.

### Measured

The `gp-sweep` run's scripted driving (a human driver: no `engageaionstart`; the Abarth 131 of the Quick Race defaults, 60 Hz) logged the motors the title sends (`rumble` lines, user 0). Time-weighted means and maxima per second, in units of 65,535:

| Situation | Left | Right |
| --- | --- | --- |
| Stationary, engine idling, sticks and brake below 160 | 0 | 0 |
| Countdown | 0 | a short rise to 5,458 (0.083) |
| Full throttle from a standstill (rear wheelspin) | 11,468 (0.175, the rear slip maximum) | 4,915 (0.075, its right share) and up to 13,128 |
| Brake held at a standstill (the car reverses with wheelspin) | 11,468 | up to 13,128 (0.2) |
| Gear changes under full throttle | pulses of 19,660 (0.3) | 0 to 3,900 |
| Braking hard at speed | 0 to 4,000 between shifts | up to 9,209 (front slip) |
| Full lock with throttle, off the track into the barrier | up to 39,029 (0.6) | up to 43,088 (0.66) |
| Coasting, no input | 0 | 0 to 1,500 |

The values match the table: 11,468 is 0.175 and 4,915 is 0.075 of 65,535, and 19,660 is 0.3. The right motor's 13,128 is 0.2, the sum of the engine at 0.8 of its limiter (0.125) and the rear slip's right share (0.075): the effects add up. In normal driving the motors stay between 0.05 and 0.2 of full strength; only impacts and the surface off the track go higher.

With the AI driving the player's car (`engageaionstart`, the e2e default) the title sends no rumble at all: the `gp-ai240` and `gp-ai60` runs logged a single all-zero call in 90 s of race. Rumble tests need a human-driven run.

The title calls `XInputSetState` for every pad on every poll, also when the values do not change. SDL ignores a repeat of the same two values except to extend its timer (`SDL_RumbleJoystick`), so the calls cost nothing at the pad.

### Deriving trigger rumble

The two motors mix several effects, so trigger rumble derived from them cannot isolate tyre slip. The closest derivation, in the spirit of the impulse triggers of later Forza games:

- right trigger (throttle) from the left motor, which carries rear slip, scaled by how far the throttle is pressed past the title's own deadzone;
- left trigger (brake) from the right motor, which carries front slip (locking under braking), scaled the same way by the brake.

So wheelspin buzzes the throttle and a locking front axle buzzes the brake. What comes along: gear changes kick the throttle trigger, impacts and kerbs buzz whichever trigger is pressed, and the engine near its limiter buzzes the brake if it is pressed then. A trigger that is not pressed stays still. Separating slip from the rest needs the mixer's per-effect values (`TireTractionFront`, `TireTractionRear`), which means a hook in the title's rumble update; that is left for later.

On Windows only the RAWINPUT and WGI drivers give trigger rumble for Xbox class pads (see the matrix), so the derived trigger rumble does nothing on today's XInput driver. DualSense trigger effects are not trigger rumble in SDL: they are raw bytes of the pad's effects report (`SDL_SendGamepadEffect`, report `0x02` over USB, `0x31` over Bluetooth, an 11-byte effect per trigger at offsets 10 and 21 of `DS5EffectsState_t` with enable bits in its first byte). SDL does not interpret them, and the owner has no DualSense to test them on, so they are left for later.

## Input latency

### How the state reaches the title

SDL updates its joysticks in `SDL_UpdateJoysticks`, which the UI thread's event loop calls: `SDL_WaitEvent` polls every millisecond while a joystick is open (`EVENT_POLL_INTERVAL_NS`, with SDL's 1 ms timer resolution) and every 3 s otherwise. The update reads each driver (XInput state, HID reports) and posts axis and button events; the runtime's event watch buffers them, and the next `GetDeviceState` of the title drains them. So a new pad state waits twice: for SDL's next update on the UI thread, then for the title's next poll. The title polls once per frame (twice per frame in the 3-2-1 countdown), so the second wait is half a frame on average and is the title's own.

### Measured

`input_latency_probe` attaches a virtual SDL gamepad (SDL's virtual joystick driver) whose right stick X a thread of its own flips every 20 to 49 ms, out of step with SDL and the title. For each flip it logs the wait until the SDL update that sent it (from the probe's set time to the event's timestamp), the wait from there to the title's read, and the gaps between SDL updates (the virtual pad's `Update` callback). The value (256) is far inside every stick deadzone. 5 s windows, races driven by the AI on this PC (RTX 5070 Ti laptop, 240 Hz panel), launcher defaults except the frame rate:

| Race frame rate | SDL update gaps p50 / p99 (ms) | New value to SDL update, mean / p99 | SDL update to the title's read, mean / p99 | Age at the read, mean / p99 | Title's reads |
| --- | --- | --- | --- | --- | --- |
| 60 (`gp-ai60`) | 1.0 / 2.3 | 0.8 / 2.4 | 7.5 to 8.3 / 16.7 | 8.2 to 9.1 / 16.9 | every 16.67 ms |
| 120 (`gp-ai120`) | 0.1 to 0.6 / 2.6 to 4.0 | 0.8 to 1.1 / 3.3 | 3.4 to 4.6 / 8.6 | 4.5 to 5.3 / 9.3 | every 8.33 ms |
| 240 (`gp-ai240`) | 0.0 / 8.4 (pairs, about every 8.4 ms) | 4.0 to 4.5 / 8.3 | 1.5 to 2.4 / 4.3 to 4.9 | 5.8 to 6.7 / 10.6 to 12.7 | every 4.2 ms |

The menus behave like the 60 Hz race at every frame rate (the title runs them at 60). At 60 and 120 Hz the runtime adds about 1 ms; the rest is the title's own polling. In the 240 Hz race the UI thread updates SDL only about every 8.4 ms, in pairs (probably its own poll and the update `GetDeviceState` queues), because it spends the time between them painting: in FM4 the UI thread always paints ([status](../status/current.md), the achievement toast keeps an ImGui dialog registered), and on this PC it presents 120 times a second at 240 Hz, the display's cap ([D3D-level HLE, 17.23](d3d-hle-n4.md#1723-vsync-24-october-2026)). A new state then waits 4 ms on average and up to 8 ms before the title can see it, about one more frame at 240 fps. Some 5 s windows have no values: SDL drops pad events while the game window has no keyboard focus (see the hint table), which the probe now avoids with `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS`.

The probe stands in for a pad that reports at random times. A real pad adds its own report interval and the XInput or HID read in SDL's update, which these numbers do not include. The runtime's own XInput backend (`input_backend=xinput`) reads `XInputGetState` inside the title's call, so it has no SDL wait at all, but it has none of SDL's pad support beyond XInput.

### Updating SDL off the UI thread

SDL documents `SDL_UpdateJoysticks` and `SDL_UpdateGamepads` as safe to call from any thread; they take SDL's joystick lock. Two ways would take the UI thread out of the path:

- **On the reading thread**, in `GetDeviceState` before the drain. The lock order stays that of the UI thread (SDL's joystick lock, then the event buffer's mutex in `HandleEvent`), provided the call is made before `DrainAndLock` and never under `controllers_mutex_` (which `SetDeviceVibration` and the device-added path hold while they call SDL). But it runs every driver's update and device detection on a guest thread: DirectInput, RAWINPUT's WGI correlation and the WGI driver make COM and WinRT calls, and the guest threads never initialise COM. The title also reads four users per frame, so the call needs a rate limit.
- **On an input thread of its own**, which initialises COM (multithreaded) and calls `SDL_UpdateGamepads` every millisecond, with `SDL_HINT_AUTO_UPDATE_JOYSTICKS=0` so the UI thread stops updating joysticks. The state is then at most about 1 ms old at every frame rate, no guest thread runs driver code, and the UI thread's event loop no longer wakes every millisecond. The probe measures it directly.

Both share one open question. SDL creates its driver objects during `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` on the UI thread, which the runtime initialises as a single-threaded COM apartment (`CoInitializeEx(COINIT_APARTMENTTHREADED)` in [windowed_app_main_sdl.cpp](../../work/runtime/src/ui/windowed_app_main_sdl.cpp)), and SDL wants its subsystems initialised on the main thread. XInput and HIDAPI make no COM calls, so for them the update is plain I/O on any thread. DirectInput and WGI objects called from another thread are fine where they are free-threaded or agile, and would otherwise be marshalled to the busy UI thread, which would bring the wait back. The input thread is the safer of the two, since no guest thread runs driver code; it is worth having only where the UI thread is busy (here, 240 Hz races), while at 60 and 120 Hz it would save under 1 ms. It is proposed as an opt-in below, to be measured with the probe and a real pad under each driver option, here and on the Ally X, whose 120 Hz panel and slower CPU may behave differently from this PC.

## Proposed options and defaults (stage 2)

Every default keeps today's behaviour. The owner's hardware test through an export decides the final defaults; the candidates for that test are in the last column.

| Option (runtime cvar, launcher Controller section) | Values | Default | Candidate for the hardware test |
| --- | --- | --- | --- |
| SDL joystick driver, set as hints before `SDL_INIT_GAMEPAD` | `default` (SDL's own: XInput for Xbox class pads, HIDAPI for PlayStation), `rawinput` (`SDL_JOYSTICK_RAWINPUT=1`, with its WGI correlation), `wgi` (`SDL_JOYSTICK_WGI=1`, `SDL_XINPUT_ENABLED=0`, `SDL_JOYSTICK_DIRECTINPUT=0`) | `default` | `rawinput` with the Series controller, for trigger rumble; `default` and `rawinput` on the Ally X |
| Paddles 1 to 4, Misc1, touchpad click | an Xbox 360 button, Guide, or none | today's: Y, B, X, A; Guide; Guide | as today |
| Stick inner deadzone (radial, rescaled), both sticks | 0 to 0.5 | 0 | 0 (raise only for a drifting stick) |
| Stick outer edge | 0.5 to 1 | 1 | 1 |
| Steering anti-deadzone (left stick only, radial) | 0 to 0.5 | 0 | 0, with the game's steering deadzone lowered instead; 0.24 only with the game's default |
| Stick response curve (exponent on the magnitude) | 0.5 to 3 | 1 (linear) | 1 |
| Trigger inner and outer range | 0 to 1 | 0 and 1 | as default |
| Rumble strength (both motors) | 0 to 2 | 1 (the launchers: 1.5 since the [Ally X test](#the-owners-rog-xbox-ally-x-test)) | 1.5 to 2: the title's driving rumble is 0.05 to 0.2 of full |
| Trigger rumble, derived from the motors | off, on | off | on, with `rawinput` |
| Trigger rumble strength | 0 to 2 | 1 | 1 |
| SDL input thread (1 ms updates off the UI thread) | off, on | off | on at 240 Hz; compare on the Ally X with `input_latency_probe` |

Details:

- **Driver option.** Named after what it does rather than "auto/GameInput/XInput": GameInput cannot start in this build, and "auto" is SDL's default. A driver that fails to start leaves the others running, so no choice can lose a pad. The pad report says which driver took it.
- **Mapping.** On Windows the paddles and Misc1 of Xbox pads never arrive (no SDL driver in this build reads them there), so the mapping matters for PlayStation pads (the DualSense's microphone button is Misc1, its touchpad click the touchpad button) and other HIDAPI pads; an Elite's own profile can still map its paddles to buttons that XInput reports. A Guide target stays subject to `guide_button`.
- **Shaping.** One helper for the physical drivers (`sdl` and `xinput`), never for the diagnostic controller or the keyboard and mouse driver, so e2e runs keep their exact values. Order: inner deadzone and outer edge on the stick's magnitude, rescaled from the deadzone's edge (as the title does, so there is no jump), then the curve, then the anti-deadzone, which lifts any non-zero magnitude into the title's own deadzone; triggers: inner and outer range on the 8-bit value. With the defaults it returns its input bit for bit. Unit tests cover the identity, the radial rescale, the anti-deadzone fed through the title's own formula (with the profile's 0.24, any stick past the runtime's deadzone must give the title a non-zero, rising steering output, checked against this note's measured transfer) and the clamps.
- **Rumble.** The strength multiplies both motors and clamps at 65,535. Trigger rumble goes through `SDL_RumbleGamepadTriggers` with the same long duration as the motors, only on pads that report `SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN`: right trigger from the left motor and the throttle, left trigger from the right motor and the brake (see [Deriving trigger rumble](#deriving-trigger-rumble)). It is tested without a pad through a virtual SDL gamepad, whose `RumbleTriggers` callback records what the driver sends.
- **Pad report.** One log line per pad at connect: name, SDL gamepad type, vendor and product ID, the driver (GUID byte 14), connection state and path, and the capabilities: rumble, trigger rumble, RGB, mono and player LEDs, gyro and accelerometer, paddles, Misc1, touchpads and the battery. The owner's logs then show what each pad exposes under each driver option.
- **Input thread.** As described in [Updating SDL off the UI thread](#updating-sdl-off-the-ui-thread). `input_latency_probe` stays as the way to measure it.
- **Launcher.** A Controller section in both launchers (the GUI launcher and `Build.ps1 -Action run`), as recent options such as `aa` were added, with `launcher_tests`; [running.md](../running.md) gains the options and a note on the game's own controller settings (the steering deadzone above all), and [status](../status/current.md) the verification.

## Stage 2

Stage 2 (25 October 2026) implements the options of the table above, every default unchanged, as the main session decided: no GameInput option, the anti-deadzone, trigger rumble and the input thread off by default, trigger rumble only on pads that report `SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN`, and a Guide target still subject to `guide_button`. How to use them is in [running.md](../running.md#controller).

### What changed

- **Driver hints.** `sdl_joystick_driver` (`default`, `rawinput`, `wgi`) sets SDL's hints before `SDL_INIT_GAMEPAD` ([sdl_input_driver.cpp](../../work/runtime/src/input/sdl/sdl_input_driver.cpp) `ApplyDriverHints`); `default` sets none.
- **Mapping.** `pad_misc1`, `pad_right_paddle1`, `pad_left_paddle1`, `pad_right_paddle2`, `pad_left_paddle2` and `pad_touchpad` name the guest button of SDL's buttons 15 to 20, defaulting to the earlier table (Guide, Y, B, X, A, Guide). The guest buttons are now the union of the held SDL buttons' targets, so a guest button held through two SDL buttons stays held until both are released.
- **Shaping.** One header-only helper ([pad_shaping.h](../../work/runtime/include/rex/input/pad_shaping.h)) shapes the sticks and triggers that the `sdl` and `xinput` backends return, from the `pad_*` cvars ([pad_settings.cpp](../../work/runtime/src/input/pad_settings.cpp)); at the defaults it returns its input bit for bit. The diagnostic controller and the keyboard and mouse driver do not use it.
- **Rumble.** `pad_rumble_strength` multiplies both motors in both backends. `pad_trigger_rumble` derives the trigger motors as in [Deriving trigger rumble](#deriving-trigger-rumble), from the triggers the guest last read for the pad's user (`InputSystem::SetState` passes them through the new `SetDeviceUserState`). It sends zero when the motors are zero, while an overlay takes the input, without window focus (SDL stops reading the pads then, so the triggers would go stale; not when background events are on, as for the probe), and with the option turned off.
- **Rumble at exit.** Closing the game window ends the process in `ReXApp::OnClosing` (`std::_Exit` after `TerminateTitle`) before the input drivers' own `OnClosing` runs, so the SDL driver's teardown never stopped anything: a pad kept the title's last motors, and with trigger rumble its trigger motors. `InputDriver::StopVibration`, called there before `TerminateTitle`, zeroes every SDL pad's motors and trigger motors and ignores later vibration calls (also a call that was waiting for the pads' lock). The XInput backend does not implement it yet.
- **Pad report.** One `SDL pad report:` line per pad at connect.
- **Input thread.** `input_sdl_thread` starts a thread that initialises COM as multithreaded and calls `SDL_UpdateGamepads` every millisecond, with `SDL_HINT_AUTO_UPDATE_JOYSTICKS=0`; it is joined before the pads close. `GetDeviceState` then queues no `SDL_PumpEvents` on the UI thread. The guest's threads read the thread's state from an atomic flag.
- **Probe.** The latency probe's virtual pad has rumble and trigger rumble callbacks and logs what it received, so the rumble paths can be checked without a physical pad.
- **Launchers.** `-Pad` knobs in `Build.ps1` and `pipeline.py` (`PAD_KNOBS`, carried to the GUI launcher in `launcher.json`), the GUI launcher's Controller section on its Controls page, and three option sets in `launcher_tests`.

### Where SDL reads each driver

The SDL source answers the COM question of [Updating SDL off the UI thread](#updating-sdl-off-the-ui-thread) for the two drivers that matter here:

- **XInput** (`default`): `SDL_UpdateJoysticks` polls `XInputGetState`, so with the input thread the pads are read there, with no COM.
- **RAWINPUT** (`rawinput`): the pads' HID reports do not wait for any update at all. SDL's joystick thread (`SDL_JoystickThread` in `SDL_windowsjoystick.c`, on by default through `SDL_HINT_JOYSTICK_THREAD`) owns the message window that RAWINPUT registers for raw input, and handles each `WM_INPUT` as it arrives (`RAWINPUT_WindowProc`), posting the sticks and buttons from that thread. Only the correlated parts, the separate triggers and the trigger motors through WGI and XInput, are read in `SDL_UpdateJoysticks` (`RAWINPUT_UpdateOtherAPIs`). Windows declares WGI's `Gamepad` class agile, so calling it from the input thread should not be marshalled to the UI thread; that part needs a physical Xbox pad to measure (see [Verification](#verification)).

### Verification

Build: `I:\FM4-build-usa`, snapshot `snapshots\gp-s2` of 25 October 2026, from commit `4154562` (`build-result.json`: that head, not dirty), fm4_rex.exe sha256 `6b7a5fa4...`, rexruntime.dll `5a6ffdb8...`; the rumble at exit on `snapshots\gp-s4`, from commit `c795512` (not dirty). The CPU tests: `fm4_pad_shaping_tests` passes (the identity at the defaults, the radial rescale, the curve, the trigger range, the clamps, the anti-deadzone through the title's measured steering transfer, the derived trigger rumble), and `launcher_tests` passes with the three new `-Pad` option sets (270 plans). This PC has no physical pad connected, so every pad in these runs is the probe's virtual pad (driver `virtual` in its pad report).

**Latency.** `input_latency_probe` runs with the AI driving (`engageaionstart`), 45 s of race, launcher defaults otherwise; the values are medians over the race's nine 5 s windows, in ms:

| Race frame rate | `driver` | `thread` | SDL update gaps mean / p99 | New value to update mean / p99 | Update to the title's read mean / p99 | Age at the read mean / p99 | Run |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 240 | `default` | off | 2.89 / 8.44 | 4.22 / 8.24 | 2.35 / 5.11 | 6.56 / 12.22 | `gp2-def240-t0` |
| 240 | `default` | on | 1.39 / 1.93 | 0.85 / 1.74 | 1.98 / 4.31 | 2.79 / 5.64 | `gp2-def240-t1` |
| 240 | `rawinput` | off | 2.89 / 8.44 | 4.28 / 8.23 | 2.38 / 4.97 | 6.77 / 12.00 | `gp2-raw240-t0` |
| 240 | `rawinput` | on | 1.42 / 1.91 | 0.85 / 1.66 | 1.87 / 4.56 | 2.73 / 5.71 | `gp2-raw240-t1` |
| 120 | `default` | off | 0.68 / 2.37 | 0.74 / 2.06 | 3.86 / 8.67 | 4.53 / 8.98 | `gp2-def120-t0` |
| 120 | `default` | on | 1.42 / 1.94 | 0.83 / 1.70 | 3.99 / 8.22 | 4.77 / 9.29 | `gp2-def120-t1` |
| 120 | `rawinput` | on | 1.42 / 1.91 | 0.86 / 1.90 | 3.64 / 8.17 | 4.50 / 9.15 | `gp2-raw120-t1` |
| 60 | `default` | off | 0.84 / 2.36 | 0.78 / 2.26 | 8.05 / 16.85 | 8.83 / 17.27 | `gp2-def60-t0` |
| 60 | `default` | on | 1.42 / 1.86 | 0.81 / 1.57 | 8.27 / 16.26 | 9.08 / 17.10 | `gp2-def60-t1` |
| 60 | `rawinput` | on | 1.43 / 1.82 | 0.80 / 1.69 | 7.87 / 16.29 | 8.66 / 17.33 | `gp2-raw60-t1` |
| 60 | `wgi` | on | 1.42 / 1.85 | 0.75 / 1.63 | 7.83 / 16.31 | 8.60 / 17.38 | `gp2-wgi60-t1` |

- In 240 Hz races the thread removes the UI thread's wait: the pads are updated every 1.4 ms instead of in pairs every 8.4 ms, a new state waits 0.85 ms instead of 4.2 ms for its update, and its age at the title's read falls from 6.6 to 2.8 ms on average and from 12.2 to 5.6 ms at the 99th percentile. The thread off reproduces stage 1's 240 Hz numbers (`gp-ai240`).
- At 60 and 120 Hz the UI thread keeps up on this PC, and the thread changes nothing beyond the windows' spread: the update interval grows from about 0.7 to 0.8 ms to 1.4 ms (`SDL_DelayNS` of 1 ms plus the update), and the wait for an update stays at about 0.8 ms on average.
- `rawinput` and `wgi` behave like `default` with the thread on: their updates take no longer, so nothing was marshalled to the UI thread. With only the virtual pad, though, neither RAWINPUT's WGI correlation nor the WGI driver had a device to read; the owner's Series controller and the Ally X runs of the [checklist](../running.md#controller-test-checklist) are needed for that.
- The thread is kept as an opt-in for every driver; nothing here argues for turning it off for one.

**Trigger rumble** through the virtual pad's `RumbleTriggers` callback, human-driven runs at 60 Hz with the stage 1 driving script (full throttle, brake held, full lock into the barrier):

- `gp2-trig60`, `pad_trigger_rumble=true`, `pad_rumble_strength=1.5`, the input thread on: the motors arrive scaled by 1.5 (11,468 as 17,202, 19,660 as 29,490, impacts clamped at 65,535). The right trigger follows the left motor and the throttle: up to 11,468 under rear wheelspin at full throttle and 19,660 on gear changes, the unscaled motor times the trigger's pressure. The left trigger follows the right motor and the brake: up to 13,361 with the brake held. In the last window, with both motors and triggers at rest, trigger rumble is back at 0.
- `gp2-trigoff60`, the defaults: the motors arrive unscaled (11,468, 4,915, 19,660), and no trigger rumble reaches the pad in any window.

**Rumble at exit**, the window closed normally (`CloseMainWindow`, WM_CLOSE) while a run goes on; the e2e harness itself kills the game, which no code can answer. On `gp-s2` the driver's teardown never ran. With `StopVibration` after `TerminateTitle` (commit `8be86aa`, snapshot `gp-s3`), a window closed during the boot hung after "Window closing" until the harness killed it, while `gp-s2` closed at the same point hard-exited 0.2 s later (`gp2-closeboot-s2`): a guest thread that `TerminateTitle` stops can still own a lock that the stop then waits for (which one was not identified). With `StopVibration` before `TerminateTitle` (commit `c795512`, snapshot `gp-s4`):

- `gp2-closedrive`, closed 8 s into `gp2-trig60`'s driving: in the seconds before the close the motors reached 29,490 (the left one, 1.5 times 19,660) and the brake trigger 7,562; then "SDL input: rumble stopped at exit" with both at 0, and the hard exit 0.2 s later.
- `gp2-close60` (2 s into full throttle, the motors still at 0) and `gp2-closeboot` (in the splash videos): the same stop and exit, without a hang.

**The game's steering deadzone.** `gp2-menu` (e2e steps through the career home menu) shows the path on screen: MY PROFILE > OPTIONS lists CONTROLLER, HUD, AUDIO, VIDEO, MULTISCREEN and HEAD TRACKING; CONTROLLER opens the CONTROLLER LAYOUT screen (LAYOUT, D PAD STEERING, VIBRATION, SWITCH GEAR UP/DOWN), whose footer has X ADVANCED; X opens ADVANCED OPTIONS with STEERING AXIS DEADZONE INSIDE highlighted at 24, then OUTSIDE 95, ACCELERATION 15 and 90, DECELERATION 15 and 95, CLUTCH and HANDBRAKE 10 and 100, and VIBRATION SCALE 100 (its slider full), saved with A. The values are the profile's defaults of [the table above](#the-deadzones-are-the-players-profile-settings) in percent.

**Smoke gate**, fast route, 240 Hz, against `s1b-rtv1x` (the FSR session's stage 1 binary): `career` PASS (`gp2s`). `photo` failed at the fast route's boot step: the main menu's `push UI_Homespace` came 0.84 s after the title scene, during the previous step's 2.5 s wait, so the check never saw it after a press, and the next presses went into the career. The same failure is in earlier runs of other sessions (`n3p-rov-a`, `n4pth2-fnu`, `n5b-freeze-1x-rov`, `n6z-nat1x-240-photo`, `n6z-nat2x-240-photo`). `photo_view` failed with it, for want of a saved photo. Re-run (`gp2s2`): both reached their goal, WARN on capture statistics only (race 212.6 fps against 207.9, p99 6.89 against 6.83 ms; the photo loads).

Not covered here: real pads (the owner's [test checklist](../running.md#controller-test-checklist); the Ally X's results follow), RAWINPUT's WGI correlation and the WGI driver with a device, the focus-loss stop of trigger rumble (the probe reads the pads in the background, so it does not apply there), the XInput backend's options and its rumble at exit, and PlayStation pads.

### The owner's ROG Xbox Ally X test

The owner ran the checklist's four Ally X profiles on the built-in pad on 25 October 2026, from an export of `f4891c4` (stage 2 in main): A `default`, B `rawinput`, C `default` with the input thread, D `rawinput` with the input thread, each with `--input_latency_probe=true`, the launcher defaults otherwise (the native path, the race limit at 117 fps under the 120 Hz VRR panel). The logs are the export's `runtime.2.log`, `runtime.1.log` and `runtime.log` with the four perf traces.

**The pad report.** The built-in pad is `0B05:1B4C`, SDL type `xbox360`, so this ROG Xbox Ally X has the same IDs as the 2024 ROG Ally X in SDL's list (see [the matrix notes](#the-matrix)):

- `default`: `name="XInput Controller #1" driver=xinput rumble=1 trigger_rumble=0`.
- `rawinput`: `name="ASUSTeK Computer Inc." driver=rawinput rumble=1 trigger_rumble=0`, path `\\?\HID#VID_0B05&PID_1B4C&MI_05&IG_00...`.
- Both: no gyroscope or accelerometer (SDL's Ally sensor fusion is for a `045E:028E` pad), no paddles, Misc1 or touchpad, `connection=unknown`, battery 10.

So neither XInput nor RAWINPUT gives trigger rumble on the Ally X: as an Xbox 360 class pad it is not of the Xbox One family that RAWINPUT's trigger rumble needs. Its triggers do have motors: they vibrate in other games, the owner reports. GameInput is the one route left to reach them, and it is not in this build ([Left for later](#left-for-later)). `pad_trigger_rumble` does nothing there; `pad_rumble_strength` applies to its two motors.

**Latency**, the probe's 5 s windows of the races (the title's read gap p50 under 12 ms), medians over the windows, in ms:

| Profile | `driver` | `thread` | Race windows | SDL update gaps mean / p99 | New value to update mean | Update to the title's read mean | Age at the read mean / p99 | Title's read gap p50 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | `default` | off | 19 | 0.73 / 2.10 | 0.82 | 4.83 | 5.72 / 13.13 | 9.79 |
| B | `rawinput` | off | 19 | 0.68 / 2.12 | 0.75 | 4.75 | 5.52 / 12.63 | 9.53 |
| C | `default` | on | 19 | 1.55 / 2.26 | 0.86 | 5.01 | 5.85 / 12.70 | 9.58 |
| D | `rawinput` | on | 15 | 1.57 / 2.22 | 0.99 | 5.44 | 6.52 / 13.98 | 10.19 |

- The races ran at about 100 fps (read gaps of 9.5 to 10.2 ms), below the 117 fps limit. At that rate the UI thread keeps up on the Ally X as on the main PC at 60 and 120 Hz: it updates the pads every 0.7 ms, and a new state waits 0.8 ms for its update.
- The input thread does not help there: its 1 ms sleep gives 1.55 ms between updates, longer than the UI thread's, and the age at the read is the same or 0.3 to 1 ms higher. It stays off by default; it is worth having only where the UI thread falls behind, as in 240 fps races on the main PC.
- `rawinput` reads the pad as fast as `default` (5.52 against 5.72 ms with the thread off).
- In the menus (60 fps) the age at the read is 8.6 to 9.5 ms, as on the main PC.
- The probe's virtual pad was the second pad (guest user 1), so it received no rumble in these runs; the title rumbles user 0, the built-in pad.

**Exit and warnings.** Every run logged "SDL input: rumble stopped at exit" when the window closed. Every run also warned `SDL GameControllerDB: file 'gamecontrollerdb.txt' does not exist.`: the default name of `hid_mappings_file`, which no export ships. A missing file under the default name is now logged as information; the warning stays for a file the user named (a non-default source of the cvar).

**The owner's notes.** The pad rumbled in the races, and rumble strength 1.5 felt best. The owner set the game's STEERING AXIS DEADZONE INSIDE to 10 and kept it.

**Decisions** (the main session, 25 October 2026):

- The launchers default to rumble strength 1.5 (`-Pad` knob `rumble`, `PAD_KNOBS` with `runtime_default` 1, in both launchers); the runtime cvar `pad_rumble_strength` keeps 1, the title's own scale.
- [running.md](../running.md#the-games-steering-deadzone) recommends 10 as the starting value of the game's steering deadzone, a step higher if the car steers by itself.
- The input thread stays off by default: no gain on the Ally X at its race rate, a gain only in 240 fps races on the main PC.
- Trigger rumble on the Ally X waits for GameInput, which needs the GameInput SDK's header and the owner's approval.

## Left for later

- **PlayStation glyphs.** The title draws Xbox button glyphs. A texture replacement set per pad type ([texture replacement](texture-replacement.md)) chosen from the pad report is the likely route.
- **Gyro steering.** The DualSense reports its gyroscope through HIDAPI, and SDL fuses the Ally's own IMU into an Xbox 360 class pad on the Ally (`IsROGAlly`). Mapping it onto the steering axis (with a centre reset) is a separate feature.
- **Trigger effects from the title's state.** Slip-only trigger rumble, and DualSense adaptive triggers (resistance from the brake, a pulse on locking), need the rumble mixer's per-effect values (`TireTractionFront`, `TireTractionRear`, the engine speed), so a hook in the title's rumble update first.
- **GameInput.** Needs SDL built with the GameInput SDK's v1 or v2 header (a download, owner's approval). It would give the Xbox Share button and trigger rumble without RAWINPUT's correlation, but no paddles; it is also the one route left to the ROG Xbox Ally X's trigger motors, which XInput and RAWINPUT do not reach.
- **Elite paddles as paddles on Windows.** Only SDL's HIDAPI Xbox drivers read them, and on Windows those never see an XInput device; libusb is not built in.

## Evidence

Build: `I:\FM4-build-usa`, snapshot `snapshots\gp-s1` of 25 October 2026, from commit `e4f69d8` (the input trace and the latency probe), fm4_rex.exe sha256 `92de4bc1...`, rexruntime.dll `35a6f6da...`. That build's probe lacks `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS`, which the commit after it adds. Runs with `e2e.py --profile copy` and the fast route's menus, `FM4_INPUT_TRACE_FILE` and `--arg input_latency_probe=true`, otherwise the launcher defaults:

| Run | Driver | Race frame rate | Steps after the race starts | Used for |
| --- | --- | --- | --- | --- |
| `e2e\gp-sweep` | human (no `engageaionstart`) | 60 | stick and trigger sweep at a standstill, then scripted throttle, brake and full lock | deadzones, stick and trigger transfer, rumble |
| `e2e\gp-drive240` | human | 240 | the scripted driving twice | rumble at 240 Hz (the same levels as at 60) |
| `e2e\gp-ai60`, `gp-ai120`, `gp-ai240` | AI | 60, 120, 240 | 90 s of race | latency; no rumble under the AI |

All five reached their goal. The traces are `input-trace.txt` in each run directory, the probe's lines are in `runtime.log`.
