# Multiplayer — server-authoritative co-op for shipped Phyxel games

_Designed 2026-07-07 (user decision). Multiplayer is a feature of **games created with the engine**
(the `create_project.py` / `GameShell` / `EngineRuntime` standalone runtime), NOT of the editor.
The editor (`editor/src/Application.cpp`) stays a desktop authoring tool; its HTTP API /
`APICommandQueue` / MCP surface is dev tooling and is explicitly NOT the network protocol.
Grounding for every claim below: two codebase surveys, 2026-07-06 (sim/render coupling; state
authority) + one standalone-runtime survey — key file refs inline._

## 1. Scope & model

| Decision | Choice | Why |
|---|---|---|
| Target scale | **2–8 player co-op** | What replication serves cheaply; interest management scales it later |
| Topology | **Listen server first** — the host player's game *is* the server | Skips the headless-Vulkan boot problem (the single most expensive blocker); host machines are beefy anyway because they run the LLM-driven components |
| Sync model | **Server-authoritative state replication** | See §2 — lockstep is unreachable |
| Dedicated headless server | **Tabled (Phase 5)** | Kept honest by the Seam Rule (§3); becomes "same server core, render-free boot" instead of a rewrite |
| LLM / TTS / AI brains | **Host-only; results replicate as events** | One machine holds keys + compute; clients receive dialogue lines / story changes / behavior switches as ordinary replicated events. LLM latency is async-friendly under replication (would be poison under lockstep) |

**Out of scope (documented, deliberate):** host migration (host quits ⇒ session ends), NAT
traversal / relays (v1 is LAN + direct IP), competitive anti-cheat (host has zero-latency
advantage — acceptable for co-op), lockstep determinism, editor-API-as-game-protocol.

## 2. Why replication, not lockstep

Lockstep needs cross-machine bit-determinism. The engine has none and retrofitting it means
rewriting the update model:

- `VoxelDynamicsWorld` is multi-threaded with `m_threadCount = hardware_concurrency`
  (`engine/include/physics/VoxelDynamicsWorld.h`) — solver order varies per machine.
- GPU XPBD debris runs on vendor-dependent Vulkan float (`GpuParticlePhysics.h`).
- Most systems (NPC/AI, clocks, water, furniture) advance on **variable frame dt**; only the
  character-physics step is a fixed 1/60 accumulator.
- Chunk generation arrives from an async worker in nondeterministic order
  (`ChunkStreamingManager`).

Replication needs none of that: the server simulates, clients render what they're told.

## 3. The Seam Rule (standing invariant, enforced from Phase 1)

> **The simulation core consumes intents and emits state + events. It never reads render state,
> the camera, the window, or raw input. Local play and remote play enter through the same door.**

Concretely:

1. The canonical tick lives in `phyxel_core` (GameShell/engine level), takes `dt` + subsystem
   refs, and compiles without `RenderCoordinator`, `WindowManager`, or `Graphics::Camera`.
2. Player actions — local keyboard included — become **intents** (§5) before they touch the sim.
   The host's own player is session 0 speaking the same intent vocabulary as remote clients.
3. Anything cosmetic (GPU debris, VFX, speech-bubble rendering, animation blending) hangs off
   replicated **events**, client-side, and is never authoritative.

This rule is what keeps the tabled headless server cheap: violate it and headless becomes
impossible *and* the netcode becomes undebuggable.

## 4. Architecture

```
HOST PROCESS (beefy machine)                      REMOTE CLIENT PROCESS
┌─────────────────────────────────┐               ┌──────────────────────────┐
│ ServerCore (authoritative)      │               │ Presentation             │
│  fixed 60 Hz sim tick           │   intents     │  render, camera, HUD     │
│  IntentQueue ◄──────────────────┼───────────────┼─ input → intents         │
│  world / entities / story / AI  │               │                          │
│  LLM & TTS services             │  replication  │  ReplicaWorld            │
│  ReplicationEmitter ────────────┼───────────────┼─► apply deltas,          │
│                                 │  (deltas,     │   interpolate entities,  │
│ Local presentation (session 0)  │   snapshots,  │   spawn cosmetic FX      │
│  renders directly off sim state │   events)     │   from events            │
└─────────────────────────────────┘               └──────────────────────────┘
```

