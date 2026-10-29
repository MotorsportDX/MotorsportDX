# Look presets after GT7 and FM2023

Two look presets beside `aces`, `gt`, `neutral` and `original`: `gt7`, after the look of Gran Turismo 7, and `fm2023`, after Forza Motorsport (2023). The project owner decided on 29 October 2026 to keep the four presets as they are and add these two, judged against the owner's look board of reference images. Both use the look mods and the post passes ([running.md, Look](../running.md#look), [post pass framework](post-pass-framework.md)) on the native path and the RTV path's classic mods alike; they get no work on the PM4 RTV path beyond what the shared mods give, which is frozen ([AGENTS.md](../../AGENTS.md)).

## 1. The references and the measures

The look board of 25 October 2026 holds 26 reference images (GT7 photo mode, press shots and gameplay, FM2023 Steam screenshots) and five split-screen frames of both games in one scene (IGN and Digital Foundry, 1024x576 video captures). The images stay outside the repository, in `<BuildRoot>\reference\look-refs` with their sources in `look-refs-sources.tsv`.

The board measures an image region by:

| Measure | Meaning |
| --- | --- |
| mean, p05, p50, p95 | Rec. 709 luma of the sRGB values (0 to 1): the mean and the 5th, 50th and 95th percentiles |
| spread | p95 - p05, the tonal range |
| sat | the mean HSV saturation, (max - min) / max per pixel |
| R/B | the mean linear red over the mean linear blue: the white balance |

The two pairs with the same framing:

| Pair | Side | mean | p05 | p50 | p95 | spread | sat | R/B |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Spa, photo mode (cmp-04) | FM2023 | 0.480 | 0.157 | 0.465 | 0.796 | 0.639 | 0.222 | 0.821 |
| | GT7 | 0.489 | 0.133 | 0.483 | 0.886 | 0.753 | 0.220 | 0.803 |
| Spa, GR Supra (cmp-05) | FM2023 | 0.408 | 0.109 | 0.359 | 0.737 | 0.628 | 0.361 | 1.275 |
| | GT7 | 0.418 | 0.097 | 0.369 | 0.795 | 0.698 | 0.419 | 1.014 |

What they show: GT7 has the wider tonal range (darker p05, brighter p95) and the bluer, cleaner sky; FM2023 is hazier, with a paler sky and less contrast in the distance, warmer (R/B 1.28 against 1.01 on the Supra pair) and a little less saturated. The board's notes add FM2023's richer ambient occlusion (its ray-traced AO in the RT and Quality modes) and a wide bloom at a low sun, and GT7's highlights that rarely clip. FM4 has neither Spa nor Watkins Glen, so the presets are judged by these measures on FM4's own tracks, not pixel for pixel.

The same measures of FM4 captures come from the 2560x1440 PPM captures of the e2e harness; a scratch tool (not in the repository) computes them the way the board's page does.

## 2. The GT7 operator

