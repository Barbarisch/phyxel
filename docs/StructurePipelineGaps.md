# Engine Gaps & Feature Needs (logged, not silently worked around)

Standing log of engine limitations hit during content/tool work. Each entry: what was needed,
what the engine did instead, the workaround used, and what a real fix looks like.

## 2026-09-22 — ~~`build_shaders.bat` alone does NOT change what the engine renders~~ **RETRACTED, THIS ENTRY WAS WRONG**

> ⚠️ **RETRACTED the same day, by direct experiment.** The engine renders `shaders/*.spv` from
> the repo root, and **`build_shaders.bat` alone IS sufficient**. Proven by making debug view 11
> output pure red, running `build_shaders.bat` ONLY (no CMake build, `shaders/` 20:43 vs
> `build/shaders/` 20:33), restarting, and capturing: the wall rendered red. The engine is
> launched with `cwd=PROJECT_ROOT` (`phyxel_mcp_server.py`) and loads the relative path
> `shaders/….spv` (`RenderPipeline.cpp`), so the `build/shaders/` copy is for the test/E2E
> targets, not the editor.
>
> **What actually happened, and the lesson:** two frames that should have differed came back
> identical, and I reached for a stale-copy explanation that fit the timestamps without testing
> it. The timestamps were real and irrelevant. The correct move was the one taken later — a
> single unmistakable probe (paint it red) that distinguishes "shader loaded" from every other
> hypothesis in one capture. **An explanation that fits the evidence is not the same as an
> explanation that was tested**, and this entry is left in place, struck through, as the record
> of getting that wrong rather than quietly deleted.

The original entry follows, for the record:



- **What happened:** editing a shader, running `build_shaders.bat`, and restarting the engine
  left the renderer showing the OLD shader. A deliberately broken `voxel.frag` produced a frame
  **identical to the correct one** (mean pixel difference 0.18/255 over the measured surface),
  which read as "the change had no visual effect" and invalidated several measurements before the
  cause was found.
- **Why:** `build_shaders.bat` writes `shaders/*.spv`. The engine loads `build/shaders/*.spv`,
  which is refreshed **only by a CMake build** ("Copying shaders to build directory"). Timestamps
  made it obvious once looked at: `shaders/voxel.frag.spv` 20:02, `build/shaders/voxel.frag.spv`
  16:14 — nearly four hours stale.
- **Why it is dangerous rather than merely annoying:** it is silent and it looks like a *result*.
  Every guard in the repo passes — the source is right, the `.spv` next to it is right,
  `shader_manifest.py --check` is green — and the frame is still wrong. It is the same shape as
  the stale-committed-`.spv` bug fixed earlier the same day, one layer further down, and it
  defeats the obvious sanity check ("I rebuilt the shader and restarted").
- **Workaround:** after `build_shaders.bat`, run a build (`build_project`) before relaunching, or
  compare `shaders/*.spv` against `build/shaders/*.spv` — `cmp -s` is enough.
- **Real fix (not done):** either have `build_shaders.bat` copy to `build/shaders/` itself, or
  have the engine load from `shaders/` with `build/shaders/` as fallback, or have startup warn
  when the two differ. Any of the three removes a whole class of "my shader change did nothing".
  **`CLAUDE.md` documents the workflow as "`.\build_shaders.bat` — rebuilds every shader" with no
  mention of the copy**, so the documented workflow is itself the trap.
- **Status:** OPEN, logged 2026-09-22 while building the §6.2 seam test.

## 2026-09-22 — `build_shaders.bat` reports SUCCESS on a failed shader compile, and the manifest guard then passes

- **What happened:** editing `voxel.frag` (adding `#include "crack.glsl"`) produced a genuine GLSL
  compile error — `ERROR: shaders/crack.glsl:90: 'worldFaceUV' : no matching overloaded function
  found` — and `glslc` generated no SPIR-V. **`build_shaders.bat` nonetheless printed
  `All shaders compiled successfully!` and exited 0.** Running `tools/shader_manifest.py --check`
  immediately afterwards reported `OK -- 82 built shaders, all .spv current with their sources`.
  The stale `shaders/voxel.frag.spv` from hours earlier was still on disk and still being loaded.
- **Why the guard did not catch it:** `build_shaders.bat` **re-records the manifest at the end of
  its own run**, so the manifest hashes the current sources against whatever `.spv` files happen to
  exist — including a stale one. `--check` then compares that freshly-written manifest to itself
  and passes. The guard validates the manifest, not the build.
- **How it was actually caught:** by hand, comparing `voxel.frag.spv`'s mtime (11:54) against the
  time of the edit. Nothing automated flagged it.
- **Why this matters more than a normal build bug:** `shaders/*.spv` are **committed artifacts** and
  glslc does not track `#include` deps. The failure mode is the repo's own worst incident — commit
  the `.glsl` with a stale `.spv`, the author's machine renders correctly because nothing rebuilt,
  every other checkout renders the OLD shader, and CI is green. That is exactly how the transposed-
  AgX fix (`20341333`) shipped a pink world to everyone but the author for five days. This is that
  incident's failure mode **plus** a case where the source does not even compile and nothing says so.
- **Workaround used:** check the `.spv` mtime by hand after every shader edit, and grep the batch
  output for `ERROR` (the errors ARE printed — they are simply not acted on).
- **Real fix — ONE change, not two (revised 2026-09-22).** This was first written up as two fixes;
  they collapse:
  **`build_shaders.bat` must propagate `glslc`'s exit code — fail the run, suppress the success
  banner, and CRITICALLY do not re-record the manifest — if any invocation returns non-zero.**
  Recording only on a fully successful run is what restores `--check`: a failed build then leaves
  the OLD manifest against NEW sources, so `--check` reddens on its own with no new mode and no
  mtime comparison. The second fix originally proposed (`--verify-fresh`) is unnecessary once the
  manifest stops being written over a failure.
  (Beware the known `.bat` trap: unescaped parens in an `echo` inside a block silently kill the
  block.)
- **Status: FIXED 2026-09-22**, and it took three cmd traps, each of which masked the next:
  1. **`if %errorlevel%` inside a block is expanded at PARSE time.** Every check inside the big
     `if defined USE_GLSLC ( ... )` block (lines 36-342, which contains `voxel.frag`) tested ONE
     stale value captured before any shader compiled. 95 compile sites affected.
  2. **`setlocal enabledelayedexpansion` + `!errorlevel!` fixes trap 1 and introduces its own:**
     with `setlocal` active, `exit /b` triggers an implicit `endlocal` that RESTORES the previous
     errorlevel, so the script halts correctly and still returns 0. Working around that with
     `endlocal & exit /b 1` stops the exit firing at all.
  3. **`exit /b 1` inside a `||` block NESTED inside the `if defined` block halts the script but
     returns 0.** Verified with a minimal repro: it propagates at one level of nesting and is
     swallowed at two.

  **Shipped shape:** every compile is `%GLSLANG% ... || goto :shader_error`, with the ONLY
  `exit /b 1` at a top-level `:shader_error` label. The manifest half needed no separate fix —
  once failures actually exit, `--update` is never reached over a failed build, so `--check`
  compares the OLD manifest against NEW sources and reddens by itself.

  **Also fixed:** `pause` on the error path hung any non-interactive caller. Now skipped when
  `SHADERS_NONINTERACTIVE=1`.

  **Regression test:** `tools/test_shader_build_fails_loudly.py` — breaks `crack.glsl` (an
  `#include`, i.e. exactly the dependency glslc does not track), asserts the build exits
  non-zero and prints no success banner, asserts a clean build still exits 0, and restores the
  tree either way. It asserts the OUTCOME, not the mechanism, so a future rewrite of the script
  in any style still has to satisfy it.

  ⚠️ **Measurement footgun found while fixing this:** PowerShell's `$LASTEXITCODE` after
  `cmd /c ".\build_shaders.bat"` reported 0 for a run that genuinely returned 1, and
  `cmd /c "... & echo %errorlevel%"` has the same parse-time expansion bug as the script itself.
  Both made a working fix look broken. Check batch exit codes from bash (`cmd //c ".\x.bat"; echo $?`).

## 2026-07-05 — asset editor crashes after ~9 hot-reloads (exit 3, silent)