- **Transport (v1):** TCP (or WebSocket) with length-prefixed binary frames. No custom UDP
  reliability in v1 — voxel worlds tolerate 50–100 ms; prediction (Phase 4) hides the rest.
- **Join baseline — the big freebie:** packaged games ship the pre-baked world DB
  (`package_game.py` copies `worlds/*.db` next to the exe). A joining client already has the
  world on disk. Join = verify world identity (DB content hash + recipe), then receive the
  **accumulated voxel-delta log** since world load + a full entity/clock/story snapshot. No
  chunk streaming over the network in v1.
- **Tick/send rates:** sim 60 Hz fixed (matches the existing physics accumulator); entity
  snapshots sampled on the tick, sent ~20 Hz, client-interpolated. Budgets are numbers to
  measure in Phase 3, not vibes.

## 5. Intent vocabulary (draft — small, validated, versioned)

The protocol is a **gameplay** vocabulary, not the editor's god-mode authoring API. Every intent
is server-validated (range, rate, permission) before it mutates anything.

| Intent | Payload | Server validation |
|---|---|---|
| `move` | dir vector, sprint/crouch flags | speed clamp vs character stats |
| `jump` | — | grounded check |
| `attack` | aim dir | cooldown, range |
| `interact` | target entity id / voxel pos | reach distance, seat/door claim rules |
| `break_voxel` | voxel pos | reach, permission, rate limit |
| `place_voxel` | pos, material, size tier | reach, inventory (later), material whitelist |
| `dialogue_choice` | npc id, choice index | active-dialogue check for that session |
| `chat` | text | length/rate |

Additions ride a protocol version handshake at join. Anything not in the table is not sendable —
there is no generic "run command" escape hatch.

## 6. Authority & replication map (per system)

| System | Authority | Wire representation |
|---|---|---|
| Voxels (place/break/furniture re-staticize) | Server | Per-voxel deltas via the existing `ChunkManager::setVoxelOccupancyCallback` tap (already fires on every occupancy change — today it feeds the water sim) + material/size info from `ChunkVoxelModificationSystem` |
| Entities / NPC transforms + anim state | Server | Snapshot @ ~20 Hz, delta-compressed, client-interpolated (`EntityRegistry` already does `toJson` transform snapshots — the shape exists) |
| Character physics / grounding | Server (CPU `VoxelDynamicsWorld` + fixed 1/60 step) | Implicit in entity snapshots; Phase 4 adds client prediction + server correction for the local player |
| GPU debris (`GpuParticlePhysics`) | **Nobody — cosmetic by construction** (no CPU readback; never writes world state) | NOT replicated. Clients spawn debris locally from a `voxel_broke` event |
| Story / objectives / triggers | Server, **shared world state** | State-change events + join snapshot (`StoryEngine`/trigger state is JSON-friendly) |
| Dialogue | Server owns NPC dialogue state **per session**; UI is client-local | `dialogue_state` events scoped to the session in conversation |
| Day/night + WorldClock | Server | Periodic clock sync (both already `toJson`/`fromJson`) |
| LLM conversations / TTS | Host-only services | Results broadcast as dialogue/story events; audio synthesized client-side or streamed (decide in Phase 3) |
| Health / inventory / respawn | Server, per-session | Session-scoped state events |

**Known single-player assumptions to break (Phase 2):** the standalone runtime holds one
`AnimatedVoxelCharacter* playerCharacter_` registered under the hardcoded id `"player"`
(generated by `create_project.py`), one control scheme, one camera. Dialogue/interaction claims
key off that id. The editor's player-singleton weave is far worse but **irrelevant** — we only
de-singletonize the standalone runtime.

## 7. Ground truth (what exists today — from the 2026-07-06 surveys)

