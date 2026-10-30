# Keyboard controls

Design note for driving Forza Motorsport 4 well on a keyboard. The title knows only the Xbox 360 pad; the runtime's keyboard and mouse driver turns keys into a virtual pad. Stage 1 (25 October 2026) gathers the evidence and proposes the model, the handling of the title's deadzones, a racing layout, the tests and the options. Stage 2 (25 October 2026) implements them as the main session decided ([Stage 2](#stage-2)); the owner's test on the laptop through an export decides the final rates. How to use it is in [running.md](../running-controls.md#keyboard).

## Summary

- **Today** the keys are digital: A or D gives full lock at once and W or S full throttle or brake, behind the title's own deadzones (steering 24% to 95%, throttle 15% to 90%, brake 15% to 95% of travel, from the player's profile). Smooth or partial input is impossible. The launchers also leave two runtime defaults in place that matter once steering is partial: W and S push the left stick up and down (the title's steering deadzone is radial, so that changes the steering), and the arrow keys move the right stick as well as the D-pad.
- **FM4's own controls** (DEFAULT LAYOUT 1): left stick steering, right trigger gas, left trigger brake, B and X shift up and down, A handbrake, LB clutch, RB look back, Y rewind, Back camera, Start pause, D-pad telemetry, right stick free look. The game can swap shift up and down, handbrake and clutch, camera and look back, and has a D-pad steering option of its own.
- **Model:** a rate limiter per axis (attack, release, reversal at the faster rate, a precision key that limits to a share), integrated exactly between key events on the steady clock, so it is the same at 60, 120 and 240 Hz. Proposed: steering 0.30 s to full lock and 0.20 s back, throttle 0.25 and 0.10 s, brake 0.15 and 0.10 s, precision 0.5. Mouse X steering with auto-centring as an alternative mode. Speed-sensitive steering needs the car's speed, which the title does not expose cheaply: left for the telemetry work.
- **Deadzones:** recommended is reading the title's live deadzones in a hook on its input conversion and mapping the keyboard's 0..1 into each inner..outer range, so the output is linear from the first press for any profile; a fixed compensation for 0.24 would jump to 21% of full lock once the player lowers the deadzone to 0.05.
- **Layout:** WASD driving with Q and E for the gears, Space handbrake, Ctrl precision, Z clutch, V look back, R rewind, C camera, Escape pause, the arrow keys for the D-pad and the menus, I, J, K, L free look; Alt combinations, F2 and the runtime's overlay keys stay free.
- **Tests:** unit tests of the model through a copy of the title's conversion, key injection through the diagnostic controller, and an e2e run that drives with the model and reads the title's own steering and pedals in `FM4_INPUT_TRACE_FILE`.
- **Options:** `-Keyboard` knobs in both launchers; the runtime keeps today's behaviour by default, and the launchers turn the model on (recommended).

## Today's keyboard path

### The driver

The keyboard and mouse driver ([mnk_input_driver.cpp](../../work/runtime/src/input/mnk/mnk_input_driver.cpp), `mnk_mode`) is fully digital:

- Key events arrive on the UI thread and set a 256-entry key table; `GetDeviceState`, called by the title's poll once per frame, builds the pad state from it.
- A trigger key gives 0 or 255. A stick key gives ±32,767 at once; opposite keys held together cancel to 0. There is no ramp, no return-to-centre rate and no partial input.
- Bindings are the `keybind_*` cvars: comma-separated alternatives, each with optional modifier prefixes (`Shift+Up`). Modifier matching is exact, so `Up` is silent while Shift is held; a bare modifier (`Shift`) can never fire. Keys pressed with Alt are ignored, so Alt+Enter (fullscreen) presses nothing.
- Focus loss clears every key. While an overlay takes the input (`is_active()` false) or the window has no focus, the state is neutral.
- `mnk_mouse` adds the mouse's motion to the right stick (`mnk_sensitivity`), with the pointer locked.
- The keyboard is a synthetic device and always feeds guest user 0 ([device_assignment.h](../../work/runtime/include/rex/input/device_assignment.h)), merged with that user's pads: buttons OR, triggers the larger, each stick axis the larger magnitude ([state_merge.h](../../work/runtime/include/rex/input/state_merge.h)). The pad shaping of [modern gamepads](modern-gamepads.md#stage-2) is never applied to it.

### The launchers' bindings

The launchers set some bindings in `launch_cvars` ([pipeline.py](../../outputs/FM4-build/pipeline.py)); the GUI launcher's Controls page shows and edits all of them and passes the launcher's cvars plus the bindings the player changed. Every binding the launchers do not set keeps the runtime's default. The effective layout today:

| Pad input | Keys | Set by |
| --- | --- | --- |
| Right trigger | W | launcher |
| Left trigger | S | launcher |
| Left stick left / right | A / D | launcher (same as the runtime) |
| Left stick up / down | W / S | runtime default |
| A / B | Space / Backspace | launcher |
| X / Y | L / P | runtime default |
| LB / RB | 1 / 3 | runtime default |
| Start / Back | Enter / Tab | launcher |
| D-pad | arrow keys | launcher |
| Right stick | arrow keys | runtime default |
| Left / right stick press | F / K | runtime default |

Two of these double up:

- **W and S also push the left stick up and down.** The title's steering deadzone is radial (see below), so the stick's Y counts in its magnitude. With today's full deflection that changes nothing: D and W give a steering output of 1 either way, and W alone gives 0. With partial steering it would: raw X 10,000 alone gives the title 0.092 of full lock (measured), but with Y at 32,767 the magnitude is 34,259 and the title's X is 10,000 · (34,259 − 7,864) / 34,259 / 23,265 = 0.331. A keyboard model must keep the left stick's Y at 0 in races.
- **The arrow keys press the D-pad and move the right stick.** In a race that toggles the telemetry and looks around at once.

### What the title makes of it

The title converts the pad state with the player's profile deadzones ([modern gamepads, The title's input code](modern-gamepads.md#the-titles-input-code)): the steering stick with a radial deadzone, inner 0.24 and outer 0.95 of full deflection, rescaled linearly between them; the throttle (right trigger) 0.15 to 0.90 and the brake (left trigger) 0.15 to 0.95 of the trigger. The conversion `sub_822B4130` receives the ten deadzone floats live from the user's block, and `FM4_INPUT_TRACE_FILE` traces them (`cfg` lines). For the keyboard that means:

- A or D gives 32,767, past the outer edge: full lock at once.
- W or S gives 255: full throttle or brake at once.
- Any keyboard ramp that starts at 0 does nothing for the first 24% of the steering and the first 15% of the pedals, and then jumps into motion. Partial throttle is impossible with 0/255.

## FM4's own controls

### The race layout

PLAY CAREER > MY PROFILE > OPTIONS > CONTROLLER opens the CONTROLLER LAYOUT screen (e2e run `gp2-menu`, frame 30). With the profile's defaults it shows LAYOUT: DEFAULT LAYOUT 1, D-PAD STEERING: OFF, VIBRATION: ON, SWITCH GEAR UP/DOWN: OFF, and a pad diagram with what each input does in a race:

| Function in a race | Pad input |
| --- | --- |
| Steering | Left stick |
| Gas | Right trigger |
| Brake | Left trigger |
| Shift up | B |
| Shift down | X |
| Handbrake | A |
| Clutch | LB |
| Look straight back | RB |
| Rewind | Y |
| Switch camera | Back |
| Pause | Start |
| Toggle telemetry | D-pad |
| Free look | Right stick |
| Toggle heat damage (as the screen labels it) | Right stick press |
| (nothing labelled) | Left stick press |

The footer has A SAVE, B CANCEL, X ADVANCED (the deadzones; see [modern gamepads](modern-gamepads.md#verification)) and Y RESET TO DEFAULT.

The profile's `ControllerOptions` ([ForzaProfile.sch](modern-gamepads.md#the-deadzones-are-the-players-profile-settings)) hold the screen's settings: `Layout` (0, DEFAULT LAYOUT 1), `DPad` (0, D-pad steering off), `RumbleSetting` (1), `InvertGearUpDown`, `InvertHandbrakeClutch`, `InvertCameraLookBack` and `RewindOnBack` (all 0). So the player can swap shift up and down, the handbrake and the clutch, and the camera and look back, and move rewind to Back, in the game itself; the screen lists them below SWITCH GEAR UP/DOWN as SWITCH HANDBRAKE/CLUTCH and the rest (run `kb-options`).

The LAYOUT row has 13 layouts, DEFAULT LAYOUT 1 to 13, without wrapping (run `kb-layouts`). All keep the left stick for steering, Back for the camera, Start for pause, the D-pad for the telemetry and the right stick press for the heat damage:

| Layouts | What moves against DEFAULT LAYOUT 1 |
| --- | --- |
| 2 to 5 | Only the face buttons: shift up, shift down, handbrake and rewind in other orders |
| 6, 7 | Shift down and up on LB and RB; clutch, look back, handbrake and rewind on the face buttons |
| 8 | Gas and brake on A and X; shift down and up on the triggers |
| 9 | Gas and brake on A and B; clutch and look back on the triggers |
| 10 | Clutch and handbrake on the right stick (no free look); the face buttons switch the camera |
| 11 | Shifting on the right stick; handbrake on RB |
| 12 | Gas and brake on the right stick; clutch and handbrake on the triggers |
| 13 | Gas on the left trigger, brake on the right one (swapped) |

With D-PAD STEERING on (run `kb-options`, frame 35) the diagram labels the D-pad STEERING as well as the left stick, and the telemetry toggle is gone.

Every one of these is a pad input, so a keyboard layout that names keys by pad input follows whatever the player chooses in the game: with SWITCH GEAR UP/DOWN on, the key on B shifts down.

### The menus

The e2e harness navigates every front-end menu with the D-pad and A and B (the fast route and the menu scenarios in [e2e.py](../../outputs/FM4-build/e2e/e2e.py)); some menus ignore the first press after they open, and the career home menu hides after a few idle seconds and shows again on the first press. Pause in a race is Start.

The menus also take the left stick, from a deflection between 37% and full, with a repeat while it is held (run `kb-assists`, main menu, before a profile loads): X at 12,000 held for 0.3 s left the highlight on PLAY CAREER (frame 16), X at 32,767 held for 0.3 s moved it two items to AUTOVISTA (frame 17), and X at -32,767 moved it back (frame 18). The racing layout does not depend on it: the arrow keys are the D-pad, which moves one item per press.

## Steering and pedal model

### Rate limiter per axis

Each analog control the keyboard drives (steering, throttle, brake) is a value that moves toward a target at a limited rate, in the title's own terms (0 to 1 of full lock or full pedal, after its deadzone; see [The game's deadzones](#the-games-deadzones)):

- **Target.** Steering: −1 while the left key is held, +1 for the right key; with both held, the key pressed last wins (a quick counter-steer needs no release first). Throttle and brake: 1 while their key is held. With nothing held, 0.
- **Attack.** Toward a target further from 0 than the value, on the same side: at 1 / `attack` per second, so `attack` is the time from 0 to full.
- **Release.** Toward 0, and toward a smaller target on the same side: at 1 / `release` per second.
- **Reversal.** Toward a target on the other side of 0: at the faster of the two rates until the value crosses 0, then at the attack rate.
- **Precision.** While the precision key is held, every target is limited to `precision` (for example 0.5): half lock and half pedal. Pressing it at full lock releases the steering to half at the release rate; letting go attacks to full again.
- An `attack` or `release` of 0 moves at once: with both at 0 the model is today's digital behaviour.

### The same at every frame rate

The model is time-based, never per poll. Between two changes of the keys the target is constant, so the value's path is a known piecewise-linear function of time. The driver advances the value to the moment of each key event (on the UI thread, before applying the event) and to the moment of each `GetDeviceState` (on the guest thread), both from `std::chrono::steady_clock`, under the driver's lock. So a key held for 100 ms moves the steering by the same amount whether the title polls at 60, 120 or 240 Hz, or twice per frame in the countdown, and a tap shorter than a frame still counts. The title polls once per frame, so what it sees is this value sampled at its polls.

- Focus loss and an overlay taking the input reset the values to 0 at once (no leftover steering when the window comes back).
- A long gap between polls (a load) needs no clamp: the value is exact for the keys that were held.

### Defaults

Proposed starting points for the owner's test, in seconds from 0 to full or back:

| Control | Attack | Release |
| --- | --- | --- |
| Steering | 0.30 | 0.20 |
| Throttle | 0.25 | 0.10 |
| Brake | 0.15 | 0.10 |

Precision 0.5. At 60 fps a steering attack of 0.30 s is 18 frames from centre to full lock. The throttle's slower attack lets the traction build before full power; the brake's faster one keeps full braking within 0.15 s.

### Clutch and handbrake

In DEFAULT LAYOUT 1 the clutch (LB) and the handbrake (A) are buttons: a button has no analog value, so their keys press them as today. The profile has analog ranges for both (`ClutchDeadzoneInside` and `Outside` 0.10 and 1.00, `HandbrakeDeadzoneInside` and `Outside` 0.10 and 1.00), for the layouts that put them on an axis (9 and 12 put the clutch on a trigger, 10 on the right stick). The model works per pad axis (left stick X, left trigger, right trigger), whatever the game's layout puts there, and the deadzone compensation uses the ranges the title passes for that axis, so in layouts 9 and 12 the clutch key on the left trigger ramps with the brake's knobs; the knobs are named after their function in DEFAULT LAYOUT 1. Layouts 8, 9, 12 and 13 move the gas and brake off the triggers or swap them, so the keyboard is meant for the others.

### Mouse steering

As an alternative mode, the mouse's X motion steers:

- Every count of horizontal motion moves the steering by `mouse_steer_sensitivity` / 1,000 of full lock (full lock after 1,000 counts at 1.0), clamped to ±1 (± `precision` while the precision key is held). The pointer is locked as for `mnk_mouse`, and the right stick then gets no mouse motion.
- Auto-centring: the steering decays toward 0 with the time constant `mouse_steer_centering` (0 turns it off), so letting go of the mouse straightens the wheels; holding a corner means holding the mouse a little further out. The steering keys still work and add to it.
- The pedals stay on the keys (or the mouse buttons, through the existing `LMB` and `RMB` key names) with the same rate limiter.

### Speed-sensitive steering

The runtime cannot see the car's speed. No hook reads the car's state, and the values the input path already passes through (the pad state, the deadzones, the rumble motors) carry no speed. The title computes speed-dependent effects internally (the wheel's force feedback scales its centring spring with speed: `SpringScaleSpeed0` and `1` in `Media/ControllerFFB.ini`), but reaching the speed means finding the car's state in guest memory, which is the postponed telemetry work. So speed-sensitive steering is left for later. Until then the game's own help applies: the ASSISTS & DIFFICULTY screen before a Quick Race has STEERING with ASSISTED (the e2e profile's EASY difficulty), NORMAL and SIMULATION, next to BRAKING, STABILITY CONTROL and TRACTION CONTROL, which also soften digital pedals (runs `fr-f1-race`, frame 21, and `kb-assists`). Its description: ASSISTED guides the car's steering when necessary, to reduce weaving and keep the car on the track; NORMAL gives full control but damps some physical effects to make driving easier (`kb-assists`, frame 34). Neither mentions the speed.

## The game's deadzones

Two ways to make the keyboard's 0..1 reach the title linearly from the first key press:

1. **A fixed compensation** for the profile defaults: steering raw = 0.24 + s · (0.95 − 0.24) of full deflection, throttle raw = 0.15 + v · 0.75 of the trigger. Exact for the default profile, but it assumes the profile. If the player lowers STEERING AXIS DEADZONE INSIDE to 0.05 (which [running.md](../running-controls.md#the-games-steering-deadzone) recommends for modern pads, and which applies to every pad of the profile), the title's output becomes (0.24 + 0.71 s − 0.05) / 0.90 = 0.21 + 0.79 s: a jump to 21% of full lock at the first press. The same holds for the pedals.
2. **The live deadzones**, read where the title uses them. The input poll `sub_822B5E60` calls the conversion `sub_822B4130` once per user per frame with the ten floats in `f1` to `f10`, in the raw units of the pad (7,864.08 and 31,128.7 for the profile's default steering; 38.25 and 229.5 for the throttle, 38.25 and 242.25 for the brake; before a profile loads XInput's 8,689 and 32,767 and 30 and 254). At that call the poll's loop counter, the user index, is in `r27` (the call returns to `0x822B5F6C`). A hook on the conversion, beside the existing trace in [fm4-ui-diagnostics.cpp](../../work/fm4-rex/src/fm4-ui-diagnostics.cpp), publishes user 0's left stick and trigger ranges to the runtime; the keyboard driver, which always feeds user 0, maps its values through them:
   - steering s ≠ 0: raw X = sign(s) · (inner + |s| · (outer − inner)), raw Y = 0. The title then computes (|raw X| − inner) / (outer − inner) = |s|: linear from the first press, for any profile;
   - pedal v > 0: raw = inner + v · (outer − inner), rounded up to the next byte above inner so that the smallest v gives a non-zero output; v = 1 gives the outer edge or more, full pedal;
   - s = 0 and v = 0 give a raw 0, released.

**Recommended: the live deadzones.** They follow the profile, before and after it loads and whatever the player sets, so nothing jumps (stage 2 found that the game keeps the steering's inner edge at 7,849 or above whatever the setting; see [Verification](#verification)); the cost is one hook that copies six floats per poll. The interface is generic: a runtime call `SetTitleInputRanges(user, ranges)` that a title integration may make, with no compensation when none is made (another title, or a build without the hook). A runtime knob turns the compensation off for comparisons.

The compensation lives in the keyboard driver, not in the title: forcing the title's deadzones to 0 for the keyboard would also remove them from a pad merged into the same user. A pad and the keyboard used at the same moment on the same axis merge to the larger raw value, as today.

## Racing layout

Proposed for both launchers' defaults, by FM4's DEFAULT LAYOUT 1. The left hand drives on WASD with the race functions around it; the right hand has the arrow keys (D-pad: the menus and the telemetry) and free look.

| Function in a race | Pad input | Proposed keys | Today |
| --- | --- | --- | --- |
| Steer left / right | Left stick X, through the model | A / D | A / D, digital |
| Gas | Right trigger, through the model | W | W, digital |
| Brake | Left trigger, through the model | S | S, digital |
| Precision (hold) | the model's share | Ctrl (binding name `Control`) | none |
| Shift up | B | E, Backspace | Backspace |
| Shift down | X | Q | L |
| Handbrake | A | Space | Space |
| Clutch | LB | Z | 1 |
| Look straight back | RB | V | 3 |
| Rewind | Y | R | P |
| Switch camera | Back | C, Tab | Tab |
| Pause | Start | Escape, Enter | Enter |
| Telemetry | D-pad | arrow keys | arrow keys |
| Free look | Right stick | I, J, K, L | arrow keys |
| Toggle heat damage | Right stick press | H | K |
| (none) | Left stick press | F | F |
| (none) | Left stick up / down | none | W / S |

In the menus the same keys are the pad's buttons: the arrow keys move (D-pad), Space selects (A), Backspace or E goes back (B), Q and R are X and Y (for example ADVANCED and RESET TO DEFAULT on the controller screen), Z and V are LB and RB, Enter or Escape is Start and Tab or C is Back.

- **Kept free:** Alt with any key (the driver ignores it: Alt+Enter toggles fullscreen), F2 (settings overlay), F3, F4, F7 and the backtick (the runtime's overlays and console).
- **The precision key is a modifier.** Today a bare modifier can never fire, and a held modifier silences every other binding. The rule changes to: a binding whose key is itself a modifier (`Control`, `Shift`) fires while it is held, and a modifier bound that way is left out when the other bindings compare their modifiers, so W, A, S, D and E keep working with Ctrl held. Bindings with a modifier prefix (`Shift+Up`) work as before. Ctrl rather than Shift because pressing Shift five times in a row opens Windows' Sticky Keys prompt and takes the focus. Ctrl with Escape opens the Start menu, so pause with Enter while holding precision.
- **What changes for existing players:** the steering and pedals ramp instead of jumping (`smooth=0` restores the digital keys); W and S no longer move the left stick (the menus move with the arrow keys, as they already can); the arrow keys no longer move the right stick (free look is on I, J, K, L, or the mouse with "Mouse moves the right stick"); shift up and down move to E and Q (Backspace still presses B); LB, RB and Y move from 1, 3 and P to Z, V and R; Escape and C are new. Bindings a player changed in the GUI launcher stay as they are. The runtime's own defaults, used outside the launchers, do not change.
- **The game's D-PAD STEERING** (`ControllerOptions/DPad`, read with the other controller options by the profile loader `sub_82425B90`) lets the D-pad steer through the title's own handling, which was not traced; with it on, the CONTROLLER LAYOUT screen labels the D-pad STEERING and drops the telemetry toggle. With it on, the arrow keys steer; it goes into the owner's checklist as a comparison with the model, not as a default.

## Testing

The harness never sends desktop input, so the keyboard is tested in three layers.

1. **Unit tests of the model** ([pad-shaping-tests.cpp](../../work/fm4-rex/tests/pad-shaping-tests.cpp) style: a header-only model in `rex/input`, a CPU test in `work/fm4-rex/tests`):
   - attack, release, reversal at the faster rate, last-pressed-wins, precision, and attack/release 0 as the digital behaviour;
   - frame-rate independence: the same key script polled at 60, 120 and 240 Hz, and with a double poll per frame, gives the same value at common times (to rounding), and a tap shorter than a frame moves the value by its duration times the rate;
   - the compensation fed through a C++ copy of the title's conversion (the radial deadzone of `sub_822B3F18` and the trigger range of `sub_82394A60`, as [modern gamepads](modern-gamepads.md#measured) measured them): with the profile's 0.24, a lowered 0.05 and XInput's pre-profile constants, the title's output equals the model's value within one raw step, starts above 0 at the first non-zero value, and never jumps;
   - the bind parser's bare-modifier rule (see [Racing layout](#racing-layout)).
2. **Key injection through the diagnostic controller.** The diagnostic controller ([fm4-test-input.h](../../work/fm4-rex/src/fm4-test-input.h), `FM4_TEST_INPUT_FILE`) gains an optional second line in its file, `keys <names>`, with key names as in the `keybind_*` cvars. It passes them to the keyboard driver (`rex::input::mnk::SetInjectedKeys`), which treats them as held keys beside the real ones, through the same bindings and the same model; injected keys count without window focus (they are not desktop input), but not while an overlay takes the input. `e2e.py` gets a `keys=<name>+<name>` action (`keys=none` releases). The launchers' cvars already turn the keyboard driver on in e2e runs (`mnk_mode=true`); with no keys injected it adds nothing.
3. **An e2e run that drives with the keyboard model**, human-driven (no `engageaionstart`), with `FM4_INPUT_TRACE_FILE`: in a Quick Race, held steering keys, taps, a reversal, the precision key, throttle and brake ramps, at 60 and 240 Hz. The trace's `in` lines give the raw state the keyboard produced and the title's own steering and pedal outputs with the steady clock: the first non-zero output after a press must be small (one poll's worth of the attack rate), full lock must come after the attack time, the release and reversal times must match, and 60 and 240 Hz must give the same curves. The `cfg` lines show the deadzones the compensation used. A second run with the steering deadzone lowered in the profile copy (the menu path of [running.md](../running-controls.md#the-games-steering-deadzone)) checks that nothing jumps. Menu navigation with injected arrow keys checks the D-pad still navigates.

## Options and launcher

Runtime cvars in the keyboard driver, and knobs in both launchers as for `-Pad`: `-Keyboard 'steer_attack=0.4,precision=0.6'` in `Build.ps1` and `pipeline.py` (`KEYBOARD_KNOBS`, carried to the GUI launcher in `launcher.json`), and a Keyboard section on the GUI launcher's Controls page above the bindings. A knob at its launcher default sets nothing beyond the launcher's own cvars.

| Knob | Runtime cvar | Values | Runtime default | Launcher default |
| --- | --- | --- | --- | --- |
| `smooth` | `mnk_smooth` | 0, 1 | 0 (today's digital keys) | 1 |
| `steer_attack` | `mnk_steer_attack` | 0 to 2 s | 0.30 | 0.30 |
| `steer_release` | `mnk_steer_release` | 0 to 2 s | 0.20 | 0.20 |
| `throttle_attack` | `mnk_throttle_attack` | 0 to 2 s | 0.25 | 0.25 |
| `throttle_release` | `mnk_throttle_release` | 0 to 2 s | 0.10 | 0.10 |
| `brake_attack` | `mnk_brake_attack` | 0 to 2 s | 0.15 | 0.15 |
| `brake_release` | `mnk_brake_release` | 0 to 2 s | 0.10 | 0.10 |
| `precision` | `mnk_precision` | 0.1 to 1 | 0.5 | 0.5 |
| `title_deadzones` | `mnk_title_deadzones` | 0, 1 | 1 | 1 |
| `mouse_steering` | `mnk_mouse_steering` | 0, 1 | 0 | 0 |
| `mouse_steer_sensitivity` | `mnk_mouse_steer_sensitivity` | 0.1 to 10 | 1 | 1 |
| `mouse_steer_centering` | `mnk_mouse_steer_centering` | 0 to 5 s | 1 | 1 |

- **The runtime keeps today's behaviour** (`mnk_smooth` off, no precision key bound), since the runtime is the SDK's and other titles use it. The launchers turn the model on with the racing layout, as they already turn the keyboard driver on (`mnk_mode=true`). Recommended for the launchers: on by default, because the digital keys cannot drive smoothly at all; `smooth=0` gives the digital keys back with the same layout. The alternative, off until the owner's test as with the pad options, would leave the export's players on digital keys unless they find the knob.
- `title_deadzones=0` maps the keyboard's 0..1 straight onto the raw range (0 to 32,767, 0 to 255), for comparisons; then the title's deadzones apply on top, as today.
- `mouse_steering=1` takes the mouse's X for the steering and leaves the right stick to the keys; it replaces `mnk_mouse` (the GUI launcher's "Mouse moves the right stick") while it is on.
- `launcher_tests` gets option sets with the knobs; [running.md](../running.md) gets a Keyboard section (the layout by function, the knobs, the game's own options that matter, an owner's test checklist), and [status](../status/current.md) the verification.

## Stage 2

Stage 2 (25 October 2026) implements the proposal with the main session's decisions: the model on by default in the launchers (`smooth=1`) and digital in the runtime, the racing layout above, the live deadzones read without changing the title's results and the profile's defaults until the first conversion, mouse steering off by default, and speed-sensitive steering left for the telemetry work.

### What changed

- **The model** ([keyboard_model.h](../../work/runtime/include/rex/input/keyboard_model.h), header only): `AdvanceAxis` (attack, release, reversal at the faster rate; 0 at once), `SteeringKeys` (the key pressed last wins), `StickFromTitle` and `TriggerFromTitle` (a value 0..1 into the title's inner..outer, rounded away from the deadzone so any non-zero value counts), and the binding parser and matcher.
- **The keyboard driver** ([mnk_input_driver.cpp](../../work/runtime/src/input/mnk/mnk_input_driver.cpp)): with `mnk_smooth` on, every key event (UI thread) and every `GetDeviceState` (the title's poll) first advances the steering, throttle and brake to that moment on the steady clock with the targets of the keys held until then, then takes the new keys. The steering goes to the left stick's X with the Y at 0 (unless `keybind_lstick_up` or `_down` are held), the brake to the left trigger, the throttle to the right one, each through the ranges of guest user 0. Focus loss and an overlay taking the input reset the values to 0. With `mnk_smooth` off and no mouse steering the output is the earlier digital one, bit for bit. Mouse steering accumulates the mouse's X, decays with `mnk_mouse_steer_centering` and adds to the keys' steering.
- **Bindings.** A binding whose key is a modifier (`Control`) fires while it is held, and that modifier is left out of every binding's comparison; the others keep their exact match (`Up` is silent with Shift held, `Shift+Up` needs it). `keybind_precision` is new, empty in the runtime.
- **The title's ranges** ([title_input_ranges.h](../../work/runtime/include/rex/input/title_input_ranges.h)): `SetTitleInputRanges` per user, `SetDefaultTitleInputRanges`, and `GetTitleInputRanges`, which returns a user's published ranges or the defaults (the identity until an integration sets them). FM4 sets the profile's defaults at startup (`FM4InstallInputRanges`) and publishes the floats in the hook of `sub_822B4130` ([fm4-ui-diagnostics.cpp](../../work/fm4-rex/src/fm4-ui-diagnostics.cpp)) when it is called from the input poll (the return address `0x822B5F6C`), user `r27`, before the original runs: the hook only reads registers, so the title's conversion is unchanged.
- **The merge of two devices** ([state_merge.h](../../work/runtime/include/rex/input/state_merge.h)): each stick now comes whole, both axes, from the device that deflects it further (`MergeStick`; the first device on a tie), instead of each axis from the larger one. So the keyboard's steering, mapped past the title's deadzone, and a pad never combine into a deflection neither has, and the title's radial deadzone sees one device's stick; the triggers stay the larger of the two and the buttons the union.
- **`--name=`** on the command line sets a string cvar to empty ([cvar.cpp](../../work/runtime/src/core/cvar.cpp)). CLI11 took the next argument as the value instead, which swallowed it; the launchers pass `keybind_lstick_up=` and `keybind_lstick_down=` this way.
- **Key injection.** The diagnostic controller reads an optional `keys` line ([fm4-test-input.h](../../work/fm4-rex/src/fm4-test-input.h)) and passes it to `rex::input::mnk::SetInjectedKeys`; `e2e.py` writes it for `key=<name>` actions ([e2e architecture](../e2e/architecture.md#diagnostic-controller-fm4_test_input_file)).
- **Launchers.** `launch_cvars` has the racing layout and `mnk_smooth=true`; `-Keyboard` knobs (`KEYBOARD_KNOBS` in [pipeline.py](../../outputs/FM4-build/pipeline.py), carried in `launcher.json`) in `Build.ps1` and the GUI launcher, whose Controls page has a Keyboard section and lists the bindings by function; three `-Keyboard` option sets in `launcher_tests`.

### Verification

Build: `I:\FM4-build-usa`, snapshot `snapshots\kb-s1` of 25 October 2026, from commit `9865a5a` on local main `3f28619` (`build-result.json`: that head, not dirty; the runtime with `REX_FSR` on), fm4_rex.exe sha256 `88f1c6da...`, rexruntime.dll `18ba4c7d...`. The CPU tests: `fm4_keyboard_model_tests` passes (the rates, the reversal, last-pressed-wins, the same values at 60, 120 and 240 polls a second with and without a double poll, a 7 ms tap, the steering and pedals through the title's conversions with the profile's deadzones, a lowered one and XInput's constants, the binding rule with Ctrl held, Ctrl and W giving 50% throttle, the whole-stick merge), `launcher_tests` passes with the three `-Keyboard` option sets (333 plans), and `test_pipeline.py` and `test_e2e_tools.py` pass.

**Driving with the model.** e2e runs on `kb-s1`, a human-driven Quick Race (the fast route's menus, no `engageaionstart`), `--profile copy`, with the keys injected through the diagnostic controller (`key=` actions): throttle, steering right, release, left, right pressed last (a reversal), precision with throttle and steering, brake, three 0.1 s taps. `FM4_INPUT_TRACE_FILE` gives the raw state the keyboard produced and the title's own output for user 0 at every poll; the rates are the medians of the changes between consecutive polls inside the range (per second of full lock or full pedal):

| Run | Race frame rate | Poll gap | Steering up / down | Throttle up / down | Brake up / down | First steering after 0 |
| --- | --- | --- | --- | --- | --- | --- |
| Expected | | | 3.333 / 5.000 | 4.0 / 10.0 | 6.667 / 10.0 | one poll of attack |
| `kb-drive60` | 60 | 16.72 ms | 3.333 / 5.000 (p10 to p90: 3.304 to 3.366, 4.959 to 5.039) | 3.980 / 10.025 | 6.686 / 9.952 | 0.053 to 0.059 |
| `kb-drive240` | 240 | 4.19 ms | 3.333 / 5.000 (3.308 to 3.356, 4.979 to 5.013) | 3.902 / 10.210 | 6.567 / 9.878 | 0.013 to 0.016 |
| `kb-drive60-dz` | 60, steering deadzone 5 in the game | 16.69 ms | 3.333 / 5.000 | 3.952 / 9.953 | 6.698 / 10.052 | 0.054 to 0.056 |

- The title's steering rises from the first poll after a press (0.056 at 60 Hz is one 16.7 ms poll at 3.33 per second; at 240 Hz 0.014 for 4.2 ms) and reaches full lock 0.3 s after the press, at both frame rates. The pedals' rates spread more (8-bit triggers, 191 steps between the deadzone's edges), with the same medians.
- The reversal: from -0.83 to -0.09 in 150 ms (5 per second, the release rate, faster than the attack), then on at the attack rate. Precision: the throttle holds 0.498 (raw 134 of 38.25 to 229.5) and the steering -0.500.
- The raw state carries the compensation: the first steering sample is raw X 9,164 (past the inner 7,864), the full throttle raw 230 (the outer edge 229.5).
- **The game's steering deadzone has a floor.** `kb-dz` lowered STEERING AXIS DEADZONE INSIDE from 24 to 5 in the run's profile copy (the screen showed 5 and was saved with A); in `kb-drive60-dz`, which ran with that profile, the conversion received 7,849 for the inner edge instead of 7,864.08, not 1,638 (0.05 of 32,767). The game keeps XInput's left stick deadzone (7,849, `XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE`) as the least inner edge, so lowering the setting below 24 changes almost nothing. The keyboard follows the value it receives either way: no jump, the same rates.
- **Menus and pause with keys.** `kb-menu` on `kb-s1`: on the main menu three injected Right presses and `Space` opened FREE PLAY (QUICK RACE showed), `Backspace` went back to the main menu, and in the race `Escape` opened the pause menu (RESUME, RESTART, PHOTO MODE) and `Backspace` closed it.
- Every run reached its goal. The trace shows a second conversion per poll, of a pad object built from the first connected user's state with XInput's constant deadzones (`sub_822B5E60` calls `sub_822B4130` again after the user loop, returning to `0x822B60F8`); the hook publishes only the user loop's.

Not covered: real keys on the laptop (the owner's [checklist](../running-controls.md#keyboard-test-checklist)), the feel of the rates, mouse steering beyond the unit tests (no desktop input in e2e), the game's D-PAD STEERING, and which of the two converted objects the car reads (the profile's deadzones, which the player tunes for driving, are on the user's).

## Left for later

- **Key glyphs.** The title's prompts show Xbox buttons. Keyboard glyphs need a texture replacement set ([texture replacement](texture-replacement.md)), switched by the device in use.
- **Speed-sensitive steering.** Needs the car's speed from the telemetry work; then the steering's limit (or its attack) can fall with speed.

## Evidence

- The runtime's keyboard driver, input system and device assignment, and the launchers' bindings, at commit `f4891c4` (local main, 25 October 2026).
- The CONTROLLER LAYOUT screen: `I:\FM4-build-usa\e2e\gp2-menu` (snapshot `gp-s2`, from `4154562`), frame 30 at 1280x720.
- The ASSISTS & DIFFICULTY screen: `I:\FM4-build-usa\e2e\fr-f1-race`, frame 21.
- The title's deadzones, their raw values and the measured stick and trigger transfer: [modern gamepads](modern-gamepads.md#the-titles-input-code) (runs `gp-sweep` and `gp2-menu`).
- The input poll `sub_822B5E60` and the profile loader `sub_82425B90` in the recompiled default.xex; the option names in `Media/ProfileSchema/ForzaProfile.sch` and in the loaded image.
- Stage 2, on `snapshots\kb-s1`: `I:\FM4-build-usa\e2e\kb-drive60`, `kb-drive240`, `kb-dz` (the steering deadzone lowered to 5 in its profile copy), `kb-drive60-dz` (driven with that profile) and `kb-menu`, each with `input-trace.txt`.
- Captures of 25 October 2026 on `game-build` from `f4891c4`, at 2x with the PPM files kept, `--profile copy`: `I:\FM4-build-usa\e2e\kb-layouts` (the 13 layouts, frames 28 to 64; its last step, back to layout 1, timed out, since the row does not wrap), `kb-options` (D-PAD STEERING on, frame 35, and the option list; its last check timed out once the list scrolled) and `kb-assists` (the left stick on the main menu, frames 16 to 18, and the ASSISTS & DIFFICULTY rows with their descriptions, frames 28 to 65).
