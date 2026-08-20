# Engine Gaps & Feature Needs (logged, not silently worked around)

Standing log of engine limitations hit during content/tool work. Each entry: what was needed,
what the engine did instead, the workaround used, and what a real fix looks like.

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
- **Road grading:** generation-time roads DRAPE the terrain surface (material stamp only - no
  cut/fill, no slope-limited profile like `StreetPaver`'s settlement streets). Steep terrain
  yields steep road surfaces. Real fix: a generation-time analog of `planTerrainPath`'s
  slope-limited lower envelope applied to `surfaceY` along the corridor.
- **Road-to-street fusion:** roads terminate at the settlement footprint edge; the settlement's
  own street network doesn't orient toward or join the arriving road (`chooseStreetAxis` knows
  nothing about the plan). Real fix: pass the road arrival bearing into settlement layout.
- **Live apply:** `worldforge_apply` is restart-required - streaming gen workers hold generator
  snapshots taken at configure time, so a mid-session plan change would seam already-generated
  chunks against new road-stamped ones. Real fix: a worker re-snapshot path in
  ChunkStreamingManager (stop workers -> refresh copies -> resume), then invalidate
  ungenerated-but-queued chunks.
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
