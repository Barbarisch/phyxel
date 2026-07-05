# Agent Context — start here for a new Claude session

This file is the **portable** working context for an AI agent (Claude Code) on this
repo. The agent's richer per-machine memory lives outside the repo
(`~/.claude/projects/.../memory/`) and will NOT be present on a different computer —
**this committed file is the substitute.** Keep it current at the end of a work session.

Absolute paths below (e.g. `C:\Users\<you>\...`) are machine-specific — adjust them.

---

## How to work this repo (hard-won operational lessons)

- **Build/run via the `phyxel` MCP tools** (`build_project`, `launch_engine`,
  `stop_engine`, `engine_running`, `screenshot`, `get_engine_logs`, …) — see CLAUDE.md.
- **The MCP server can hang.** If an MCP tool stalls (and the user says you've been
  "stuck"), believe them — you have **no clock visibility between tool calls**. Don't
  argue about elapsed time. Switch to driving the engine **directly over HTTP at
  `localhost:8090`** (e.g. `curl http://localhost:8090/api/...`) and build with raw
  cmake.
- **Raw build** (CMake is not on PATH):
  ```
  $env:PATH += ";C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
  cmake --build build --config Debug --target phyxel
  ```
- **Always `stop_engine` / kill `phyxel.exe` before rebuilding** — the linker cannot
  overwrite a running exe (LNK1104 "cannot open phyxel.exe").
- **⚠️ MULTI-INSTANCE / PORTS — the user runs several sessions at once.** Each engine
  binds its own **API/MCP port** (default **8090** = the engine-dev slot). **NEVER
  `taskkill //IM phyxel.exe`** or `stop_engine` blindly — it kills OTHER sessions'
  engines. Check `tasklist //FI "IMAGENAME eq phyxel.exe"` and only kill the specific
  PID you launched. Don't assume 8090 is yours — if another session may be up, launch
  with `--port <N>` and drive `curl localhost:<N>` (MCP tools target `PHYXEL_API_PORT`/
  8090). The shared `phyxel.exe` can't relink while ANY instance runs it — don't kill
  others to unblock a build; wait or build a separate output. Game projects from
  `create_project.py` get a unique `engine.json api_port` + a `.mcp.json` with matching
  `PHYXEL_API_PORT`, so a session opened in the project folder auto-uses its own port
  (`phyxel.exe --project <dir>` reads `api_port` from engine.json).
- **Verify fixes by RUNNING the engine** (build → launch → trigger the scenario →
  capture `screenshot`/`get_visual_diagnostic`/`get_engine_logs`). "Compiled clean" is
  NOT verification.
- **Stale-binary trap:** if behavior doesn't change after a build, the binary may be
  stale. (MSVC incremental linking did this — fixed via `/INCREMENTAL:NO` on the editor
  target.) Detect by grepping the built exe for a string literal unique to your edit:
  `grep -c "my marker" build/editor/Debug/phyxel.exe`. Trace obj → lib → exe to find
  where it goes stale.
- **The HTTP command queue has a 5s game-loop budget.** Heavy commands (`open_project`,
  large destruction ops) **time out the 5s wait but still complete** — re-query state,
  don't assume failure. (Reducing this for `open_project`'s heavy DB load is an open item.)
- **Visual testing:** use the **CharacterTestbed** project — the bundled default world
  DB renders magenta/stale. It lives at `C:\Users\<you>\Documents\PhyxelProjects\CharacterTestbed`
  (machine-specific). `launch_engine` needs the **full** path; a bare name resolves to the
  repo root and the engine exits "Project directory does not exist". After launch it sits
  on the project selector — follow with `open_project <fullpath>`.
- **Profiling endpoints:** `/api/debug/frame_profile` (CPU phase tree),
  `/api/debug/gpu_scopes` (per-pass GPU), `/api/debug/engine_timing` (fps / commandRecordTime /
  visibleInstances). **Measure per-pass before optimizing — don't guess the bottleneck.**
- **Logger API:** `LOG_INFO("Tag", "msg {}", x)` (`{}` placeholders) OR
  `LOG_INFO_FMT("Tag", "msg" << x)` (stream). Printf `%s`/`%.2f` print as literal text.
  The `{}` path silently DROPS the line past ~5 args — use `_FMT` (ostringstream, no limit)
  for many args.
- **Never auto-commit.** Commit/push only on explicit request. End commit messages with the
  `Co-Authored-By:` trailer.
- **Doc-sync gate (keep the forwarding surface current).** When you change engine code, update
  the matching downstream surface (docs, the `phyxel-gamedev` skills, MCP tool descriptions, the
  CLAUDE.md/.mcp.json templates) — the map is `docs/ForwardingSurface.md`. A **pre-push hook +
  CI** (`tools/check_doc_sync.py`) BLOCKS a push that changes `engine/`/`editor/`/`scripts/mcp/`/
  `shaders/` without touching any surface file. Enable the hook once per clone:
  `git config core.hooksPath .githooks`. Reconcile semantically with **`/sync-docs`**; opt a
  genuinely doc-irrelevant change out with `[skip-docs]` in a commit message
  (or `git push --no-verify` to bypass entirely). This exists because skills/docs silently drifted
  behind engine changes (e.g. stale UI guidance).

---

## Engine ground truth (supersedes older docs/comments)

