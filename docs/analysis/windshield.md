# Cockpit Windshield and Side Windows (19 and 20 October 2026)

In the race cockpit camera FM4 draws no window glass for the player's car, so the windshield and the side windows look like open frames ([cockpit brightness](cockpit-brightness.md#windshield-and-side-windows)). This note designs a post pass that adds the glass on the RTV path, and records the prototype: `windshield.hlsl` in [work/fm4-rex/passes](../../work/fm4-rex/passes), behind the look knob `windshield`, 0 by default and 1 in the `pc` preset since 20 October 2026 ([on top of pc](cockpit-brightness.md#in-the-pc-preset-20-october-2026)).

## What the frame gives

The post pass framework ([post pass framework](post-pass-framework.md)) already holds everything the pass needs; no runtime change is required.

- **The cockpit frame.** The manifest's `[motion] interior` names the interior shell `4EE552137CA415E3`. In a frame where it draws, `xe_motion_interior_slot` is the slot of its stencil reference (the player's car ID); otherwise it is `XE_MOTION_NO_INTERIOR`. The interior is then covered by a mask at the stored depth 1.0, so its pixels are the player's car ID at that depth (`xe_motion_is_interior`, [motion vectors](motion-vectors.md#9-the-gaps-in-the-game-19-october-2026)).
- **The window pixels.** In a cockpit frame every pixel that is not the interior sees the world through glass: the windshield, the side windows and the rear window, with the track, the sky and the other cars behind them. The interior covers the pillars, the dashboard, the gauges and their glass, the steering wheel and hands, and the interior mirror (the motion vector debug view 5 marks all of them as interior). Pixels at the mask depth without the interior's ID, or within 1% of the near plane, are neither (the motion vectors' "unknown").
- **The 3-2-1 camera** draws the car's own exterior glass (`E62CFDF09D758B6C`) and no interior shell, so the pass stays off there. The chase, hood and bumper cameras draw no interior under the mask.
- **The HUD, the side mirrors and the virtual rear-view mirror** draw after the final point, over `final_color`, so a pass at the final point cannot touch them.
- **The camera.** The glass is fixed to the head, which moves a little against the car; a screen-fixed pattern (dust) is close enough. The view direction of a pixel (`xe_scene_view_position`) would separate the windshield from the side windows if an effect needs it; the prototype treats them alike.

### The frame test

`xe_motion_interior_slot` alone would switch the glass on in any frame that draws the interior shell. The pass also needs the interior's pixels to cover at least `WINDSHIELD_MIN_INTERIOR` (0.1) of the frame (one thread group sums the interior share of the 1/8 blocks into one texel). In the cockpit camera of the default car on the Bernese Alps the interior covers 51% of the frame.

### Where it runs

At the final point, after TAA and before the HUD:

