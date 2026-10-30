# Career season soak and performance survey, 30 October 2026

The first long run of the World Tour in one game process: 19 events and 22 races in 2.04 hours, driven by the `career_season` scenario ([e2e harness](../e2e/README.md#career-season-and-soak)). The run is also the project's first soak test (memory, handles and GPU memory) and a performance survey per event and phase. This note records the measurements and ranks the optimisation candidates they point to. It changes no code.

## 1. The run

| Item | Value |
|---|---|
| Run | `cs-soak1` on `I:\FM4-build-usa`, 30 October 2026, 08:31 to 10:34 |
| Game build | `6a0c784` (`build-result.json`, not dirty); the driver from `fdea103` |
| Settings | native path, 2x (2560x1440), 240 Hz, the default look and quality, the title's AI driving (`engageaionstart`) |
| Profile | a copy of the BuildRoot's profile at Amateur Division event 3 of 5 |
| Queue | one acquisition of class `perf`, the GPU alone; the BuildRoot's shader and pipeline caches |
| Stop rule | `--seasons 1 --min-hours 2 --max-hours 3`: the first event that ended after 2 hours |
| Result | exit 0, no failure, 19 events in one process, 0.36 GB of run files |

Four development runs (`cs-dev1` to `cs-dev4`, class `check`, 2x, 240 Hz) came first and found the driver's gaps in the season's end and the next division ([section 6](#6-what-the-development-runs-found)).

## 2. The career

| # | Event | Division | Places | Credits before → after |
|---|---|---|---|---|
| 1 | Class F Asian Tour (Tsukuba Short) | Amateur | 1 | 8,610 → 15,840 |
| 2 | Class F TopGear Challenge (bowling) | Amateur | (no place) | 15,840 → 17,090 |
| 3 | Class F World Championship (Hockenheim Short, two heats) | Amateur, its last event | 1, 1 | 17,090 → 33,800 |
| 4 | Inline Four Showcase | Clubman, its first event | 1 | 33,800 → 41,940 |
| 5 | Ford Ka Spec Series (Sunset Club) | Clubman | 1 | 41,940 → 56,505 |
| 6 | Multi-class race (Camino Viejo Mini) | Clubman | 1 | 56,505 → 66,765 |
| 7 | Class E North American Tour (Mazda Laguna Seca) | Clubman | 1 | 66,765 → 73,870 |
| 8 | Class E European Tour (Nürburgring GP Short) | Clubman | 3 | 73,870 → 94,450 |
| 9 | Mountain Chase Yaris S (Fujimi, 1 vs. 1) | Clubman | 1 | 94,450 → 101,325 |
| 10 | Class E World Championship (two heats) | Clubman, its last event | 4, 1 | 101,325 → (not read) |
| 11 | Normal Aspiration Run Off (Maple Valley Short) | Sportsman Series, its first event | 1 | (not read) → 144,910 |
| 12 | Class C European Tour (Mugello Club) | Sportsman Series | 1 | 144,910 → 151,925 |
| 13 | Class C TopGear Challenge | Sportsman Series | (no place) | 151,925 → 153,175 |
| 14 | Class C North American Tour (Sebring Club) | Sportsman Series | 2 | 153,175 → 158,890 |
| 15 | Class C World Championship (Infineon NASCAR, two heats) | Sportsman Series | 1, 1 | 158,890 → 206,840 |
| 16 | Class C Asian Tour | Sportsman Series | 1 | 206,840 → … |
| 17 | Class B North American Tour (Club Circuit) | Sportsman Series | 1 | … |
| 18 | European Autocross B-Class | Sportsman Series | (no place) | … |
| 19 | Class B European Tour | Sportsman Series | 3 | … |

- Two divisions ended in the run, each with the title's season end screen (`208_SEASON_END`) and the list that offers the next one: Amateur after event 3 and Clubman after event 10. The Sportsman Series was at its ninth event when the run stopped. The driver's record also marks events 8 and 15 as a season's last event, which they are not: the SEASON STATUS box shows five rows, so a longer season's last row is not on it. The driver now marks a season's end only by the title's end screen ([section 6](#6-what-the-development-runs-found)).
- The credits check (the credits after an event less those before it equal its earnings and reward credits): 16 of 19 events match, none differ, 3 are not read. The two events around each unread event screen (4 and 11) are checked together: 101,325 + 9,505 + 9,080 + 25,000 = 144,910.
- The AI driver drives the player's own car of each event; the driver changes and buys no car. Eleven gift cars were accepted with the title's default choice.

## 3. Soak

Samples every 30 s (246 samples) of the game process, after the warm-up (the first race's end, 319 s after the launch):

| Metric | First → last | Slope a hour | Floor rise a race | Threshold | Verdict |
|---|---|---|---|---|---|
| Private bytes | 5,307 → 7,642 MB | +963 MB | +96 MB, 16 rises in a row | 50 MB | FAIL |
| Dedicated GPU memory | 2,384 → 3,588 MB | +343 MB | +37 MB | 25 MB | FAIL |
| Shared GPU memory | 225 → 357 MB | +57 MB | +6 MB | none | |
| Handles | 3,376 → 3,603 | +152 | +14 | 100 | FAIL |
| Threads | 84 → 81 | −0.2 | 0 | 5 | PASS |
| GDI and USER objects | 11 and 30, flat | 0 | 0 | none | |
| Race frame p50 | 4.77 → 4.95 ms (first third against last third) | | | +10% | PASS (+3.7%) |
| Race frame p99 | 7.78 → 8.04 ms | | | +10% | PASS (+3.3%) |

The growth slows down over the run:

| Window after the launch | Private bytes a hour | Dedicated GPU memory a hour | Handles a hour |
|---|---|---|---|
| 5 to 60 minutes | +1,362 MB | +613 MB | +151 |
| 60 to 90 minutes | +1,099 MB | +242 MB | +87 |
| 90 to 123 minutes | +187 MB | +92 MB | +84 |

The private bytes' floors between races (the lowest sample from one race's end to the next) were 7,262, 7,322, 7,245, 7,324, 7,339 and 7,351 MB over the last six races: about +18 MB a race, against about +100 MB a race in the first hour.

Reading:

- Nothing grows without a limit in this run. Every event was on a track and with cars the run had not loaded before, so a cache that holds each new track's or car's data grows the same way. Growth that slows to a fifth while new content still comes fits a cache that fills, not a leak per race.
- The thresholds still fail, and the run cannot tell a cache from a slow leak. A second soak that repeats events (the same track and car) after the first pass over them would: a cache stays flat on a repeat, a leak keeps its slope.
- Threads, GDI and USER objects are flat. Handles rise by about 85 a hour in the second half. The race frame times hold within 4% over the run.

## 4. Performance survey

Per phase, over the whole run (`season-perf.json`; frame times from the perf trace, GPU time from the GPU timestamps, the hitch count is frames over twice the phase's median and over 34 ms):

| Phase | Seconds | fps | p50 / p95 / p99 ms | Frames over 50 ms | Hitches | GPU ms a frame | Render / submit thread ms a frame |
|---|---|---|---|---|---|---|---|
| Menus | 1,466 | 59.5 | 16.7 / 17.1 / 17.4 | 40 | 59 | 2.27 | 0.86 / 0.60 |
| Loading | 375 | 36.8 | 33.1 / 33.7 / 41.2 | 31 | 15 | 4.42 | 1.63 / 0.96 |
| Pre-race menu | 140 | 98.2 | 8.5 / 33.0 / 33.6 | 19 | 59 | 7.80 | 3.70 / 2.21 |
| Countdown | 187 | 156.2 | 5.8 / 9.8 / 10.4 | 0 | 2 | 5.83 | 4.22 / 2.91 |
| Race | 4,464 | 194.8 | 4.8 / 6.8 / 7.6 | 0 | 3 | 5.02 | 3.48 / 2.94 |
| Finish (after the line, the title's 30 fps) | 107 | 30.7 | 33.3 / 33.7 / 33.9 | 5 | 2 | 7.06 | 3.46 / 1.96 |
| Rewards (post-race, earnings, level, gift car) | 622 | 32.6 | 33.3 / 33.9 / 42.2 | 161 | 115 | 6.84 | 2.34 / 1.49 |

The race's GPU time a frame by part: the scene 3.55 ms, GTAO 0.71, TAA 0.34, the windshield 0.33, motion vectors 0.06, the swap 0.04.

Per event, the race and the screens around it:

| # | Track | Race s | Race fps | Race p99 ms | Race GPU ms | Pre-race p99 ms | Rewards p99 ms | Rewards frames over 50 ms |
|---|---|---|---|---|---|---|---|---|
| 1 | Tsukuba Short | 181 | 198.0 | 7.38 | 4.94 | 33.6 | 49.6 | 12 |
| 2 | TopGear Loop | 77 | 212.4 | 6.11 | 4.62 | 33.5 | 33.9 | 3 |
| 3 | Hockenheim Short | 376 | 193.3 | 7.76 | 5.05 | 41.2 | 42.2 | 16 |
| 4 | Inline Four Showcase | 226 | 205.3 | 7.00 | 4.76 | 33.6 | 33.8 | 3 |
| 5 | Sunset Club | 277 | 193.7 | 7.74 | 5.06 | 33.6 | 41.6 | 10 |
| 6 | Camino Viejo Mini | 188 | 195.3 | 7.60 | 5.00 | 33.6 | 34.0 | 4 |
| 7 | Mazda Laguna Seca | 251 | 195.9 | 7.55 | 4.99 | 33.9 | 41.8 | 10 |
| 8 | Nürburgring GP Short | 249 | 189.8 | 7.86 | 5.15 | 33.7 | 34.0 | 4 |
| 9 | Fujimi (Mountain Chase) | 149 | 196.9 | 7.13 | 4.97 | 33.7 | 49.7 | 9 |
| 10 | Class E World Championship | 303 | 188.6 | 7.93 | 5.18 | 33.5 | 37.1 | 17 |
| 11 | Maple Valley Short | 260 | 201.5 | 6.71 | 4.86 | 33.5 | 33.8 | 3 |
| 12 | Mugello Club | 236 | 197.9 | 7.30 | 4.94 | 33.7 | 51.9 | 11 |
| 13 | TopGear Loop | 72 | 211.8 | 6.22 | 4.60 | 14.1 | 33.9 | 3 |
| 14 | Sebring Club | 259 | 192.4 | 7.40 | 5.09 | 33.6 | 66.3 | 13 |
| 15 | Infineon NASCAR | 415 | 196.2 | 7.59 | 4.98 | 33.6 | 49.6 | 14 |
| 16 | Class C Asian Tour | 225 | 185.5 | 8.09 | 5.28 | 33.4 | 51.5 | 12 |
| 17 | Club Circuit | 261 | 188.8 | 7.97 | 5.19 | 33.6 | 34.1 | 3 |
| 18 | European Autocross | 172 | 206.7 | 6.47 | 4.72 | 9.0 | 33.8 | 1 |
| 19 | Class B European Tour | 290 | 185.0 | 8.26 | 5.28 | 33.7 | 51.7 | 13 |

Frames over 50 ms: 256 in the run, 159 of them in the rewards, 41 in the menus, 31 in the loadings, 20 in the pre-race menus, 5 after the finish line, none in a race. 14 of them hold shader or pipeline work (370 ms in all). The longest:

| Frame | When | Where | Shader or pipeline work in it |
|---|---|---|---|
| 733 ms | 6 s after the launch | boot | none (the look passes compiled on the workers then: 24 pass compiles, 894 ms) |
| 566 ms | event 7 | menus | none |
| 389 ms | event 12 (Mugello Club) | loading | 365 ms: a wait for a pipeline that the creation threads were making (3 early pipelines, 783 ms, the longest 369 ms) |
| 381 ms | event 15 | level rewards (`910`) | none |
| 365 and 306 ms | event 10 | level progress (`909`) | none |
| 342 ms | event 14 | level rewards (`910`) | none |
| 150 to 262 ms, once an event with a gift car | events 1, 3, 5, 7, 9, 10, 12, 14, 15, 16, 19 | the gift car's paint screen (`114`), 4.9 to 5.2 s after it opens | none |

By screen, the rewards' frames over 50 ms fall on the gift car's paint screen (59), the level progress (42), the gift car's YES dialog (32) and the level rewards (25).

The shader and pipeline work in play over the whole run: 7 vertex and 89 pixel shader translations (9 ms), 39 pipelines on the draw path (3 ms), 25 waits for pipelines (768 ms), and look mods compiled on the workers on new tracks (none on the draw path).

## 5. Optimisation candidates

Ranked by what a player would notice. The first ranking came from this run's measurements alone; the reward screens' traces of [section 9](#9-the-reward-screen-hitches-30-october-2026) re-ranked it: their hitches are the title's own work, so they moved from the first place to the last.

1. **The race at 2x is GPU-bound below 240 fps.** 194.8 fps with 5.02 ms of GPU a frame; 240 fps needs 4.17 ms. The scene is 3.55 ms, and the look's passes are 1.44 ms, GTAO the largest (0.71 ms, 14% of the frame). Candidates: GTAO at a lower resolution or fewer samples at 2x, and a check of what the scene's 3.55 ms holds at 2x against 3/2 (3.28 ms of GPU a whole frame there in the Level 1 gate). The render thread is busy 3.48 ms of a 4.79 ms frame, so after the GPU it is the next limit.
2. **The title render thread's pow for the gamma ramp.** Found by the reward screens' traces ([section 9](#9-the-reward-screen-hitches-30-october-2026)): `SetGammaRamp` recomputes its ramp with `pow` every frame, 9.5% of the title render thread's samples in races and a third on the reward screens. A memo of the guest's own results takes it off ([section 11](#11-native-crt-helpers-30-october-2026)).
3. **Pipelines for a new track at its loading.** One wait of 365 ms at Mugello Club's loading for a pipeline the creation threads were making (the longest early pipeline 369 ms), and 25 waits of 768 ms over the run. These are pipelines that the pipeline library did not hold for a track first seen in this run. Adding the career's tracks to the offline pass, or creating a track's pipelines while its loading screen shows, would take them off the loading's frames.
4. **The pre-race menu's GPU time and hitches.** It has the highest GPU time a frame (7.80 ms; the scene 6.47 ms, the showroom car at 2x) and the most hitches a minute at Hockenheim (27). Its p99 of 33.6 ms is the menu's own 30 fps transitions. A trace of what the showroom draws at 2x would say whether it needs the race's scale.
5. **Long frames in the menus (566 ms at event 7) and at boot (733 ms).** The boot's frame is in the window where 32 look mods compiled on the workers (14 s on the workers, 110 pending), so the BuildRoot's mod cache lacked the default look's defines after the look presets' latest change; the menus' 566 ms frame holds no shader work. Both happen once; a trace of the menu frame is the next step.
6. **Memory growth while new content loads.** Not a frame time cost, but the soak's open question ([section 3](#3-soak)), answered by the soak that revisits tracks ([section 10](#10-the-soak-that-revisits-tracks-and-cars-30-october-2026)).
7. **The reward and gift car screens (frames of 150 to 380 ms).** 159 of the run's 256 frames over 50 ms, about 12 an event with a level-up, on the gift car's paint screen and the level screens. They are the title's own CPU work, the synchronous compression of the profile's save and the first draw of a new car, with about 1 to 3% of their time in the runtime ([section 9](#9-the-reward-screen-hitches-30-october-2026)); the main session's decision of 30 October 2026 leaves them. The screens run at the title's 30 fps.

## 6. What the development runs found

The driver needed these changes before the long run. All are in `career_season.py` and covered by `test_career_season.py`.

- **Tilted artwork misleads Windows OCR.** The Inline Four Showcase's event screen has a banner tilted by 15 degrees; Windows OCR estimated the text angle at −14.9 degrees and deskewed the whole image, so the straight menu read garbled and START RACE not at all. Blanking the division's vertical title did not help: the angle came from the banner. The event screen opens with START RACE focused, so the driver presses A there when it cannot read the menu, and takes the credits from the World Tour row. Events 4 and 11 ran this way.
- **The season's end.** `208_SEASON_END` ends a season and moves on by itself; `207_SEASON_LIST_DIALOG` then names the next division ("You now qualify for next year's CLUBMAN DIVISION.") and takes A.
- **The SEASON STATUS box shows five rows.** A season of more events ends below them, so the box cannot say which event is a season's last. The driver now marks a season's end only on `208_SEASON_END`. The long run predates that change, so its `seasons_done` lists events 8 and 15 as well.
- **Places and earnings.** The post-race menu's place did not read; the earnings and level screens' "YOU PLACED" does. An earnings total read as "+8,1 IO CR" is read with the letters as digits.
- **The title's 30 fps after the finish line.** A race's last 8 s after the line run at 30 fps until the race scenario ends. The survey counts them as the phase `finish`, so the race phase holds the driven race only.

## 7. Kernel and XAM stubs called

From the long run's `runtime.log` and its four rotated parts (the whole run), and the same list in the four development runs:

| Stub | Calls in the 2-hour run | When |
|---|---|---|
| `XamXStudioRequest` | 240,039 | from the career's start, about 33 a second, every screen |
| `NetDll_getsockopt` | 38 | from the first event on, about two an event |
| `EtxProducerRegister` | 21 | from the career's start on, about one an event |
| `EtxProducerUnregister` | 19 | from the first event on, about one an event |
| `XamVoiceSetMicArrayIdleUsers` | 6 | at the boot |
| `IoDismountVolumeByFileHandle` | 1 | at the boot |
| `ObCreateObject` | 1 | at the career's start |
| `ObReferenceObject` | 1 | at the boot |
| `XFileFsDeviceInformation` | 1 | at the boot |
| `XamGetActiveDashAppInfo` | 1 | at the career's start |
| `XamMediaVerificationCreate`, `XamMediaVerificationClose` | 1 each | at the boot |
| `XamNuiCameraTiltSetCallback` | 1 | at the career's start |
| Unimplemented XAM message `app=000000FE, msg=0002B003` | 1 | at the career's start |

No stub was called for the first time after the first event: the list is the boot's, the career's start and each event's own calls. `XamXStudioRequest` alone writes most of the log (240,039 lines in 2 hours), so the runtime's 5 MB log rotation keeps only part of a longer run. The other scenarios (Quick Race, photo mode, replays, DLC) have not been listed.

## 8. Not covered

- One run of each kind: no repeat of the soak, and no soak that revisits a track.
- 60 and 120 Hz, other scales and looks, RTV, the ROG Xbox Ally X.
- Buying or changing cars: the player's own car (a Ford Ka) and the title's eligibility carried every event.
- The third division's end, and seasons after it (the run stopped at 2 hours, as set).

## 9. The reward screen hitches, 30 October 2026

The candidate that the first ranking put first: what the frames over 50 ms on the reward and gift car screens wait on. The perf trace's windows of cs-soak1 showed the host render thread mostly idle in them (750 to 830 ms a second waiting for the guest) while one guest thread used a whole core, so the time is on the guest's side. A stack sampler of the process's busy threads (`REX_THREAD_PROFILE`, [traces](../e2e/traces.md)) then ran in two career runs at 2x and 240 Hz on the native path: `rh-prof2` from the BuildRoot's profile (20 reward frames over 50 ms, 2.47 s) and `rh-prof3` from cs-soak1's last profile (25 frames, 3.01 s, the worst 375 ms on `910`). Guest functions are named through the game's linker map and the function catalog ([function catalog](function-catalog.md)).

Inside those frames:

| Thread | Busy | Where the time goes |
|---|---|---|
| The title's main thread (`F8000050`) | 0.9 to 1.1 s of the frames' 2.5 to 3.0 s | 336 to 420 ms in a deflate longest-match function (`sub_82C9B658`) and about 90 ms more in the same zlib code: `Game::MainLoop` → the user transaction log's pump → `Database::CSqliteMemoryStream` → `IOSys::CCompressionStream`, the synchronous compression of the profile's SQLite database for its save; and the title's SQLite queries (`CDatabase`, `CRecordSet`, `SELECT MakeID FROM Data_Car`) |
| The title's render thread (`F8001A74`) | 1.7 to 2.0 s | 552 to 590 ms in `pow` (`sub_8230FCF0`) from `CD3D9GraphicsDevice::SetGammaRamp`, every frame, not only in these; the first draw of a new car, `CCarPresentation::Render` → `CCarModel::DrawPart`, its material setup with the title's `strstr` (`sub_82C88DA0`, 108 to 136 ms) on names such as `glassLHL`, the worst frame's main cost |
| The runtime | about 1 to 3% of the samples | waits; no access violation or write watch path in the stacks, and the host render thread not involved |

So the hitches are the title's own CPU work: saving the profile and drawing a new car for the first time. On the Xbox 360 the same code runs on a slower in-order core; that it hitches there too is an inference, not a measurement. cs-soak1's `910` frames with a 53 MB upload and the host render queue's 106 to 145 ms record did not recur under the profiler (where a `910` frame reached 375 ms, the queue's longest record was 21 ms), so that pattern stays open as a one-off.

The smallest mitigations were: a native `strstr` (about 4 to 5% of the hitch time), a native zlib for the save's compression (the largest share, but a different byte stream in the saves, a compatibility risk), and a native `pow` for the gamma ramp (every frame's gain, not the hitches'). The main session's decision of 30 October 2026: leave the hitches; take `strstr` and the gamma ramp's `pow` as native helpers ([section 11](#11-native-crt-helpers-30-october-2026)); no native zlib.