- **What happened:** driving the archetype visual survey via `POST /api/asset-editor/reload`
  (switching .voxel templates in a running `--asset-editor` instance), the engine process died
  with exit code 3 on the ~9th consecutive reload. The log shows the reload COMPLETED ("Asset
  Editor: scene ready") and then the process vanished — no error, no crash log. Smells like
  resource churn in the reload path (Vulkan buffer lifetime / double-free on the Nth scene
  teardown), possibly related to the vulkan transition crash noted in game-dev feedback round 3.
- **Workaround:** restart the asset editor process every ~4 reloads.
- **Real fix:** make `reload_asset` idempotent under churn — soak test: 50 consecutive reloads
  of mixed-size templates in one process; also `/api/asset-editor/reload`'s queueAndWait
  timeout (5s) is shorter than a large template's stamp time, so callers get "Request timed
  out waiting for game loop" for reloads that actually succeed — return an async job id or
  raise the timeout.

## 2026-07-07 — fill_region silently fails above the y=31→32 vertical-chunk seam

- **What happened:** during material-swap verification in the CharacterTestbed world,
  `fill_region` calls spanning y=30..33 placed exactly the y=30–31 half and reported the
  y=32–33 half as `failed: 8` — on all 10 fills, uniformly. `query_voxel` confirmed the
  "failed" cells were EMPTY AIR, not occupied: the failure is placement into the vertical
  chunk (cy=1) above the seam, not an occupancy skip. Same family as the vertical-chunk
  placement gap the 10-story-tower stress test surfaced for structures — apparently still
  present in the `fill_region` path (the target chunk may not be created/loaded on demand).
- **Workaround:** kept the test fixtures below y=32.
- **Real fix:** `fill_region` (and any direct placement route) must create/load the target
  chunk the way the structure placer now does, and the response should distinguish
  "occupied, skipped" from "placement failed" so seam bugs can't hide inside the failed
  count. Red test: fill a 2×4×2 box straddling y=31/32 in a fresh world, assert 16/16 placed.

## 2026-07-09 — build_settlement responses lost to the 5s queueAndWait timeout

- **What happened:** terrain-mode `POST /api/settlement/build` (era/tier village on Perlin
  hills) runs site analysis + per-parcel terracing + the MST path network on the game loop —
  well over the API's 5 s `queueAndWait` window. The caller gets `Request timed out waiting
  for game loop` while the settlement builds FINE seconds later; the response JSON (the
  program echo {era,tier,seed}, dropped_plots, below_tier_min, path stats) is simply lost, so
  callers must scrape phyxel.log for what the build reported. Flat-mode villages fit the
  window; terrain mode reliably does not. Same family as the asset-editor reload timeout
  (2026-07-05 entry). `generate_world` right after project load hits it too.
- **Workaround:** poll phyxel.log for `main_street terrain:` / `build_settlement:` lines.
- **Real fix:** route long-running composite commands (`build_settlement`, large
  `build_structure`) through the async job system (submit → job id → status returns the full
  response JSON), or raise/parameterize the queueAndWait window. The response payload matters
  here: it carries the determinism echo and the honest-degradation counts the discipline
  depends on.

## 2026-08-07 — schema:"v2" build_structure silently ignores `type` (typology defaults to hall_house)

- **Symptom:** `POST /api/structure/build {"schema":"v2","type":"tavern","footprint":[16,20],...}`
  builds a hall_house with zero tables — no error, no warning. The `type` → typology mapping
  (`tavern` → `tavern`) lives ONLY in the v1 compatibility conversion (the width/depth path in
  `StructureBuildService`); the direct v2 path reads `typology` and quietly falls back to the
  default when it is absent.
- Related: `"footprint"` must be a JSON ARRAY `[w,d]` — the object form `{"width","depth"}`
  realizes as a "realize failed: empty footprint" error.
- **Workaround:** always pass explicit `"typology"` (+ `"function"`) and the array footprint.
- **Real fix:** apply the same type→typology alias in the v2 path (or refuse a `type` that
  contradicts the resolved typology), and accept the object footprint shape or reject it with
  a message naming the array form.

## 2026-08-16 - WorldForge V1 punts (docs/WorldForge.md), logged so nothing is silently "done"

- **Bridges at river crossings:** the world plan MARKS every order>=3 crossing on a road
  (position + Strahler order, in the plan JSON and `WorldForgeRoad::crossings`), but nothing is
  built there - the road stops at the carved channel and resumes on the far bank. Real fix:
  ValidationLedger placer #44 `place_bridges`, consuming the crossing records (order -> span/width
  from the same Doll-et-al channel-geometry tables the carve uses).
- ~~Road grading~~ **RESOLVED 2026-08-20**: per-road slope-limited grade profile baked in
  the plan (lower envelope + junction reconciliation + two-sided bridge-deck pins);
  sampleColumn pulls corridor surfaceY to it (cut AND fill). Mountain network measured
  0/6656 centerline steps over 1 cube (was 45). The 2026-08-20-morning abutment ramp was
  REMOVED same day - deck pins subsume it.
- **Road-to-street fusion:** roads terminate at the settlement footprint edge; the settlement's
  own street network doesn't orient toward or join the arriving road (`chooseStreetAxis` knows
  nothing about the plan). Real fix: pass the road arrival bearing into settlement layout.
- ~~Live apply~~ **RESOLVED 2026-08-21**: worldforge_apply applies LIVE on worlds with no
  saved chunks - ChunkManager::restreamWorldLive() stops the gen workers (fresh generator
  snapshots re-taken on the next pump), evicts every resident chunk (deferred deletion;
  DIRTY chunks are DISCARDED, not saved - saving them smuggled old-plan content back as
  stale islands, observed live), clears the surface-band + evicted-LOD caches, and the
  far-terrain mesher re-configures its private generator copy. Guards: in-flight
  worldforge_build (raw plan pointer - UAF), draining boot DB backlog. Saved-chunk worlds
  keep the refusal / force+restart path. WorldForgeLiveApplyTest pins bare-apply staleness,
  restream pickup, and fresh-generator seam equality on the real async pump.
- **Heightmap/Flat worlds:** no hydrology bake -> no WorldForge plan (surfaced as an error).
  Same family as the "far-terrain heightmap worlds skip the hydrology bake" gap.
- **Roads at distance:** no far tier renders roads beyond chunk residency (far-terrain tiles
  carry no road channel) - a P-DERIVED violation at distance, noted in LodTierLedger.
- **Lazy realization:** settlements only realize via the orchestrated `worldforge_build` job;
  there is no build-on-stream-in for unbounded exploration (deliberate V1 scope decision).

## 2026-08-16 - streaming "wedge" during worldforge_build was Debug-build CRAWL, not pump death (RETRACTED in part, kept for the measurements)

- **CORRECTION (same day, measured):** a controlled A/B after cancelling the job showed the pump
  ALIVE at ~7 chunks/min (18->29 resident over 90 s, plain focus, no job) - the "freeze" was a full
  48-slot request queue draining at Debug-crawl speed in a forest/creek region (dense flora stamping
  is a recorded 450-625 ms/chunk; plus water spans + fine ponds). generation_pending pinned at its
  cap is NORMAL at that rate. The anchor-jump-wedge theory is therefore UNPROVEN here; the walking
  anchor + wall-clock residency deadline shipped anyway (good hygiene, and the recorded spawn-swap
  boot gap still stands). The residency-scale lesson is real: a town footprint needs ~150-300
  resident chunks = 20-40 min PER SITE on Debug - worldforge_build verification belongs on RELEASE
  (163 chunks/s measured), per the standing "never size an investment off Debug" rule.
- **What happened:** the first live `worldforge_build` run set the new streaming focus override
  (ChunkManager::setStreamingFocusOverride) directly to a site ~630 u from the player - an instant
  anchor teleport. Within ~2 min the generation pipeline froze: `/api/debug/load_state` pinned at
  `generation_pending: 48` with resident count crawling, zero ChunkStreaming log lines after
  21:12:12, engine/API/game-loop alive throughout, job residency polls ticking normally. Clearing
  the override did NOT revive generation - the wedge is permanent once entered. Same signature as
  the recorded "streaming pump dies after ~2 h uptime" open bug (chunk count freezes, zero
  ChunkStreaming logs, silent gen-worker death + pending-slot leak hypothesis), triggered here in
  minutes by the anchor jump; also consistent with the recorded "spawn-swap to a far coordinate
  never finished booting" gap. Note also an earlier full CRASH this session: a player teleport into
  unstreamed terrain at (-102,701) killed the process silently ~2 min later while the player
  free-fell to y=-516k (chunks only load within loadDistance of the player's 3D position, so a
  falling player outruns its own terrain forever).
- **Workaround (shipped in worldforge_build):** the focus driver now WALKS the anchor 64 u per
  residency poll instead of teleporting; plus a wall-clock residency deadline so a wedged pump
  surfaces as `refused: residency_timeout`, never a hang.
- **Real fix:** the recorded pump-death fix shape (gen-worker heartbeat + dead-worker restart +
  pending-slot reclaim), plus making a large anchor delta safe in ChunkStreamingManager (it is
  reachable from ordinary gameplay: teleports, scene transitions, respawns).

## 2026-08-16 - remote-settlement residents fall through evicted terrain (worldforge_build measurement)

- **What happened:** the first complete `worldforge_build` run (Release, 3 sites) spawned 15
  scheduled residents across the sites; when the job released the streaming focus, the remote
  sites' chunks evicted and every resident free-fell through the missing occupancy grids
  (observed positions y=-4.5k to -234k). Residents are also not DB-persisted (recorded gap), so
  they'd vanish on reload regardless.
- **Workaround (shipped):** `WorldForgeBuildService::settlementParamsFor` passes
  `"residents": false` - worldforge-built settlements ship without residents in V1.
- **Real fix:** persist Location/resident records with the settlement (the recorded persistence
  gap) and re-spawn residents on chunk stream-in near a built site - which also fixes plain
  `build_settlement` towns after any reload, not just worldforge ones. NPC ground-truth also
  wants the "kinematic bodies wedge when chunks evict" family fixed (fauna D1).

## 2026-08-17 - player loses ground while worldforge_build owns residency (operational hazard)

- **What happened:** during a worldforge_build (and manual `worldforge_focus`), the streaming
  anchor moves to the build site, the spawn area's chunks evict once outside unloadDistance, and
  the parked player free-falls through the vanished floor (observed at y=-2938 on the 8-site run).
  The job restores the player anchor at the end, but the player is deep underground by then.
- **Workaround:** world-BAKING is an authoring activity - keep the player parked and respawn
  (force_respawn / reload) after the bake; or run bakes before entering play.
- **Real fix options:** freeze player physics while a residency override is active; or a dual
  anchor (player + focus) with a small player-side keep-alive ring; ties into the recorded
  "falling player outruns its own terrain" teleport hazard.
- **RESOLVED 2026-08-20 - kinematic residency gate** (AnimatedVoxelCharacter::
  kinematicResidencyHold): on a streaming world, when the chunk at the feet AND the chunk
  below are both absent from chunkMap, the ground is UNKNOWN (all-air chunks stay
  resident, so absence = not-yet-streamed) and the character holds in place - zero
  vertical velocity, grounded stance - releasing the instant residency returns. Covers
  BOTH this hazard and the teleport-into-unstreamed-terrain family, for the player and
  every NPC on the same controller. The chunk-below escape keeps jumps above the streamed
  surface band under normal gravity; static worlds are untouched
  (CharacterResidencyGateTest, 4 tests, red-first: held character fell 19.8u/2s before).

## 2026-08-17 (later) - bridges V1 shipped; remaining bridge gaps

Placer #44 V1 landed (docs/WorldForge.md "Bridges"): flat Wood plank decks span every
order>=3 crossing, baked in the plan and emitted per-column by generateChunk; L4-verified
(voxel scan + in-ravine screenshot, BridgeVis project). The 2026-08-16 "bridges" punt above
is superseded. Still open, logged here so they are not silently "done":
- ~~Railings/piers/abutments~~ **RESOLVED 2026-08-20**: deck-edge columns raise a
  2/3-voxel WoodPlanks subcube parapet (span interior only - clamped-distance endpoint
  arcs would have walled off the bridge ENTRANCE, caught red by the walkway-intrusion
  assertion), spans >= 24 u get solid Stone piers bed-to-deck at ~12 u stations, pier
  columns emit no water span. All derived per query - plan hashes unchanged, ledgers
  stay valid. Residual: no parapet post rhythm/openings.
- 2026-08-20 (later): the M3-owed TraversalProbe agent walk landed as L3 tests
  (BridgeCrossingIsAgentWalkable + BridgeAbutmentRampStepsTheLowBankUp) and drove two
  emission changes: decks are now strictly span-interior (the clamped-distance check had
  grown a floating deck DISC beyond each endpoint), and genuinely-low banks get a stepped
  Stone ABUTMENT RAMP (1 cube per 2u, reach 8u) so the deck mounts along the road line -
  the abutment-massing residual above is partially closed. Natural terrain steps > 1 cube
  on approaches BEYOND the ramp remain the road-grading gap (unchanged).
- Channels wider than 96 u yield NO deck (bake log warns) - big rivers stay uncrossable.
- Decks are flat; no arc/clearance shaping for tall boat traffic (cosmetic for now).
- Plan-hash note: the bridges field changes all plan hashes; pre-bridge realization ledgers
  refuse re-runs with the stale-ledger guard (by design - regenerate or clear the ledger).

## 2026-08-18 - RESOLVED: resident persistence + free-falling remote residents (ResidentSpawner)

Both 2026-08-16/17 resident gaps above are closed by core/ResidentSpawner (the FaunaSpawner
pattern driven by PERSISTED Locations):
- Locations now persist in world_meta["locations"] at every save point (sync save_world,
  async save job, worldforge checkpoints) and restore at project load.
- Residents are DERIVED state, never stored: the spawner clusters locations into settlements
  (union-find, 64u links - each settlement keeps its own tavern), plans via ResidentPlanner,
  spawns when a location's ground voxel is resident, despawns BEFORE eviction can drop them,
  respawns identically (deterministic names) on return or reload, and adopts build-spawned
  NPCs by name. Settlement builds via worldforge pass residents:false; the spawner owns them.
- L4 (Release, canonical 3-site world): 7 town residents spawned during the bake, despawned
  cleanly when the focus walked away (0 falling - previously y=-233k), 15 locations restored
  after reload, the SAME 7 names respawned on stream-in, despawned again on evict.
- Note: one reload in this verification hit the RECORDED intermittent boot hang
  (reference_engine_boot_hang: init stalls while the API answers; log froze mid chunk-load
  16s after boot, before the spawner ever ticked). Not reproduced on retry; still open.

## 2026-08-18 - road-arrival street orientation shipped; physical junction still open

The 2026-08-16 "road-to-street fusion" gap is HALF closed: worldforge builds now pass
{"street_axis"} derived from the first arriving road's bearing, and chooseStreetAxis takes
a bounded per-cell preference (1500 in the x1000 relief score - tips comparable terrain
toward the road's axis, never overrides water/cliffs or a decisively flatter spine;
ChooseStreetAxisHonorsRoadPreference red-first). Still open: the PHYSICAL junction - the
road terminates at the settlement footprint edge and the main street starts inside it, so
a few unpaved cubes can separate them; full fusion means extending the street paving (or
the road) to meet at the boundary.

## 2026-08-18 (later) - street-road junction closed (both halves)

The remaining physical-junction gap above is now closed: roads trim to the footprint
boundary (+1 inset, was +8 - RoadsReachTheFootprintEdge red-first), and chooseStreetAxis
takes a bounded lateral preference (30/cube capped 1500) fed by street_offset = the road's
arrival center in site-local coords, so the main street lands where the road actually
enters. L4 (fresh canonical world): site 0's street chose "axis Z offset 65" = exactly the
requested arrival alignment - the street runs along the road's final approach and meets its
end head-on. Note for scans: street paving is MICRO-resolution Cobblestone - cube-level
surface scans do not show it (misread this before finding it in the paving logs).

## 2026-08-18 - far-road LOD: already worked, now pinned; coarse-ring thinning remains

The "roads have no far tier" gap logged 2026-08-16 (and echoed in LodTierLedger) was WRONG
about the present: far-terrain tiles sample sampleSurface per column, which has stamped
road material since M1, and FarTerrainManager::configure copies the CONFIGURED streaming
generator - the worldforge plan rides the copy. Roads therefore render in far tiles with
zero far-terrain code (FarTerrainMesherTest.RoadsShowInFarTiles pins steps 2 and 4;
corroborated live from an elevated camera - a gravel line crossing far snowfield tiles).
Both docs corrected. REMAINING (real): point-sampling thins a 5-6u road at coarse rings -
step 8 renders dashes, step 16 mostly loses it (beyond ~2 km). Fix shape: a supersampled
road hit per far column (query roadAt at 2-3 subpositions, majority wins) or a widened
roadHalfWidth for far sampling only.

## 2026-08-18 (later) - far-road thinning RESOLVED (supersampled far columns)

The coarse-ring residual above is closed: FarTerrainMesher now tests each far column
CELL CENTRE against roadAt with acceptance halfWidth + step/2, so any cell the road passes
through reads as road - the far ribbon is continuous at every ring (1-cell-wide line, the
correct far-map thickness). Near columns untouched (far-tile-only widening). Red-first:
the RoadsShowInFarTiles continuity assertion (road-column area >= 0.8x the centerline arc
length per step) measured 38 columns for 461u of road at step 8 before the fix.

## 2026-08-20 - silent engine death on BridgeVis (Release), second sighting

During the road-grading L4 look (Release, BridgeVis fresh world, ~15 min uptime: focus
walk, streaming settled, orbit screenshots, then a slow API voxel-scan), the process died
with NO crash line - the log just stops. The last minute is exclusively a wedged fauna
NPC spamming "[PatrolBehavior] STUCK (replan): pos=(-2357.75,65,-2387.3), pathNode=0/0"
every 1.5 s (the recorded "kinematic bodies wedge when chunks evict" fauna family - the
NPC sits exactly at deck height 65 near the bridge). First sighting was 2026-08-17 during
a teleport-fall (that trigger is now fixed by the residency gate; this one had no fall).
No repro, no stack. Two leads for a future session: (a) the wedged-NPC replan loop as a
correlate, (b) phyxel.log is 11.5M lines - rotate it; a log-write failure would be
invisible. Logged, not chased.

## 2026-08-21 - third-person "feet jitter" triage: sim exonerated, it is the no-AA speckle

User report: in locked 3rd person, orbiting the camera makes the character's feet/ground
look like they micro-adjust; moving the character does not. Measured live (10 Hz trace
during a user-performed RMB orbit): player position FROZEN to the millimeter (zero
variance, all axes), camera boom rigid at exactly 4.000u, grounding never fired, and the
shadow fit already does world-anchored texel snapping (RenderCoordinator fitVolume). Every
simulation-side suspect is exonerated. The visible effect is the RECORDED sub-pixel
speckle defect (docs/RenderOptimization.md:489,513 - no AA): grass blades and voxel edges
re-rasterize under any view change, most visible at the ground contact, masked by whole-
view motion, invisible at rest. Fix = the WorldRenderV2 M3 anti-aliasing milestone, not a
tweak. (Same session: the map-panel scrollbar oscillation and the stale far-tile terraces
were real bugs, fixed in e11b51d6.)

## 2026-08-21 - OPEN: bald grass strips along terrain step contours (evidence trail)

User-visible: bare "staircase" strips along 1-cube step contours in meadows, reading as
broken LOD; view-angle contrast makes them pop. Established by measurement on BridgeVis:
- World data CORRECT: streamed voxels == generator surface on 109/110 columns (the one
  outlier is a tree); forced remesh is a no-op (mesh faithful to data).
- NOT far-terrain tiles (disabled via /api/debug/far_terrain -> artifact persists,
  tiles_drawn 0), NOT occlusion culling (disabled -> identical frame), NOT ghost chunks
  (one-object-per-coord guard added d32dcece; ghosts counter reads 0), NOT the shader
  edge taper (edgeTaperFloor=1.0 -> strips stay bald; the shader only scales height,
  never discards), NOT the blade PLANTING scan (GrassBladeCoverageTest: terraced floor
  gets 1024/1024 blade instances headless).
- With bladeWidth 3x, most of the strip fills in EXCEPT clean RECTANGULAR bald patches
  hugging the upper side of step edges - rectangle-shaped absence suggests something
  structural (merged-quad-correlated? per-instance-range?) rather than per-cell logic.
Next session's tool: a debug overlay coloring each grass-topped cell by whether a blade
INSTANCE exists for it in the live chunk buffer (CPU-side dump of m_grassInstances per
chunk via an API route) - that splits "instances absent" from "instances invisible" in
one look. Related open defects in the same visual family: T-junction cracks at
greedy-merge borders, no-AA sub-pixel speckle.

## 2026-08-21 - OPEN: character parts wash out under direct sun / go slate-navy in ambient

Observed during creature_forge L4 (CharacterTestbed, Debug + LodTest, Release): animated-character
box albedo renders with far higher lighting contrast than terrain. Mid-brown albedo (~0.55) reads
near-white on sun-facing faces and desaturated slate-blue on ambient-only faces (noon: all vertical
faces). NOT an asset defect: an RGB probe rig (pure red/green/blue boxes) proved per-box .anim
colors reach the renderer with correct channels; the imported fox control washes out identically.
Effect: every fauna rig's authored palette is only recognizable at oblique sun angles; at noon or
in canopy shade creatures read grey. Suspect the character instancing path lacks the warmer
ambient/bounce terms terrain gets from lighting.glsl (characters were tuned pre-lighting-revamp).
Next probe: render one character + one terrain block with IDENTICAL albedo side by side and diff
the lit values per face orientation. Workaround used by creature_forge: keep spec `shading.gradient`
gentle (bottom -0.22, not anyCreature's -0.88) so the engine's own contrast doesn't compound it.

**2026-08-22 reproduction (still open, still not an asset defect).** Hit again during the W8
exotics L4 in CharacterTestbed on Release. At the project's default `ambientStrength` 1.0 with the
sun straight down, all five new rigs plus a *previously-shipped* `forge_bear` spawned beside them
render essentially WHITE — the control is what rules out the new rigs. Dropping ambient to 0.12
makes form fully readable but colour still reads slate-blue rather than the authored grey-green
(`.anim` Box lines carry the right values, e.g. 0.406/0.406/0.375). So the two halves of this gap
are one gap seen at two ambient levels, not two bugs. Practical note for the next L4: binding
`tint` cannot compensate — it multiplies an albedo the lighting is already crushing. Verify rig
SHAPE by bone probe plus a low-ambient screenshot; do not try to judge palette in this scene.
Two API traps found while chasing it: `set_day_night`'s `time` param and a `/api/daynight` POST
both leave `timeOfDay` pinned at 12.0, and *enabling* day/night resets ambient to 1.0 — so lower
ambient only sticks with day/night disabled.

## 2026-08-26 - TABLED: imported-rig characters render UNPOSED (vertical) beyond ~5-7u

User-visible: Meshy/Quaternius-class rigs in the hall look "really fucked up" — the bear rears
bolt upright, head buried. TABLED by user decision after a long session; this entry is the full
trail so the next attempt does not repeat it.

**What is ESTABLISHED (each point measured, most twice):**
- The clip FILES are sound: offline FK sweeps (33 samples/cycle) hold bind within tolerance;
  key quat convention (XYZW), Bone-line convention, engine parser, `updateAnimation`,
  `blendAnimation` defaults, and `interpolateRotation` were each read and are all consistent.
- The failure is **camera-distance-dependent with a ~5-7u threshold**: the SAME instance
  renders a correct horizontal bear when the camera is within ~4u and a coherent VERTICAL
  (raw-GLB-frame) bear beyond it. Reproduced repeatedly with 12 identical `bear_meshy` NPCs;
  which instances look broken in a group shot is just which ones are past the threshold.
- It is NOT the character LOD tier: `POST /api/debug/characters {"lod1":0,"lod2":0}` (LOD
  disabled) changes nothing.
- It is NOT the anim-update LOD gate (thresholds 30/60/120/220u — far above 6u).
- It is NOT the CPU pose path: a temporary diagnostic in the parts-sync loop showed every
  instance syncing all 20 bone groups with 0 skips every tick (the `boneOffsets` gate never
  fires); the FSM also keeps advancing on "frozen" instances, and a commanded Attack state
  change does not unfreeze the visual.
- The vertical pose is a COHERENT whole-body rotation ≈ the root bind rotation (Meshy bakes
  ~90° X into every bone from the GLB import). Forge rigs are equally affected but INVISIBLY —
  their bind rotations are ~identity, so an unposed forge rig just looks like a statue. That is
  why this shipped unnoticed: the bug predates the bestiary work and only imported rigs expose it.
- Next suspect when resumed (was mid-read when tabled): the instanced character draw path in
  `RenderCoordinator::buildCharacterDraws` — specifically whether characters inside ~6u take a
  different (correct) route than the instanced `m_charDrawsMain` path, and how
  `boneModels[boneBase + inBoneIndex]` resolves for the far group. The distance that matters is
  per-CHARACTER camera distance; find what else keys off it besides `lodForDistanceSq`.

Meanwhile the retargeted mocap clips (quat-continuity-enforced) are committed and gated; they are
not the problem and should not be reverted when this is picked back up.

## 2026-08-23 - OPEN: the imported `stag` rig stands VERTICALLY (antlers reach the ground)

Reported from the Bestiary Hall and confirmed on Release: the stag renders as an upright column —
body vertical, legs splayed at the base, head/antlers pitched down to ground level. Its BIND pose is
fine (FK over `stag.anim` gives width 1.25 x height 1.95 x depth 2.25, a proper horizontal
quadruped, with the antler geometry inside the `Head` bone at y 1.47-1.93 and feet at -0.03), so
the geometry is not the problem — the ANIMATION is.

The bound `Idle` clip carries large CONSTANT rotations that look like a rest-pose rebase that never
happened on import: `Back` sits at -69.1 deg elevation for all 101 keys, `BackUpperLeg.*` at -82 deg,
`Neck1` at -40 deg. A clip whose every key holds the same big offset is describing a different rest
orientation than the bind pose it is being applied to. `quad_horse` is likely the same family of
problem (it is the other imported rig that also cannot play Attack).

Not fixed. The stag has 13 clips (`Idle`, `Idle_2`, `Idle_Headlow`, `Eating`, `Gallop`, ...), so a
cheap mitigation may be re-binding `Idle` to a clip whose rest orientation matches — but an offline
FK probe of the candidates returned an identical head extent for all of them, which means the probe
itself was not applying the pose correctly and should not be trusted. Next step: re-derive the
import's rest-pose rebase rather than shopping for a clip that happens to look upright.

## 2026-08-23 - REQUEST: elementals need VFX, not geometry

The `elemental` variant reads as a humanoid because it IS one — an amorphous creature is exactly
what a rigid box skeleton cannot express. Sculpting it further (swollen torso, tapered base) has
already been tried and it still reads as a person. The honest fix is particle/VFX support attached
to a character (swirling motes, a flame or dust body), which the Spell VFX system already has the
primitives for. Logged rather than bodged into the mesh.

## 2026-08-23 - OPEN: engine dies during clear_region while the Bestiary Hall is staged

Hit twice, reproducibly, while preparing the Bestiary Hall demo arena: with the hall staged
(46 NPCs, 46 DISTINCT rigs), a `clear_region` job kills the process outright — no ERROR line, no
exception, the log just stops mid-job (`Job N started: clear_region` is the last entry). Both times
the world edits made in that session were lost with it, which is a second-order trap: the terrain
silently reverts to its pre-edit state on the next launch and the next attempt looks like the clear
"didn't apply".

**It is NOT simply "many characters during a terrain edit."** Discriminating test run the same
session: 46 NPCs spawned the ordinary way (`spawn_encounter`, 16 goblins + 15 wolves + 15 orcs)
survived two back-to-back `clear_region` calls over the same volume with no crash. `clear_region`
with nothing staged is also fine (4 calls, clean). So the trigger involves something the hall does
that an encounter does not. Candidates, untested: 46 distinct `.anim` templates resident at once
(vs 3); the very large rigs the hall stages (tarrasque 7u, ancient dragon 6u) grounding big capsules
against an occupancy grid mid-rebuild; or `NPCBehaviorType::Idle` vs `Combat` taking a different
grounding path.

Not yet root-caused — no stack was captured. **Workaround in the meantime: prepare the arena
BEFORE staging the hall, and do not edit terrain while it is up.** Next probe: attach a debugger (or
enable crash dumps) and clear terrain with the hall staged; if that is slow, bisect by staging a hall
subset (large rigs only vs small rigs only) to separate "rig count" from "rig size".

## 2026-08-21 - Bestiary Forge punts (logged at M6)

- **Flight locomotion for winged rigs**: forge_dragon_young / forge_griffon ship folded-wing,
  ground-only. The engine has no flying gait class for spawned NPCs (the dragon body plan is
  ground clips too). A flight tier needs: airborne capsule mode, a Fly FSM state + clips, and
  wing-beat membrane animation (membrane bones exist and are animatable today).
- **Natural-weapon melee family**: CombatBehavior installs the 'unarmed' weapon moveset for
  every NPC; forge monsters route Attack through body-plan clipDefaults instead, which works
  but bypasses the family system (no light/heavy chains, no block). A 'natural' family
  (bite/claw/slam) in melee_anim_families.json + a per-species family hook on CombatBehavior
  would unify them.
- **Turn-based defender AC**: CombatAISystem derives a pseudo-AC from HP% for generic
  entities; now that NPCs carry monsterId, the defender's real AC could come from its stat
  block the same way the attacker's attacks now do.
- **Quaternius monster_* overlap**: monster_orc/monster_dragon etc. duplicate bestiary roles
  with no clip_meta and (dragon) a missing idle clip. DECISION: keep them (unknown consumers,
  zero maintenance); bindings.json points only at the new rigs; revisit retirement after a
  consumer inventory.
- **MonsterRegistry had no loader call anywhere**: fixed as a lazy load inside the
  spawn_encounter handler — a proper boot-time load (WorldInitializer) would also serve
  hand-keyed turn-based combats whose acting id happens to equal a stat-block id.

## 2026-08-26 - CityForge baseline gaps (logged at M0, docs/CityForgePlan.md)

- **Residents job counter regression**: seed-7 city build (SettlementTest, 160x160, Release)
  reported `residents: {planned: 28, spawned: 0}` in the job result while 28 resident NPCs
  were live in the world (outliner + Entities count). The counter was made "shape-robust"
  2026-08-18; something has re-broken the spawned tally for the sync-world path. Cosmetic but
  it is exactly the kind of always-zero ledger the displaced-voxels lesson warns about.
- **Secondary streets host no frontages**: planCityLayout allocates burgage rows only on the
  main + cross axes; every block interior is empty grass, which is the single biggest reason
  a 160x160 "city" reads as a spread-out village. Secondary-street infill rows are the real
  density lever (after tenement typology exists).
- **MCP get_job_status false "No game project is loaded"**: the running engine had
  SettlementTest loaded (engine_running agreed) yet the MCP tool refused; HTTP /api/jobs
  works. Drive jobs over HTTP until fixed.
- **Typology glut at city tier**: seed 7 drew 7 taverns / 5 blacksmiths out of 33 buildings
  (weights alone, no per-typology cap). A city should not be 21% taverns; wants a max-share
  cap in the draw (CityForgePlan M3).

## 2026-08-27 - CityForge user-feedback backlog (docs/CityForgePlan.md)

- **Floating foliage after settlement builds**: leftover canopy/trunk pieces hang in the air
  around cleared plots and streets (parcel clearing + road-corridor felling remove cells in
  their own bands; a tree whose trunk sat inside the band leaves its overhanging canopy
  orphaned in the air outside it). Fix shape: felling must remove the CONNECTED tree
  (flood Log*->Leaf* from the removed trunk), not just the cells inside the corridor.
- **Elevation handling**: settlement placement wants gentle-hill tolerance - flatten only where
  a pad/street needs it and keep surrounding relief (today's terrain mode reads flat/terraced).
- **Interior light bleed**: placed interior point lights illuminate through walls; exterior
  walls read bright at night. Engine-level: lights have no voxel occlusion (blocklight Phase 2
  / shadowed point lights are the fix; see project_lighting_overhaul).
- **Business signs missing in settlements**: user reports no trade signs on built-city shops.
  sign_item exists only for tavern; the other 5 trades are the open asset_requests rows.
  ALSO verify the settlement build path actually reaches planSignMount.

## 2026-08-27 - pre-existing red: ForgeGateTeeth.AllowInvalidSkipsProgramGateEnforcement

Deterministic failure on Release at commit 2e02c019 AND with pre-CityForge
FurnitureCatalog + data files (A/B'd): an allow_invalid build refuses at REALIZE
("chimney from the fireplace in 'kitchen' would rise through the middle of
room 'chamber_1' on story 1"), where the test expects allow_invalid to defer the
program-gate error and reach later gates. NOT caused by the sign/fence/density
work (verified by stash A/B). Likely a hearth-siting drift from an earlier
committed session; needs its own bisect. The rest of the 18-suite forge sweep
(129 tests) is green.

## 2026-08-27 - SignMount v2 punts (CityForgePlan M3d)

- **Swinging signs**: user wants projecting boards to hang LOOSELY and swing from collisions.
  Item props are fixed kinematic bodies; KinematicAnimator has hinge parts but no physics
  coupling, and VoxelDynamicsWorld has no constraint type for a hinged fixed prop. Needs a
  hinge-constraint feature before signs (or lanterns, chains) can dangle.
- **Flush boards vs windows**: the world probe sees AIR at a window opening, so an over-door
  flush board could in principle cover a window hole (beside-door poses are probe-gated but
  air-blind the same way). A window-aware check needs the AssemblyPlan portals, not occupancy.
- **KinematicVoxelManager x-axis surface faces (2/3)** were left on the legacy mapping - no
  x-axis-projected assets exist; fix like case 1 (opposite-slice reversed) when one appears.

## 2026-08-27 - M4 tenement findings

- **PlacedObject metadata carries no `typology`**: assembly_plan.plan has corners/walls/roof/
  stairs/etc but not the typology that produced it, so "what did this city actually build?"
  can only be answered from the forge LOG or from resident-NPC ids (res_<typology>_<x>_<z>).
  A typology field on the structure's metadata would make live composition auditable.
- **New typologies must be added to `locationTypeForTypology`** (StructureRealizer.cpp) or they
  derive as Custom locations and get NO residents - silent, since the build itself succeeds.
  Hit by `tenement` (0 residents until fixed). A data-driven map (room_program `location_type`)
  would remove the hidden C++ coupling; logged rather than done.
- **Residents `spawned` counter still reports 0** while residents ARE live (69 planned / 69
  live / counter 0). Second sighting; the counter reads a shape the sync path doesn't fill.

## 2026-08-27 - orphaned-canopy sweep: what it does NOT cover

The sweep (FloraSweep.h, settlement unit "clearing orphaned canopy") closes the
floating-foliage class for SETTLEMENT builds. Still open:

- **Other clearers do not sweep**: single `build_structure` pads, `clear_region`, and the
  destruction path can each orphan canopy with no sweep behind them. The planner is generic
  (pure, probe-driven) - wiring it into those paths is a small follow-up each.
- **A tree taller than the scan band is skipped**: the box tops out at maxGround+44, and a
  component touching the box shell is deliberately left alone (support unknown). A redwood
  whose crown exceeds the band therefore never sweeps. Conservative and honest, but it means
  very tall species can still leave floaters.
- **Site prep still deletes healthy trees wholesale**: parcel clearing wipes plots + margin 4
  (measured 8640 cells for a 2-plot hamlet), which is what removed the L4 control tree before
  the sweep ran. Separate concern from orphans; a "fell whole trees, do not slice them" pass
  would be the real fix for the look.

## 2026-08-27 - M5 town_hall: what the engine refused, and what is owed

- **Civic hall ships HEARTHLESS - USER-SETTLED 2026-08-28** ("we can skip a fireplace in the town
  hall"): the town hall needs no fix. The underlying siting limit still applies to OTHER
  typologies, so it stays logged below. A ground-floor hearth's stack rises
  through the middle of the council chamber above, and the realizer's flue gate refuses it
  ("no silent lean"). Correct gate, real limitation. OWED: hearth siting that prefers a stack
  landing on an upper-room WALL (it already reserves stack columns; it does not yet score
  candidate hearth poses by what the stack hits upstairs). Same family as the pre-existing
  ForgeGateTeeth.AllowInvalidSkipsProgramGateEnforcement red.
- **Not modelled vs the archetype sheet** (disclosed in room_program sources, never faked):
  the OPEN ARCADE ground floor (no open-story/column mechanism in the realizer), the bell
  turret, the lock-up cell.
- **No civic PLOT reservation**: the moot hall reaches the market place only by being drawn
  from the core-ring palette with a cap of 1. A reserved civic plot fronting the square is the
  better form (and would let the hall face the place deliberately).
- **Typology cannot request a STYLE**: the town hall draws its style from the same independent
  hash as every dwelling, so it can come up timber. A civic typology wanting ashlar has no way
  to say so.

## 2026-08-28 - town wall (M7) v1 limits

- **The circuit is a RECTANGLE**: the band follows the site rect + margin, not the terrain or
  the built extent. On sloping ground it will step with the terrain (each column seats on its
  own surface) but it will not follow a contour, and it encloses the whole rect including empty
  fringe. A hull around the actual built area is the better form.
- **No wall-walk ACCESS**: thickness 2 gives a walkable top, but nothing reaches it - stairs or
  a tower door are owed before the allure is usable.
- **Gatehouses are openings, not buildings**: a gate is a lintel bridging the passage. No gate
  towers, no doors, no portcullis.
- **Wall vs the WorldForge road**: gates align with the SETTLEMENT's streets. An arriving
  inter-settlement road that does not line up with a street will meet wall, not gate.
- **Castle / keep precinct** (user-asked): not started.

## 2026-08-28 - PROVENANCE AUDIT of the wall (user challenge) - two real defects found

The user asked for proof the wall is a deterministic procedural feature, not hand-placement.
Audit method: (1) enumerate every call site of the planner - exactly ONE production caller,
the settlement pipeline; (2) confirm the walled city's world edits were only generate_world +
build_settlement (no place_voxel/fill_region/spawn_template anywhere in the region); (3) build
the SAME city (same seed/params) at two different origins and diff the wall cell-for-cell.

Check (3) FAILED at first, and found a real defect each time:
- **Flora punched holes in the circuit.** StructureGenerator::place does not overwrite an
  occupied cell, so a tree standing on the wall line silently cost that cell - two identical
  cities differed by exactly the cells where flora stood (e.g. a missing merlon at a z where
  two Log cells sat). FIXED: the wall unit now CLEARS its line before stamping, like the
  street grader clears its corridor. Re-run: both cities logged an identical
  "8770 cubes, 10 gates, 4 towers, line cleared 10688, displaced 348", and a 1741-cell scan
  of each west wall diffed to ZERO differences.
- **Gate centring was not translation-invariant.** `(lo+hi)/2` truncates toward zero, so a
  settlement at negative coordinates put its gates one cube off from an identical settlement
  at positive ones. FIXED with a flooring midpoint; pinned by
  TownWallTest.ThePlanIsTranslationInvariant (which is what caught it).

LESSON worth generalising: "deterministic" needs BOTH halves proven - the plan is a pure
function (unit test, translation-invariant), AND the stamping owns its cells (live A/B at two
origins). Any future placer that writes into terrain should get the same two-part treatment;
a plan-only determinism test would have passed while the world quietly differed.

## 2026-08-28 - M8 tower house: three real fixes, and a harness trap that cost the most

Building the keep form surfaced three genuine engine gaps, all fixed:
- **Upper floors were always the INN plan.** generateUpperChambers (gallery + chambers) was the
  only auto layout, so a TOWER - stacked single rooms, the defining form - got a gallery its
  short length cannot carry. Typologies can now declare `upper_plan: "single"`.
- **`upper_purpose` is one value for every floor.** A tower is store -> hall -> chamber; with
  every upper floor private the circulation gate correctly refused chamber-through-chamber.
  Typologies can now declare `upper_purposes: [...]` per floor.
- **The entrance could open INTO the stair shaft.** stairWellRect insets only the foot, so the
  well hugs one wall for most of its length; on a SHORT building the centred exterior door
  landed inside the shaft, the building could not be entered, and every upper floor reported
  "unreachable" while the stair was built correctly. The tavern escapes by one cube (door z=8,
  well z1-7), which is why it was never seen. keepEntranceClearOfStairs moves a COLLIDING door
  only, so existing plans are untouched (parity digests green).

**THE TRAP (cost more than all three): `/api/structure/build` takes `stories` as an ARRAY of
story objects, `[{"height":3}]`, not an integer.** Passing `"stories": 3` yields a degenerate
program whose upper floors are unreachable - and the failure looks exactly like a geometry bug.
The settlement path always passes the array, which is why tenements build in cities but the
same typology "failed" from a hand-written direct call. When a direct build fails a gate that
the settlement passes, DIFF THE PARAMS against SettlementBuildService makeBp before diagnosing
the engine. Verified by building the same tower with the settlement's param shape: clean.

## 2026-08-28 - tower tops: a roof needs a room under it (user)

Round drum towers + two attested tops shipped (TownWallSpec tower_shape / tower_cap):
- **Round is the curtain-wall default**, grounded on Conwy's 21 drum towers - the same source
  the wall spec already cited while building SQUARE corner towers. A tower HOUSE stays
  rectangular; that is a different building and was already correct.
- **Parapet (English/Welsh) vs conical "pepperpot" (French/German)** is a regional split
  expressed as data, not a taste call: Conwy/Caernarfon/Beaumaris vs Carcassonne/the Loire.
- **USER RULE, now enforced: if a character cannot stand under it, it is not a roof.** The
  first cone sat straight on a SOLID drum - a stone point on a lump. Under a cone the drum's
  top is now hollow (rim = chamber wall, interior stops short), giving a measured 3-cube /
  27-micro chamber against the engine's 16-micro agent box. Verified by scanning the tower's
  centre column live: solid y17-23, AIR y24-26, slate cone y27-31.
- The cone also rises 2 courses per inward step, so it stands about as tall as it is wide; a
  one-course-per-step cone measured as a stubby cap and read as a stone lid.

STILL OWED on towers: no ACCESS to the chamber or the wall-walk (no stair, no door) - the room
under the cone is real but unreachable, which is the next thing to fix; the parapet tower is
still solid below its deck; no arrow loops.

## 2026-08-28 - the mural tower became a STRUCTURE (user: "not a dumb pile of voxels")

TowerForge (core/TowerForge.{h,cpp}) plans a tower that works: hollow shaft, spiral stair,
floors, doorway, arrow loops, fighting deck. The ACCEPTANCE TEST is a TraversalProbe walking
in the door and climbing to the top chamber - not a screenshot, not a cell count - with a
sensitivity control that must FAIL when the stair is deleted.

Four defects the probe found, none of which a screenshot would have shown:
1. **A cube stair is scenery.** The agent steps up 4 micro; a cube is 9. Treads are SUBCUBE
   plates rising 3. This is the whole reason the stair is sub-voxel.
2. **A tread flush with the floor still blocks**: a plate at y=3 has its surface at 6, a
   6-micro lip. The ground storey is floored (surface 3) and the first tread sits at 3.
3. **The door opened onto the side of the stair.** Winding the spiral from an arbitrary angle
   put a tread that had already climbed 9 micro directly inside the doorway - the probe never
   got in (flooded 3558 cells, all at y=0, i.e. it was circling OUTSIDE). The flight now
   starts AT the door, which is what a newel stair does anyway.
4. **A discrete circle's ring takes DIAGONAL steps at its corners** and the agent only walks
   orthogonally: the climb died at (5,1)->(6,2), stuck at y=15. Diagonals are now bridged with
   the interior cell orthogonally adjacent to both, giving a genuinely 4-connected flight.

Sizing is a REFUSAL, not a fudge: a 6-cube drum reports "the stair consumed the whole interior
- no room to arrive in" and falls back to solid WITH a warning. 9 cubes (Conwy's own ~9 m
scale) is the smallest that holds wall + stair + room. The build reports towers_walkable so a
solid fallback can never be mistaken for a working tower.

STILL OWED: the tower's door is not connected to the wall-walk or the street door-to-door (you
can enter from outside, but the parapet walk above the curtain has no stair); no floors/ladder
in the tower_house yet (that typology's battlements flag is still unconsumed).

---

## 2026-08-28 — FUNCTIONAL-WIRING AUDIT (the "is it a voxel blob?" sweep)

User question after the tower defect: *"what else in the 'forge' systems are you making things
that are really just voxel blobs and dont actually serve the function for which they exist?"*

The tower's failure mode generalizes, and naming it is the point of this section: **the
validation ladder (L1 exists -> L2 structural invariant -> L3 agent traversal -> L4 live) has no
rung that asserts an emitted object is REGISTERED with the engine system that makes it function.**
`place_doors` scores "L3 ok" in the ValidationLedger while nothing in the world can open a door,
because L3 measures whether the character box fits through the hole. Proposal: a fifth axis
**W (wired)** — after a build, query the owning manager and assert the object is in it.

### Measured this session

* **Town-wall GATES — FUNCTIONAL, now proven** (`TownWallPassageTest`, 6 tests, all green). An
  agent walks from outside the circuit through all four gates, with a sealed-gate control on
  every side and a squat-lintel control. `gateClearCubes` moved into `TownWallSpec` so the plan
  and the stamper share one number and the test measures the opening really built.
* **Corner tower entered FROM THE TOWN — FUNCTIONAL, now proven**
  (`TheCornerTowerIsEnteredFromInsideTheTown`). `TowerForgeTest` proved the climb in an empty box
  and `TownWallTest` proved the circuit closes; neither saw the seam between them, which is
  exactly where a tower doorway could open into the curtain it stands on. The composed walk
  (town ground -> doorway -> top chamber) passes at the live `tower_size: 9`.
* **Wall-walk — GEOMETRY-ONLY, measured** (`AuditTheWallWalkIsCurrentlyUnreachable`). There IS
  standing room on the inner course, and no agent can reach it: no stair, no ramp, no tower door
  at walk level. The crenellations are decoration on an unreachable surface. The test asserts the
  defect today and says to flip it to EXPECT_TRUE when access ships.

### Found by audit, NOT yet fixed (each verified at the cited call site)

> **The work queue for all of these lives in
> [`docs/FunctionalWiringBacklog.md`](FunctionalWiringBacklog.md)** — ordered by
> unlocks-per-effort, each stated as a W-test you can write red first. This section stays the
> evidence record; that doc is the plan.

1. **Generator furniture has NO interaction points.** `PlacedObjectManager::placeTemplateMicro`
   (PlacedObjectManager.cpp:700-741) never populates `obj.interactionPoints` and never records
   `metadata["kinematic_part_ids"]`; the sibling `placeTemplate` does both (:681, :687). Every
   interior fixture and yard prop goes through the micro path. Consequences: `find_fitting_seat`
   and `sit_character` find **zero seats in a freshly built settlement**, and a chest lid cannot
   even animate. It half-heals on DB reload via `recomputeAllInteractionPoints`, but that reads
   `obj.position` (the FLOORED cube) not `obj.microAnchor`, so restored points sit up to ~0.89 m
   off. Separately `chair.voxel` carries no `# interaction_point:` line at all, and
   `tools/regen_furniture.py` writes seat anchors only into the `.metrics.json` sidecar —
   re-running it would delete the six remaining seat points in the library.
2. **No generated door is a door.** `registerDoor` has no call site in generation (only
   DoorManager.cpp itself and the MCP handler in editor/src/Application.cpp). Openings are carved
   to air and framed; no leaf is placed, though `door_wood.voxel` and its siblings are fully
   authored with handle interaction points. Same for wall gates and fence gates —
   `gate_timber.voxel` is an orphaned asset with zero references in engine/.
3. **Windows: about half are permanently solid.** Open/closed is a fixed per-opening hash
   (StructureRealizer.cpp:353-357) and the closed leaf is painted into the STATIC micro canvas
   (:371-373, :396-398) — masonry, not a shutter: no kinematic part, no manager, can never open.
   No typology declares `glass` yet, so "you can see through it" is false in practice.
4. **No scheduled NPC ever enters a generated interior.** Every location anchor is deliberately
   pinned two cells OUTSIDE the wall (StructureRealizer.cpp:120-131) because the NavGraph cannot
   route exterior->interior (StructureBuildService.cpp:137-143, "measured: 12x no_route"). The
   seats, hearths, beds and tableware are in rooms nobody visits. **This is the single largest
   geometry-only surface in the pipeline** and it was not previously logged here.
5. **`tower_house` gets ZERO residents.** It is the only room_program.json typology that falls
   through `locationTypeForTypology` to `LocationType::Custom` (StructureRealizer.cpp:70-79), and
   ResidentPlanner.cpp:35-37 skips anything that is not Home/Work/Tavern.
6. **Arrow loops are letterbox windows, not loops.** TowerForge.cpp:230-234 cuts a 6-micro
   vertical gap across a whole rim cube column, so the real opening is **9 wide x 6 tall x 9 deep
   micro, about 1.0 x 0.67 x 1.0 m** — roughly 1.5x wider than tall, the inverse of a real loop
   (~30 mm wide, 1-2 m tall). Too short for the 16-micro agent to use as a window either, and no
   line-of-sight or ranged system reads `loopCells`. Wants a 1-2 micro x 18+ micro vertical slot
   with an internal embrasure splay.
7. **`LocationType::Market` / `GuardPost` / `Temple` / `Farm` have no producer** — only the
   string/enum converters (LocationRegistry.cpp:16-19, 29-32). The market square, its stalls, the
   well and the statue are therefore pure voxels: no vendor, no water, no trade. There is no trade
   system to wire to (`Schedule::merchantSchedule` targets a hardcoded "market" id that nothing
   ever registers), and **no crafting system exists at all** — engine/{src,include}/core contains
   no crafting file, so the anvil, bellows, oven and workbench are props. CLAUDE.md's
   "CraftingSystem" entry under Gameplay is STALE and should be corrected.
8. **Trade signs are write-only.** Which board hangs is real typology data, but after hanging,
   `setMetadata(sid, "signage", ...)` is never read by anything, and it stores a PlacedObject id
   rather than the Location id — there is no sign-to-location lookup in either direction. All sign
   items are `"fixed": true`, so they get no pickup point and cannot be interacted with or read.
9. **Nav obstacles are missing outside the editor.** `setNavObstacleProvider` is wired only in
   editor/src/Application.cpp:1655; GameShell never calls it, so in a packaged game every well,
   stall, statue and woodpile is nav-invisible and NPCs treadmill into them. Also
   NPCManager.cpp:378-387 clamps the nav grid to +/-256 columns and logs that NPCs outside it have
   no nav — a large or streamed city can fall partly outside.
10. **NavGrid is street-blind.** No road or paving term in NavGrid.cpp or AStarPathfinder.cpp:
    paving is uniform-cost terrain, so nothing makes an NPC prefer a street to a garden.
11. **Fence gate-at-door has no agent coverage on the shipped path.** `FencePolicyTest`'s
    `GateWindowFollowsTheDoor` asserts integer arithmetic only — no probe. The one real fence-gate
    walk (ParcelFenceTest:74) exercises `planParcelFence`, which is **dead in production**: the
    stamper uses `planParcelFenceRuns` + `fenceGateWindowAt`. SettlementWalkabilityTest:218 walks
    the older centred `fenceGateWindow`. Net: no agent has walked the gate the engine builds.
12. **`MarketDressingTest.DressedSquareStaysWalkable` is mislabelled L3.** It is a 2-D
    4-neighbour cube flood with no agent box, height or step-up, so it cannot detect a headroom,
    width or step failure. CityForgePlan.md calls it L3.
13. **The road does not reach the gate.** The wall band sits outside site+margin while paving
    covers only the street rects inside the site — about 4 cubes of raw ungraded terrain between
    the end of the paved street and the outside of the gateway.
14. **One tower clipping one building refuses the WHOLE circuit** (TownWall.cpp:157-163). With
    `tower_size: 9` protruding into the site, a dense city can lose its entire wall to a single
    overlap. It should shrink or drop that tower and say so, not refuse everything.
15. **`remove_subcube` desyncs the physics occupancy grid from the chunk's real content**
    (found 2026-08-29 while verifying the lighting rebuild's M1). Measured live: place a microcube
    at (5,20,5) slot (0,0,0), then a full subcube at slot (2,2,2), then remove **only** the
    subcube. `GET /api/debug/occupancy_cell` then reports
    `content.micro_slots: [{slot:[0,0,0], count:1}]` — the microcube is still there — while
    `grid` reports `cube_filled:false`, `subdivided:false` and no masks at all.
    Mechanism: the removal path ends in `VoxelOccupancyGrid::markSubdivided(lp, false)`, which
    erases `m_subcubeFilled`, `m_subcubeSubdiv` and **all 27** microcube mask entries
    (`VoxelOccupancyGrid.cpp:47-55`), and nothing re-adds the microcubes that survived.
    Consequence: the grid under-reports real geometry, so a character can walk through voxels that
    are actually present, and anything else sourcing solidity from the grid (including the new GPU
    light occupancy) inherits the hole. The fix is to rebuild the cell's masks from the chunk's
    remaining sub-voxel content after a removal rather than clearing wholesale. **Not worked around
    in the lighting mirror** — that deliberately reports exactly what the grid says.

## 2026-09-23 — Five Vulkan validation errors fire on every run, unrelated to glass

Found by the glass-transparency OIT investigation (`docs/GlassTransparency.md` §15.8), which ran the
editor under `PHYXEL_VALIDATION=1` with OIT disabled (control) and enabled (experiment). All five appear
with **identical counts in both arms**, so none is caused by OIT or glass. Not fixed there — out of
scope — and logged here so they are not rediscovered as "new":

1. `vkQueueSubmit … expects VkImage … to be in layout VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL —
   instead, current layout is VK_IMAGE_LAYOUT_UNDEFINED` (hits the duplicate-message limit). Some
   sampled image reaches a submit never transitioned. **This is almost certainly the error the
   `transparent_voxel.frag` comment blamed when it disabled OIT in `7a36910f`** — it fires just as
   often with OIT off. The image is unnamed (`0xe6…`); naming images via `VK_EXT_debug_utils` would
   identify it. The reflection image has a seed barrier (`PostProcessor.cpp:2260`) and is not it.
2. `vkCreateGraphicsPipelines … vertex attribute at location 4 not consumed by vertex shader` (×9) —
   a pipeline binds the full `InstanceData` layout to a shader that does not read `inLight`.
3. `vkCreateGraphicsPipelines … fragment stage declared input at Location 3 … not an Output declared
   in the vertex stage` (×3) — a vertex/fragment interface mismatch on `flags`.
4. `vkAllocateDescriptorSets … 12 STORAGE_BUFFER descriptors from a pool of 10` — works on this driver,
   "will fail on others" (`VK_ERROR_OUT_OF_POOL_MEMORY_KHR`). A portability hazard for packaged games.
5. `vkQueueSubmit … signal semaphore may still be in use by VkSwapchainKHR` (×2) — the classic
   per-image vs per-frame semaphore reuse.

Raw per-arm messages: `oit_validation_control.json` / `oit_validation_oit_on.json` from that session.

## 2026-09-24 — LOD meshes draw glass OPAQUE (and cull faces behind it)

Found while planning `docs/GlassTransparency.md` §17 (D1). `LodChunkMesh::emitFaces` writes
`inst.reserved = 0` (`LodChunkMesh.cpp:148`) for every face, so a LOD mesh carries **no transparent
bit**: glass in a LOD chunk is drawn by the opaque pass as a solid surface, never by the OIT pass. It
also culls faces against any solid neighbour (`:139`), glass included, so opaque faces behind LOD glass
are missing too. Both must be fixed together — fixing the culling alone would add faces hidden behind
opaque LOD glass. Distance-driven chunk LOD is default-OFF, so this is latent; it becomes visible the
day chunk LOD is enabled with glass in range. Update `docs/LodTierLedger.md` when it is fixed.

## 2026-09-24 — `AtlasManagerTest.BuildAtlasFromSourcePNGs` fails whenever the BC7 cache exists

Found in the §17 full-suite sweep (docs/GlassTransparency.md §17.13). The test asserts
`info.pixels.size() == layerBytes * count`, but the content-keyed BC7 cache (`cc6a3a38`) skips the
PNG decode on a cache hit ("source decode skipped"), so `pixels` is empty (0 vs 490,733,568). The
cache lives in `cache/textures/` under the working directory, and launching the engine from the
repo root creates it, so the test fails for anyone who has run the engine there. **Verified:**
with `cache/textures` moved aside the test passes; restored afterwards. Fix belongs to the test
(or `getAtlasInfo` on the cached path), not to any feature; not fixed here.

