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