**The shipped-game runtime** is a per-project exe: `MyGame : GameShell : GameCallbacks`, hosted
by `EngineRuntime::run()` (`engine/src/core/EngineRuntime.cpp:140-184`). Its entire sim
orchestration is a **~50-line generated `onUpdate`** emitted by `create_project.py`
(`_generate_game_cpp`): SceneManager update → menu/pause early-out → physics step →
`GameShell::updateGameplayCamera` → jump/land trigger events → TriggerSystem → NPCManager →
StoryEngine → SpeechBubbleManager → InteractionManager → DialogueSystem. Every existing project
owns a **copy** of that ordering — engine features can't live in generated code, hence Phase 1.

**Standalones do NOT contain (for gameplay purposes):** combat, respawn, water, doors, furniture,
GPU debris, Python scripting, JobSystem. v1 replication surface is therefore small.

**Update (2026-07-13, post-dates this design):** standalones now CAN optionally host a scoped
`EngineAPIServer` / `APICommandQueue` / `CommandRegistry` via `Core::GameApiService`
(`engine/include/core/GameApiService.h`, wired into `GameShell`, gated behind
`EngineConfig::testApiEnabled` / the `--test` launch flag — see CLAUDE.md "Standalone test API").
This is dev/test-only (localhost bind, must be explicitly enabled, never shipped on) and serves a
small harness-oriented subset (`/api/state`, `inject_input`, `get_screen_state`, `fire_trigger`,
`navgrid_*`, `ui_click`, `engine_timing`, …) — it is NOT the multiplayer protocol and does not
change §5's intent-vocabulary plan; noted here only because the "Standalones do NOT contain
EngineAPIServer" ground-truth claim above is no longer accurate as stated.

**Renderer coupling (why headless is tabled, not free):** `EngineRuntime::initialize()`
unconditionally creates `WindowManager` + `VulkanDevice` + render pipelines
(`EngineRuntime.cpp:66-105`); device selection requires a window surface
(`WorldInitializer.cpp:227-244`); every `Chunk` is constructed with a `VkDevice` and embeds its
`ChunkRenderManager` (`Chunk.h:66,72,109`). Listen-server defers all of this.

**Already in our favor:** per-voxel change callback (`ChunkManager::setVoxelOccupancyCallback`),
fixed 1/60 physics accumulator, sparse SQLite world format (≈ a wire format for the join
baseline), `EntityRegistry::toJson`, clock `toJson/fromJson`, GPU debris provably cosmetic,
`GameShell.h:14-21` already declaring the shell-migration direction Phase 1 completes.

## 8. Phases — contract, validation depth, red test

Per the standing validation discipline (CLAUDE.md): depth is planned up front, red-before-green,
stress phase on every increment.

### Phase 1 — Promote the tick into the engine (the seam)

- **Contract:** one canonical, engine-side simulation tick (GameShell or a `SimulationCore` it
  owns) with the exact ordering currently emitted into generated `onUpdate`; the scaffold's
  `onUpdate` thins to `shell.updateSimulation(engine, dt)` + game-specific hooks; the tick
  function compiles in `phyxel_core` with **no** render/window/camera/raw-input types in its
  signature or body (Seam Rule §3.1). `create_project.py` regenerates against it.
- **Required depth:** L2 (structural) + L4 (a real scaffolded game runs on it).
- **Red test:** an engine unit/integration test that constructs the sim tick headlessly-in-spirit
  (real window allowed for now, but the tick called directly, no `render()`), steps a world
  containing an NPC with a schedule + a proximity trigger + a physics-dropped character for 600
  ticks, and asserts: NPC advanced along its path, trigger fired exactly once, character grounded.
  **Fails today because no engine-side tick exists to call** (does not compile — that is the red).
- **Stress:** tick 10,000× with 50 NPCs; assert no drift vs a 600-tick baseline extrapolation and
  no per-tick allocation growth.
- **Side value (even if multiplayer stalls):** tick-order fixes propagate to every game on
  rebuild instead of rotting in per-project copies — finishing the migration `GameShell.h`
  already promises.

### Phase 2 — PlayerSession (de-singletonize the standalone player)