- After TAA, because the glass moves with the camera while the motion vectors follow the world behind it: in the TAA history a reflection or dust would smear. The window mask is taken from the jittered scene depth and stencil, so its edges are softened over the 3x3 around each pixel.
- On a still camera (since 20 October 2026, when a still camera is jittered again and TAA accumulates its phases) the jitter still moved the rim, the glare and the dust from frame to frame. There `windshield_compose` accumulates the light the glass adds (linear: the composed color less `final_color`) in the history texture `windshield_history`, as TAA's step 11: the mean of the frames since the camera stopped, up to `TAA_STILL_FRAMES`. In motion the light of the frame is used as before, so the glass has no lag when the camera starts or stops.
- At the final point the image is display referred (after the exposure and the tone curve). The glass works on the squared values (about linear). At the scene_color point (before the game's post chain, through the write-back), the glare would bloom with the rest of the scene and the tint would pass through the exposure. That is a later option; the final point keeps the effect independent of the look's tone curve.

## The effects

All scale with the run-time param `WINDSHIELD` (the look knob `windshield`, 0 off, up to 2). The others are defines of the manifest, live through `shader_mod_set` and `defines:` steps (a recompile).

| Effect | Model | Defines (default) |
| --- | --- | --- |
| Tint | The light through the glass times a color: laminated glass absorbs a little red. | `WINDSHIELD_TINT` (1.0), `WINDSHIELD_TINT_COLOR` (0.88, 0.95, 0.93) |
| Dashboard reflection | Per column, the dashboard's top edge is the first interior pixel below the window that is dark (linear luma at most `WINDSHIELD_REFLECTION_HOOD`) and nearly neutral (its lowest channel at least half its highest): the hood, which lies under the interior's mask too but is seen through the glass, is car paint, lit from outside or colored, and is skipped. The interior that lies as far below that edge as the pixel lies above it, times `WINDSHIELD_REFLECTION_SQUASH` (the dashboard is seen at a grazing angle), is added where it passes the same test, fading out over `WINDSHIELD_REFLECTION_HEIGHT` of the frame height. | `WINDSHIELD_REFLECTION` (0.12), `WINDSHIELD_REFLECTION_HEIGHT` (0.3), `WINDSHIELD_REFLECTION_SQUASH` (0.6), `WINDSHIELD_REFLECTION_HOOD` (0.03) |
| Glare | The light above `WINDSHIELD_GLARE_THRESHOLD` (linear luma) seen through the glass, spread over `WINDSHIELD_GLARE_RADIUS` of the frame height: low sun and bright sky veil the glass around them. | `WINDSHIELD_GLARE` (0.3), `WINDSHIELD_GLARE_THRESHOLD` (0.3), `WINDSHIELD_GLARE_RADIUS` (0.25), `WINDSHIELD_GLARE_COLOR` (1.0, 0.95, 0.85) |
| Dust | A fixed pattern on the glass, in frame heights so it is the same at every resolution: smudges of value noise, soft round specks (one cell of 1/120 frame height in eight), and on the windshield the arcs of two wipers around pivots below the frame. The windshield is where the view direction is within 30 degrees of straight ahead, fading out by 45 (side windows have no wipers). Lit by the glare only: it shows toward the sun and vanishes in the shade. | `WINDSHIELD_DUST` (1.0) |

The added light is blended like a screen (it fades toward white and never clips harder than the image). `WINDSHIELD_DEBUG` shows 1 the pixel kinds (window cyan, interior red, the mask without an interior magenta; a bar at the top left green in a cockpit frame, red otherwise), 2 the added light alone (4 times) and 3 the dust pattern.

Options not built:

- A darker band at the top of the windshield (a shade band), from the view direction's elevation.
- Rain drops and wipers (the game's rain is not in FM4's tracks).
- A reflection of the sky instead of the dashboard on the side windows, from the view direction.
- The glass at the scene_color point through the write-back (see above).
- A hook that makes the title draw its own exterior glass in the cockpit camera; its shader is made for views from outside ([cockpit brightness](cockpit-brightness.md#windshield-and-side-windows)).

## The passes

[windshield.hlsl](../../work/fm4-rex/passes/windshield.hlsl), five passes at the final point, after TAA and before the motion debug view, all with `if = "WINDSHIELD > 0 && XE_SCENE > 0"`:

| Pass | Size | Does |
| --- | --- | --- |
| `windshield_reduce` | 1/8 | Per block of 8x8 final pixels: the mean linear color and the share of its interior pixels (`windshield_interior`) and of its window pixels (`windshield_window`); per final pixel its kind (`windshield_kind`, R window, G interior). |
| `windshield_frame` | one group | The interior's share of the frame, the mean of `windshield_interior`'s shares, into one `R32_FLOAT` texel (`windshield_frame`). |
| `windshield_light` | 1/8 | The reflection (RGB, a downward search of at most `WINDSHIELD_REFLECTION_HEIGHT` of the column and three taps across) and the glare (A, 32 taps on a golden angle spiral) into `windshield_light`. |
| `windshield_compose` | final | `final_color` with the glass over the window pixels into `windshield_out`, alpha -1 on the pixels it leaves alone (every pixel outside the cockpit camera), or a debug view. The coverage is the window share of the 3x3 around the pixel, weighted 1 2 1 (four bilinear taps of `windshield_kind`); the dust is computed only where the glare is above 0. |
| `windshield_output` | final | A pixel pass into `final_color` that discards the pixels with alpha -1, so they keep their value bit for bit. |

The cockpit test runs in `windshield_light` and `windshield_compose`: outside the cockpit camera the light is zero and every pixel is discarded.

## Verification (19 and 20 October 2026)

BuildRoot `I:\FM4-build-pass` (game build of 19 October 2026, no runtime change: the passes are files of `post_pass_path`), the USA Disc 1 through `game_data_root` and a copy of the USA BuildRoot's profile, which starts races in the cockpit camera. `e2e.py` at 2x (2560x1440) with `--keep-ppm`, 60 Hz, quality `high`, the title's AI driver (`engageaionstart`), Quick Race with the default car on the Bernese Alps Festival Circuit, freezes with rewind (`Y`). `WINDSHIELD` and `WINDSHIELD_DEBUG` were switched by `defines:` steps with hot reload, or every 1.3 s by `post_pass_variants`. Results stay in `I:\FM4-build-pass\e2e\windshield-*`, outside the repository.

| Run | Settings | What it checks |
|---|---|---|
| `windshield-1` | `-Look pc` (TAA), the first version | Debug view 1 in the intro, the 3-2-1 camera, the race HUD and the cockpit; three cockpit freezes and one in each of the hood, bumper and both chase cameras |
| `windshield-2` | `-Look "pc,taa=0"` | Frozen frames are byte-identical within a setting without TAA's jitter: `WINDSHIELD` 0 against 1 in three cockpit freezes and in the hood and far chase cameras |
| `windshield-3` | `-Look pc` | The look at 1x and 2x (`WINDSHIELD` 0, 1 and 2, debug views 2 and 3) after the reflection and dust changes |
| `windshield-glare` | `-Look pc` (with `windshield=1`), 20 October 2026 | Four cockpit freezes: the glass off, the first glare and dust values, and two stronger sets (below) |
| `windshield-cost`, `-4`, `-5` | `REX_FRAME_GRAPH_EVERY=1`, `post_pass_variants` on and off | GPU time, and the final version on and off in one cockpit freeze and one hood camera freeze |

## Results

- **Detection.** Debug view 1 marks the windshield and the side window as window, and the dashboard, the gauges, the wheel and hands, the pillars, the headliner, the hood, the wipers, the interior mirror and the left side mirror as interior. The frame bar is green in every cockpit frame and red in the intro camera, the 3-2-1 camera, and the hood, bumper and both chase cameras. The interior covers 51% of the frame.
- **Only the window pixels change** (`windshield-2`, TAA off): in a cockpit freeze, `WINDSHIELD` 1 against 0 changes 99.2% of the 1,450,980 window pixels (the rest are pixels the change rounds away) and 0.51% of the 1,876,431 interior pixels, the one-pixel rim along the window edges that the soft coverage reaches; the same frame with 0 before and after is byte-identical. Under the HUD's translucent bar only the side window below it changes (x 0 to 218); the HUD's text, gauges and minimap, the interior mirror and the side mirror do not. The final version (`windshield-5`) gives the same picture: on and off alternate exactly with the variant periods, and the difference covers the windows only.
- **Other cameras do not change.** In frozen hood and far chase frames, `WINDSHIELD` 0 and 1 are byte-identical (`windshield-2`; the hood camera again with the final version in `windshield-5`).
- **The look** (`windshield-3`, TAA on): at 1 the world through the glass is a little darker and greener, with a faint haze over the bright sky; at 2 the haze and the dust show toward the sky. The dashboards of FM4 are nearly black, so its reflection is barely visible. The first version mirrored the window's lower edge, which is the hood's top edge (the hood lies under the interior's mask): it reflected the red hood as a red smear above it, and its dust specks were square value noise. The hood test and the round specks replaced both.
- **GPU time** (2x, one cockpit phase of 45 s per run, the one-draw pass of the final combine, which also holds the final point, in frames within the variant periods): +0.54 ms with the first versions, where `windshield_reduce` summed 64 pixels per thread; +0.28 ms (0.686 against 0.407 ms) with one thread per pixel and a group per block (`windshield-5`, 565 and 559 frames). TAA at the final point costs 0.21 ms.
- `fm4_post_pass_shader_tests` passes with the new manifest (the glass off, as by default). With `WINDSHIELD=1` it also runs the glass in every frame of its TAA tests on WARP, which takes too long to finish; it was stopped.

- **Glare and dust strength** (`windshield-glare`, 20 October 2026). The project owner found them hardly visible at strength 1 with the first values (`WINDSHIELD_GLARE` 0.15, threshold 0.45, radius 0.2, `WINDSHIELD_DUST` 0.5): only the brightest sky passed the threshold, and the haze over it barely changes a pixel that is already bright. In the same frozen frames two stronger sets were compared: v1 (glare 0.3, threshold 0.3, radius 0.25, dust 1.0) shows a light haze and the wipers' arcs as dust over the cars and the road ahead toward bright sky and snow; v2 (glare 0.5, threshold 0.2, radius 0.3, dust 1.5) a thick milky patch over dark cars. The project owner chose v1, the defaults since 20 October 2026.

- **The still camera's accumulation** (`ck-diag`, `ck-ws`, 20 October 2026; BuildRoot `I:\FM4-build-pass`, USA Disc 1, 2x, a cockpit rewind freeze with one tile and TAA, `WINDSHIELD` switched live): before the accumulation, consecutive captures 2.1 s apart changed in 0.09 to 0.17% of the pixels by more than 4 of 255 with the glass and 0.06 to 0.07% without it, the difference on the windows' rim and the dust; with it, 0.06% with the glass once settled, the same as without it (0.05 to 0.06%), while the glass still changes the window pixels (26.8% of the frame against the glass off). Every frame in which the glass ran had `windshield_history` ready. A smoke gate at 240 Hz and 1x (`stws` against `stmerge`) passed 3 of 3 cells (`career` PASS, `photo` and `photo_view` WARN on capture statistics only).

## Open

- The look of the effects is a first proposal; the project owner put `windshield=1` into `pc` on 20 October 2026.
- The windshield and the side windows are told apart only by the view direction (for the wiper arcs); the reflection's mirror line is the dashboard's top edge in each column, which assumes a dark, nearly neutral dashboard.
- Other cars, tracks, rain, night and the USA against the Japan/Asia release were not checked; convertibles with the roof open would get glass where there is none.
