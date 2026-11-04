# Controls

Part of [running Motorsport DX](running.md): pads and the options of `-Pad`, racing wheels and
`-Wheel`, and the keyboard and `-Keyboard`, with the owner's test checklists. The launcher's other
options are in [running.md](running.md#options).

## Contents

- [Controller](#controller)
  - [The game's steering deadzone](#the-games-steering-deadzone)
  - [-Pad knobs](#-pad-knobs)
  - [Pad drivers](#pad-drivers)
  - [Trigger rumble](#trigger-rumble)
  - [Input thread](#input-thread)
  - [Pad report and latency probe](#pad-report-and-latency-probe)
  - [Controller test checklist](#controller-test-checklist)
- [Racing wheel](#racing-wheel)
  - [Racing wheel test](#racing-wheel-test)
  - [Racing wheel in the game](#racing-wheel-in-the-game)
  - [Racing wheel force feedback](#racing-wheel-force-feedback)
  - [Racing wheel options](#racing-wheel-options)
  - [Racing wheel test checklist](#racing-wheel-test-checklist)
- [Keyboard](#keyboard)
  - [Keys](#keys)
  - [What changed for keyboard players (25 October 2026)](#what-changed-for-keyboard-players-25-october-2026)
  - [-Keyboard knobs](#-keyboard-knobs)
  - [Keyboard test checklist](#keyboard-test-checklist)

## Controller

A connected controller works without setup through the runtime's SDL backend. The game
knows only the Xbox 360 pad: two rumble motors, 8-bit triggers and no paddles. `-Pad`, and
the Controller section of the GUI launcher's Controls page, change how the runtime reads a
modern pad and how it rumbles. Every default keeps the pad as the game has always seen it,
except the rumble strength: the launchers default to 1.5, because the game's driving rumble is
weak (0.05 to 0.2 of full strength) and 1.5 felt best in the owner's test on the ROG Xbox Ally
X (25 October 2026). The runtime's own default stays 1, the game's own scale.
The design and the measurements behind the options are in
[modern gamepads](analysis/modern-gamepads.md).

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common -Pad 'driver=rawinput,trigger_rumble=1,rumble=1.5'
```

### The game's steering deadzone

The game applies deadzones of its own after the runtime, from the player's profile. The
steering stick's inner deadzone is 24% by default: the first quarter of the stick's travel
does nothing, and full lock comes at 95%. On a modern pad whose stick centres precisely, that
dead quarter is the main reason small corrections do not steer.

The game's own setting does not help: it keeps the inner edge at XInput's left stick
deadzone, 7,849 of 32,767 (24%), or above, so lowering STEERING AXIS DEADZONE INSIDE below 24
changes almost nothing (measured on 25 October 2026, see
[keyboard controls](analysis/keyboard-controls.md#verification)); it can only raise the
deadzone. Use the runtime's steering anti-deadzone instead:

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common -Pad 'anti_deadzone=1,stick_deadzone=0.05'
```

`anti_deadzone=1` maps the left stick into the deadzone the game reports for the player's
profile (read live, so it stays exact whatever the profile says), so the steering follows the
stick from the first movement and reaches full lock at full deflection. `stick_deadzone=0.05`
keeps a stick that does not rest exactly at 0 from steering on its own; raise it a step if the
car still drifts. Both are off by default for now. Leave the game's STEERING AXIS DEADZONE
INSIDE at 24.

The game's controller settings are at PLAY CAREER > MY PROFILE > OPTIONS > CONTROLLER (the
CONTROLLER LAYOUT screen), then X (ADVANCED) for the ADVANCED OPTIONS screen, saved with A. It
holds, in percent, STEERING AXIS DEADZONE INSIDE (24) and OUTSIDE (95: full lock), the
throttle range (ACCELERATION AXIS DEADZONE INSIDE and OUTSIDE, 15 and 90), the brake range
(DECELERATION AXIS DEADZONE, 15 and 95), the clutch and handbrake ranges, and VIBRATION SCALE,
the game's own rumble scale, at 100 with its slider full; `rumble` above 1 is the way to
stronger rumble. Y resets the screen to the defaults. The game's rumble switch is VIBRATION on
the CONTROLLER LAYOUT screen.

### -Pad knobs

Knobs are `name=value`, separated by commas; a later knob wins. A knob left out takes the
launchers' default; a value equal to the runtime's default sets nothing. Each knob is a runtime
setting, named in the last column.

| Knob | Values | Default | Description | Runtime setting |
| --- | --- | --- | --- | --- |
| `driver` | `default`, `rawinput`, `wgi`, `gameinput` | `default` | SDL's Windows pad drivers; see [Pad drivers](#pad-drivers). | `sdl_joystick_driver` |
| `rumble` | 0 to 2 | 1.5 (the runtime's: 1) | Multiplies the game's two motors, clamped at full strength. The game's rumble in normal driving is 0.05 to 0.2 of full strength, so the launchers default to 1.5; `rumble=1` gives the game's own strength. | `pad_rumble_strength` |
| `trigger_rumble` | 0, 1 | 0 | Trigger rumble derived from the game's motors; see [Trigger rumble](#trigger-rumble). | `pad_trigger_rumble` |
| `trigger_rumble_strength` | 0 to 2 | 1 | Multiplies the derived trigger rumble. | `pad_trigger_rumble_strength` |
| `thread` | 0, 1 | 0 | Updates SDL's pads every millisecond on a thread of their own; see [Input thread](#input-thread). | `input_sdl_thread` |
| `anti_deadzone` | 0, 1 | 0 | Left stick only: maps the stick into the game's own steering deadzone as the game reports it for the player's profile, so the steering follows the stick from the first movement and reaches full lock at full deflection; see [the game's steering deadzone](#the-games-steering-deadzone). Use it with `stick_deadzone`. | `pad_steering_anti_deadzone` |
| `stick_deadzone` | 0 to 0.5 | 0 | Both sticks: a radial deadzone, rescaled from its edge, for a stick that does not rest at 0; 0.05 with `anti_deadzone`. | `pad_stick_deadzone` |
| `stick_outer` | 0.5 to 1 | 1 | Both sticks: the deflection that gives full output. | `pad_stick_outer` |
| `stick_curve` | 0.5 to 3 | 1 | Both sticks: an exponent on the deflection; above 1 is finer near the centre. | `pad_stick_curve` |
| `trigger_deadzone` | 0 to 0.5 | 0 | Triggers read 0 up to this fraction. | `pad_trigger_deadzone` |
| `trigger_outer` | 0.5 to 1 | 1 | The trigger travel that gives full output, for triggers that do not reach the end. | `pad_trigger_outer` |
| `misc1` | `a`, `b`, `x`, `y`, `lb`, `rb`, `back`, `start`, `ls`, `rs`, `up`, `down`, `left`, `right`, `guide`, `none` | `guide` | The Xbox 360 button that SDL's Misc1 presses (the DualSense microphone button). | `pad_misc1` |
| `right_paddle1`, `left_paddle1`, `right_paddle2`, `left_paddle2` | as `misc1` | `y`, `b`, `x`, `a` | The buttons of paddles P1, P3, P2 and P4. | `pad_right_paddle1`, ... |
| `touchpad` | as `misc1` | `guide` | The button of the PlayStation touchpad click. | `pad_touchpad` |

- The stick and trigger knobs apply to physical pads (the SDL and XInput backends), never to
  the keyboard or the e2e harness's diagnostic controller. The order is: the deadzone and the
  outer edge on the stick's magnitude, then the curve, then the anti-deadzone. The
  anti-deadzone is applied per guest user, before the pad's stick and the keyboard's steering
  are merged; the keyboard maps its steering into the same range, so the device that steers
  further wins with its whole stick.
- A Guide target reaches the game only with the runtime's `guide_button` setting on.
- On Windows, Xbox pads report neither the Share button nor the Elite paddles in this build
  (no SDL driver here reads them); an Elite's own profile can map its paddles to other
  buttons. `misc1`, the paddles and `touchpad` apply to PlayStation and other pads that
  report them.

### Pad drivers

| `driver` | What SDL uses | Xbox One and Series pads | ROG Xbox Ally X built-in pad |
| --- | --- | --- | --- |
| `default` | SDL's defaults: XInput for Xbox class pads, HIDAPI for PlayStation pads | two motors, no trigger rumble | XInput, two motors |
| `rawinput` | Raw Input, with Windows.Gaming.Input (WGI) correlating the triggers and the trigger motors | two motors; trigger rumble once correlated, which needs a first button or stick input | Raw Input, two motors; no trigger rumble (an Xbox 360 class pad; checked on the owner's Ally X) |
| `wgi` | WGI, with XInput and DirectInput off | two motors and trigger rumble; the axes read 0 without window focus | WGI, if WGI lists the pad |
| `gameinput` | Microsoft's GameInput (API v3) before SDL's other drivers; without a GameInput v3 runtime, as `default` | two motors, trigger rumble and the Share button (Misc1) where GameInput reports them | GameInput, for its trigger motors, which XInput and Raw Input do not reach (the owner's test) |

PlayStation pads go through SDL's HIDAPI drivers with every choice (rumble, light bar,
sensors, touchpad). A driver that does not start leaves SDL's others running, so no choice
loses a pad. Raw Input is off in remote desktop sessions.

`gameinput` needs a GameInput runtime of API v3 or newer: the GameInput redistributable
(`GameInputRedist.dll`; the development PC has version 4.0.302 in System32). Windows' own
`GameInput.dll` there (0.2309.26100.9278) offers only the older API. The runtime is not
shipped with the export. At start the log says which applies, `SDL GameInput: a GameInput v3 runtime is available` or
`SDL GameInput: no GameInput v3 runtime (HRESULT ...)` with the reason (not installed, or older
than v3); without one, SDL's other drivers take the pads as with `default`. The pad report's
`driver=gameinput` shows a pad that GameInput took. SDL's GameInput driver is built with the
MIT header and loader of Microsoft's GameInput package and a patch to SDL
([setup](setup.md#the-gameinput-package)).

### Trigger rumble

With `trigger_rumble=1`, a pad with trigger motors (`trigger_rumble=1` in its
[pad report](#pad-report-and-latency-probe)) rumbles its triggers from the game's two motors:
the right trigger from the left motor, scaled by how far the throttle is pressed, and the
left trigger from the right motor, scaled by the brake. Wheelspin buzzes the throttle and a
locking front axle the brake; gear changes, impacts and kerbs also buzz a pressed trigger. A
released trigger stays still. Trigger rumble is zero while the motors are, while the window
has no focus, and while an overlay takes the input. The game sends no rumble while its AI
drives the player's car. On Xbox pads only `rawinput` and `wgi` give trigger rumble. With the
SDL backend (the default), closing the game window stops every pad's motors and trigger
motors, with or without these options; a game process that is killed cannot stop them.

### Input thread

SDL updates the pads in the UI thread's event loop, every millisecond while a pad is open.
In 240 fps races the UI thread is busy presenting, and on the main PC it updated the pads
only every 8.4 ms, so a new pad state waited about 4 ms longer before the game could read it.
`thread=1` updates the pads on a thread of their own every millisecond instead, and the UI
thread stops updating them. Measured with the latency probe on the main PC (25 October 2026):
in 240 fps races a new state was 2.8 ms old on average when the game read it (5.6 ms at the
99th percentile) instead of 6.6 ms (12.2 ms), with `default` and `rawinput` alike; at 60 and
120 fps, where the UI thread keeps up, it made no difference. On the ROG Xbox Ally X (races at
about 100 fps under its 117 fps limit) it brought no gain either: its 1 ms sleep updates the
pads every 1.55 ms, and the UI thread already did every 0.7 ms. It stays off by default.

### Pad report and latency probe

When a pad connects, `runtime.log` gets one line for it with what SDL exposes under the
chosen driver: the name, type, vendor and product ID, the SDL driver that opened it
(`xinput`, `rawinput`, `wgi`, `hidapi`, `dinput`, ...), the connection, the capabilities
(rumble, trigger rumble, LEDs, gyroscope and accelerometer, paddles, Misc1, touchpad) and
the battery. For example, the probe's virtual pad:

```text
SDL pad report: device=1 name="FM4 latency probe" type=standard vendor=0000 product=0000 driver=virtual connection=unknown rumble=1 trigger_rumble=1 rgb_led=0 mono_led=0 player_led=0 gyro=0 accel=0 paddles=0 misc1=0 touchpad_button=0 touchpads=0 power=unknown battery=-1 path="" guid=ff00bd8a464d34206c6174656e007601
```

`--input_latency_probe=true` (on the GUI launcher's Advanced page, Extra cvars) attaches a
virtual SDL pad whose right stick moves by 256, inside every deadzone, at random times, and
logs every 5 seconds how long a new state took to reach the game (`SDL latency probe (ms):`):
the gaps between SDL's pad updates, the wait from a new value to the update that sent it,
from there to the game's read, the whole age at the read, and the gaps between the game's
reads, then the rumble the virtual pad received. It is a measurement tool; leave it off for
normal play.

### Controller test checklist

For the owner's test of the options on real pads, from an
[export](running.md#export-to-another-computer) with the GUI launcher. Leave the game's STEERING AXIS
DEADZONE INSIDE at 24, or set it back to 24: a lower value does nothing
([above](#the-games-steering-deadzone)). Play a Quick Race with the default car and track in
each profile, at least two minutes of driving with wheelspin, braking and a kerb or two.

Xbox Series controller:

| Profile | Controller section |
| --- | --- |
| A | the defaults (`default` driver) |
| B | Pad driver Raw Input, Trigger rumble on (Rumble strength 1.5, the default) |
| C | as B with Rumble strength 2 |
| D | the defaults with Steering anti-deadzone on and, under Sticks and triggers, Stick deadzone 0.05 |
| E | Pad driver GameInput, Trigger rumble on (Rumble strength 1.5, the default) |

- Note for each profile whether the pad works in the menus and in races, whether the first
  rumble comes at once or only after a first input, whether the trigger rumble feels right
  (throttle on wheelspin, brake on locking) and which rumble strength you prefer.
- For profile D: whether small corrections steer now, how the steering feels against A, and
  whether the car steers by itself with the stick at rest (then raise the stick deadzone a
  step and note the value that holds).
- For profile E: whether the pad works, whether the triggers rumble (against B), and whether
  the Share button does anything; send its `SDL GameInput:` line and pad report
  (`driver`, `trigger_rumble`, `misc1`).

ROG Xbox Ally X built-in pad, race frame rate 120 (its panel). Profiles A to D ran on
25 October 2026 ([results](analysis/modern-gamepads.md#the-owners-rog-xbox-ally-x-test)):

| Profile | Controller section | Extra cvars (Advanced page) |
| --- | --- | --- |
| A | the defaults | `--input_latency_probe=true` |
| B | Pad driver Raw Input | `--input_latency_probe=true` |
| C | the defaults, Input thread on | `--input_latency_probe=true` |
| D | Pad driver Raw Input, Input thread on | `--input_latency_probe=true` |
| E | the defaults with Steering anti-deadzone on and Stick deadzone 0.05 | none |
| F | Pad driver GameInput, Trigger rumble on (Rumble strength 1.5, the default) | none |

- The Ally X's pad reports no trigger rumble under XInput or Raw Input (`trigger_rumble=0`),
  so Trigger rumble does nothing with those drivers; GameInput is the route left to its
  trigger motors (profile F).
- For profile E: as profile D of the Series controller.
- For profile F: whether the pad works and whether the triggers rumble; send its
  `SDL GameInput:` line and pad report (`driver`, `trigger_rumble`, `misc1`). Also note the
  version of `C:\Windows\System32\GameInputRedist.dll` (Properties, Details) if the file
  exists.

What to send back, from the export folder's `logs\runtime.log` (the line types below,
nothing else is needed). The log keeps the earlier runs; each run starts at its own
`SDL joystick driver option:` line, so note which profile ran in which order:

```powershell
Select-String -Path logs\runtime.log -Pattern 'SDL pad report:','SDL joystick driver option:','SDL input','SDL GameInput','SDL latency probe' | ForEach-Object Line
```

- `SDL pad report:`: what each pad exposes under the driver of that run.
- `SDL joystick driver option:` and `SDL input thread`: which options the run used.
- `SDL GameInput:` (with `gameinput`): whether a GameInput v3 runtime was found, and why not.
- `SDL input: rumble stopped at exit`: the pads' rumble was stopped when the window closed.
- `SDL latency probe (ms):` (every 5 seconds) and `SDL latency probe at exit:`: the probe's
  timings and the rumble it received.

The lines contain the pads' names and device paths, not the Windows user name.

## Racing wheel

Native racing wheel support ([racing wheels](analysis/racing-wheels.md)) is complete up to the
owner's hardware test, and **off by default** until then: no real wheel has been tested yet.
The MOZA R9 base with the ESX wheel and the SR-P pedals is the planned test set
([known issues](known-issues.md#tested-hardware)).

- The runtime and the GUI launcher read the wheel through SDL's joystick API. Its steering,
  pedals and buttons drive the first player's car at the wheel's full resolution, beside the
  controller.
- Each car has its own steering lock, with an end stop past it, and the cockpit's drawn steering
  wheel turns 1:1 with the wheel.
- The game's own steering torque, at its physics rate, drives the force feedback through a
  safety governor with a hard force cap (0.25 of the wheel's full force by default), together
  with kerb, surface and impact vibrations.
- A wheel that SDL maps as a gamepad also reaches the game as a pad, as before.

A player meets the wheel in this order, all on the launcher's Controls page:

1. The **Wheel test** tab, without the game: find the wheel's axes and buttons and check the
   force's direction ([Racing wheel test](#racing-wheel-test)).
2. The **Racing wheel** tab: turn **Use a racing wheel** on, then set the steering, the pedals,
   the buttons and the force feedback, in that order
   ([Racing wheel in the game](#racing-wheel-in-the-game),
   [Racing wheel force feedback](#racing-wheel-force-feedback)).
3. Play.

From the command line, the same settings are knobs of `-Wheel`
([Racing wheel options](#racing-wheel-options)):

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common -Wheel 'on=1,shift_up=4,shift_down=5'
```

### Racing wheel test

The **Wheel test** tab runs without the game (`MotorsportDX.exe --wheel-test` opens it with the
test running). Start it with the wheel centred and your hands on the wheel. Its rows are
**Wheel test** (starts and stops it), **Force cap** (0.25 by default, at most 0.5 in the test;
set it before the test starts), **Invert force**, **Record forces** and **Test force**. The
panel at the right shows:

- the wheel SDL found (the first joystick of SDL's wheel type, else the first joystick that is
  not a gamepad), its vendor and product ID, whether SDL types it as a wheel (`SDL wheel`) and
  whether it has force feedback and a spring;
- every axis at the device's resolution (-32,768 to 32,767), the buttons held and the hats;
- the force sent, the ramp, the reason the force is zero, and how long each update to the
  device took.

The test forces, on the **Test force** row (left and right pick one, A sends it):

- **Direction pulse 5%** pushes towards positive for one second, to the right. If the wheel
  turns left, stop the test, turn **Invert force** on and start it again; turn **Invert
  force** on in the Racing wheel tab's force feedback section as well, for the game.
- **Spring 10%** and **25%**: a centring spring that the wheel's firmware runs.
- **Sine 10%** and **25%**: a 1 Hz back-and-forth sent as a constant force every millisecond.
- **Stop force** ends them.

Every force goes through the governor: it ramps in over 1.5 seconds, never exceeds **Force
cap**, is spread out when it would jump, and drops to zero at once when the launcher's window
loses focus or the test stops. Pressing Play stops the test. The launcher writes the test's
lines to `logs\wheel-test.log` (its `Wheel report:` line names the wheel's axes, buttons and
force feedback effects) and, with **Record forces**, every force sent to
`logs\wheel-test-forces.csv`.

While the test runs, the Racing wheel tab shows the steering and each pedal's value as the car
will get it, and its **Rest**, **Pressed** and button rows take their values from the wheel.

### Racing wheel in the game

**Use a racing wheel** opens the wheel in the game; **Drive with the wheel** (on by default)
lets it drive the car, off leaves only its force feedback. The tab's sections, in order:

- **Steering:** the axis, an inversion, and the wheel's rotation lock to lock (**Wheel
  rotation**: set it to Pit House's maximum angle, since the wheel reports that rotation over
  its whole axis).
  - **Car steering lock**, lock to lock, is each car's own. The game gives each car a
    road-wheel lock: 42 degrees for most cars, 36 for GT racing cars and 30 for prototypes.
    This row is the lock of a 42-degree car (900 by default); GT cars get 6/7 of it (771) and
    prototypes 5/7 (643).
  - **Fixed steering lock** replaces it with one lock for every car (0: each car's own).
  - The lock is never more than the wheel's rotation. The car steers 1:1 with the wheel and
    reaches full steering at half the lock either way. Past it, the **End stop** holds the
    wheel with force feedback ([below](#racing-wheel-force-feedback)).
  - The game's steering deadzones and the pad's steering rate limit do not apply: while the
    wheel steers, the car gets the game's own filters for a 900-degree wheel, which follow the
    wheel linearly.
  - The cockpit's drawn steering wheel turns 1:1 with the wheel up to the car's lock. Past
    about 90 degrees the driver's hands keep the game's full-turn pose.
- **Throttle, brake and clutch pedal:** each an axis (None: no pedal), its raw values at rest
  and fully pressed (A on **Rest** and **Pressed** takes them from the wheel test while it
  runs; the order may be either way round), a deadzone at the rest end and a response curve
  (gamma; above 1 is finer at the start of the travel). The values reach the car as floats,
  not as the pad's 8-bit triggers.
- **Buttons:** shift up and down (sequential shifts in the game's manual gearbox), the clutch
  and the handbrake while held. A on a button's row takes the next button pressed while the
  wheel test runs.
- **Force feedback:** [below](#racing-wheel-force-feedback).

The wheel merges with the controller as the game merges two bindings of one action: the
larger pedal, the steering of larger magnitude, a shift from either. The menus are still
driven with the controller or the keyboard. The launcher's **Wheel value** field (with **Show developer options** on
the Advanced page) shows the profile's `-Wheel` value.

The game's anti-cheat rule withholds a race's credits and experience when a lap has fewer
than 10 frames with a fast steering change. Its threshold is a change per frame, so at a high
frame rate smooth steering could miss it; the threshold is scaled to the frame's time so that
it means what it means at 60 fps, for every device, pads included
([stage 2](analysis/racing-wheels.md#stage-2-done-29-october-2026)).

### Racing wheel force feedback

In a race you drive, the wheel gets the game's own force feedback
([stage 3](analysis/racing-wheels.md#stage-3-done-29-october-2026)), as the game made it for
the Xbox 360 wheel, but sent from a thread of its own at 1,000 Hz:

- a constant force from the game's steering torque. The game computes it once a frame; the
  runtime runs the game's own torque model after each of the physics steps (about 360 a
  second) and plays those values back about a frame behind, so the force has the same detail
  at 60 fps as at 240 fps ([stage 4](analysis/racing-wheels.md#stage-4-done-29-october-2026));
- a centring spring, which the wheel's firmware runs: the game fades it in between 1 and
  10 m/s and out when the rear slides (the rear slip between 0.25 and 0.5), so the wheel
  goes light when the car oversteers;
- a light damper, also in the firmware;
- the vibration of kerbs, rumble strips, grass and gravel, from the game's surface rumble, and
  a short jolt when the car hits a wall, another car or an object, from its collision rumble,
  both scaled by the game's wheel rumble table and run by the firmware as vibrations;
- the end stop of the car's steering lock ([stage 5](analysis/racing-wheels.md#stage-5-done-29-october-2026)):
  past the lock, a force back towards it that starts softly, reaches its strength 6 degrees
  past the lock and holds it, with a damper that takes the bounce out of the stop. It is part
  of the constant force, inside the cap, and only in a race you drive.

The launcher's rows (the End stop row is in the Steering section, the others in the Force
feedback section):

| Row | Knob | Default | What it does |
| --- | --- | --- | --- |
| **Force cap** | `ffb_cap` | 0.25 | The hard limit of everything together (the constant force, the spring, the damper, the vibrations and the end stop), a fraction of the wheel's full force, whatever the gain. The spring and the damper take at most half of it, the vibrations a quarter. |
| **Force gain** | `ffb_gain` | 1 | Multiplies the game's force, spring and damper before the cap. |
| **Minimum force** | `ffb_min_force` | 0 | Lifts small forces above a wheel's own friction; 0 for a direct-drive base. |
| **Smoothing** | `ffb_smoothing` | 10 ms | A low-pass on the game's force; 0: none. |
| **Centring spring**, **Damper** | `ffb_spring`, `ffb_damper` | 1 | 1: as the game scales them; 0: off. |
| **Kerbs and surface**, **Impacts** | `ffb_road`, `ffb_impact` | 0.25, 0.5 | The vibrations' strength at the game's strongest rumble, a fraction of the wheel's full force; 0: off. |
| **Physics-rate force** | `ffb_physics` | on | Off: the frame's torque instead of the physics steps'. |
| **Invert force** | `ffb_invert` | off | If the wheel test's direction pulse turned the wheel to the left. |
| **End stop** | `end_stop` | 1 | The end stop's strength, a fraction of the wheel's full force before the cap; 0: off, the wheel turns on to its own rotation. |

The force is zero at once outside a race you drive: in the menus, at the race's end, in
replays, when the AI drives your car, and when the window loses focus. When the game stops
updating the race (pause, photo mode, the rewind, loading), the force falls to zero within
about 0.1 seconds. After every such stop it ramps in again over 1.5 seconds. It also stops at
exit (closing the game window stops and releases the wheel before the process ends) and on a
crash (an unhandled exception filter and a SIGABRT handler stop the effects). A process that is
killed cannot stop them itself; what the wheelbase then does is part of the owner's test below.
Pit House's own settings (force feedback strength, road sensitivity, damper, friction, inertia)
apply on top of the game's.

### Racing wheel options

Every launcher row is a knob of `-Wheel`, and each knob sets a runtime cvar (also settable on
the Advanced page's **Extra cvars**). With the wheel on, the game logs a `Wheel report:` line, a
`Wheel: input` line with its mapping and a `Wheel: force feedback host started` line with the
force settings.

| Knob | Default | Runtime cvar |
| --- | --- | --- |
| `on` | 0 | `wheel`: opens the wheel; needs the SDL input backend (the default) |
| `input` | 1 | `wheel_input` |
| `steer_axis` | 0 | `wheel_steer_axis` |
| `steer_invert` | 0 | `wheel_steer_invert` |
| `steer_range` | 900 | `wheel_steer_range` (degrees) |
| `car_lock` | 900 | `wheel_car_lock` (degrees, the lock of a car with a 42-degree road-wheel lock) |
| `steer_lock` | 0 | `wheel_steer_lock` (degrees, one lock for every car; 0: each car's own) |
| `end_stop` | 1 | `wheel_end_stop` (0 to 1; 0: off) |
| `throttle_axis`, `brake_axis`, `clutch_axis` | 1, 2, -1 | `wheel_throttle_axis`, ... (-1: none) |
| `throttle_rest`, `brake_rest`, `clutch_rest` | -32768 | `wheel_throttle_rest`, ... |
| `throttle_pressed`, `brake_pressed`, `clutch_pressed` | 32767 | `wheel_throttle_pressed`, ... |
| `throttle_deadzone`, `brake_deadzone`, `clutch_deadzone` | 0 | `wheel_throttle_deadzone`, ... (0 to 0.9) |
| `throttle_gamma`, `brake_gamma`, `clutch_gamma` | 1 | `wheel_throttle_gamma`, ... (0.2 to 5) |
| `shift_up`, `shift_down`, `clutch_button`, `handbrake_button` | -1 | `wheel_shift_up_button`, ... (-1: none) |
| `ffb_cap` | 0.25 | `wheel_ffb_cap` |
| `ffb_gain` | 1 | `wheel_ffb_gain` |
| `ffb_min_force` | 0 | `wheel_ffb_min_force` (0 to 0.5) |
| `ffb_smoothing` | 10 | `wheel_ffb_smoothing` (milliseconds, 0 to 200) |
| `ffb_spring` | 1 | `wheel_ffb_spring` (0 to 2) |
| `ffb_damper` | 1 | `wheel_ffb_damper` (0 to 2) |
| `ffb_road` | 0.25 | `wheel_ffb_road` (0 to 1) |
| `ffb_impact` | 0.5 | `wheel_ffb_impact` (0 to 1) |
| `ffb_physics` | 1 | `wheel_ffb_physics` |
| `ffb_invert` | 0 | `wheel_ffb_invert` |

Cvars without a knob:

| Cvar | Default | Description |
| --- | --- | --- |
| `wheel_name` | empty | Only a joystick whose name contains this. |
| `wheel_ffb_sink` | `device` | `record` sends no force to any device and only records it (with `wheel_ffb_record`); the window's focus then does not stop the force, so a check without focus records it. |
| `wheel_ffb_record` | empty | A CSV file of every force sent: the time, the target and the force sent, the spring's and the damper's saturation and coefficient, the ramp, the block reasons and the governor's flags, the device update's cost, and the game's sample (whether you drive, its sequence number, the steering torque, the speed and the rear slip), the road's vibration and the impact sent, the impacts counted, the game's road and impact levels, the frame's force, whether the physics-rate force drives it, that force and its torque, the playback's delay and the points received, the wheel's angle, the lock and the end stop. |
| `wheel_virtual` | empty | For checks without hardware: an SDL virtual wheel driven by a CSV script (`time_s,steer,throttle,brake,buttons`, raw axis values interpolated between rows and a mask of buttons held; it repeats), and pads read without window focus. |

### Racing wheel test checklist

For the owner's MOZA R9 with the ESX wheel and the SR-P pedals, from an
[export](running.md#export-to-another-computer). Keep one Pit House profile for every test and note its
settings (maximum angle, force feedback strength, road sensitivity, damper, friction,
inertia). Keep **Force cap** at 0.25 until step 30.

Stage 0, the device and the forces without the game:

1. Open Pit House with that profile, then the GUI launcher, Controls page, **Wheel test** tab.
   Leave **Force cap** at 0.25, turn **Record forces** on, then turn **Wheel test** on.
2. Check the device line: the name, `vendor 346e`, `SDL wheel`, `Force feedback: yes`.
3. Turn the wheel lock to lock and press each pedal fully: note which axis moves, and its
   values at both ends and at rest. Press each button and both paddles: note their numbers.
4. **Direction pulse 5%**: the wheel must turn to the right. If it turns left, note it,
   stop the test, turn **Invert force** on and repeat.
5. **Spring 10%**, then **Spring 25%**: the wheel must centre gently, ramping in over about
   1.5 seconds. **Stop force**.
6. **Sine 10%**, then **Sine 25%**: a slow back-and-forth. Note any roughness or clicks.
7. With a force running, Alt+Tab to another window: the force must stop at once. Return:
   it ramps in again.
8. With a force running, end `MotorsportDX.exe` in Task Manager: note whether the force stops,
   and how quickly.

Stage 2, driving with the wheel:

9. Start the launcher again, turn **Wheel test** on, and on the **Racing wheel** tab turn **Use
   a racing wheel** on. Set the steering axis and **Wheel rotation** to Pit House's maximum
   angle, leave **Car steering lock** at 900 and **Fixed steering lock** at 0. Turn the wheel
   with a steering row focused: **Steering now** at the right must read -1 at 450 degrees left
   (a road car's lock), 0 in the centre and 1 at 450 degrees right (else turn **Invert
   steering** on). If step 4 needed **Invert force**, turn it on in the Force feedback section
   too.
10. For the throttle and the brake: set each axis, then press A on **Rest** with the pedal
    released and on **Pressed** with it pressed fully; with a row of that pedal focused, **The
    pedal now** must go from 0 to 1 over the travel. Set the clutch pedal's axis to None (the
    SR-P has none).
11. Press A on shift up and shift down and then the paddle, the clutch on a button and the
    handbrake on another. Turn **Wheel test** off, then Play.
12. In the game's options, set the transmission to manual (and once to manual with clutch).
    Drive a lap of a known car and track in the cockpit view: the drawn steering wheel must
    follow the R9 1:1 at every angle up to the car's lock without lag; the car must respond to
    the smallest steering movement; the throttle and brake must be smooth from the first
    millimetre to the end of the travel; each paddle press must shift exactly one gear; the
    clutch button must disengage while held (manual with clutch), the handbrake button lock
    the rear wheels.
13. Finish a career race: the race result must show its credits and experience (the
    anti-cheat rule must not withhold them).
14. Note the steering feel: anything that looks rate limited, smoothed or exaggerated at
    speed (the game's STEERING assist setting changes it; try Simulation).

Stage 3, the game's force feedback:

15. In the launcher's **Force feedback** section, set **Force gain** to 0.25. Start a Quick
    Race and drive a lap: on the straight the wheel must centre; in corners it must pull
    towards the centre more the harder the car turns, and go light when the front or the
    rear slides. Note any knocks, buzzing or notches, and whether the wheel oscillates when
    you let go of it on a straight.
16. Repeat with **Force gain** 0.5, then try **Smoothing** 0 and 20 ms, and note which feels
    best.
17. Spin the car once: the force must not jump when the car comes to rest or drives off.
18. Pause during a corner: the force must stop within a fraction of a second, and ramp in
    again over about 1.5 seconds after RESUME. The same for photo mode and the rewind.
19. Alt+Tab during a corner: the force must stop at once.
20. Finish the race: at the finish line, when the AI takes over the car, and in the results
    and the replay the force must be zero.
21. Rate the centre feel (the game's spring: try **Centring spring** 0 and 1) and the loss of
    grip at the limit, and note the Pit House settings you used; say which **Force cap**
    still felt safe.

Stage 4, the physics-rate force and the effects:

22. Drive a lap at 60 fps and at 240 fps (Display page, **Race frame rate**) with **Force gain**
    0.5: the force must feel equally detailed at both, with no buzz on the straight. Then turn
    **Physics-rate force** off and on and note the difference.
23. Drive over kerbs and onto the grass: the wheel must vibrate on the kerbs and the grass, not
    on the track. Try **Kerbs and surface** 0, 0.25 and 0.5.
24. Brush a wall and hit one, and touch another car: each must give a short jolt; a scrape
    must not buzz continuously. Try **Impacts** 0, 0.5 and 1.
25. Pause on a kerb and during a scrape: the vibration must stop with the force.
26. In a replay and when the AI drives your car: no vibration and no force.
27. Note any knock when a vibration starts or ends, and which kerb and impact strengths feel
    right.

Stage 5, each car's steering lock and the end stop (Pit House at 900 degrees or more):

28. In a road car (the career's first cars), turn the wheel slowly to full lock both ways at
    a standstill and while driving: the car's steering must reach full lock at 450 degrees,
    and the wheel must stop there, softly at first and then firmly. Note how far past the lock
    you can push it, and whether the stop knocks, buzzes or bounces when you let go of the
    wheel against it.
29. Turn quickly into the stop, and let the wheel spin back from it: no oscillation at the
    stop, no jolt.
30. Try **End stop** 0.5 and 1, and a higher **Force cap** if 0.25 felt safe: note which holds
    best.
31. In a GT racing car (for example the Aston Martin DBR9 or the Corvette C6-R) and a prototype
    (for example the Audi R10 or the Peugeot 908): the stop must come at about 385 and 320
    degrees either way. In the cockpit view the drawn wheel must follow the R9 1:1 up to the
    stop; note how the hands look at large angles.
32. With **Fixed steering lock** 540, every car must stop at 270 degrees.

Before the races of steps 15 to 20, 22 to 26 and 28 to 32, add
`--wheel_ffb_record=<the launcher folder>\logs\wheel-forces.csv` (a full path) to the Advanced
page's **Extra cvars**, so the game records its forces. What to send back: the notes of steps
3 to 32 and the files that [bug reports](bug-reports.md#racing-wheel) list for a wheel.

## Keyboard

The game knows only the Xbox 360 pad; the runtime's keyboard driver turns keys into a
virtual pad. The launchers bind the keys in a racing layout by the function each pad input
has in the game's DEFAULT LAYOUT 1, and turn on a steering and pedal model: the steering,
throttle and brake move at limited rates instead of jumping to full, and their values are
mapped into the game's own deadzones, so a short press steers a little and a held key
steers more. The design and the measurements are in
[keyboard controls](analysis/keyboard-controls.md).

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common -Keyboard 'steer_attack=0.4,precision=0.6'
```

### Keys

| Function in a race | Keys | Pad input |
| --- | --- | --- |
| Gas | `W` | right trigger |
| Brake | `S` | left trigger |
| Steer left / right | `A` / `D` | left stick |
| Precision (hold): half lock and half pedal | `Ctrl` | |
| Shift up | `E`, `Backspace` | B |
| Shift down | `Q` | X |
| Handbrake | `Space` | A |
| Clutch | `Z` | LB |
| Look straight back | `V` | RB |
| Rewind | `R` | Y |
| Switch camera | `C`, `Tab` | Back |
| Pause | `Escape`, `Enter` | Start |
| Telemetry | arrow keys | D-pad |
| Free look | `I`, `J`, `K`, `L` | right stick |
| Toggle heat damage | `H` | right stick press |
| (left stick press) | `F` | left stick press |
| Switch between borderless fullscreen and the window | `Alt+Enter` | (`bind_fullscreen`) |
| Open or close the [in-game settings](running.md#in-game-settings) menu | `F2` | hold View and Menu (Back and Start) for a second |

In the menus the same keys are the pad's buttons: the arrow keys move, `Space` selects
(A), `Backspace` or `E` goes back (B), `Q` and `R` are X and Y (for example ADVANCED and
RESET TO DEFAULT on the controller screen), `Z` and `V` are LB and RB, and `Enter` or
`Escape` is Start.

- The keyboard driver ignores keys pressed with Alt, so `Alt+Enter` does not also press
  Start. `F2`, `F3`, `F4`, `F7` and the backtick stay free for the overlays and the console.
- `Ctrl` works while the other keys are held: a modifier bound as a key of its own is left
  out when the other bindings compare their modifiers. `Ctrl+Escape` opens the Windows Start
  menu, so pause with `Enter` while holding precision.
- The keys press pad inputs, so the game's own controller options move the functions:
  with SWITCH GEAR UP/DOWN on, `E` shifts down. The game has 13 layouts (LAYOUT on the
  CONTROLLER LAYOUT screen); 8, 9, 12 and 13 move the gas and brake off the triggers, where
  the model does not reach them.
- The game's D-PAD STEERING (CONTROLLER LAYOUT screen) makes the D-pad, so the arrow keys,
  steer through the game's own handling instead of toggling the telemetry.
- The game's assists apply on top: STEERING (ASSISTED, NORMAL, SIMULATION), BRAKING,
  STABILITY CONTROL and TRACTION CONTROL on the ASSISTS & DIFFICULTY screen.
- The GUI launcher's Controls page edits the bindings on its **Key bindings** tab and keeps the
  ones you changed; X resets one to this layout.

### What changed for keyboard players (25 October 2026)

- The steering, throttle and brake ramp up and back instead of jumping to full;
  `-Keyboard smooth=0` (or Smooth steering and pedals off on the Controls page) gives the
  digital keys back with the same layout.
- `W` and `S` no longer push the left stick up and down, and the arrow keys no longer move
  the right stick. The menus still move with the arrow keys; free look is on `I`, `J`, `K`,
  `L` (or the mouse with "Mouse moves the right stick").
- Shift up and down are `E` and `Q` (`Backspace` still presses B). LB, RB and Y moved from
  `1`, `3` and `P` to `Z`, `V` and `R`; `Escape` pauses, `C` switches the camera, `Ctrl` is
  the new precision key and `H` toggles the heat damage.
- Bindings you changed in the GUI launcher stay as you set them. Outside the launchers the
  runtime keeps its own defaults and digital keys.

### -Keyboard knobs

Knobs are `name=value`, separated by commas; a later knob wins. A knob at its default sets
nothing. The GUI launcher's Controls page has them on its Keyboard tab.

| Knob | Values | Default | Description | Runtime setting |
| --- | --- | --- | --- | --- |
| `smooth` | 0, 1 | 1 | The steering and pedal model; 0: digital keys, full at once. | `mnk_smooth` |
| `steer_attack` | 0 to 2 s | 0.3 | Seconds from centre to full lock while a steering key is held. | `mnk_steer_attack` |
| `steer_release` | 0 to 2 s | 0.2 | Seconds from full lock back to centre after it is released. A reversal (the other key) runs back to centre at the faster of the attack and the release, then on at the attack. | `mnk_steer_release` |
| `throttle_attack`, `throttle_release` | 0 to 2 s | 0.25, 0.1 | The same for the throttle. | `mnk_throttle_attack`, `mnk_throttle_release` |
| `brake_attack`, `brake_release` | 0 to 2 s | 0.15, 0.1 | The same for the brake. | `mnk_brake_attack`, `mnk_brake_release` |
| `precision` | 0.1 to 1 | 0.5 | The share of full lock and full pedal while the precision key is held. | `mnk_precision` |
| `title_deadzones` | 0, 1 | 1 | Map the keys into the game's own deadzones (read live from the game, so a lowered in-game deadzone gives no jump); 0 for comparisons. | `mnk_title_deadzones` |
| `mouse_steering` | 0, 1 | 0 | The mouse's horizontal motion steers, with auto-centring; the right stick then gets no mouse motion. | `mnk_mouse_steering` |
| `mouse_steer_sensitivity` | 0.1 to 10 | 1 | Thousandths of full lock per count of mouse motion. | `mnk_mouse_steer_sensitivity` |
| `mouse_steer_centering` | 0 to 5 s | 1 | Time constant of the return to centre while the mouse rests; 0 turns it off. | `mnk_mouse_steer_centering` |

With both steering keys held, the one pressed last wins. The model runs on the clock, not
per frame, so it behaves the same at 60, 120 and 240 fps. A connected pad and the keyboard
work together on player 1: each stick comes from the device that deflects it further, the
triggers from the one pressed further.

### Keyboard test checklist

For the owner's test on the laptop, from an [export](running.md#export-to-another-computer) with the
GUI launcher. Play a Quick Race with the default car and track in each profile, at least two
minutes with corners in both directions, braking into a hairpin and a kerb or two. Keep the
game's assists as they are (the export's profile), and note them.

| Profile | Keyboard section |
| --- | --- |
| A | the defaults |
| B | Steering attack 0.2, Steering release 0.15 |
| C | Steering attack 0.45, Steering release 0.3 |
| D | Smooth steering and pedals off (the earlier digital keys) |
| E | the defaults, with the game's D-PAD STEERING on, steering with the arrow keys |
| F | the defaults, Mouse steering on |

- For each profile: can you hold a steady line through a long corner, correct a slide
  without overcorrecting, and keep the car straight on a straight? Which steering attack
  feels right?
- Throttle and brake: can you feed in the throttle out of a corner without wheelspin, and
  brake hard enough? Do the throttle and brake rates need changing?
- Precision (`Ctrl`): useful in tight corners and on the throttle? Is 0.5 the right share?
- Menus: do the arrow keys, `Space`, `Backspace`, `Enter` and `Escape` work everywhere you
  went? Anything that does not respond, or presses twice?
- Profile E: how does the game's own D-pad steering compare with the defaults?
- Profile F: is mouse steering usable, and how does the auto-centring feel?
- Note the race frame rate (the laptop's panel) and the steering deadzone in the game's
  options, if you changed it.

Nothing needs to be sent from the logs for this test; the notes are the result.