- **Contract:** `PlayerSession { id, character, controlScheme, healthState, … }`; the runtime
  holds N sessions; session 0 is the local player; the hardcoded `"player"` entity id becomes
  `player_<session>`; triggers/interaction/dialogue key off session-scoped ids. **No networking
  in this phase** — sessions 1..N are drivable programmatically (scripted intents).
- **Required depth:** L3 (functional) — two sessions must be *usable*, not just constructible.
- **Red test:** spawn 2 sessions offline; drive session 1 with scripted move/jump intents while
  session 0 idles; assert both characters ground independently, session 1 crosses a trigger that
  session 0 doesn't fire, each takes damage independently. **Fails today: singleton
  `playerCharacter_` + hardcoded `"player"` id.**
- **Stress:** 8 sessions, all moving, 10 min simulated; assert per-session trigger/dialogue
  isolation at every step, no cross-session state bleed.

### Phase 3 — Transport + replication (first real multiplayer)

- **Contract:** host listens; client connects (LAN/direct IP), handshakes protocol version +
  world identity (DB hash + recipe), receives voxel-delta log + entity/clock/story snapshot,
  then: client intents (§5) in, replication out. Client renders replicated state; cosmetic FX
  from events. Server validates every intent.
- **Required depth:** L3 + L4 (two real engine instances).
- **Red tests:** (a) *convergence* — two instances on one machine, scripted client breaks/places
  50 voxels + walks a path; after quiescence, a deterministic world checksum (sparse voxel scan +
  entity transform digest) matches server↔client exactly — fails until delta replication exists;
  (b) *validation* — a hand-forged out-of-range `break_voxel` intent is rejected and mutates
  nothing — fails until server-side validation exists.
- **Stress:** join mid-session after 10,000 accumulated voxel deltas (log replay correctness +
  join time budget); 4 clients hammering `break_voxel` at rate-limit ceiling; kill a client
  socket mid-frame (host must not stall — the HTTP `queueAndWait` blocking pattern from the
  editor is exactly what NOT to copy).
- **Measured budgets (record the datum):** join time, bytes/sec per client at 8 players
  steady-state, host frame-time delta vs single-player.

### Phase 4 — Feel

- **Contract:** client-side prediction + server reconciliation for the local character; remote
  entity interpolation (~100 ms buffer); interest management (chunk-radius entity/voxel
  subscriptions). Latency-injection harness (artificial 50/150/300 ms) drives all acceptance.
- **Required depth:** L3 under injected latency.
- **Red test:** at 150 ms injected RTT, local character motion shows no rubber-banding beyond a
  measured correction threshold on a scripted run — fails before prediction exists.

### Phase 5 — Headless dedicated server (TABLED)

- **Contract (when resumed):** the Phase-1 ServerCore boots without `WindowManager` /
  `VulkanDevice` / render pipelines; `Chunk` data splits from `ChunkRenderManager`;
  `--server` scaffold target; `package_game.py` emits a server exe.
- **Blockers already mapped:** `EngineRuntime.cpp:66-105`, `WorldInitializer.cpp:227-244`,
  `Chunk.h:66,72,109`, unconditional Vulkan/GLFW link (`engine/CMakeLists.txt:47-68`).
- **Cheapness precondition:** the Seam Rule held through Phases 1–4.
- **Side value when done:** headless CI for game logic on GPU-less build agents.

## 9. Open questions (decide before the phase that needs them)

1. **Phase 2:** does per-session health/inventory live on the character entity or the session?
   (Leaning: session owns it; character carries only physical state.)
2. **Phase 3:** TTS delivery — synthesize client-side (ship Piper voices with the game) vs
   stream audio from host. Leaning client-side; host streams text events.
3. **Phase 3:** voxel-delta log persistence — in-memory only (session-lifetime) vs appended to
   the world DB (would also give single-player edit history). Start in-memory.
4. **Phase 4:** single-player convergence — run SP as a local server session 0 with no socket
   (one code path forever), or keep the direct path. Decide after Phase 3 perf data.
5. **Multi-scene games:** does a scene transition move all sessions together (co-op party model)
   or per-session? v1: all together; per-session scenes are a design change, not a netcode one.