- **Physics is HYBRID, and Bullet is removed** (the `external/bullet3` submodule was dropped
  entirely; `stb_image`/`stb_truetype` are now vendored at `external/stb`;
  `getActiveBulletCount()` is hardcoded 0). Two live in-house backends:
  - **`GpuParticlePhysics`** (Vulkan compute, warm-started) — the stable,
    count-scalable path; **destruction (`DamageSystem`) always routes here** via
    `queueSpawn`. Any doc/comment calling GPU/AVBD "broken/experimental" is STALE.
    - **TWO pipelines exist; only ONE is live.** The default is the **AVBD constraint
      solver** (`solver_*.comp`), selected by `m_useNewPipeline` — hardcoded `true`,
      never toggled off. The older **XPBD pipeline** (`particle_integrate.comp` /
      `particle_collide.comp`, the `m_integratePass`/`m_collidePass` dispatch in the
      legacy branch of `recordComputeCommands`) is **dead code** kept for reference.
      `particle_expand.comp` and the grid-sort passes are **shared** (NOT legacy).
      **All particle physics changes go in the `solver_*.comp` shaders.** Trap that
      already bit once: the character-vs-debris push lived only in the legacy
      `particle_collide.comp`, so the player passed straight through GPU debris until
      it was ported to `solver_integrate.comp`. Character collision now works in the
      live solver (debris inherits character velocity on AABB overlap).
  - **`VoxelDynamicsWorld`** (custom CPU sequential-impulse rigid-body world) — furniture,
    the **static-terrain occupancy grids characters ground against**, and the **left-click
    break-debris path** (`breakCube` → `addGlobalDynamicCube` → `DynamicObjectManager`).
  - **Break routing** (`VoxelManipulationSystem`, see `docs/DynamicVoxelPhysics.md`): a
    left-click break PREFERS CPU `VoxelDynamicsWorld` and only falls back to GPU particles
    when smoothed FPS drops below a threshold. So "GPU is primary" is true for
    destruction/scale, NOT for every single break — it's genuinely hybrid. Don't overstate it.
  - Each GPU particle is one independent body; constraints are contacts only (no welds → no
    coherent rigid multi-voxel fragments yet — that's the destruction P5 idea).
- **Static collision = per-chunk `VoxelOccupancyGrid`** (sub-voxel: cube→subcube→microcube,
  bitset O(1) tests) registered into `VoxelDynamicsWorld::m_grids`. It is NOT redundant with
  `ChunkManager` cubes — don't "simplify" it away. See `docs/AgentContext.md` collision rule
  below.
- **Voxel mutation perf rule:** never re-mesh a chunk per-voxel inside a batch operation.
  `removeCubeFast` defers the re-mesh (marks the chunk dirty); the per-frame
  `ChunkManager::updateDirtyChunks()` re-meshes each touched chunk once. (This was a 48×
  destruction speedup: 5.4s → 113ms.)
- **Collision registration RULE (CPU grids):** any code path that loads chunks from the DB
  MUST follow `loadAllChunksFromDatabase()` with `ChunkManager::buildAllChunkPhysics()` —
  otherwise the occupancy grids are never registered and **characters fall through the
  world**. `AnimatedVoxelCharacter` logs a one-time ERROR ("No terrain occupancy grids
  registered…") if this is ever skipped again.
- **Collision registration RULE (GPU grid) — the analog:** the GPU particle solver has its
  OWN occupancy grid, separate from the CPU one. Every world-load/build path MUST also call
  `ChunkManager::rebuildOccupancyFromChunks()` — otherwise **debris particles have no floor
  and fall straight through the world** (while the character still stands, because that's the
  CPU grid). The init-time rebuild runs with 0 chunks, so it does NOT cover the
  `--project`/`autoLoadGameDefinition` or DB-load paths — those each call it explicitly.
  Symptom of a missing call: log shows only `Occupancy grid rebuilt from 0 chunks`.

---

## Current workstreams & roadmap (update me at session end)

- **FAR-TERRAIN LOD + ASYNC STREAMING (committed 2026-07-03; feature OFF by default).**
  Two related bodies of work landed together:
  1. **Far-terrain LOD** (`engine/{include,src}/graphics/FarTerrain*`, `shaders/far_terrain.*`): blocky
     heightmap tiles synthesized from `WorldGenerator::sampleSurface` on a worker thread — camera-follow
     rings out to 2048+ world units, no Chunk objects/physics/light bake. **Status: Phases 0-3 done and
     verified** (deterministic across relaunch, watertight-mesh unit tests in
     `tests/graphics/FarTerrainMesherTest.cpp`, zero stutters at 2048u in Release). **OFF unless enabled**
     via `POST /api/debug/render_distance {"distance":2048}` then `POST /api/debug/far_terrain
     {"enabled":true,"maxDistance":2048}`. ⚠️ **Before flipping this on in a future session, know what is
     NOT done (Phase 4):** no `game.json` config, no automatic far-plane extension (must raise
     render_distance manually FIRST), no horizon fog, Debug-build flights with it on still show 50-470ms
     spikes (Release is clean), no far water/flora, ignores player edits at distance. Procedural/streaming
     worlds only. Test world: `PhyxelProjects/LodTest` (from `samples/game_definitions/lod_test.json`).
     Phase 5 (chunk-downsample LOD for real chunks) designed but not built — plan + post-mortem of the old
     reverted attempt in the 2026-07-03 session plan.
  2. **Async streaming overhaul (ALWAYS ON — this is the load-bearing change):** chunk generation, flora
     stamping, DB loads, occupancy-grid fill and chunk destruction all moved off the main thread
     (`ChunkStreamingManager` gen worker + disposal worker, `m_storageMutex` serializes SQLite); remeshes go
     through a two-tier budgeted queue (`DirtyChunkTracker::markChunkForRemesh[Idle]` — the Idle tier is for
     cosmetic neighbour re-culls/light ripples and runs only in quiet frames); pristine generated chunks are
     `markClean()` (deterministic regen — DB stores only player EDITS now; stops DB bloat); evict-saves are
     dirty-gated. **Result: seconds-long streaming freezes → zero stutter warnings in Release.** Key traps
     for future sessions: `markChunkDirty` sets the DB-PERSISTENCE flag (use `markChunkForRemesh*` for
     render-only staleness); world-position hashing in shaders MUST wrap (`mod(pos, 2048)`) in the VERTEX
     stage only — fragment-stage interpolated positions can't be fixed this way (see grass.vert/foliage.vert;
     the voxel.frag attempt was reverted for seam artifacts); the single shadow map is fitted to
     `min(renderDistance, 160)` — don't raise that cap without cascades; standing >100km from origin wobbles
     (world-space float pipeline; camera-relative rendering is the eventual fix). Per-stage pump timers +
     stutter breadcrumbs are left in (warn >100ms) — keep until the async paths have soaked. Six pre-existing
     engine bugs were fixed in the same arc (streaming dying silently after relaunch, every-evict DB saves,
     light-ripple phantom saves, 27-remesh cascade, GPU-particle slow sync, silent-terminate worker paths).
     One unresolved: a single untraced silent crash while AFK (no dump; workers now log before dying).

- **STRUCTURE GENERATION v2 — the ACTIVE focus (branch `feature/structure-generation-v2`).**
  **Read [`docs/structure-generation/README.md`](structure-generation/README.md) first** — canonical entry
  for all structure-gen work. Companion docs: [`ValidationLedger.md`](structure-generation/ValidationLedger.md)
  (per-placer required-vs-current proof depth) + [`DimensionReference.md`](structure-generation/DimensionReference.md)
  (generated grounded dimension canon, every value cited — regenerate via `tools/gen_dimension_doc.py`).
  Pipeline: StructureBrief → BuildingProgram → `autofillRoomLayout` → `StructureRealizer::realizeShell` →
  `StructureGenerator::place`. Build at runtime: `POST /api/structure/build {"schema":"v2",...}` (the MCP
  `build_structure` tool is the OLD v1 path).
  - **The branch is AHEAD of main by the whole structure-gen line AND has `main` merged in** (incl. main's
    lighting rework). If shipping: it builds (core+editor+tests), suite green except 8 pre-existing failures
    (AStar/ChunkData/Inventory/MaterialRegistry-stale-count/NavGrid/CharacterSkeleton).
  - **Shipped this arc (all grounded + red-before-green + both auditors PASS):** terrain-aware
    `build_settlement` (seats buildings on local ground) + walkable terrain-following paths; non-rect L-plan
    footprints; grounded thin fences + parcels; **first FUNCTIONAL typology — the `tavern`** (taproom+kitchen+
    service, L3-navigable); **generative MULTI-STORY** (inn upstairs guest chambers + auto-generated switchback
    stair, L3 climb proven); **inn ASSET DEPTH** — grounded+conformant `tavern_bar`/`back_bar`/`bar_stool`/
    `candle_stand`/`wall_lantern`/`chandelier`/`mug`/`bottle` (deterministic micro builder
    `tools/regen_furniture.py`); surface-clutter placement (mugs on tables); **silent-furniture-drop FIX**
    (placer packs walls + reports unplaced); **all 16 furniture types conformant** (0 drift); **build-freeze
    perf FIX** (place 13.8s→0.9s via bulk-collision deferral).
  - **#1 KNOWN ISSUE — RENDER DENSITY (measured, NOT fixed):** a single subcube/microcube tavern = **412k
    visible faces → ~49 FPS** (empty flat world = 80 faces / 357 FPS). Cause: the static renderer greedy-merges
    only full-CUBE faces, not subcube/microcube — so subcube-thin walls explode the face count. Fix paths in
    [`docs/RenderOptimization.md`](RenderOptimization.md) #40 (greedy-mesh sub/micro), or coarser cube walls,
    or distance LOD. NOT a merge/perf regression (empty world is fine). **This caps how dense we can build until
    it's addressed.**
  - **Other open threads:** DATA-INTEGRITY "removing parent subcube" warning spam during furniture placement;
    furniture chunkiness at microcube scale; `tavern` not yet in the `build_settlement` typology palette (so
    settlements don't spawn inns yet); more functional typologies owed (smithy/market/temple/well/barn/town
    hall); gallery/corridor upstairs (today a linear plan); gameplay wiring (NPC-navigable interiors, openable
    doors, locations/spawn points); `addCubesBatch` collision unguarded (perf follow-up).
  - **Standing discipline (enforced, non-negotiable):** ground every dimension (grounding-auditor, "source or
    stop"); red-before-green + solution-auditor on every "works/fixed" claim (Stop-hook gate in `.claude/`);
    agent-designed stress test pushing the scaling axis; "reachable" must mean physically walkable (L3
    TraversalProbe). See the [[ground-all-dimensions]], [[stress-test-phase]], [[solution-auditor-gate]],
    [[physical-usability-invariants]] memories.

- **Rebindable keybindings — DONE (all 3 steps), compile-verified end to end; live click-through
  unverified.** The LAST piece of the NO-ImGui-in-gameplay umbrella shipped. **(1) InputManager is now
  the single source of truth for action→key:** a `std::unordered_map<string,KeyboardKey> actionBindings_`
  seeded in the ctor from `GameSettings::defaultKeybindings()` (so the editor works with no config),
  with `bindAction` / `isActionPressed("MoveForward")` / `getActionKey` / `clearActionBindings`.
  `FpsScheme`/`TankScheme` (ControlScheme.h) now query `isActionPressed(...)` instead of
  `isKeyPressed(GLFW_KEY_…)` for move/jump/sprint/crouch (Q-strafe/R-dodge/ALT-block stay raw — not in
  the settings vocabulary). The scaffold (`create_project.py`) pushes `settings_.keybindings` into the
  InputManager via `bindAction` right after `loadFromFile`. **(2) UISystem one-shot key capture:**
  `beginKeyCapture(onCaptured,onCancelled)` / `isCapturingKey` / `cancelKeyCapture`; `handleInput`
  consumes all input while active, arms only after a keys-released frame (so the opening click isn't
  grabbed), ESC cancels. Backed by `InputManager::scanPressedKey()` (raw scan, bypasses ImGui/console
  gating) + `currentModifiers()`. **(3) Keybindings sub-panel:** `settings_screen.json` gained a
  "Keybindings…" button → a `keybindings` panel (12 rows = label + `kb_<Action>` key button showing
  `{{keybind.<Action>}}`, button action `{type:rebind,binding:<Action>}`). `MenuDefinition` got the
  `rebind` action + `MenuActions::onRebindKey`, and `open_submenu`/`close_submenu` were generalized to
  any overlay namespace (not just `menu:`) so `settings:` can navigate sub-panels. The scaffold wires
  `onRebindKey` (begin capture → write GameSettings + live `bindAction` + save + refresh the row label;
  ESC restores) and resolves `{{keybind.*}}` in `onResolveVariable`. **Verified:** engine + editor +
  all tests build clean; 10/10 InputManager unit tests; a throwaway scaffolded project compiles +
  links to a real exe; settings JSON validated. **NOT verified:** the actual click→press-key→rebind
  interaction at runtime — this automated session can't give the engine window OS foreground focus
  (`SetForegroundWindow`/`AttachThreadInput` both refused), so injected keys never reach `glfwGetKey`.
  Next session, verify live per [[standalone-window-driving]] (TAP keys via foreground+keybd_event;
  PostMessage WM_KEYDOWN does NOT reach glfwGetKey) — or just run a standalone by hand: Settings →
  Keybindings… → click a key → press a new key; confirm it sticks across restart (settings.json).
- **Destruction system** (`docs/DestructionSystem.md`, `engine/core/DamageSystem`): P1 area
  damage, P2 damage accumulation + per-material toughness, P3 structural-collapse with
  "connected-to-main-mass" anchor, and the lag-spike fix are DONE + committed. Roadmap:
  **P4** visual cracks on damaged-but-unbroken voxels; **P5** GPU weld constraints for
  coherent breakable fragments; bedrock/anchor pin flags.
- **Character grounding robustness:** fall-through root cause fixed (DB-load paths now build
  + register physics) with fail-loud + auto-register invariant. Done + committed.
- **Character ↔ debris interaction:** the character now PUSHES GPU debris (one-way). The push
  lives in the live AVBD `solver_integrate.comp` (NOT the dead legacy `particle_collide.comp`).
  It uses the character's **12 per-limb segment boxes** (4 torso + 4 arm + 4 leg) uploaded each
  frame via `setCharacterColliders` (same boxes fed to the CPU `setKinematicObstacles` path),
  with a **broadphase union AABB** tested first for a cheap early-out. Also fixed debris falling
  through the floor (GPU occupancy grid not rebuilt on world-load paths — see GPU rule above).
  Done + committed.
  - **Trap (cost me a debug cycle):** `GpuParticlePhysics::MAX_CHAR_SEGMENTS` caps how many
    segment boxes upload. The character builds **12**; when the cap was 8 it silently dropped
    the trailing 4 (the LEGS), so short floor-resting debris (subcubes/microcubes) was never
    pushed while full-height cubes were. If a body region stops colliding, check this cap and
    `buildSegmentBoxes`' `kSegments` count FIRST.
  - **Known gap:** the lowest boxes are the shins (`mixamorig:LeftLeg`/`RightLeg`) — there is
    NO dedicated foot box, so debris directly under the foot tip can slip the shin box. Fix if
    it matters: also upload the controller capsule (reaches the floor) as an extra collider.
  - GPU debris does NOT push the character back (would need a GPU→CPU readback) — out of scope.
- **Spell VFX system:** 3-layer architecture (dumb archetypes → per-spell composition →
  gameplay modifiers) implemented. `VfxSystem`/`VfxDirector`/`SpellVfxMapper` +
  `VfxRenderPipeline`. Done + committed.
- **Game-dev feedback round 1 (branch `feature/game-triggers`):** the first real
  `/feedback` → `/triage-feedback` cycle, driven by the TestVideoGame1 game-dev session
  (entries preserved in `docs/feedback/`). Implemented:
  - **Gameplay events** — `player_jumped`/`player_landed` emitted into `poll_events`
    (edge-detected centrally in `AnimatedVoxelCharacter::update`); `get_player_state`
    MCP/HTTP (position, velocity, grounded, FSM state).
  - **`TriggerSystem`** (`engine/core`, unit-tested) — declarative `{when, then[], once}`
    from game.json (top-level or per-scene) and MCP (`add_trigger`/`list_triggers`/
    `remove_trigger`). Events + `timer` + `entity_reached_region`; actions
    `complete_objective`/`fail_objective`/`transition_scene`/`quit_game` (+ standalone
    `show_victory`/`show_credits`). Hosted by the editor Application AND the generated
    standalone game (template hosts its own TriggerSystem, feeds player events, pumps it).
  - **Standalone shell screens** — `ScreenState::Intro/Victory/Credits` +
    `renderIntroScreen`/`renderVictoryScreen`/`renderCreditsScreen`; generated games start
    at Intro and get the victory→credits→menu flow; `screen_.showVictory()` or a
    `show_victory` trigger enters it.
  - **Scene-system bugs fixed** (pre-existing; multi-scene games could never actually run):
    `SceneManager::update()` was never pumped (editor now pumps it per-frame; the generated
    standalone template too); `setSubsystems()` had no caller (persistent `GameSubsystems`
    member, refreshed per-frame; the standalone template's local-variable dangling pointer
    fixed the same way); `SceneDefinition::fromJson` ignored the DOCUMENTED nested
    `"definition"` key (payloads silently dropped → empty scenes → fall-through → wedged
    loop); scene loads now also rebuild the GPU occupancy grid (the every-load rule).
  - **Acceptance:** the JUMP! game's full loop runs no-code in the editor — menus →
    world → jump → `player_jumped` → trigger → credits scene active.
- **Game-dev feedback rounds 2–3** (TestVideoGame1 / MazeRunner sessions; entries +
  resolutions in `docs/feedback/archive.md`). Round 2: package_game completeness,
  standalone menu/shell collision, MCP launch port, multi-scene auto-load, worldDatabase
  double-nesting, debuggability (all merged, `1f15b79`). Round 3 highlights:
  - **⚠️ STANDALONE RENDER PARITY (root cause worth remembering):** the editor viewport
    shows the RAW offscreen scene texture; the swapchain post-process pass is only ever
    VISIBLE in packaged games — so its bugs (double gamma onto the SRGB swapchain,
    un-thresholded bloom ≈ 2× brightness, SSAO horizon band at screen center) shipped
    unseen. `post_process.frag` is now an editor-parity composite (scene + OIT only);
    re-enable bloom/SSAO/tonemap only once they render in the editor preview too.
    Packaged games ship loose `.spv` — a shader fix can be hot-dropped into
    `<game>/shaders/` without rebuilding.
  - **Scene transitions clear entities now** — `SceneCallbacks.clearEntities/clearNPCs/
    endDialogue` are wired by the editor AND the generated standalone (they were invoked
    by SceneManager but never SET → players accumulated 2→6→7 across transitions).
  - **Camera mode** (`camera.mode`: first_person/third_person/free) in game.json +
    `set_camera`; standalone only defaults to ThirdPerson when no mode is authored.
  - **Timer countdown HUD** (`"hud": true` + `hudLabel` on timer triggers →
    `UI::renderCountdownHud`, foreground) and **menu `{{token}}` interpolation**
    (`{{playtime}}`, `{{story.<var>}}` via `GameMenuRenderer::onResolveVariable`).
  - **move_entity "player"** now teleports the LIVE character controller.
- **Game-dev feedback round 4** (TestVideoGame1 / "The Gilded Tankard" tavern-mystery
  session, 2026-06-10; entries + resolutions in `docs/feedback/archive.md`):
  - **Dialogue → gameplay state (the headline):** dialogue nodes now carry declarative
    `"actions"` (set_story_variable / complete_objective / fail_objective /
    transition_scene / quit_game — the SAME vocabulary as trigger `then`, routed through
    `TriggerSystem::executeHostAction`), choices carry a `"condition"` on story variables
    (`equals/not_equals/gte/lte/exists`; missing variable FAILS CLOSED), and every node
    shown fires a **`dialogue_node_reached`** `{tree,node,speaker}` gameplay event.
    `TriggerSystem::onEvent` now matches **any** non-reserved `when` key against the
    event payload (was id-only). Hosts (editor + scaffold template) wire three
    DialogueSystem hooks: setEventSink → triggers, setActionExecutor →
    executeHostAction, setVariableResolver → StoryEngine WorldState. "Convince 3 NPCs
    then win" is now fully data-drivable.
  - **Fill semantics:** structure fills + `fill_region` accept `"replace": true`
    (overwrite occupied voxels); the loader logs per-fill `placed/failed` at INFO with a
    hint when a non-replace fill collides (was a silent `LOG_DEBUG`).
  - **Material validation:** `validate()` rejects unknown material names in structures;
    the fill loader skips them with LOG_ERROR (they used to render as the magenta
    missing-texture checkerboard). Engine CLAUDE.md + phyxel-world skill material lists
    corrected from `resources/materials.json` (19 materials; Cork/Rubber are GONE).
  - **Templates-first guidance:** phyxel-world skill + GameCreationGuide now say
    "interiors/props: `search_templates` FIRST, fills are for shells/terrain" with a
    mixed fills+templates example (the session hand-built a whole tavern out of fills
    while 50 purpose-built templates sat in the catalog). `list_templates` added to the
    MCP `_NO_PROJECT_TOOLS` whitelist (catalog browsing during menu scenes).
  - **ROADMAP — tavern asset batch (queue for a BlockSmith `/generate` session):**
    tankard/beer mug, ale/wine bottle, bottle-shelf row, large keg w/ tap, hanging tavern
    sign, chandelier/candle wheel, rug/floor mat, cellar trapdoor, food set
    (plate/bread/cheese), sack/grain bag, wall decor (antlers/painting), serving tray.
  - **ROADMAP (IN PROGRESS): engine-side game-shell base classes.** The scaffold embeds
    ~29KB of shell logic (ScreenState machine, menu renderer wiring, trigger executor,
    camera follow) in every generated game — copies rot and engine fixes don't propagate
    (observed across the 06-06 vs 06-07 scaffolds). `Core::GameShell` now EXISTS
    (commit `6abd527`, 2026-06-09) and the scaffold emits a subclass of it; the camera/
    control loop is the first responsibility migrated. REMAINING: migrate the rest of the
    scaffold shell (ScreenState machine, menu renderer wiring, trigger executor) into
    GameShell, and regenerate older scaffolded projects (`create_project.py --force`).
    - **Camera & control system — ALL 4 PHASES COMPLETE** (`docs/CameraControlSystem.md`;
      commits 3567f15, 9949055, 6392a1c, d6726e4, cd29ac5, 6abd527; 2026-06-08/09).
      `CameraRig` (first_person/third_person/overhead/isometric — ortho rigs own
      projection via `Camera::getProjectionMatrix`, **glm::orthoRH_ZO** or Vulkan clips
      everything) + `ControlScheme` (fps/tank) + one shared `GameplayCameraController`
      used by the editor AND standalones (killed the duplicated loop that shipped the
      W-backward/no-mouse bug). Authoring: per-scene `game.json camera.mode/controlScheme`
      (GameShell re-resolves per transition), `set_camera` MCP `mode`+`control_scheme`
      (MCP server restart needed for the new schema), editor Camera panel Rig/Scheme
      combos (V toggle clears a forced rig). Direct-boot `startScene`→world-scene now
      reaches Playing (was stuck on Intro via two state stomps). Unscheduled polish:
      parse game.json rig knobs (distance/fov/eyeHeight, doc §4.1) into the rigs.
  - **Known intermittent:** a `vulkan-1.dll` crash (0xc0000409, fault offset d7205) on
    scene transitions — predates these changes (user's 06-06 session hit the identical
    signature). Six rapid menu↔world cycles didn't reproduce it; no repro recipe yet.
- **Game-dev feedback round 5** (UIShowcase multi-scene session, 2026-06-18; entries +
  resolutions in `docs/feedback/archive.md`). Dominant signal: **the generated standalone
  ships strictly LESS than the editor, and ImGui is bolted over the data-driven UI.** Most of
  this is round-5 scope for the "engine-side game-shell base classes" item below — the throughline
  is "migrate it into `GameShell` so generated games inherit it instead of re-deriving the editor's
  logic." **STATUS (round-5 push, 2026-06-18): the 3 must-fix-first bugs + the token-resolver are DONE
  in the scaffold generator (`tools/create_project.py`, commit `f069b47`); the `hud`-block override is
  DONE (`5f60a50`); the editor multi-scene HUD gap turned out to be ALREADY fixed (`94b1ec1`); the menu
  Back soft-lock is FIXED engine-side (`UISystem`, `161e006`). A full standalone (`R5Verify` from
  `ui_showcase.json`) was built + driven this session and LIVE-VERIFIED three of these: boots without
  crash + data-driven menu renders (resource seeding), the Credits "Playtime: 0:0X" token is RESOLVED
  not literal (token resolver), and Credits→Back returns to main with NO bounce (soft-lock). Remaining:
  the NO-ImGui-in-gameplay umbrella, paused-HUD suppression, and the GameShell migration of all of it.**
  **Standalone-driving recipe (hard-won, reusable — the feedback-#9 tooling, see [[standalone-window-driving]]):**
  the standalone has NO HTTP API. Find the GLFW window by title via `EnumWindows` (its `MainWindowHandle`
  is 0 / `FindWindow` is flaky). Drive it with `PostMessage(h, WM_LBUTTONDOWN/UP, …, MAKELPARAM(clientX,
  clientY))` and observe with `PrintWindow(h, hdc, PW_RENDERFULLCONTENT=2)` — BOTH are focus- AND
  z-order-independent, sidestepping the foreground-lock + global-cursor flakiness that wasted several
  attempts. **CRITICAL: use the real client size from `GetClientRect` (here 1024×576 under 125% DPI, NOT
  the 1280×720 canvas); menu hit-boxes are at `canvasCoord × clientW/1280`.** To-do list:
  - **⚠️ Standalone scaffold parity (the headline cluster):**
    - **✅ DONE (`f069b47`) Resource seeding (crash/empty-world):** scaffolded `shaders/` was EMPTY (no
      `.spv` → instant exit right after "Framebuffers created successfully") and `resources/` was
      missing engine defaults (`animated_characters/humanoid.anim`, `ui/default_hud.json`, `fonts/`,
      `textures/` + `materials.json`/`biomes.json`). Fix: `CMakeLists.txt` POST_BUILD now copies
      `${PHYXEL_ROOT}/{shaders,resources}` FIRST, then layers the project's own dirs on top (project
      files win). (`package_game.py` already seeded these — only the dev-build POST_BUILD was affected.)
    - **✅ DONE (`f069b47`) Player invisible + literal `{{tokens}}`:** scaffold called `setNPCManager` but
      NOT `renderCoordinator_->setEntities(&entities_)`, so the player (lives in `entities_`, not
      NPCManager) never rendered — now added. **LIVE-VERIFIED**: after the eye-height fix raised the
      third-person orbit center, the voxel humanoid player renders clearly in the `R5Verify` world.
      Also `cb.onMenuSceneLoaded` built `MenuActions` without `onResolveVariable` → literal
      `{{story.gold}}`/`{{playtime}}`; now wires `acts.onResolveVariable = gameMenuRenderer_->onResolveVariable`
      — **LIVE-VERIFIED**: the standalone Credits panel showed "Playtime this session: 0:0X" resolved, not literal.
    - **✅ DONE (`5f60a50`) Ignores game.json `hud` block:** scaffold loaded `loadHudInto(..., nullptr)`
      hardcoded in onInitialize (before game.json was parsed), so it ALWAYS loaded `default_hud.json`.
      Fix: split like the editor's `setupGameHud()` — onInitialize keeps `initUISystem()` + data-provider
      registration, but the HUD PANELS now load in `loadGameDefinition()` once `gameDef` is parsed, passing
      `gameDef.contains("hud") ? &gameDef["hud"] : nullptr` (before the multi-scene transition; single call
      since `loadHudInto` addScreen()s per panel). **`combat.mode` honoring deferred** — the scaffold has no
      `CombatDirector` yet; that belongs with the GameShell migration / combat-HUD re-homing track.
    - **✅ DONE + LIVE-VERIFIED (`161e006`) Menu Back soft-lock:** `UISystem::handleInput`
      (and `injectClick`) delivered ONE click to EVERY visible screen in one pass; `close_submenu` reveals
      `menu:<startPanel>` mid-loop and (when that panel is iterated LATER — `screens_` is actually an
      `unordered_map`, so it's HASH-order-dependent, not the alphabetical `std::map` the feedback guessed)
      it re-consumes the same click → Credits→Back bounces. Fix: both methods now snapshot the visible
      screens via `visibleScreenSnapshot()` BEFORE dispatch, so a screen revealed by an onClick can't
      receive that same click (order-independent). LIVE-VERIFIED on the `R5Verify` standalone: opened
      Credits, clicked Back (which sits over the main menu's Credits button) → returned to MAIN, no bounce.
      (The editor is NOT a valid surface — its multi-scene menu transition is unreliable/stuck, feedback
      #2, and never loads the menu into the foreground UISystem; had to build + drive a real standalone.)
    - **✅ DONE (`f069b47`) Double dialogue box:** ImGui `renderDialogueBox` AND data-driven `hud_dialogue`
      both drew while dialogue was active. Editor gates the ImGui path to AI conversations (Application.cpp
      ~2801); scaffold never did. Fix: gate the scaffold's ImGui box to `isAIConversation()` — complementary
      to the `hud_dialogue` visibility provider (`active && !isAIConversation()`), so trees → data-driven
      panel only, AI → ImGui box only.
    - **HUD/prompts persist while paused:** ESC pause leaves the data-driven HP bar + `[E]` prompt
      rendered under/over the ImGui pause menu (they don't coordinate visibility).
  - **NO ImGui in gameplay (architecture umbrella — parent of the pause-look / double-dialogue /
    pause-overlay items):** today the scaffold renders nearly the entire non-menu-scene UI through ImGui
    (`renderIntroScreen`/`renderVictoryScreen`/`renderCreditsScreen`/`renderMainMenu`/`renderCountdownHud`/
    `renderPauseMenu`/`renderSettingsScreen`/`renderDialogueBox`/`renderSpeechBubbles`/`renderInteractionPrompt`)
    — a different, unstyled app bolted onto the data-driven menus. End state: every one of these is a
    data-driven UISystem panel, the scaffold carries NO ImGui gameplay calls, ImGui is opt-in debug only.
    Needs data-driven equivalents for pause/settings/victory/intro/credits + speech-bubble + interaction-prompt
    + countdown, wired in `GameShell`.
  - **✅ ALREADY FIXED (`94b1ec1`, confirmed live this session) Editor multi-scene HUD gap:**
    `setupGameHud(gameDef)` + `combat.mode` were only called on the SINGLE-scene path; both multi-scene
    paths (`autoLoadGameDefinition` ~5335; `load_game_definition` handler ~13155) now call them BEFORE
    transitioning. The fix landed same-day as the feedback. Confirmed live: loading `ui_showcase.json`
    over HTTP into the editor logs `[HUD] Loaded engine default HUD` + all `hud_*` panels on the
    multi-scene path. (Note: the editor's menu-scene *transition itself* is still unreliable — see the
    soft-lock note above — but the HUD-setup gap is closed.)
  - **ROADMAP (NEW): functional smoke-test harness for generated games (HIGH PRIORITY / process).**
    Sessions keep "verifying" via API/state probes (`/api/dialogue/state` says active → "dialogue works")
    and ship obvious visual defects (invisible player, double dialogue, literal `{{tokens}}`, soft-locked Back,
    Continue→Credits dead-end, void world). The standalone has NO HTTP API, so input must be injected — this
    session bootstrapped it in PowerShell (find GLFW window by title via EnumWindows since MainWindowHandle=0,
    screenshot via CopyFromScreen, click via ClientToScreen+mouse_event at the 1280×720 canvas, keys via
    keybd_event). Ship as a real tool. The harness must (a) drive the BUILT game not the editor API,
    (b) exercise EVERY interactive element + state transition (not a happy path), (c) judge the rendered OUTPUT
    via a vision pass. Bake the checklist into game-dev session instructions so thorough play-testing is the
    DEFAULT: every button/Back fires (no dead-ends/soft-locks); Continue continues / New Game fresh / Quit quits;
    no duplicate-overlapping UI, nothing missing, all `{{tokens}}` resolved; behavioral "feel" (characters face
    each other in dialogue, camera frames the speaker, NPCs react, nobody in the floor). Bar: don't call a
    feature "verified" without watching it work on screen and trying to break it.
  - **Editor menu live-preview reliability (gotcha, same family as the soft-lock):** driving a menu scene's
    buttons via `POST /api/ui/click` at scaled coords (canvas 1280×720, `sx = ui.width()/1280`) returned
    `{"consumed":true}` and open/close submenu worked, but a `transition_scene` button never fired (no
    SceneManager transition logged) and Back went unresponsive after a couple interactions; had to drive
    `POST /api/scene/transition` directly. Confirm whether `onMenuSceneLoaded`→`loadMenuInto` handles
    `transition_scene` like the standalone, or whether queued `ui_click`s race the scene pump.
  - **Dialogue "feel" (to verify/fix):** talking to an NPC, player + NPC don't clearly turn to FACE each
    other and the camera doesn't reframe to the conversation (you talk to the player's back). A polished
    dialogue start should rotate both participants + ease the camera to an over-the-shoulder framing. The
    kind of behavior the smoke-test harness should assert.
  - **Combat `end_turn` gotcha (doc/small-fix):** `/api/rpg/combat/end_turn` only RECORDS a pending player
    intent applied by `PlayerTurnController`; an API-started encounter (`combat/start` → `beginEncounter`)
    never engages that controller, so `end_turn` is a no-op. `/api/rpg/combat/next_turn`
    (`CombatDirector::advanceTurn`) advances directly — use it to script turns in a test. Consider making
    `end_turn` advance the director when no controller turn is active, or document it.
  - **Live-inspection follow-ups (2026-06-18, user poked the `R5Verify` standalone):**
    - **✅ DONE + LIVE-VERIFIED First-person camera at knee height:** `GameShell` fed the rig a flat
      `eyeHeight = 0.5` over the character's FEET (`worldPosition` is the capsule bottom) → FP looked out of
      the shins. Now derives `eyeHeight = getControllerHalfHeight() × 1.8` (≈ eye level, scales with the
      model); explicit `game.json camera.eyeHeight` wins. Verified on `R5Verify`: FP view raised to standing
      height, third-person now frames the body (default humanoid halfHeight 0.95 → eye ≈ 1.71, was 0.5). **NOTE: the editor has a PARALLEL hardcoded `eyeHeight` (0.5/0.6)
      on its own `cameraCtl_` path (Application.cpp ~3429/4856) — still needs the same treatment.**
    - **✅ DONE + LIVE-VERIFIED ESC pause menu was ImGui-styled** — FIRST SLICE of the NO-ImGui-in-gameplay
      umbrella. New engine `UI::loadPauseMenuInto`/`unloadPauseMenuFrom` build a data-driven `pause:*`
      overlay from `resources/ui/pause_menu.json` (dark scrim + PAUSED + Resume/Settings/Main Menu/Quit),
      via new `MenuActions::onResume/onSettings/onMainMenu` + action types `resume`/`open_settings`/`main_menu`.
      Scaffold loads/unloads it to match `ScreenState::Paused` + drives `handleInput`; the ImGui
      `renderPauseMenu` call is removed. Verified on `R5Verify` (ESC → styled overlay, Resume → gameplay).
      **Pattern established for the remaining screens** (Intro/Victory/Credits/Settings): a `load<X>Into`
      builder + `<x>:*` screen namespace. (Settings since migrated; HUD-suppression-while-paused #11 since
      done — see the dedicated entries below. EDITOR still uses ImGui `renderPauseMenu`.)
    - **✅ DONE + LIVE-VERIFIED Intro/Victory/Credits screens (2nd umbrella slice):** refactored the pause
      builder into a shared `loadOverlayFromFile`; added `loadGameScreenInto(ui,"intro"|"victory"|"credits",…)`
      / `unloadGameScreenFrom` + `MenuActions::onShowCredits` + `show_credits` action. Authored
      `resources/ui/{intro,victory,credits}_screen.json` with `{{title}}`/`{{tagline}}` tokens. The scaffold
      now has ONE reconcile loop mapping `ScreenState`→overlay (pause/intro/victory/credits), (un)loading on
      change + driving `handleInput`; the three ImGui `render*Screen` calls are gone. Verified on `R5Verify`
      (temp `player_jumped→show_victory` trigger → data-driven VICTORY! with resolved title → Credits with
      resolved title+tagline). **Remaining ImGui gameplay surfaces:** `renderMainMenu` (fallback) +
      `renderSettingsScreen` (heaviest — sliders/dropdowns/keybinds) + `renderCountdownHud` + speech bubbles
      + interaction prompt; HUD-suppression-while-paused (#11); intro any-key-continue (Continue button only
      now); the editor's own ImGui screens.
    - **✅ DONE + LIVE-VERIFIED Settings screen (3rd umbrella slice):** `buildMenuElement` now builds
      slider/checkbox/dropdown bound BIDIRECTIONALLY to `GameSettings` via new `MenuActions::onGetSetting`
      /`onSetSetting` (floats; checkbox=0/1, dropdown=index) + `onBack`/`back` action. Authored
      `resources/ui/settings_screen.json` — standard **Graphics** (Resolution/V-Sync/Fullscreen/FOV),
      **Audio** (Master/Music/SFX), **Controls** (Mouse Sensitivity). Scaffold folds Settings into the one
      reconcile loop, wires get/set to GameSettings + window/camera/device (apply live, save on Back); ImGui
      `renderSettingsScreen` removed. Verified on `R5Verify`: pause→Settings shows all widgets at current
      values (1600x900 / Off / FOV 45 / vols), FOV slider click 45→111 applied, Back saved + returned to
      pause. **Deferred:** keybind rebind, brightness/invertY (no apply path), AI settings (dev-only).
      **Polish:** open-dropdown overlap; layout tall (bottom rows near client edge at 125% DPI).
    - **✅ DONE + LIVE-VERIFIED Fallback main menu (4th slice):** data-driven `mainmenu:*` overlay
      (`mainmenu_screen.json`, `{{title}}` + New Game/Options/Quit) via new `onStartGame`/`start_game`;
      scaffold reconcile maps `ScreenState::MainMenu`→it; ImGui `renderMainMenu` removed. Verified
      (pause→Main Menu → data-driven title screen). **With this, EVERY `ScreenState` screen is off ImGui:
      Intro/MainMenu/Victory/Credits/Settings/Paused.** (Menu-scene games still render their menu scene,
      not this fallback.)
    - **✅ DONE + LIVE-VERIFIED Countdown HUD (5th slice):** `hud_countdown` panel in `default_hud.json`
      (top-center, isTitle), `visibleWhen "countdown.active"`, text bound to `countdown.text`; scaffold
      registers both providers from `TriggerSystem::getActiveCountdowns()` (label + `M:SS.s` of the first
      active countdown) and no longer calls ImGui `renderCountdownHud`. Verified on `R5Verify` (temp 120s
      `timer` trigger → "Escape in  1:52.6" top-center). **Gotcha:** a `timer` trigger needs a NON-EMPTY
      `then` array or it's rejected (`Triggers: loaded 0`). **Follow-ups:** single countdown (no Repeater);
      no red-under-10s urgency (labels lack a color bind); editor still ImGui.
    - **✅ DONE + LIVE-VERIFIED Speech bubbles + interaction prompt (6th slice) — and the SCAFFOLD IS NOW
      ImGui-FREE FOR GAMEPLAY:** new `UISystem::worldToScreen` (project world→screen px) + `addWorldLabel`
      (per-frame imperative world-anchored text, drawn after retained screens, centered + above the point,
      cleared each frame). Scaffold projects each `SpeechBubbleManager` bubble + the `InteractionManager`
      nearest-NPC pos and queues labels; ImGui `renderSpeechBubbles`/`renderInteractionPrompt` removed.
      Verified on `R5Verify` (player spawned next to Elder Maewyn → mouse-looked to frame her → "Interact"
      prompt renders above the NPC). **The only ImGui render call left in the generated scaffold is
      `renderDialogueBox`, gated to AI conversations (intentional — UISystem lacks scroll + text-input).**
      Verifying this needed live mouse-look injection (`mouse_event` relative motion drives the captured
      FPS camera once the window is foregrounded) — extends the [[standalone-window-driving]] recipe.
    - **✅ DONE + LIVE-VERIFIED HUD-suppression while paused (#11, 7th slice):** `loadPauseMenuInto` now
      hides every non-`pause:*` screen (the translucent scrim used to let the HUD bleed through);
      `unloadPauseMenuFrom` re-shows them (visibleWhen re-gates). The scaffold also gates speech-bubble /
      interaction-prompt world labels to `ScreenState::Playing`. Verified on `R5Verify`: the top-center
      countdown HUD shows during gameplay and VANISHES when paused, returns on resume.
    - **✅ DONE + LIVE-VERIFIED AI conversation box (8th slice — the LAST gameplay ImGui surface):** built
      the missing UISystem text-input infra: GLFW **char capture** (`WindowManager` char callback re-owned in
      `reinstallScrollCallback` after ImGui steals it → `InputManager::handleChar`/`getTypedChars`/per-frame
      clear), a **`UITextInput` widget** (UISystem routes typed chars + edge-tracked Backspace/Enter to the
      focused field; `buildFromJson` "textinput" type), and `UI::setupAIDialogue` (dialogue.ai* providers +
      the `hud_ai_dialogue` panel's field submit → `submitPlayerMessage`). Scaffold + editor drop the ImGui
      `renderDialogueBox` entirely; scaffold gates tree keybinds (E/Enter/1-4) off during AI typing. Verified
      in the editor (project-open + world scene + active AI convo via an Ollama provider): box renders with
      the NPC greeting, the field auto-focuses (no placeholder), and typed chars echo in.
      **⚠️ VERIFICATION GOTCHAS worth remembering:** (1) the editor SKIPS `UISystem::handleInput` in
      launcher mode (`!launcherActive_` gate) — input only runs with a PROJECT open (`--project`), though the
      HUD still RENDERS; (2) the editor's GLFW window title under `--project` is the PROJECT name (e.g.
      "R5Verify"), class `GLFW30` — not "Phyxel"; (3) inject text with `PostMessage(WM_CHAR)` (no keydown =
      no editor keybinds fire) — `keybd_event` letters trigger editor keybinds and wreck the scene; (4) an AI
      convo needs a configured provider to START ("no API key" otherwise) — Ollama needs no key. See
      [[standalone-window-driving]].
      **Follow-ups:** long input overflows the field (no horizontal clip/scroll); em-dash → `?` in the bitmap font.
    - **✅ DONE Deferred settings rows (9th slice):** added Brightness (→ `setAmbientLightStrength`),
      Invert-Y (→ new `InputManager::setInvertY` flipping `mouseDeltaY`), and an AI section (Provider dropdown
      + Model/API-Key `textinput` fields) to `settings_screen.json` (now two columns). Needed: new
      `MenuActions::onGetSettingText`/`onSetSettingText` (STRING settings) + a `textinput` type in
      `buildMenuElement`; scaffold wires them to `GameSettings` + `AIConversationService::setLLMConfig`.
      **Keybind rebind STILL deferred (real blocker, not laziness):** `InputManager::registerAction` uses
      HARDCODED GLFW keys; `GameSettings.keybindings` is only saved/loaded, never applied — a rebind UI is a
      dead control until the input action system is rewired to read from settings. Polish: API-key unmasked;
      long input overflows the field.
      **Also fixed (found during verification): ESC pause is now EDGE-TRIGGERED in the scaffold** (member
      `escPrev_`). `isKeyPressed` is HELD-STATE, so the old `if (isKeyPressed(ESC)) togglePause` toggled pause
      every frame ESC was held → a long press = even # of toggles = no pause (flaky). **NO key-input
      regression existed** — diagnosed via a temporary log: `glfwGetKey(ESC)=PRESS` + `WantCaptureKeyboard=0`,
      i.e. isKeyPressed works. **VERIFICATION GOTCHA:** `PostMessage(WM_KEYDOWN)` does NOT update GLFW's
      polled `glfwGetKey` state (only WM_CHAR took that path); use foreground + `keybd_event` (real OS key
      events) for game KEYS in the standalone, and TAP briefly (a long hold multi-toggles). See [[standalone-window-driving]].
    - **NO-ImGui umbrella — what's LEFT (none gameplay-facing in shipped games):** keybind rebind (blocked on
      the input-action rewire above), and the EDITOR's own ImGui screens
      (`renderPauseMenu`/`renderMainMenu`/`renderSettingsScreen`/`renderCountdownHud`/`renderSpeechBubbles`/
      `renderInteractionPrompt` on the editor's `cameraCtl_`/Application path — separate from the scaffold).
      **The generated/shipped game is now 100% ImGui-free for gameplay UI.**
      The `buildMenuElement` widget+binding additions (slider/checkbox/dropdown + onGetSetting/onSetSetting)
      are reusable for any future data-driven control screen. **Also worth doing:** for menu-scene games,
      pause/credits/victory "Main Menu" should `transition_scene` back to the menu scene rather than the
      `ScreenState::MainMenu` fallback overlay.
- **Water system — FULL FEATURE MERGED TO `main`** (commit `80f9998`, 2026-06-06). Design:
  `docs/WaterSystem.md`. Default **OFF** (per-world `"water":{"enabled","seaLevel",...}` block
  in game.json, applied in `Application::autoLoadGameDefinition`), so it's inert for projects
  that don't opt in. **Architecture:**
  - **Sim** = a mass-conserving **cellular automaton** in `WaterSimulation` (pure CPU,
    unit-tested — `tests/core/WaterSimulationTest.cpp`, 12 tests). `WaterManager` runs it over a
    **fixed region** (origin `(0,8,0)`, dims `64×32×64` — water ONLY exists inside that box),
    reads terrain solidity from chunks (`syncSolidsFromChunks` → `hasVoxelAt`, **one bool per
    full voxel**), steps at 20 Hz. Features: gravity/compression upflow, horizontal leveling,
    evaporation sink, ocean seam (connectivity-gated implicit reservoir via `fillOcean`),
    springs (persistent sources), channels (no-evap riverbeds), destruction auto-flood (voxel
    occupancy callback from `ChunkManager`).
  - **GPU port** = `shaders/water_flow.comp` (gather formulation, opt-in via `water_gpu`
    command). Works + conserves mass, but **NOT yet a perf win** (synchronous per-step readback);
    CPU is the reference path.
  - **Render** = `WaterRenderPipeline` (flat implicit sea plane, procedural sky/sun reflection)
    + `WaterCellRenderPipeline` (per-cell instanced surface; **sloped seamless tops** via a
    shared corner-height grid, **side faces + vertical waterfall curtains**, depth-darkened
    translucency). Mist = soft `VfxSystem` bursts at detected waterfall lips (`WaterManager::
    waterfalls()`), with `VfxBurstParams.posJitter` for an even cloud.
  - **Debug HTTP** (`/api/debug/`): `place_water`, `water_sync`, `water_stats`, `set_sea_level`,
    `add_ocean_seed`, `clear_ocean`, `place_spring`, `clear_springs`, `set_channel_region`,
    `water_gpu`, `water_save`.
  - **GOTCHAS:** (1) `place_water`/`place_spring` into a **solid voxel does nothing** — target the
    AIR cell (e.g. spring on a platform whose top voxel is y19 goes at **y20**). (2) Sub-voxel
    terrain (subcubes/microcubes) is **NOT supported** — the sim is full-voxel; partial voxels
    collapse to all-solid/all-empty. (3) Water lives only in the fixed 64×32×64 region.
  - **Waterfall test recipe:** build a platform + a ≥1.5-cell cliff + a catch pool with side
    walls + end dam; `water_sync`; `place_spring` on the platform's AIR cell; `place_water` to
    feed; mist auto-spawns at lips where the drop ≥1.5. (Over-pouring fills the catch pool and
    erases the drop → fall/mist stop; drain by removing the dam.)
  - **NEXT (all on `main`, branch `feature/water-system` retained):** **real planar reflection**
    — newly **UNBLOCKED**, main fixed the mirror pass (axis/winding/torn geometry); re-enable the
    dormant reflection branch in `water.frag`, give water its OWN reflection pass (don't reuse the
    old broken path). **Refraction + depth-color-through-surface + foam** — needs a POST-SCENE
    water pass sampling `PostProcessor`'s offscreen color + depth (move water out of the scene
    pass). **Buoyancy/swim** gameplay. **GPU perf** (async/no-readback + active-set/sleep +
    GPU-expand rendering). **Sub-voxel floors** (cheap option: per-cell fractional floor height
    from sub-occupancy — no cell-count change). **Uniform-span mist** (decouple emission from
    per-cell live flow to remove the slight side-bias). None are blockers.
- **Render perf:** 18 → 235 FPS via removing two per-frame brute-force loops (mirror-voxel
  scan cache + `getPerformanceStats` O(1)). Open ideas: skip OIT pass when no transparent
  voxels, 36→6 index cube draw, backface cull (winding is fragile — see render docs).
- **Performance program — toward "100s of characters + rich worlds" (2026-06-15):** the
  emphasis is performance-as-a-design-constraint. Standing instrumentation = the per-pass
  endpoints (`/api/debug/frame_profile`, `gpu_scopes`, `engine_timing`); grade features
  against a frame budget (e.g. 120 fps = 8.3 ms; render ~4.5 / characters ~2 / physics ~1).
  Measure first — this session almost optimized a non-bottleneck (see below).
  - **Character/crowd perf (DONE + verified):** per-character CPU cost was the variable cost
    (NOT render/GPU; GPU upload ruled out — `updateCharacterInstanceBuffer` is a trivial
    host-coherent memcpy). Measured **Release ≈0.15 ms/char, linear**; Debug ≈1.3 ms/char
    (glm un-inlined + per-frame `std::map` allocs). A user's "1000→250 fps from one character"
    was almost certainly a **Debug build** (Release is a non-issue). Shipped four opts, all
    verified: (1) **`RagdollCharacter` caches `parts` grouped by boneGroupId**
    (`getPartGroups()`, lazy-rebuilt on size change + `markPartGroupsDirty()` in
    `clearBodies`) — kills the per-frame `std::map` in the update loop AND both render-batch
    sites (`RenderCoordinator` main + shadow); 5-char `Entities` phase **6.7→2.7 ms** Debug.
    (2) binary-search keyframes (`AnimationSystem::findKeyframeIndex`). (3) persistent-map the
    character instance buffer (`VulkanDevice`, was map/unmap every frame). (4) deleted the
    per-frame static-`debugFrame` bone-dump `LOG_TRACE` block. **Plus per-character animation
    LOD** (`AnimatedVoxelCharacter::update`): distant chars tick at 30 Hz (>30 u) / 15 Hz
    (>60 u), banked dt folded into the next tick so movement/root-motion are preserved;
    per-instance `m_lodJitter` (±20% period) destaggers crowds (10 far chars 5.05→0.47 ms,
    spike 5.2→1.8 ms). Viewer pos set per-frame via `setViewerPosition` in BOTH the editor
    (`Application`) and **`GameShell::updateGameplayCamera`** (so packaged games inherit it).
    LOD thresholds are header constants; `setLODEnabled(false)` disables. **Caveat: distance
    LOD does NOT solve "100s ON SCREEN"** — on-screen = near = full rate. That needs the
    structural crowd track (PARKED, user-deprioritized): GPU skinning / Vertex Animation
    Textures, cross-character draw-call batching (today each char = ~15-20 draws × main+shadow
    passes), shared-pose dedup, impostors.
  - **World perf findings (measured, 64-chunk Perlin, 2026-06-15):** **frustum culling WORKS**
    (14/64 chunks drawn looking in, 0 looking away; 1 instanced draw per visible chunk — draws
    are NOT the world bottleneck). Interior face culling is excellent (12.3M of 12.6M faces
    dropped at mesh time). **GAPS:** (a) **no greedy meshing** — 325K faces = 1.30M verts
    (4/face), every face its own quad; merging coplanar same-material faces would cut Static
    Geometry vtx cost, but the static pipeline is instanced-unit-quads (8 B `InstanceData`) so
    it's an architectural rework; (b) **no occlusion culling** (`fullyOccludedCubes:0`) —
    big lever for caves/cities/interiors; (c) **no voxel LOD** — distant chunks full-res, so
    view distance scales Static Geometry + Shadow linearly (the lever for large worlds).
  - **SSAO is OFF now (DONE + verified, 2026-06-15):** the SSAO pass ran **unconditionally
    (~3.3 ms GPU)** but its output was **consumed by nothing** — `post_process.frag` binds
    `ssaoTex` but the multiply is disabled (shader header comment) and `voxel.frag` never
    samples it. Flipped `PostProcessor::ssaoEnabled` default → **false** (`renderSSAO` already
    early-returns on it). Verified: SSAO scope 3.3→0 ms, frame ~10.3→6.5 ms / fps ~100→155
    (Debug), **visuals identical** (output was unused). Re-enable (`ssaoEnabled=true`) ONLY
    when the post-process SSAO multiply is fixed + re-enabled so the cost buys a visible
    result. Per-pass GPU now (Debug, 64-chunk view): Scene/StaticGeo 2.9, Shadow 1.4,
    ImGui 0.69 (editor-only), Post 0.2.
  - **Shadow range 150→110 (DONE + verified, 2026-06-15, commit `14709ca`):** the shadow pass
    renders every chunk within `ShadowMap::m_shadowRange` of the camera (360°, no frustum
    cull) into the 2048² map, so cost ~range² and is vertex-bound over distant high-relief
    terrain. After SSAO it was the top GPU cost (~3.68 ms in mountains). 150→110 cut it to
    ~0.92 ms (8-sample stable), shadows still correct/sharper near camera. Slider-tunable.
  - **Occlusion culling Phases 1–2 (DONE, flag-gated OFF, 2026-06-15, commits `39b433a`,
    `2cce03f`):** Minecraft-style chunk visibility graph. `Chunk::computeVisibilityMask()`
    flood-fills 32³ air into 6-connected components on rebuildFaces → `m_faceConnect[]` /
    `facesConnected(a,b)` (opaque cubes block; empty/subdivided/transparent = air, errs toward
    visible). `RenderCoordinator::applyOcclusionCulling()` BFS from the camera chunk through
    air-connected, frustum-visible chunks; unreached = culled. Conservative (no anti-wraparound)
    so **no false holes** (verified). Toggle: `POST /api/debug/occlusion {enabled:bool}` (→
    `setOcclusionCullingEnabled`) or `PHYXEL_OCCLUSION=1`. **KEY FINDING — chunk-granularity
    occlusion ≈ "cave culling": it only helps where solid FULLY fills chunks
    (underground/caves/enclosed interiors); OPEN SURFACE scenes see ~0 benefit** because the
    continuous above-ground air layer connects every chunk's graph (a city street culled 0;
    sub-chunk buildings don't partition the air). Verified working in a wall+gap test (drawCalls
    2→1, identical screenshots). Open follow-ups: anti-wraparound pruning (more culling, risks
    holes — test carefully), sub-chunk/portal occlusion for surface scenes, wire into game.json.
  - **Greedy meshing + variable-size faces (DONE + verified, 2026-06-15, commit `e916d1e`):**
    voxel-LOD track Phase A+B. A static cube-face instance can now span a WxH rectangle:
    `packCubeFaceDataSized` encodes (sizeU-1,sizeV-1) in the cube face's spare bits 20-31
    (sizeU/sizeV = vertexID bit0/bit1 axis extents; =1 reproduces packCubeFaceData).
    `static_voxel.vert` scales the unit quad's two in-plane axes + tiles UV (subcube/microcube
    branches untouched); `voxel.frag` wraps the atlas sample with `fract()` (half-texel inset)
    + `textureGrad` (continuous derivatives → no mip seam). `ChunkRenderManager::rebuildCubeFaces`
    now greedy-merges visible cube faces per (direction, slice), keyed on textureIndex+flags,
    honoring the cross-chunk NeighborLookupFunc. **Verified (64-chunk Perlin): 325,636→53,486
    faces / 1.30M→214K verts (6.1x), pixel-identical (textures tile seamlessly incl. close-up,
    multi-material splits correct), Debug FPS 171→277.** Flat/built surfaces reduce ~1000x.
    Render-only; collision/physics untouched. The variable-size face format is the foundation
    Phase C reuses.
  - **Phase C — distance voxel LOD: ATTEMPTED, BROKEN, REVERTED (2026-06-15, reverts
    `917dd09`/`acea8ef` of `1966a2e`/`6c9d74a`). Full writeup + retry plan was in `docs/VoxelLOD_TODO.md` (removed in the 2026-06-27 docs cleanup; recover from git history if revisiting).**
    Downsampled distant chunks to 16³/8³ + skirts.
    Earlier "verified" claims were INVALID — two compounding traps:
    1. **⚠️ RENDER-DISTANCE GOTCHA (remember this):** `Application::maxChunkRenderDistance` is
       **~96 at runtime** (set low somewhere, NOT the 1000 header default), so the camera far
       plane frustum-culls everything past ~96 u. Every "far LOD terrain" screenshot was the
       96 u cutoff, not LOD. Raise it with **`POST /api/debug/render_distance {distance}`** (the
       one piece kept from this attempt — genuinely useful) before judging anything at distance.
    2. The LOD>1 **"boundary shell"** (drawing full boundary walls on every coarse chunk) sealed
       chunks in boxes, **masking a broken coarse mesh**. With render distance raised AND walls
       culled, the downsampled mesh is **holey / fragmented** (sparse disconnected faces, proven
       up close + in wireframe). So the downsampling + coarse cross-chunk culling is wrong.
    LESSON for the next attempt: validate LOD at a **proper render distance** AND **up close**
    (force tiny lod distances so a coarse chunk is right in front), in **wireframe** — not from
    afar where a render cutoff or boundary shells can fake "it works". The variable-size face
    format (greedy meshing, below) is sound and is still the right foundation; the bug is in the
    downsample-to-watertight-coarse-mesh step.
  - **NEXT perf levers (ranked):** retry voxel LOD with a watertight coarse mesher (validate
    up-close + wireframe + proper render distance) → occlusion in real cave/dungeon content
    (toggle exists) → crowd-rendering (VAT/instancing, parked) which is the true blocker for
    100s on screen.
- **Debris/particle solver perf:** the GPU particle solver (`GpuParticlePhysics`,
  `recordComputeCommandsNew`) dominated frame time under debris load. **Per-pass GPU timing
  is now built in** — `recordComputeCommands` takes an optional `GpuProfiler*` and emits
  phase scopes on the first physics tick: Setup / GridClear / GridBuild / SortScan /
  SortScatter / NarrowVoxel / ColoringCSR / Solve / Finalize, nested under "GPU Particles"
  in the `gpu_scopes` endpoint. **How to use:** break a wall (`apply_damage`, ~3000 debris),
  then `curl /api/debug/gpu_scopes` + `/api/debug/engine_timing`.
  - **FIXED:** the inter-particle broadphase prefix sum (`particle_sort_scan.comp`) was a
    SINGLE GPU thread serially scanning all 64³=262,144 grid cells every tick — ~24ms/tick,
    a FIXED cost regardless of particle count, ~72% of the solver. Replaced with a 3-pass
    work-efficient parallel scan (`particle_scan_block` / `_blocksums` / `_add`). Result
    (Debug, 3166 debris): SortScan 24ms→0.2ms, solver 130ms→10ms, ~7→50 FPS, and physics
    back to 1 tick/frame (the low-FPS→more-ticks spiral stops). Lesson: a `dispatch(cmd, 1)`
    over a large buffer is a serial-scan trap — check dispatch sizes.
  - **Next targets** (per the per-pass breakdown): NarrowVoxel (~5ms) and Solve (~3ms).
  - **Settling "popcorn" (PARKED — don't reopen without a plan):** debris stays too
    energetic, esp. in concave/bowl piles. Root cause: it's a pure position-based solver
    (NO restitution term anywhere — confirmed; so it's not "bounciness" to tune down) —
    resolving penetration moves position, and position deltas become velocity, so overlap
    injects energy. Two feeders: (1) spawn overlap — FIXED, shatter pieces now spawn at
    distinct non-overlapping sub-cell positions (`DamageSystem::applyDamage`), which removed
    the spawn-time burst but did NOT fully calm dense piles; (2) dense bowls — many
    simultaneous contacts under-converge in 8 solve iters + there's no sleep system, so
    residual jitter never halts (bodies are fully solved until the ~25s lifetime expires).
    Remaining real fix = a **contact-aware sleep/freeze with hysteresis** (also the biggest
    steady-state perf win). RISK: legacy XPBD sleep "oscillated in/out on stacks" — needs
    island-based waking + separate sleep/wake thresholds. Player-wake is easy (the per-limb
    char push in `solver_integrate.comp` just clears the sleep flag, as legacy did);
    debris-waking-debris (island propagation) is the hard part. User tabled this to avoid
    regressing working behavior — pick it up deliberately, not casually.
- **Spell-cast animation pipeline (2026-06-10): Phases 0–2 DONE, verified in-engine.**
  `tools/anim_pipeline/`: `anim_format.py` (parse/write/splice the text .anim — round-trip
  verified lossless on humanoid.anim), `anim_lint.py` (mechanical-jank gates: unit quats,
  >120° slerp-ambiguous segments, loop closure; plus per-bone angular velocity/accel envelope
  calibrated from known-good clips via `calibrate`), `pose_dsl.py` (sparse-key clips as
  (time, named-pose, ease) on top of the idle@0 stance; smoothstep subdivision; sign-continuous
  quats by construction), `generate_casts.py` (8 cast clips spliced into humanoid.anim),
  `probe_axes.py` + `spell_anim_resolver.py`.
  - **6 animation families** mapped from SpellDefinition by rules in
    `resources/spells/spell_anim_families.json` (overrides: fireball/hold_person → bolt):
    bolt=cast_quick, thrust=cast_standard, call_down=cast_call_down, touch=cast_touch,
    ward=cast_ward, ritual=cast_windup/loop×N/release. Cast-time speed = playback rate
    clamped [0.7,1.4] toward per-CastingTime targets + structural loop-count for rituals;
    skill hook = rate × (1 + 0.05·(prof−2)). clip_meta carries castFamily/castRole/releaseFrame
    (VFX fire moment; mirrors hitFrameFraction).
  - **CRITICAL conventions** (cost a wrong-direction round): character at rotation 0 FACES +Z
    (do NOT deduce facing from screenshots — user caught arms animating backward); arm-bone
    deltas on idle stance: −Z=swing forward (casting axis), +X=across body, +Y=twist. Clip rot
    keys are ABSOLUTE local rotations → full-body clips must key all 52 channels. clip_meta
    comments only legal at top of file. `orbit_screenshots` multi-view in one call can return
    identical images (request one view per call); `get_bone_positions` = 12 segment boxes only.
  - **Phase 3 (C++ wiring) DONE + verified live (2026-06-10):**
    - `AnimatedCharacterState::Cast` + `castSpell(vector<CastSegment{clip,speed,loops}>)` in
      AnimatedVoxelCharacter — segment queue (ritual = windup + loop×N + release), per-segment
      playback rate (multiplies the animTime tick; duration checks use duration/speed),
      `setOnCastRelease` fires once at the FINAL clip's hitFrameFraction (carries releaseFrame).
      Movement frozen during cast; rejected while sitting/airborne/already casting.
    - `Core::SpellAnimMapper` (engine/src/core/SpellAnimMapper.cpp) — C++ port of
      tools/anim_pipeline/spell_anim_resolver.py; loads
      `resources/spells/anim/spell_anim_families.json` (moved into anim/ subdir so
      SpellRegistry's *.json glob doesn't eat it).
    - `cast_spell` handler (Application.cpp) — resolves caster ("caster" param: player/NPC),
      lazy-loads SpellRegistry + mapper, faces the target, plays the plan, defers VFX +
      destruction to the release callback; falls back to immediate VFX when spell unknown /
      "animate":false / character can't cast. Verified: fire_bolt (bolt @0.7 speed, deferred
      VFX in logs) and animate_dead (full 6.3s windup/4-loop/release sequence polled via
      /api/animation/state, VFX at release).
    - **TRAP FIXED:** the clip_meta parser std::stof'd every value — a string value
      (castFamily=bolt) threw out of loadModel and KILLED character creation ("invalid stof
      argument"). Now per-key try/catch + explicit castFamily/castRole skip; and
      reloadAnimations now applies clip_meta too (hot reload used to silently drop
      hitFrameFraction/warp tuning).
    - **Remaining ideas:** keybind/player-input casting, cast-cancel on damage, upper-body-only
      casts while moving (needs bone masking), NPC behavior-tree cast action.
- **Melee animation families (2026-06-10): clips + mapping DONE, engine wiring NOT started.**
  Same pipeline as casts. 5 authored clips in humanoid.anim (melee_stab_1h, melee_chop_2h,
  melee_sweep_2h, melee_thrust_spear, melee_parry — all lint-PASS + verified in-engine) +
  REUSED Mixamo mocap tagged via clip_meta (boxing/elbow_punch=unarmed,
  attack/melee_attack_horizontal/melee_attack_down=slash_1h, body_block=block) — mocap has
  real weight transfer, always prefer reuse over DSL for big body moves. Weapon→family
  mapping: `resources/rpg_items/anim/melee_anim_families.json` (anim/ subdir dodges
  RpgItemRegistry's *.json glob), rules on weapon.damageType + properties: Reach→spear,
  TwoHanded/Heavy→two_handed, Piercing→stab_1h, default→slash_1h; overrides spear→spear,
  quarterstaff→two_handed; no weapon→unarmed. clip_meta keys: meleeFamily/meleeRole
  (primary/secondary/quick/block) + hitFrameFraction.
  - **Authoring lessons:** distribute torso twist across ALL THREE spine bones
    (Spine+Spine1+Spine2) — dumping it on Spine1 alone trips the velocity envelope and reads
    twitchy (real mocap Spine1 peaks ~117 deg/s while ARMS hit 650–1085). Spine axes
    (measured): +X bow forward, −Y right-handed windup, +Y follow-through. Melee calibration
    envelope now includes melee_attack_h/down + elbow_punch.
  - **Melee wiring (2026-06-11): DONE + verified live.** Held weapon now drives attacks:
    `Core::MeleeAnimMapper` (loads rpg_items/anim/melee_anim_families.json) resolves a
    family per held ItemDefinition via chain: explicit `weaponFamily` field → RpgItemRegistry
    entry with same id (property/damageType rules) → ToolType heuristic (Sword/Axe/… →
    slash_1h) → unarmed. `AnimatedVoxelCharacter::setAttackCombo(clips)` + the Attack
    transition cycles `m_attackCombo`; the Attack state now uses the CURRENT clip's
    hitFrameFraction (clip_meta) for the onHitFrame callback instead of the character-level
    default. Application::updateHeldItem sets the combo on every hand change (+ boot init via
    m_heldComboInit). Test hook: POST /api/player/attack ("player_attack" in
    dispatchItemAPICommand) — simulates left-click, returns the active combo. Verified:
    iron_sword → melee_attack_horizontal → melee_attack_down (cycling); empty hand →
    boxing → elbow_punch.
  - **Combat Phase A — souls-style controls + chain mechanics (2026-06-12): DONE + verified
    live, zero new clips.** The plan: A=mechanics with existing clips, B=authored sword_1h
    flagship moveset (chain-pose continuity!), C=grips+speed classes (sword_2h/dagger/
    Versatile), D=spear/mace+weight polish, E=souls systems (stamina/roll/lock-on/stagger).
    - **Data:** melee_anim_families.json families now carry attacks (= LIGHT CHAIN, ordered),
      heavy, block, blockHold, speedClass; new speedClasses table {fast 1.15/0.45,
      standard 1.0/0.35, heavy 0.85/0.30} = {rate, chainWindow}.
      `MeleeAnimMapper::resolveMovesetDef(item)` → MeleeMovesetDef.
    - **FSM (AnimatedVoxelCharacter):** `setMoveset`, `lightAttack()` (buffers mid-swing →
      Attack state consumes the buffer at the chain window tail and plays the next link;
      ending without input resets the chain), `heavyAttack()` (committed one-shot, no
      chaining v1), `setBlocking(held)` → new Block state (guard clip frozen at
      blockHoldFrac while held, release → Idle). Attacks play at moveset rate (animTime
      tick × currentAttackRate, duration checks scaled — same pattern as Cast).
    - **Controls (ControlScheme.h):** LMB = light, Shift+LMB = heavy, guard = RMB in FPS
      scheme but LEFT_ALT in Tank scheme (RMB is the orbit hold there). Edge guards in
      GameplayCameraController (attackHeld_/heavyHeld_).
    - **Test hooks:** POST /api/player/attack {"type":"light"|"heavy"},
      /api/player/block {"held":bool}. Verified: buffered chain attack→melee_attack_horizontal,
      chain reset after idle, heavy=melee_chop_2h, block frozen at progress 0.5 → Idle.
    - **Phase B — sword_1h flagship moveset (2026-06-12): DONE + verified live.** 5 authored
      clips (all lint-PASS): sword1h_guard (block stance, blockHold 1.0 = freeze at full
      guard), sword1h_light1/2/3 (slash-across → backhand → overhead chop), sword1h_heavy
      (overhead coil with a held "tell" beat → committed chop, hit 0.57). **Chain-continuity
      pattern that works with the DSL: every link starts AND ends at the shared sword_guard
      hub pose; strikes land by ~40%, the 60-100% recovery tail is what a buffered chain
      input cancels; the 0.2s crossfade smooths hub re-entry.** slash_1h family now uses
      these (melee_attack_h/down/attack freed for other movesets). Verified: 3 rapid presses
      chain light1→light2→light3; slash impact frame + heavy coil storyboarded with the held
      sword visible.
    - **HYBRID CLIPS — the anti-stiffness technique (user-driven; THE default for melee):**
      pure pose-DSL hips rotation/dip moved the body as one rigid block (legs FK'd at idle —
      user: "if the feet move the knees dont"). Fix: `build_clip(..., legs_from=(srcClip,
      t0, t1))` samples the LOWER BODY (Hips incl. position + both full legs,
      LOWER_BODY_BONES in pose_dsl.py) from a mocap clip segment time-mapped onto the
      authored clip — real footwork/weight transfer under authored arms. Sword clips use
      melee_attack_horizontal (lights 1-2, different phases), melee_attack_down (light3 +
      heavy), body_block (guard stance legs). Pose Hips/HipsOffset deltas are ignored in
      hybrid mode. Also added: `HipsOffset` pose key (root translation deltas) for
      non-hybrid clips.
    - **Hit-frame damage (2026-06-12): DONE + verified live.** The player's onHitFrame
      callback (wired in Application::createAnimatedCharacter — it had been a dangling hook
      since CombatSystem was written) calls CombatSystem::performAttack with damage/reach
      from the held ItemDefinition (unarmed fists: 2 dmg / 1.6 reach), heavy ×1.6
      (AnimatedVoxelCharacter::isCurrentAttackHeavy), forward = visual front (+Z yaw
      convention), reach = weapon stat + 0.5 arm. Emits "player_hit" game events. Verified
      vs a dummy NPC: sword light 8, heavy 12.8; fists 2 and ONLY in close range (weapon
      reach is a real mechanic — the dummy at 2.24 units was sword-reachable but not
      fist-reachable).
    - **NEXT — Phase C/D/E:** sword_2h + dagger + Versatile grip variants (Phase C — the
      sword poses parameterize/mirror/retime, hybrid legs_from for footwork), spear/mace
      movesets + weight polish (D), souls systems: stamina/roll/lock-on/stagger + hit
      reactions (E). Also open: user's sword-feel nitpicks (unspecified, theirs to list),
      NPC movesets, thrust input, block actually mitigating damage (Block state exists but
      incoming damage ignores it).
    - **Combat Phase E — dodge/roll + real-time enemy AI (2026-06-16): DONE + verified live,
      committed.** The big souls slice. Three parts:
      1. **Directional dodge w/ i-frames** (`AnimatedVoxelCharacter` `Dodge` state +
         `dodge(dirXZ)`/`dodgeFromInput()`): a scripted ease-out kinematic lunge (~3.2 u /
         0.8 s) through the normal collision/gravity integrator, movement-relative (neutral =
         backstep), faces the dodge dir so the forward-roll clip reads for all 4 dirs. I-frame
         sub-window via `isDodgeInvulnerable()`; foot-IK suppressed mid-roll; clip fit to the
         window via `currentDodgeRate()`. `R` key (both ControlSchemes, edge-guarded in
         GameplayCameraController) + `POST /api/player/dodge {direction}`.
      2. **i-frame skip hook**: `CombatSystem::setInvulnerabilityQuery(fn(Entity*))` — attack
         resolution skips a target reporting `isDodgeInvulnerable()` (covers player AND NPCs).
         Unit-tested (`CombatSystemTest`).
      3. **Real-time enemy AI** (`Scene::CombatBehavior : NPCBehavior`, NOT the turn-based
         `CombatAISystem`): drives the NPC's character via **setControlInput** (so the full
         melee FSM ticks — `setMoveVelocity` would bypass `updateStateMachine`), acquires the
         nearest live non-self fighter (`getEntitiesByType("animated")`+`"npc"`), approaches/
         faces/circle-strafes/attacks (real swing mocap)/backs off, AND **dodges** incoming
         swings (rolls when the target is mid-Attack within reach; `evadeChance`/cooldown gated
         so fights resolve). NPC swings deal damage via the NPC's `onHitFrame`→`CombatSystem`
         (mirrors the player wiring). `NPCBehaviorType::Combat`, behavior string `"combat"`/
         `"aggressive"` in spawn_npc; `CombatSystem` threaded through `NPCContext` (NPCManager→
         NPCEntity). **Verified live: enemy hunts+kills the player (→respawn), player rolls
         through attacks untouched (0 hits in 8 s of rolling), and TWO combat NPCs duel each
         other — trading hits and dodging.**
      - **Player-health bridge:** `CombatSystem::setOnDamage` routes damage landing on the
        player Entity into `Application::playerHealth` (the HUD/RespawnSystem health), since the
        player has TWO health stores (Entity `HealthComponent` vs HUD). **Single-source
        unification of the two is a follow-up.**
      - **Roll-anim import:** "Stand To Roll"/"Run To Dive" FBX → `roll_forward`/`dive_forward`
        in humanoid.anim (see [[anim-fbx-import-pipeline]] memory; needs FBX2glTF on PATH +
        `tools/bin/import_rolls.py`; fixed `anim_editor.py` parser to skip `RootMotion` lines).
    - **Combat follow-ups (2026-06-16, all DONE + verified live + committed, after the
      Phase E push):**
      - **Hit-contact/aiming:** melee hits originate at the swinging hand via
        `AnimatedVoxelCharacter::getAttackOrigin()` (most-forward forearm/hand segment box),
        not a chest cone — punches connect on actual contact. Player reach ~1.0 unarmed /
        weapon-scaled; combat NPCs engage closer (attackRange 1.5) with a forgiving hand reach
        (2.2) so swings still land amid dodge-drift.
      - **Death:** `Death` state — `die(backward)` plays death_front/back once then freezes on
        the ground (terminal, no loop). A killing blow routes through `CombatSystem::onDamage`
        → `target->die()`; player death plays then RespawnSystem revives + `reviveToIdle()`s.
      - **Unconscious/KO:** `KnockedOut`→`GetUp` states — `knockOut(seconds)` lies in `ko_lay`
        then rises (long `get_up` clip rate-scaled to ~3 s). Test hook `POST /api/player/knockout`.
      - **Hit reactions (was Phase 4):** `HitReact` state on taking damage (light flinch /
        heavy stagger, re-stun-immunity); clips hit_head/stomach/rib.
      - **Weapons + attack types:** `CombatBehavior` resolves its moveset via `MeleeAnimMapper`
        (same as the player's held weapon); `spawn_npc` accepts `"weapon":"<items.json id>"`
        (iron_sword→slash_1h). Unarmed combo gained a **kick** (boxing→elbow_punch→kick).
      - **Enemy AI tuning:** run-approach + no post-swing back-off + hold-at-range = reliable
        trades (was 0 hits); evadeChance 0.45.
      - New clips imported (FBX from the user via `tools/bin/import_rolls.py`): roll_forward,
        dive_forward, hit_head/stomach/rib, dodge_right, death_front/back, ko_lay, get_up, kick.
      - **KNOWN GAPS / next polish:** roll_forward reused for back/left/right (looks off
        sideways — needs directional roll clips, `dodge_right` is imported-but-unwired); no
        stamina; no lock-on; **two player-health stores** (Entity HealthComponent vs
        `Application::playerHealth`) still not unified — combat damages both via a bridge;
        combat NPCs still **drift** across the arena via dodge-rolls; fights are slowish;
        the `get_animation_state` `progress` reads 0.0 for end-frozen Death (cosmetic); a
        bigger/bounded test arena is needed (small DebrisPushTest platform → fall-offs; use a
        generated **Flat** world for combat testing).
- **Turn-based combat system — BG3-style (2026-06-17): S1–S5 DONE + committed, a full fight
  is playable & verified live. Branch `feature/turn-based-combat` (NOT pushed). Design +
  per-subsystem status: `docs/TurnBasedCombat.md`.** Goal: support BOTH real-time (the souls
  slice) AND turn-based, mode locked **per-game** via `game.json combat.mode`
  ("turn_based"|"real_time", default real_time). Systems-first build; the headless D&D
  mechanics (`InitiativeTracker`/`ActionEconomy`/`AttackResolver`/`ConditionSystem`/…) already
  existed and were the foundation. Subsystems:
  - **S1 `Core::CombatDirector`** (`ee71aa7`) — single source of truth: mode + in-combat +
    whose-turn + sides + encounter lifecycle. Owns the `InitiativeTracker`; the HUD + AI read it
    through the director (replaced the loose `m_rpgInitiative`). Headless, 10 unit tests.
  - **S2 damage unification** (`e1d36f5`) — `CombatSystem::applyDamage(target,id,amount,src,type,
    knockback,hitBone)` is now the ONE place health is mutated + events/death dispatched;
    `performAttack` + the scripted `damage_entity` route through it. **Fixed the dual
    player-health stores:** `RagdollCharacter::setHealthComponent(external)` lets the player
    character SHARE `Application::playerHealth` (bound in the `createAnimatedCharacter` player
    factory); the combat onDamage bridge no longer double-decrements. +7 tests.
  - **S3 `Core::TurnActor` + `ITurnActorBody`** (`f0e9135`) — headless turn-execution bridge:
    translates move/attack intents into body commands gated by `ActionBudget`, owns the single
    **feet↔world-unit constant `0.3048`** (world units ≈ metres; 30 ft ≈ 9.1 u, 5 ft ≈ 1.52 u)
    and the turn-advance-vs-animation handshake (busy until the swing starts AND finishes, with a
    timeout). 14 tests.
  - **S4 enemy turn AI** (`2625a0f`) — `CombatAISystem` reworked from a one-shot instant action
    into a per-turn phase machine (Thinking→Moving→Attacking→Done) that drives the enemy through
    `TurnActor` + the **`Scene::CharacterTurnBody`** adapter (AnimatedVoxelCharacter→ITurnActorBody)
    over multiple frames: real walk/attack animation, budget-gated, **D&D d20-vs-AC** to-hit,
    damage via the S2 funnel. Gated to turn-based mode via the director. Verified live.
  - **S5 player tactical control** (`1725420`) — `Core::PlayerTurnController` (player-side mirror
    of the AI): binds a TurnActor to the player on their turn; move/attack/end-turn intents;
    attacks resolve d20-vs-AC via the funnel. Action bar (Action/Bonus/Move/**End Turn**) in
    `renderCombatHUD`; real-time WASD/LMB suppressed during the player turn (camera still follows).
    Intents are HTTP-driven (`combat/player_move|player_attack|end_turn`). 6 tests. Verified live.
  - **TESTING RECIPE (live turn-based):** launch directly (NOT MCP launch_engine — it deadlocks)
    with a project that has ground (`DebrisPushTest`); `POST /api/rpg/combat/set_mode {mode:
    turn_based}`; spawn an enemy with **`spawn_npc`** (NPCEntity, own health — `spawn_entity`
    "animated" HIJACKS `animatedCharacter` via the player-factory AND shares playerHealth, so
    use it only for the player); **NB the NPC's registry id is `npc_<name>` not `<name>`** (the
    initiative HP bar reads `0/1` if you use the wrong id); `combat/start` with `participants
    [{entity_id,player_side,initiative_bonus,speed}]`; `combat/set_initiative` to force order;
    drive the player turn via `combat/player_attack {target_id}` etc.; the enemy turn runs
    automatically (think delay 0.6 s). Combat HTTP lives under the **rpg handler** →
    `POST /api/rpg/combat/<action>`.
  - **S6 targeting + hit-chance + click picking** (`d900488`) — `AttackResolver::hitChance`
    (pure), PlayerTurnController targeting queries (`hitChanceVs`/`targetAC`/`distanceTo`/
    `inReachOf`/selectedTarget), `Application::resolveCombatPick(ray)` (enemy-AABB → attack /
    ground-plane → move) shared by the live LMB cursor ray (`tryCombatClick`) AND the HTTP twin
    `combat/player_pick`; `combat/targeting_info`/`select_target`; action-bar hit-chance readout.
  - **Player spellcasting** (`0a0a28d`) — `PlayerTurnController::castSpell` spends the action,
    resolves AttackRoll / SavingThrow / AutoHit / heal through the funnel at the cast RELEASE
    frame, drives the cast anim + VFX via `Application::playCastVisual` (factored from the
    real-time `cast_spell`). `combat/player_cast`. Spell registry preloaded so the lookup works.
    Spell attack bonus / save DC are stopgaps like pseudo-AC.
  - **AoE spells** — `SpellDefinition` gains `AreaShape`(None/Sphere/Cube/Cone/Line)+`areaSizeFeet`
    (Sphere exact; others bounded); fireball = Sphere 20ft. `castSpell` rolls base dmg once, applies
    full/half/0 per enemy in radius (5e fireball), `aoeTargetsAt`/`combat/aoe_preview`.
  - **S7 combat camera** (`3c0db05`) — `Application::updateCombatCamera` frames the ACTIVE
    combatant (auto-pans on turn change), pulls back to a tactical distance on combat entry,
    orbit(RMB)+zoom via the third_person `CameraRig`; no movement input fed. Replaces the
    cameraCtl path for the whole turn-based encounter.
  - **KNOWN FOLLOW-UPS:** (a) `createAnimatedCharacter` is the PLAYER factory — it binds the
    shared `playerHealth` AND wires a "player" onHitFrame to EVERY animated char it makes; the
    debug `spawnTestAINPC` reuse reverts the health, but non-player animated characters generally
    shouldn't inherit either (real enemies = NPCEntity, which is fine). (b) `CombatBehavior`'s
    real-time onHitFrame is not mode-gated (only the player's is) — fine while CombatBehavior
    isn't ticked in turn-based, gate it if they coexist. (c) NEXT = **S8** HUD polish (portraits,
    dice/damage floaters, ground movement-range ring + path spline); reactions/OAs (S9),
    conditions UI (S10), encounter authoring (S11), voxel-native depth (S12). Open: ground-point
    AoE targeting (vs centring on a target entity); Cone/Cube/Line are radius-approximated.
  - **Pseudo-AC stopgap:** generic entities have no CharacterSheet, so both sides currently
    derive AC = `8 + floor(HP%·6)`. Replace with real sheets/monster stats later.
- **Items system P1 (2026-06-11): DONE + verified live end-to-end.** "Items" = holdable
  things (weapons, torches, cups) with a three-state lifecycle: WORLD PROP ⇄ INVENTORY ⇄ HELD.
  - **Data:** `ItemDefinition` gains `holdable` + `held{gripBone, gripOffset, gripEulerDeg,
    scale, light{color,intensity,radius}}` (items.json-authored grip — tune by edit+restart).
    Item models are **microcube templates** (`weapons/sword_fine.voxel`, `items/torch.voxel`) —
    full-cube templates scaled down look like bricks; the user requires skinny item geometry.
    NOTE: `KinematicVoxel.scale` is an arbitrary vec3, so a finer-than-microcube item voxel
    class later is only a template-format extension (props never bake into chunks).
  - **World props:** `Core::ItemPropManager` — spawn from item def via kinematic voxel group
    (NEVER chunk-baked), registered as category="item" PlacedObject (metadata.itemId) with a
    synthetic "pickup" interaction point; `PlacedObjectManager::registerItemProp` +
    remove() skips clearRegion for items + recompute rebuilds pickup points; props rebuild
    after DB load via `rebuildFromPlacedObjects()` (called in the load path).
  - **Pickup:** `PickupInteractionHandler` (priority 30 > door 20 > seat 10), [E] → inventory
    + `item_picked_up` event. **Held:** `Application::updateHeldItem()` polls the selected
    hotbar slot per frame (= hotbar auto-equip); held visual is a kinematic group following an
    invisible grip-bone attachment (`getAttachmentTransform`); held `light` follows the hand
    (torch verified). Drop (`drop_item`) spawns the prop ahead of the visual front (+Z conv).
  - **APIs:** `/api/items/spawn|drop`, `/api/interact` (simulates [E] — NOTE an older
    "interact" command already existed in the chain and wins; same effect), MCP `spawn_item`/
    `drop_item`. `AnimatedVoxelCharacter::resolveBoneId` aliases bone names
    ("right_hand"→"mixamorig:RightHand") — the old equip_item attachment had silently failed.
  - **TRAPS:** (1) Application::processAPICommands else-if chain is AT MSVC's C1061 nesting
    limit — new commands MUST go in dispatchXxxAPICommand helpers (dispatchItemAPICommand
    added). (2) Interaction detection uses the PLAYER position only when control target is
    AnimatedCharacter; otherwise the FREE CAMERA position — when testing interactions via API,
    move the PLAYER next to the point (radius 2.0), not the camera. (3) set_camera needs
    mode:"free" to reposition the free camera.
  - **Items P2 — declarative item effects (2026-06-11): DONE + verified live.**
    `ItemDefinition::effects[]` (ItemEffectDef): per-effect template-local `anchor`,
    `vfx{color,rate,count,size,speed,upBias,gravity,lifetime,intensity,posJitter}` (periodic
    `VfxSystem::spawnBurst` — the waterfall-mist pattern), `light{}`, and `when{state:
    any|held|prop, nearby{type,name,radius}}`. **Effects live on the ITEM, not the held
    state** — a dropped torch keeps burning. `Core::ItemEffectSystem` ticks instances
    (held_player + every prop) with transform providers (KinematicVoxelManager::getTransform),
    4 Hz condition checks, light callbacks injected by Application (LightManager), nearby
    query via EntityRegistry::getEntitiesByType + distance + id-substring filter. Legacy
    `held.light` auto-migrates to an always-on effect in ItemDefinition::fromJson (single
    runtime path; updateHeldItem's own light code removed). Demos in items.json: torch
    "flame" (always), iron_sword "foe_sense" (blue aura + light when any npc within 8 —
    verified toggling ON/OFF by moving an NPC). Condition vocabulary designed to extend
    (time-of-day, health, faction once NPCs carry factions).
  - **NEXT (items P3+):** use verbs (held weapon → melee-family attack — converges with melee
    wiring; consumables), survival-mode drop consume semantics (creative never decrements),
    torch gripEulerDeg flip (currently hangs head-down in hand), prop lay-flat orientation,
    NPC held items, containers/loot, equipment-screen integration for the D&D layer.
- **Open items:** `open_project` / heavy commands time out the 5s game-loop budget (one-time
  heavy load, cosmetic); no world DB versioning.

---

## User working preferences

- **Single source of truth** for state; **incremental** architecture simplification.
  **Avoid big rewrites and over-abstraction** — when investigation shows a refactor isn't
  warranted, say so and stop.
- **Measure before optimizing**; reproduce + instrument rather than guessing.
- **Performance is a first-class design constraint** (goal: rich worlds + 100s of characters
  on screen). Weigh perf impact on every design decision; keep a frame budget in mind and use
  the per-pass profiling endpoints as standing instrumentation. (Also: Debug-build numbers are
  NOT representative of shipped perf — confirm config before treating a slowdown as real.)
- **Verify before destructive "repair":** impossible-looking output usually means the
  harness/tooling is wrong (e.g. a stale binary / hung MCP), not the source — confirm with
  `git diff HEAD` / a grep before "fixing" working code.
- **Editor UI:** action buttons (Reset/Delete/…) always visible regardless of state;
  per-object properties on the object's panel; global action settings on their own panel.
- Wants thorough design/planning discussion before building large features.
- **Detailed by default — in the ENGINE (2026-07-05):** procedural generators must produce
  sub/micro surface detail as their DEFAULT output, never behind an opt-in flag or "hero" tier.
  The user should never have to ask for detail. Exemplar: tree_forge rasterizes all thick wood
  at subcube resolution unconditionally (emit() re-compresses interiors to cheap cubes, so
  detail costs only the surface shell — +2.4% prims on an oak); its `round_trunk` flag was
  DELETED rather than defaulted-on. Apply the same standard to any future generator (structure
  gen, ProceduralTree, rocks/flora): coarse interior + fine surface, detail unconditional,
  perf spent where it's visible.

---

*Last meaningful update: **turn-based combat S1–S7 + spellcasting + AoE (2026-06-17)** — see the
"Turn-based combat system" workstream above + `docs/TurnBasedCombat.md`. A BG3-shaped turn-based
fight is playable & verified live on branch `feature/turn-based-combat` (NOT pushed):
click-to-move/attack with a hit-chance readout, player spellcasting (attack/save/auto/heal +
fireball AoE with full/half-on-save), a tactical camera that auto-frames the active combatant,
and enemy AI taking real animated turns. Single source of truth = `CombatDirector`; damage
unified through `CombatSystem::applyDamage`; `TurnActor` bridges turns to the live FSM;
`CombatAISystem` (enemy) + `PlayerTurnController` (player) drive turns; combat HTTP under
`/api/rpg/combat/<action>`. ~85 turn-based unit tests green. **MERGED TO `main`.** NEXT MAJOR
TRACK = a proper **Game-HUD system** — the combat HUD we built is an editor-ImGui STOPGAP in the
wrong layer (overlaps editor panels, won't ship). **HUD DESIGN NOW RESOLVED (2026-06-17) →
`docs/HudSystem.md`:** build on **`UISystem`** (the existing retained custom-Vulkan widget tree —
NOT ImGui; already ships + themeable), **data-driven JSON authoring + code escape hatch**, **both
editor previews** (play/"Game view" over the viewport + a HUD-preview panel), **BG3/D&D look**.
**FIRM USER PRINCIPLE: ZERO ImGui in a shipped game's real UI** — menus/screens/HUD/dialogue all
move onto the custom-Vulkan `UISystem`; ImGui stays editor-only + an optional strippable debug
overlay. This commits a migration of ALL shipped UI off ImGui (`HudSystem.md` §11a):
`GameMenuRenderer` (live ImGui menu path), `GameMenus.cpp` Intro/Victory/Credits screens,
`renderCountdownHud`, host-side dialogue render, and the standalone host (minimal_game + scaffold).
Discovery: there are TWO data-driven shipping game-UI systems already (`UISystem` retained-Vulkan +
`GameMenuRenderer` ImGui). **VERTICAL SLICE DONE + VERIFIED LIVE (2026-06-17, NOT committed):** a
data-driven health bar on `UISystem` — `HudDataContext` (header-only typed providers + `bind` field +
`applyBindings`), `UIProgressBar` widget (drawRect, parsed by MenuDefinition), `Application::
setupGameHud` loads top-level `game.json "hud"` + registers player.health providers + applies bindings
per frame. **Solved the editor-preview gap:** `UISystem` now renders LAST IN THE SCENE PASS into the
offscreen image (was post-process/swapchain), so the HUD shows in the editor Viewport panel AND ships
via post-process — one pipeline, no ImGui. Verified in DebrisPushTest: bar reads "HP 100/100",
`damage_player 62` → "HP 38/100". **The whole HUD-system work (19 commits) is now MERGED to `main`
+ pushed** (was `feature/hud-system`); remaining work catalogued in `docs/HudSystem.md` ("Open Items / Remaining Work" section).
**Then re-homed the combat HUD onto `UISystem`**: round banner + turn label + action bar + hit-chance
(`combat.*` providers, `visibleWhen`-gated) + **Initiative turn-order list** via a new `UIRepeater`
widget + list binding + **End Turn** `UIButton`→`endTurn()` — all verified live in turn-based combat.
New infra: `visibleWhen`, HUD-as-array-of-panels, `UI::applyHudBindings` (RenderCoordinator applies
to all screens), `HudDataContext` = pure registry. **`renderCombatHUD` DELETED — combat HUD fully
data-driven on UISystem, verified live post-deletion (End Turn click advances round).** Added a
reusable **UI click-injection** test hook (`UISystem::injectClick` + `POST /api/ui/click {x,y}` →
`ui_click`) so agents can test interactive HUD/menu widgets without a mouse — GOTCHA:
`UIPanel::handleClick` hit-tests the panel's own rect first, so size panels to contain children.
Default modules done: health, objectives, combat set, **hotbar** (all verified live in the editor).
**UIImage arbitrary RGBA textures DONE** (`UIRenderer::loadTexture` + per-texture descriptor sets +
draw-run batching + a `mode` push-constant in ui.frag; `UIRepeater.horizontal`). **TTF fonts DONE** (`BitmapFont::initializeTTF`
bakes a TTF via stb_truetype into the R8 atlas, AA glyphs reuse mode-0, metrics normalized → no
layout shift; UISystem prefers JetBrainsMono, falls back to bitmap; `UIRenderer::setWhitePixelUV`
relocates drawRect's white texel). So UISystem now has crisp text + RGBA images + rects — the full
toolkit to replace ImGui menus. **ENGINE DEFAULT HUD DONE**: ships `resources/ui/default_hud.json`;
setupGameHud loads it when game.json has no "hud" → games get the full HUD with ZERO authoring (own
"hud" overrides). Verified live. **DIALOGUE (standard trees) MIGRATED to UISystem** (UILabel word-wrap + `dialogue.*`
providers + `hud_dialogue` panel; ImGui `renderDialogueBox` now AI-conversation-only). **MENUS MIGRATED (editor path)**: UISystem
gained widget `position` + panel `freeLayout` (absolute layout) + fullscreen bg; `UI::loadMenuInto`
converts the GameMenuRenderer schema → `menu:*` UISystem screens with button actions (transition/
quit/open-close submenu); editor `onMenuSceneLoaded` routes to it, ImGui `GameMenuRenderer->render`
removed from the editor loop. Verified via the `/api/ui/load_menu` debug hook (PHYXEL DEMO menu +
Options submenu nav). Not ported: animations/fonts/colors/{{tokens}}. **GOTCHA: multi-scene
`load_game_definition` hits the pre-existing scene-transition vulkan crash — verify menus via the
direct `/api/ui/load_menu` hook, not a scene load.** NEXT ImGui→UISystem: **SCREENS** (Intro/Victory/
Credits — standalone-shell-driven, hard to verify in editor); **standalone-host wiring** (EngineRuntime
still uses GameMenuRenderer; minimal_game DISABLED in CMake — verify via packaged run); game.json
font/theme config; bundle resources/ui+font in packages.
Combat follow-ups deferred: reactions/OAs, conditions UI, ground-point AoE targeting. Earlier: **performance program kickoff
(2026-06-15)** — see "Render perf" workstream. Shipped + verified (NOT yet committed):
character-update opts (cached
bone→parts grouping, binary-search keyframes, persistent instance-buffer map, removed
debug bone-dump) + per-character animation LOD with crowd jitter, wired into both editor and
`GameShell`; and **SSAO disabled by default** (was ~3.3 ms GPU for an unused result —
frame ~10.3→6.5 ms / fps ~100→155 Debug, visuals identical). Measured world perf: frustum
culling WORKS; gaps are no greedy meshing / no occlusion culling / no voxel LOD. NEXT levers:
occlusion culling → greedy meshing → voxel LOD; crowd rendering (VAT/instancing) parked but is
the real blocker for 100s on screen. Earlier: full water system merged to `main` (`80f9998`,
2026-06-06; CPU CA sim + per-cell render + mist/ocean/springs/channels/flood, opt-in GPU port,
default-OFF). NOTE: `main` has 9 PRE-EXISTING failing unit tests (material/atlas counts,
inventory, skeleton hinge, nav StepUp) unrelated to this work — flag for separate triage.
Debris settling/"popcorn" + no sleep system still PARKED. Fresh session? Skim this, then
`git log --oneline -15`.*
