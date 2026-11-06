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
- The thresholds still fail, and the run cannot tell a cache from a slow leak. A second soak that repeats events (the same track and car) after the first pass over them would: a cache stays flat on a repeat, a leak keeps its slope. That soak ran on 30 October 2026 ([section 10](#10-the-soak-that-revisits-tracks-and-cars-30-october-2026)).
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
2. **The title render thread's frame loop spins.** Found by the reward screens' traces ([section 9](#9-the-reward-screen-hitches-30-october-2026)) and measured in [section 11](#11-the-title-render-threads-gamma-ramp-30-october-2026): between two frames the title's render thread turns its frame loop without waiting, and every turn recomputes the gamma ramp with `pow`, 17 to 92 turns a frame. That takes about 160 ms of CPU a second in races and 270 to 600 ms a second in the menus, loading and reward screens: CPU and power that a handheld such as the ROG Xbox Ally X would get back. A wait for the next frame in place of the spin ([section 12](#12-the-render-threads-idle-wait-30-october-2026)) takes 5.2 to 6.0% of the process's CPU time and 4.0 to 8.0% of the energy off a Quick Race, in every phase; it is on by default since 31 October 2026.
3. **Pipelines for a new track at its loading.** One wait of 365 ms at Mugello Club's loading for a pipeline the creation threads were making (the longest early pipeline 369 ms), and 25 waits of 768 ms over the run. These are pipelines that the pipeline library did not hold for a track first seen in this run. Adding the career's tracks to the offline pass, or creating a track's pipelines while its loading screen shows, would take them off the loading's frames.
4. **The pre-race menu's GPU time and hitches.** It has the highest GPU time a frame (7.80 ms; the scene 6.47 ms, the showroom car at 2x) and the most hitches a minute at Hockenheim (27). Its p99 of 33.6 ms is the menu's own 30 fps transitions. A trace of what the showroom draws at 2x would say whether it needs the race's scale.
5. **Long frames in the menus (566 ms at event 7) and at boot (733 ms).** The boot's frame is in the window where 32 look mods compiled on the workers (14 s on the workers, 110 pending), so the BuildRoot's mod cache lacked the default look's defines after the look presets' latest change; the menus' 566 ms frame holds no shader work. Both happen once; a trace of the menu frame is the next step.
6. **Memory growth while new content loads.** Not a frame time cost. The soak that revisits tracks ([section 10](#10-the-soak-that-revisits-tracks-and-cars-30-october-2026)) answered the soak's open question ([section 3](#3-soak)): private bytes and dedicated GPU memory are host caches that fill once for each new track and car and stay level when they come back. What is left is a slow handle leak, about 15 a race; its host Thread handles, from guest threads that have exited, are closed since 6 November 2026 ([section 13](#13-the-thread-handle-fix-6-november-2026)), and Events still rise by about 4 a race.
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

The smallest mitigations were: a native `strstr` (about 4 to 5% of the hitch time), a native zlib for the save's compression (the largest share, but a different byte stream in the saves, a compatibility risk), and a native `pow` for the gamma ramp (every frame's gain, not the hitches'). The main session's decision of 30 October 2026: leave the hitches; try `strstr` and the gamma ramp's `pow` as native helpers; no native zlib. Measured, neither saved anything a player or the power meter would see, and both were taken out again ([section 11](#11-the-title-render-threads-gamma-ramp-30-october-2026)).

## 10. The soak that revisits tracks and cars, 30 October 2026

The question of [section 3](#3-soak): does the memory that grows while new tracks and cars load keep growing when the same tracks and cars come back? The run `qr-soak3` (I:\FM4-build-usa, build `053474b`, at 2x and 240 Hz on the native path, the default look, the BuildRoot's caches, the perf class of the GPU queue) drove Quick Race (`quickrace_loop`, [e2e harness](../e2e/README.md#quick-race-loop)) over the same 11 distinct pairs of manufacturer and track in the same order, three times, with the title's AI driving 180 s of each race on the default 12-car grid:

Abarth on the Bernese Alps' Festival Circuit, AMC at Catalunya, Austin-Healey at Hockenheim, BMW at Indianapolis, Cadillac at Ladera, Citroën at Maple Valley, Devon at Mugello, Ferrari on the Nürburgring Nordschleife (the large track, with the full grid), GMC at Road America, Holden at Sebring and Hyundai at Silverstone.

The run lasted 2.83 h: 33 races, 11 a pass, each pass about 0.94 h, no failure. The pow memo and the native strstr that were being tried then ([section 11](#11-the-title-render-threads-gamma-ramp-30-october-2026)) were off, so the title ran its own functions as on the previous main.

Per pass, the growth a hour inside the pass and the floors (each pair's lowest sample) from the first pair to the last:

| Pass | Private bytes | Dedicated GPU memory | Handles |
|---|---|---|---|
| 1 (new pairs) | +1,463 MB/h, floors 2,980 → 6,390 MB (+3,410) | +776 MB/h, 1,328 → 3,047 MB (+1,719) | +220/h, 3,200 → 3,412 |
| 2 (revisit) | +695 MB/h, 6,445 → 7,058 MB (+613) | +176 MB/h, 3,078 → 3,214 MB (+136) | +178/h, 3,437 → 3,569 |
| 3 (revisit) | +44 MB/h, 7,083 → 7,130 MB (+47) | +1 MB/h, 3,241 → 3,238 MB (−3) | +163/h, 3,589 → 3,716 |

`soak_report.py` judges a run with passes on its last pass: private bytes PASS (44 against 50 MB a hour), dedicated GPU memory PASS (1 against 25), handles FAIL (163 against 100), threads PASS (81 to 82 live threads). The whole run's slopes, beside them, are 574 MB, 186 MB and 179 a hour. The race frame times rose by 5.4% (p50, 4.68 → 4.93 ms) and 6.7% (p99, 7.60 → 8.11 ms) from the first third of the races to the last, within the 10% threshold.

Reading:

- **Memory and GPU memory are a cache.** They fill on the first pass, grow by a sixth of that on the second and are level on the third. The growth is on the host's side: the guest's physical memory did not move (below), so it is the runtime's caches for each new track's and car's content.
- **Handles leak, slowly.** By object type, inside each pass: Thread +115, +112 and +108 (about 10 a race), Event +82, +49 and +51; the rest within ±12. With the live threads level, these are the host Thread handles and Events of guest threads that have exited and are never closed. At about 15 a race (500 in 3 h) this is harmless; the Thread handles are fixed since 6 November 2026 ([section 13](#13-the-thread-handle-fix-6-november-2026)). Semaphores rose from 69 to 1,846 during the boot and the first race and stayed level after.
- **The guest's memory is fixed.** The guest's physical memory (`REX_GUEST_MEM_TRACE`, [traces](../e2e/traces.md)) held 386.8 of 512 MB committed from 80 s after the launch to the end, the same in every pass: vA0000000 370.7, vC0000000 16.0, vE0000000 0.1 MB. The title made 2 physical allocations of 1 MB or more in the whole run, both 1.3 s after the launch from `sub_82759050` (called from `sub_826CBE80`): 314.7 MB at A89F0000 and 56.0 MB at BC4A0000, both of 64 KB pages. It made one free, 64 KB from `D3DDevice_Swap`. Loading new tracks and cars does not commit guest memory; the title manages these pools itself. The virtual heaps peaked at 50.4 MB (v00000000) and 60.8 MB (v40000000). The title never called `MmQueryStatistics`, so `available_pages` has no sample.
- **The runtime's error log** has one key, `BaseHeap::Release failed because address is not a region start` (8 times). It is not new: cs-soak1 has it 38 times.

Two development runs found the driver's bugs:

- `qr-soak1` repeated its pairs. The selects open on their first entry each time they are entered from Free Play, so the driver now counts its presses from the first entry.
- `qr-soak2` stopped at Ladera, where the AI finished the race in 131 s and the driver saw the result's dialog instead of the post-race screen. The driver now looks for the post-race screen among the race's screen events.

## 11. The title render thread's gamma ramp, 30 October 2026

Section 9 found `pow` (`sub_8230FCF0`) under `CD3D9GraphicsDevice::SetGammaRamp` (`sub_82873A30`) in a third of the title render thread's samples on the reward screens and 9.5% in races, and `strstr` (`sub_82C88DA0`) in the first draw of a new car. Two helpers were tried on build `ac1cfbd`, each with a switch:

- **A memo of `pow`** at `SetGammaRamp`'s three call sites: a bounded table (49,152 entries) that returned the guest `pow`'s own earlier result for the same base and exponent bits, on the first thread that called there.
- **The host's `strstr`** in place of the title's byte loop.

They were measured in nine runs on `I:\FM4-build-usa` (one `check` acquisition, the native path, 240 Hz), each pair on the same binary with both helpers off and on.

**The picture is the same.**

- The parity freeze at 3/2 (`pc-off`, `pc-on`, 2 runs each) passes: the same draws and predication.
- The frozen frames differ in 26 to 33% of their pixels, but two runs with the helpers off differ in 32% too: the frozen frame moves by a few pixels from run to run. The 1:1 crops show the same colours and tones.
- The hash of every gamma ramp (`FM4_GAMMA_RAMP_HASH`, [traces](../e2e/traces.md#gamma-ramp-hash-fm4_gamma_ramp_hash)) gave the same set and the same sequence with the memo on and off, at 1x and 2x.
- A run with the memo's check (`pc-qrchk`) ran the guest `pow` again on 278 million hits: no result differed.
- The memo held 208 entries and answered 84% of its lookups; the rest had a base of 0, which it does not keep.

**The frame times.** Race frame times at 1x and 2x (Quick Race, two races of 150 s each; `pc-qr1-off`, `-on`, `pc-qr2-off`, `-on`):

| Run | Race p50 | Race p99 | Median fps |
|---|---|---|---|
| 1x, off | 4.11 and 4.15 ms | 5.03 and 6.30 ms | 239.0 |
| 1x, on | 4.16 and 4.13 ms | 5.30 and 5.13 ms | 238.7 |
| 2x, off | 4.62 and 4.54 ms | 6.96 and 6.84 ms | 206.9 |
| 2x, on | 4.57 and 4.65 ms | 6.98 and 7.42 ms | 200.1 |

At 1x the race holds the 240 Hz limit, and at 2x the GPU limits it (4.56 and 4.70 ms a frame), so the title render thread is never the limit.

The career runs with the thread profiler (`pc-car-off`, `pc-car-on`) ran at 2x for 16 minutes each: three events of the BuildRoot profile with their reward screens. Each had 21 reward frames over 50 ms, 2,437 and 2,475 ms in all. The native `strstr` took its samples from 92 to 9, about 0.2 s over the run, without shortening those frames.

**The CPU.** The process's CPU time and the package's power per phase (`cpu_power.jsonl`) did not fall with the memo:

| Phase | 1x off / on | 2x off / on | Career off / on |
|---|---|---|---|
| Race | 2,966 / 3,105 ms a second, 49.1 / 50.4 W | 3,329 / 3,327, 49.3 / 54.0 W | 3,527 / 3,573, 51.5 / 54.9 W |
| Menus | 2,239 / 2,177, 35.2 / 34.3 W | 561 / 546, 24.6 / 24.7 W | 847 / 622, 30.0 / 26.2 W |
| Rewards | 642 / 739, 24.4 / 26.4 W | 856 / 756, 26.2 / 26.7 W | 1,531 / 1,505, 38.3 / 36.9 W |

Only the career's menus fell; the Quick Race menus did not. The reason is that the ramp is not computed once a frame.

**The frame loop spins.** All the title render thread's samples with `SetGammaRamp` on the stack have one call chain:

1. `CRenderThread::ThreadProc` (`sub_822A90B0`) calls `CRenderThread::RunFrame` (`sub_822DB318`) in a loop until the thread is told to stop.
2. `RunFrame` first calls `CRenderThread::UpdateCombineFade` (`sub_822A71A0`). That reaches `SetGammaRamp` through `sub_8243BB08`, which builds the ramp with `pow`: 128 or 256 entries for three colours.
3. `RunFrame` then takes the queue's critical section and polls the render thread's deferred command queue (`sub_822C0840`): a look at the queue's list, with no wait. The game's main thread fills that queue with each frame's recorded commands.
4. Only when a frame is there does `RunFrame` run it (`CDeferredQueue::ExecuteCommands`, `sub_822E56B0`), present it and wait for its fences.
5. With the queue empty, `RunFrame` returns, and `ThreadProc` calls it again at once.

So between two frames the thread spins, and every turn recomputes the ramp. The trace counts the turns as `SetGammaRamp` calls a second, with the helpers off:

| Phase | 1x | 2x | Turns a frame |
|---|---|---|---|
| Race | 4,317 | 5,789 | 18 at 1x (239 fps), 28 at 2x (207 fps) |
| Menus (59 fps) | 997 | 2,407 | 17 to 41 |
| Rewards (32 fps) | 1,825 | 2,952 | 57 to 92 |
| Loading | 2,522 | 1,192 | |

With the memo each turn was cheaper, so the thread turned 2.2 to 2.5 times as often. The time stayed in the spin; the memo only moved it from `pow` to the rest of the turn.

The time the spin takes on the title render thread, from the profile of `pc-car-off` (2x; about 10 to 25 ms a second of it are the hash trace's writes):

| Phase | The thread running | In `UpdateCombineFade`, the spin | In `ExecuteCommands`, the frame |
|---|---|---|---|
| Race | 712 ms a second | 158 | 547 |
| Countdown | 768 | 257 | 501 |
| Pre-race | 670 | 420 | 230 |
| Rewards | 698 | 598 | 83 |
| Loading | 386 | 319 | 58 |
| Menus | 357 | 271 | 75 |

The spin is the title's own design: on the Xbox 360 the render thread has a hardware thread of its own. On a PC it takes a sixth of a core in races and more than half a core on the reward screens. A handheld such as the ROG Xbox Ally X shares its power budget between those cores and the GPU.

**Decisions.**

- Following the main session's rule (keep a helper only for a measured saving), the memo and the native `strstr` were taken out.
- The gamma ramp hash stays as a trace, to check a change to the loop.
- A wait in place of the spin was decided on the same day ([section 12](#12-the-render-threads-idle-wait-30-october-2026)).

## 12. The render thread's idle wait, 30 October 2026

The main session's decision of 30 October 2026, after section 11: let the title render thread wait for its next frame instead of spinning, off by default behind a switch, and measure it.

**The queue's two sides.**

- The consumer is `RunFrame`'s poll, `sub_822C0840`. It is called from `0x822DB85C` and `0x822DB95C` on the queue at `CRenderThread+2472`. Under the queue's `CScopedLock` at `+64`, it takes the first buffer from the list at `+28` when the list's size at `+32` is not 0, and returns whether there is one.
- The producer is the main thread's `sub_82379E90`, reached from `Game::MainLoop` through `sub_822A2550`. Under the same lock, it inserts the recorded buffer (`+52`) into that list with `sub_82940438`, the call that returns to `0x8237A1BC`, and writes the new size to `+128`.
- The main thread itself throttles in `sub_822AA630`. While more than one frame is queued, it yields (`NtYieldExecution`) and checks again.

**The change** (`fm4-render-idle-wait.cpp`, `fm4_render_thread_idle_wait`, off by default):

- A hook of `sub_82940438` counts a publication after the insert from `0x8237A1BC`.
- The hook of `RunFrame`'s poll notes the count before the poll, and whether the poll found the queue empty. It never waits there, because it runs under the queue's lock, which the main thread needs in order to queue.
- With the switch on, the next `RunFrame` first sleeps until the next publication, at most 1 ms. That sleep is outside every lock: `ThreadProc` holds none between two `RunFrame` calls. Turns that did not poll (the loading frames' path) or that found a frame do not wait.
- The perf trace's `render_idle_waits` gives a second's waits, their timeouts and time, and the frames queued.

The main thread queues two buffers a frame: 464 a second at 232 fps.

**The measurements.** Seven check runs on build `677b07d`, I:\FM4-build-usa, the native path, 240 Hz, each pair off and then on:

- Quick Race at 1x, 3/2 and 2x (`iw-qr1-off`, `-on`, `iw-qr32-*`, `iw-qr2-*`): two races of 150 s each, the gamma ramp hash on.
- The parity freeze at 3/2 (`iw-off`, `iw-on`).

The process's CPU time and the package's power per phase (`cpu_power.jsonl`), off → on:

| Phase | 1x | 3/2 | 2x |
|---|---|---|---|
| Race | 3,076 → 2,833 ms a second, 54.4 → 50.7 W | 3,023 → 2,947, 52.2 → 51.2 W | 3,219 → 2,959, 54.5 → 51.3 W |
| Menus | 2,314 → 2,193, 37.7 → 36.3 W | 1,238 → 1,081, 34.4 → 31.2 W | 583 → 502, 26.3 → 25.1 W |
| Rewards | 864 → 689, 28.2 → 26.6 W | 1,028 → 811, 30.8 → 29.6 W | 829 → 784, 28.0 → 27.6 W |
| Loading | 1,818 → 1,803, 37.7 → 37.2 W | 1,950 → 1,759, 39.2 → 36.8 W | 1,998 → 1,700, 38.3 → 35.0 W |
| Countdown | 2,158 → 2,411, 43.2 → 46.2 W | 2,107 → 2,353, 44.0 → 46.0 W | 2,166 → 2,560, 43.9 → 46.7 W |
| Whole run | −6.8% CPU, −5.7% energy | −4.7%, −3.8% | −7.8%, −5.0% |

The pre-race menu, 11 s a race, waits in none of its turns and moves either way.

The turns of the loop (`SetGammaRamp` calls) in races fell from 5,014, 3,833 and 6,286 a second to 1,219, 1,320 and 1,040. The remaining turns of about five a frame do not poll, or poll a queue that holds a frame. 25 to 31% of the waits in races ended at their 1 ms limit.

The pacing:

| Run | Race fps off / on | Race p50 off / on | Race p99, two races, off / on |
|---|---|---|---|
| 1x | 231.8 / 232.3 | 4.16 / 4.14 ms | 5.18, 5.21 / 5.86, 5.00 ms |
| 3/2 | 230.2 / 230.6 | 4.16 / 4.17 ms | 5.59, 5.66 / 5.65, 5.41 ms |
| 2x | 200.9 / 201.2 | 4.61 / 4.61 ms | 7.49, 7.16 / 7.07, 7.23 ms |

- Loading screens ran at 37.1 → 37.6, 37.4 → 38.2 and 38.3 → 37.2 fps; the menus at 59.1 to 59.4 fps in every run.
- The countdown ran at 172.9 → 170.9, 165.8 → 161.8 and 145.4 → 144.6 fps, with p99 9.45 → 9.23, 9.40 → 9.58 and 10.09 → 12.53 ms.
- The driver's menu navigation reached every race at the same times: the race started 72.6 / 72.0 s and 62.3 / 62.3 s into its cycle at 1x.
- The gamma ramp's hashes were the same set and sequence off and on at every scale. The ramp changes once, 0 to 1 ms after the first ramp at the boot, and never during a fade. So `UpdateCombineFade` does not step a visible fade on each turn, and fewer turns do not change one.
- The parity freeze passes: the same draws and predication. The frozen frames differ in 26% and 32% of their pixels, as two runs of one setting do, and the 1:1 crops show the same picture.

**Reading.**

- The wait takes 76 to 260 ms of CPU a second and 1.0 to 3.7 W off the race, and more in the menus, loading and reward screens. A whole run uses 4.7 to 7.8% less CPU time and 3.8 to 5.7% less energy, with the race's frame rate and p50 unchanged.
- The countdown goes the other way, in all three pairs: 246 to 394 ms of CPU a second and 2 to 3 W more, and 1 to 2% fewer frames. The cause is not measured. A guess is the main thread's throttle, which yields in a loop while two frames are queued: a render thread that has just woken takes its frame later than one that polls.
- Under the rule of the task (on by default only with a saving and no pacing change), the switch stayed off by default until that phase was explained. It was, and the wait is on by default since 31 October 2026 ([12.1](#121-the-countdown-and-the-wait-on-by-default-31-october-2026)).

### 12.1 The countdown, and the wait on by default, 31 October 2026

**The countdown's cost.** A profiled pair (`cd-prof-off`, `-on`: 1x, two races of 60 s, `REX_THREAD_PROFILE`) put it on the title render thread, not on the main thread.

- The extra cost came in one of the two countdowns of a run with the wait: the first race's in the runs above, the second one's here (2,657 against about 2,200 ms of CPU a second).
- In that countdown, the main thread's time rose by about 40 ms a second, and its throttle loop did not show up in the profile.
- The render thread's time rose from about 660 to 785 ms a second:
  - its wait for the GPU in `Resource::WaitForFence` fell from about 280 to 24 ms a second;
  - the gamma ramp's `pow` doubled, from about 73 to 156 ms a second.

The cause is a second path through `RunFrame` that does not poll the queue.

- At `0x822DB70C`, `RunFrame` branches to `loc_822DB9E4` in two cases:
  - **the frame-index branch:** its frame index `r22` is not past `[this+2356]`;
  - **the queue-empty branch:** `[this+2892]` is 2 (the race) and the queue's count (`+128`) is 0.
- That path calls `Sleep(0, alertable)` (`sub_8238C778`, returning to `0x822DBA08`) and returns. Its turns spin past the wait, which follows only the polls.

**The fix** (`75bf3d0`), with the main session's condition that the frame-index branch stays as it is:

- The `RunFrame` hook keeps its `CRenderThread` and the frame queue's count at the turn's start.
- The hook of `sub_8238C778` from `0x822DBA08` tells the two branches apart (`r22` is non-volatile).
- On the queue-empty branch, when the count is still 0, it first sleeps until a frame is queued after the turn began, at most 1 ms, and then runs the title's `Sleep(0)`. This happens outside every lock.
- `render_idle_waits` counts both branches, with the switch on or off.

Off, the queue-empty branch turned 2,600 to 14,000 times a second in the loading, reward and pre-race screens and in some countdowns: that was most of their spin. The frame-index branch took 0 to 1 turn a second.

**The measurements** on build `75bf3d0`, the same runs as above (`iw2-qr1`, `iw2-qr32`, `iw2-qr2`, `iw2-off`, `iw2-on`). The 1x pair was repeated with on first, then off (`iw3-qr1`): the first 1x pair's off run drew more power as it went (42 to 62 W over its fifths), and that put its energy at +6.2% for the wait.

| Run | Whole run CPU | Energy | Race CPU off → on | Race power | Countdown CPU | Countdown power |
|---|---|---|---|---|---|---|
| 1x, on first | −6.0% | −4.0% | 2,807 → 2,690 ms a second | 50.9 → 48.9 W | 2,488 → 2,040 | 46.2 → 41.1 W |
| 1x, off first | −5.8% | +6.2% (drift) | 3,082 → 2,923 | 60.1 → 63.6 W | 2,125 → 2,122 | 48.5 → 55.9 W |
| 3/2 | −5.2% | −5.0% | 2,908 → 2,815 | 53.6 → 51.7 W | 2,088 → 2,135 | 43.7 → 42.8 W |
| 2x | −5.7% | −8.0% | 3,009 → 2,923 | 53.1 → 49.7 W | 2,468 → 2,354 | 48.0 → 44.6 W |

- The menus, reward, loading and pre-race screens use less CPU in every pair, by 40 to 630 ms a second.
- The loop's turns fell to 100 to 440 a second in every phase.
- The pacing did not change:

| Run | Race fps off / on | Race p50 off / on | Race p99, two races, off / on | Countdown fps off / on |
|---|---|---|---|---|
| 1x, on first | 233.5 / 232.8 | 4.12 / 4.13 ms | 4.95, 4.97 / 4.93, 5.55 ms | 175.1 / 173.7 |
| 3/2 | 231.0 / 232.6 | 4.16 / 4.15 ms | 5.85, 5.02 / 5.15, 5.10 ms | 167.6 / 166.6 |
| 2x | 194.2 / 203.3 | 4.72 / 4.59 ms | 6.95, 7.59 / 7.07, 6.90 ms | 147.2 / 151.1 |

- The loading screens ran at 37.4 to 38.4 fps and the menus at 59.1 to 59.4 fps in every run.
- The gamma ramp's hashes were the same set and sequence in every pair. The driver's menu navigation reached every race at the same times.
- The parity freeze at 3/2 passes. `compare.py` warns only that the guest's idle wait rose from 1 to 58 ms a second, which is the wait itself.

**Decision.** Every phase uses less CPU and power with the wait, within the noise of one pair: the 3/2 countdown's +47 ms a second came with 0.9 W less. The frame rate and frame times stay the same. So `fm4_render_thread_idle_wait` is on by default in both launchers (`pipeline.launch_cvars`, which the GUI launcher's `launcher.json` comes from), as `guest_ring_wait_sleep` is. `-Rov`, the uncorrected reference, turns it off.

The ROG Xbox Ally X A/B is the owner's, with `--fm4_render_thread_idle_wait=false` in the extra cvars of the profile without the wait ([render thread idle wait A/B](../running.md#render-thread-idle-wait-ab)).

## 13. The thread handle fix, 6 November 2026

**The cause.** `XThread::Exit` (`work/runtime/src/system/xthread.cpp`) took a reference to its own thread object (`auto self = retain_object(this)`) so that `ReleaseHandle()` could not delete the object before the host thread ended, then ended the host thread with `rex::thread::Thread::Exit`. On Windows that is `ExitThread`, which runs no destructors, so the reference was never dropped. Every guest thread that exited, whether it returned from its start routine (`Execute` ends in `Exit`) or called `ExTerminateThread`, kept its `XThread` for the rest of the session, and with it the host thread's handle (`Win32Thread`, closed only by the object's destructor). `Terminate` on its own thread did the same. The code came from the ReXGlue SDK; its comment names `pthread_exit`, which on Linux unwinds the stack and drops the reference.

**The fix.**

- The reference is parked in the kernel (`KernelState::DeferExitedThreadRelease`) instead of on the exiting thread's stack.
- `KernelState::ReapExitedThreads`, called at the start of every `XThread::Create`, drops the references of the parked threads whose host thread has ended (a wait of 0 ms on it), outside the global lock, since the last reference deletes the object and its destructor takes that lock.
- A guest handle still keeps the object, as before: a wait on an exited thread's handle still returns at once, and `NtClose` or `ObDereferenceObject` frees it.
- The thread's main fiber (the host thread converted with `ConvertThreadToFiber`) can only be converted back on its own thread. `XThread::ReleaseMainFiber` converts it back in `Exit` and `Terminate` while the thread still runs on it; elsewhere, for an object deleted on another thread, it no longer calls `Destroy()`, which would have converted the calling thread instead.

**The check.** Build `6afa374` with the fix, `I:\FM4-build-usamg` (llvm-mingw), the native path, 2x, 240 Hz, one `perf` acquisition:

- `hl-qr1`, `quickrace_loop` with 4 pairs and 2 passes (8 races, 0.66 h). Thread handles at each cycle's start: 165, 170, 169, 168, then 168, 168, 168, 167 on the second pass, against +10 a race in `qr-soak3` (163 to 490 over 33 races). `soak_report.py`: handles PASS on pass 2 (41 an hour against 100), threads level. Events rose by 2 to 7 a race on the second pass, as in `qr-soak3` (0 to 7). Private bytes and GPU memory fail on the second pass of so short a run, as they did on the second pass of `qr-soak3`; they are the caches that level on the third.
- `hlsm`, the smoke tier against `m2fmg` (M2's full gate, build `1fea8e4`): 3/3 cells, race 239.21 against 239.23 fps, p99 4.82 against 4.83 ms; the photo cell's WARN is its final capture, the save dialog over a car that stopped in the tunnel, and `photo_view` warns as it often does; no new error key.

**Left.** Events still rise by about 4 a race. They are most likely the runtime's `XEvent` objects for guest events that the title initialises in its own memory (`XObject::GetNativeObject`): a new object is made for each newly initialised guest event and is never freed, since the runtime cannot tell when the title stops using that memory.

**A crash on the first soak, and two more references.** The first 3-hour soak with the fix, `hl-soak1`, ended after 0.36 hours, in the fifth race of the first pass (Ladera Test Track): `[gpu] D3D12 device removed: HRESULT 0x887A0006 - DEVICE_HUNG`, then "Graphics device lost" and exit `0xC0000409`. No earlier run log on either BuildRoot has a removed device. Since an exited thread's object is now deleted, every raw `XThread*` that outlives its thread is a use after free, and two were found:

- `XTimer` (`work/runtime/src/system/xtimer.cpp`) kept the thread that set the timer as a raw pointer for its callback, which queues an APC on that thread when the timer fires. The callback now holds its own reference to the thread, and the member is gone.
- `XMutant` (`work/runtime/src/system/xmutant.cpp`) kept its owning thread as a raw pointer. It is an `object_ref<XThread>` now.

Neither was the cause, see below. One window is left as it was: `KernelState::GetThreadByID` reads raw pointers from `threads_by_id_` and could retain an object that another thread is deleting at that moment.

**The cause of the hang: the thread's guest memory.** The soak `hl-msvc` (the build with both references, MSVC, `I:\FM4-build-usa`) hung the GPU at the same place, 0.37 hours in, in the Ladera race. A loop of 7 Ladera races alone did not hang, so the order of the races matters, not the track. Runs of the first 5 pairs (7 races each, the fifth Ladera), MSVC:

| Build | Runs that hung |
|---|---|
| `6afa374`, without the fix | 0 of 3 |
| the fix of `7aded3a` and `52c0283` (the full soaks `hl-soak1`, `hl-soak2`, `hl-msvc`) | 2 of 3 |
| the fix, the thread's guest memory kept | 0 of 3 |

Deleting an `XThread` freed its guest memory: the KTHREAD (the object's guest object), the PCR, the TLS and the scratch block, which exited threads had always kept before. With the memory reused, the title, which most likely keeps pointers to the exited thread, wrote into whatever took its place. `~XThread` now leaves that memory allocated (`XObject::KeepGuestObject`), a few KB a thread, as before; the host objects and handles are still freed. The thread handles stay level in all three runs (158 to 162).

**The 3-hour soak.** `hl-soak2`, the build with the two references that still freed the guest memory, the same settings: 2.82 hours, 3 passes of the same 11 pairs as `qr-soak3`, 33 races, no failure, no removed device. `soak_report.py` on the third pass:

| Measure | `hl-soak2` (6 November 2026) | `qr-soak3` (30 October 2026) |
|---|---|---|
| Handles | PASS, 54 an hour (+44 over 11 races) | FAIL, 163 an hour |
| Host Thread handles | level, 168 to 169 at each race's start | 163 to 490 over 33 races |
| Events | +4 a race (770 to 854 over the last 22 races) | about +4 a race |
| Live threads | PASS, 81 to 82 | PASS, 81 to 82 |
| Private bytes | FAIL, 112.6 MB an hour (+118 MB) | PASS, 44 MB an hour (+47 MB) |
| Dedicated GPU memory | FAIL, 35.1 MB an hour (+28 MB) | PASS, 1 MB an hour (−3 MB) |
| Race frame times, first to last race | p50 +1.3% (4.66 → 4.72 ms), p99 +1.7% (7.00 → 7.12 ms) | p50 +5.4%, p99 +6.7% |

The handle leak is fixed apart from the Events. The memory measures fail where `qr-soak3` passed: about 11 MB of private bytes and 3 MB of GPU memory a race on the third pass.

**The soak with the guest memory kept.** `hl-msvc2`: build `a19ccff` (the guest memory kept), MSVC, `I:\FM4-build-usa`, the same settings and pairs: 2.82 hours, 33 races, no failure, no removed device. On the third pass handles PASS (45 an hour), host Thread handles level (159 to 160 at each race's start), Events +4 a race (798 to 842), live threads PASS (77 to 79), race frame times +5.2% (p50) and +6.6% (p99) from the first to the last race. Private bytes FAIL (124.8 MB an hour, +106 MB) and dedicated GPU memory FAIL (49.3 MB an hour, +26 MB), as on `hl-soak2` with llvm-mingw, so the toolchain is not the difference from `qr-soak3`. Whether the commits between `053474b` and `6afa374` or the thread handle fix keep the memory rising on the third pass is open; a revisit soak of `6afa374` would tell. About 10 MB a race is no risk within a session of a few hours.