Polyphony Digital published GT7's tone mapping in the SIGGRAPH 2025 course "Physically Based Shading in Theory and Practice", with a sample implementation under the MIT license (`gt7_tone_mapping.cpp` 1.0). Its code was read from the research of 25 October 2026 (the verifiers' copies of the slides and the sample); nothing was downloaded for the port. `fm4-gt7.hlsli` (included by `fm4-look.hlsli`) ports its SDR path as the tone curve `gt7` (`FM4_LOOK_TONEMAP` 4), credited in the [third-party notices](../../outputs/FM4-source-control/third-party-notices/README.txt) and [sources and licensing](../sources-and-licensing.md#third-party-code-gt7-tone-mapping).

- **Units.** Frame buffer values with 1 at 100 cd/m^2. SDR white is a paper white of 250 cd/m^2 (2.5), the curve's peak; the result is divided by 2.5.
- **The curve, GT tone mapping V2.** With peak P = 2.5 and the sample's parameters alpha 0.25, gray point m 0.538, linear section l 0.444 and toe strength c 1.28: below l P, a power toe m (x / m)^c blended into the line x by smoothstep(0, m, x); from l P on, the convergent shoulder kA + kB exp(x kC), with k = (l - 1) / (alpha - 1), kA = P (l + k), kB = -P k exp(l / k) and kC = -1 / (k P), which meets the line with slope 1 and converges to P (l + k) = 1.185 P. The result is limited to P, so the curve clips at x = 1.472 P.
- **Hue.** The input and the per-channel result ("skewed") go to ICtCp (BT.2100, PQ). The output takes the skewed intensity I and the input's chroma Ct and Cp, faded out by 1 - smoothstep(0.98, 1.16) of the input's I over the peak's I, so colors above about paper white go to white smoothly. Then 0.4 of the per-channel result and 0.6 of the ICtCp result.
- **Gamut.** The sample takes linear Rec. 2020, GT7's working space, where its emissive reds sit at the gamut's edge. FM4's working space is Rec. 709, and the port takes its values as they are. Its first version (29 October 2026, until section 7) converted them to Rec. 2020 and back: FM4's reds then sat inside that gamut, and the per-channel curve turned a bright one orange (section 7).
- **Middle gray.** As with the other curves of the look, the exposed scene value is scaled so that middle gray (0.18 out) stays where the game's curve puts it, which keeps the game's auto exposure and the track's grading LUT. With the scale 2.5, the normalized operator gives 0.18 at an input of 0.1806, is linear from there to 0.444, and reaches white at 1.472. The input at 0.01 gives 0.0043 (the toe), at 1 gives 0.835.

Compared with the presets' curves, the toe darkens the shadows (a lower p05), the long linear middle keeps the midtones, and ICtCp keeps the hues of bright colors where a per-channel curve skews them toward yellow and white.

## 3. The GT7 preset

`gt7` is `tonemap=gt7` on the mods of the other presets (`aces` without its curve), with `sky_saturation=1.15,sky_contrast=0.3` for the deeper, more contrasted sky of the references (the A/B of 18 October 2026 found these two knobs gave a slightly deeper blue sky; [look mod coverage](look-mod-coverage.md#in-game-checks-sky-and-vegetation-18-october-2026)).

Exposure: GT7's own exposure models the eye's adaptation, so sunny scenes stay bright and night stays dark. FM4 has no night races, and the preset keeps the game's auto exposure through the middle gray anchor above.

## 4. The FM2023 knobs

FM2023's grade is not documented, so its preset is tuned against the references by the measures of section 1. Four knobs are new:

- **`haze`, the aerial haze.** A compute pass of the post pass framework at the scene_depth point, `haze_amount` in [haze.hlsl](../../work/fm4-rex/passes/haze.hlsl), turns each scene pixel's view distance d into an amount 1 - exp(-max(d - `HAZE_START`, 0) / `HAZE_DISTANCE`) (30 m and 800 m), times the strength (the pass param `HAZE`); the sky (W at the far plane) takes `HAZE_SKY` (0.35) of the strength, and the cockpit interior's mask, whose depth reads as the near plane, none. The amount goes into the texture `haze_amount` (slot 3). The final combine mods (`fm4_haze`, the classic fetch 0 hook and the native contract's entry, which declares `post:haze_amount`) mix the scene in linear terms toward a pale blue-gray by it, with a luminance of `haze_brightness` times 2 stops above the game's middle gray after the exposure, so the haze has the same brightness at every exposure. The knob passes the strength to the pass (`HAZE`) and turns the mix on (`FM4_LOOK_HAZE`).
- **`temperature`, the white balance** of the final image after the grading LUT: red times 1 + 0.12 t and blue times 1 - 0.12 t, the luminance kept, so t = 1 raises R/B by about 1.27, the step between the two games on the Supra pair.
- **`bloom_width`, a wide bloom:** the bloom input (already blurred by the game) tapped on two rings of 8, 3% and 6% of the screen's width out, the mean of their linear energy added to the bloom times the knob.
- **`ao`** stays the GTAO pass's strength; the preset raises it toward FM2023's richer occlusion.

The preset `fm2023` is `tonemap=aces` on the mods of the other presets with `saturation=1.05`, `ao=0.9`, `haze=0.3` and `temperature=0.35` (sections 5 and 6; `bloom_width=0.5` until section 6).

## 5. Tuning, 29 October 2026

Frozen frames of the `freeze` scenario (a Quick Race on the Bernese Alps rewound to its oldest frame, the fixed seed, the cockpit camera), on the native path at 2x (2560x1440), on the main build's snapshot `whl9` (the runtime and game code of main `46b1a59`) with the look and post passes of the working tree, each look its own run (`I:\FM4-build-usa\e2e\lk1-*`, `lk2-*`, `lk3-*`). Measures of section 1 on two regions of the 2560x1440 capture: the world through the windshield (x 0.30 to 0.68, y 0.24 to 0.50 of the frame: the stands, the pit building, the mountains and the sky) and the sky alone (x 0.40 to 0.62, y 0.25 to 0.36).

| Look | Region | mean | p05 | p50 | p95 | spread | sat | R/B |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `original` (the game's own, with TAA) | world | 0.646 | 0.039 | 0.758 | 0.925 | 0.886 | 0.219 | 0.733 |
| | sky | 0.853 | 0.772 | 0.881 | 0.931 | 0.159 | 0.126 | 0.759 |
| `aces` | world | 0.574 | 0.157 | 0.639 | 0.798 | 0.641 | 0.201 | 0.692 |
| | sky | 0.715 | 0.630 | 0.738 | 0.797 | 0.167 | 0.168 | 0.681 |
| `gt7` | world | 0.592 | 0.181 | 0.644 | 0.842 | 0.660 | 0.228 | 0.613 |
| | sky | 0.744 | 0.638 | 0.771 | 0.840 | 0.203 | 0.236 | 0.561 |
| `fm2023` first try (`haze=0.5,temperature=0.5,saturation=1.0`, sky share 0.5) | world | 0.610 | 0.185 | 0.685 | 0.803 | 0.618 | 0.107 | 0.881 |
| | sky | 0.739 | 0.687 | 0.755 | 0.801 | 0.113 | 0.074 | 0.860 |
| `fm2023` (`haze=0.3,temperature=0.35,saturation=1.0`, sky share 0.35) | world | 0.591 | 0.169 | 0.660 | 0.800 | 0.630 | 0.141 | 0.807 |
| | sky | 0.726 | 0.657 | 0.745 | 0.799 | 0.142 | 0.107 | 0.793 |

The `original` frame's sky reaches 0.93: the presets' `sky=0.7` darkens it.

- **`gt7` against `aces`**: a deeper, more saturated blue sky (sky saturation 0.236 against 0.168) with more contrast in the clouds (sky spread 0.203 against 0.167), brighter highlights that still stay below white (p95 0.842 against 0.798) and a wider range (0.660 against 0.641), with the midtones in place (p50 0.644 against 0.639). The ICtCp blend keeps the sky blue where the per-channel ACES curve turns it toward cyan-white, so the picture is cooler (R/B 0.61 against 0.69). The cockpit interior is darker (the toe). These follow the board's GT7 traits.
- **`fm2023` against `gt7`**: warmer by R/B 1.32 times (the board's Supra pair: 1.26), a lower range (spread 0.630 against 0.660; the board: 0.628 against 0.698) and lower highlights (p95 0.800 against 0.842), the distant mountains faded and the sky paler. The first try faded the picture too far: the world's saturation fell to 0.107, against 0.228 for `gt7` (the board has FM2023 at 0.86 times GT7's saturation), so the haze went from 0.5 to 0.3, its share on the sky from 0.5 to 0.35 and the warmth from 0.5 to 0.35. At 0.3 the world's saturation is 0.141; most of that region is distant mountains and sky, which the haze is meant to fade. `haze=0.4,saturation=1.05` measured the same (0.139), so the preset takes `saturation=1.05` with `haze=0.3`.
- Not judged in this frame: the occlusion and the bloom width, which need a sunlit scene and a low sun.

The pass `haze_amount` loaded and ran on the native path (`Post passes: texture haze_amount 2560x1440`); the runs' logs have only main's four errors.

### In the chase camera

The project owner asked on 29 October 2026 that the look tests run in the chase camera, not the cockpit; the board's reference pairs are chase camera frames too. `e2e.py --camera chase` ([race camera](../e2e/README.md#race-camera)) gives the same frozen grid frame from behind the player's car, on the build `89c62ea` (snapshot `lkc1`, runs `lkc1c-aces`, `lkc1c-gt7`, `lkc1c-fm2023`, 2x on the native path). The frame is sunny with scattered cloud. Measures of the whole frame above the rewind's hint bar (y below 0.9), and of the sky:

| Look | Region | mean | p05 | p50 | p95 | spread | sat | R/B |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `aces` | frame | 0.339 | 0.012 | 0.242 | 0.806 | 0.794 | 0.342 | 0.936 |
| | sky | 0.704 | 0.632 | 0.713 | 0.792 | 0.160 | 0.205 | 0.626 |
| `gt7` | frame | 0.360 | 0.021 | 0.266 | 0.852 | 0.831 | 0.319 | 0.868 |
| | sky | 0.735 | 0.637 | 0.745 | 0.843 | 0.206 | 0.275 | 0.512 |
| `fm2023` | frame | 0.347 | 0.017 | 0.250 | 0.808 | 0.791 | 0.291 | 1.050 |
| | sky | 0.723 | 0.661 | 0.730 | 0.796 | 0.135 | 0.125 | 0.764 |

Against the board's Supra pair (FM2023 against GT7: spread 0.90 times, p95 0.737 against 0.795, saturation 0.86 times, R/B 1.26 times), `fm2023` against `gt7` has a spread 0.95 times as wide, a p95 of 0.808 against 0.852, 0.91 times the saturation and 1.21 times the R/B: every measure moves the way the references do, by a little less. `gt7` against `aces` widens the range (0.831 against 0.794, p95 0.852 against 0.806) and deepens the sky (saturation 0.275 against 0.205, a bluer R/B). In the 1:1 crops the distant mountains fade toward a pale, warm sky with `fm2023` and stand out against a deep blue one with `gt7`.

Next, with the owner's verdict on taste: a low sun for the bloom width and a sunlit car side for the occlusion, and one of FM4's tracks that the references show (Laguna Seca, Suzuka, Road America, Sebring) where the career or a Quick Race can reach it.

## 6. Second round: a low sun on Maple Valley, the GPU cost, 29 October 2026

`e2e.py --location "Maple Valley"` ([race location](../e2e/README.md#race-location)) puts the frozen Quick Race grid on Maple Valley Raceway, one of the tracks of the references, in the chase camera: a low sun from the left behind the camera, a glare on the asphalt and long shadows. Snapshot `lkc1`, 2x on the native path. The measures add the clipped share: pixels whose brightest channel is 250 or more of 255.

| Look | mean | p05 | p50 | p95 | spread | sat | R/B | clipped |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `aces` | 0.572 | 0.017 | 0.704 | 0.911 | 0.894 | 0.185 | 1.101 | 0.4% |
| `gt7` (exposure 0) | 0.599 | 0.026 | 0.707 | 0.990 | 0.964 | 0.186 | 1.006 | 9.2% |
| `gt7,exposure=-0.3` | 0.551 | 0.020 | 0.642 | 0.957 | 0.937 | 0.193 | 1.005 | 4.4% |
| `gt7,exposure=-0.5` | 0.519 | 0.016 | 0.597 | 0.922 | 0.906 | 0.197 | 1.004 | 1.7% |
| `gt7,exposure=-0.6` | 0.502 | 0.015 | 0.576 | 0.903 | 0.888 | 0.199 | 1.002 | 0.9% |
| `fm2023` (`bloom_width=0.5`) | 0.573 | 0.016 | 0.708 | 0.912 | 0.896 | 0.188 | 1.214 | 0.7% |
| `fm2023,bloom_width=0` | 0.573 | 0.017 | 0.706 | 0.911 | 0.894 | 0.188 | 1.215 | 0.7% |

- **`gt7` clipped the low sun.** Its curve reaches white at 1.47 in the units where middle gray is 0.18, 3 stops above it (section 2), where ACES only approaches white. With the game's exposure, 9.2% of the Maple Valley frame (the sunlit wall, the glare on the asphalt) and 2.1% of the Bernese Alps frame were white, against the references' highlights that rarely clip. The preset takes `exposure=-0.5`: 1.7% on Maple Valley and 0.1% on the Bernese Alps. The price is a darker picture than `aces` (mean 0.519 against 0.572 on Maple Valley, 0.295 against 0.339 on the Bernese Alps) with a p95 below `aces`' on the Bernese Alps (0.747 against 0.806); the deeper blue sky stays (sky saturation 0.292 against 0.205 there). Whether the owner prefers the brighter picture with clipped highlights is a question of taste: `-Look "gt7,exposure=0"` is the first version.
- **The wide bloom showed nothing.** With `bloom_width` 0.5 or 1 the frame measures the same as without it, and the 1:1 crops of the sky and the buildings near the sun's side show no difference: the game's bloom texture is faint where the sun is not in view.
- **GPU cost alone** (one cell at a time under an exclusive queue slot, the frozen frame's median GPU time, 2x): `aces` 5.02 and 5.24 ms in two runs, `gt7` 5.27 ms, `fm2023` with `bloom_width=0.5` 5.76 and 5.58 ms, without it 4.91 ms. The GT7 operator costs within the runs' spread of about 0.2 ms, the haze pass 0.02 ms, and the wide bloom's 16 taps about 0.66 ms. The preset `fm2023` therefore leaves `bloom_width` out until a frame with the sun in view shows what it adds; the knob stays.
- Ambient occlusion (`ao=0.9` in `fm2023` against 0.6): the 1:1 crops of the car show no difference that can be told from the run's own changes; the car's rear faces away from the low sun, in shade, where the final combine's occlusion weighs most, so a frame with the sun on the car's side is still to be judged.

