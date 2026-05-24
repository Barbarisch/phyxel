# Character Scripting / LLM-Influenced Character AI — TODO

Tracks the design stepping-stones that follow from
`tools/demo_living_scene.py`. The demo already structures each
character action as a self-contained `Scene` record
(`name`, `setup`, `act`, `teardown`); the work below promotes that
shape into a real agent runtime.

> Status: **planning** — nothing here is implemented yet. Pick this up
> in a fresh session.

## Goals

1. Define **complicated** character behaviour declaratively (sequences,
   reactions, schedules, social moves).
2. Allow an **LLM to influence or fully drive** that behaviour, with
   safety rails so the agent can't desync from world state.
3. Reuse the existing HTTP API surface — every "thing a character can
   do" already has a route. The agent never reaches into engine
   internals; it just calls the same endpoints the demo does.

## Architecture sketch

```
  ┌──────────────────────┐   plan/intent JSON     ┌───────────────────┐
  │   LLM planner        │ ──────────────────────▶│  Plan Executor    │
  │ (cloud or local)     │                        │ (Python, asyncio) │
  └──────────────────────┘ ◀──────────────────────└─────────┬─────────┘
            ▲                  world snapshot               │
            │                  + event log                  │ tool calls
            │                                               ▼
  ┌──────────────────────┐                        ┌───────────────────┐
  │  Memory / Journal    │ ◀──────────────────────│  Phyxel Engine    │
  │  (SQLite, vector DB) │       state +          │  (HTTP API 8090)  │
  └──────────────────────┘       events           └───────────────────┘
```

## Building blocks (in suggested order)

### 1. Tool registry

Promote each `Scene` action in `tools/demo_living_scene.py` to a
typed `Tool`:

```python
@dataclass
class Tool:
    name: str
    description: str            # LLM-readable
    args_schema: dict           # JSON schema
    preconditions: Callable     # e.g. "character_not_busy"
    invoke: Callable            # the API call(s)
```

Initial tools:
- `sit(object_id, point_id?)`
- `stand_up()`
- `open_door(placed_object_id)`
- `close_door(placed_object_id)`
- `climb_up(target)`
- `climb_down(target)`
- `ladder_start(rail_x, rail_z, top_y, bottom_y)`
- `ladder_input(vertical)`
- `push(target_id?, force, reach)`
- `say(text)`, `face(target)`, `walk_to(x, y, z)`

### 2. Observation primitives

The agent can't act blind. Add read-only "perception" tools:
- `character_state()` — pos, facing, busy flag, current animation
- `nearby_objects(radius)` — placed objects within range
- `nearby_npcs(radius)`
- `path_to(x, y, z)` — does a navigable path exist?
- `time_of_day()` — already in `WorldClock`
- `inventory()`, `relationships(npc_id)` — pull from existing RPG systems

### 3. Plan Executor

Same loop as `for scene in SCENES`, but `SCENES` is a queue the LLM
appends to. Single-character, single-threaded; one in-flight tool at
a time per character. Roughly:

```python
while not goal_met:
    obs   = collect_observations()
    plan  = llm.plan(goal, obs, history)         # streamed tool calls
    for step in plan.steps:
        result = tools[step.name].invoke(**step.args)
        history.append((step, result))
        if step.is_terminal: break
```

### 4. Memory / Journal

Reuse what already exists:
- `gameEventLog` (engine-side) → tail into the agent's context.
- `StoryEngine` for persistent arcs.
- SQLite world DB for facts ("door X was last opened by Y at time Z").
- (Future) vector store for episodic memory.

### 5. Multi-character & scheduling

- Each character runs its own Plan Executor.
- Shared world state means agents observe each other's moves through
  the same `gameEventLog`.
- Scheduling via `WorldClock` ticks — agents may run at lower frequency
  than render frames.

### 6. Safety rails

- All tool args are JSON-schema validated before hitting the engine.
- Preconditions reject impossible actions (sit while sitting, open
  locked door without key) and return a structured error the LLM can
  reason about.
- A budget cap (max tool calls per LLM round) prevents runaway loops.
- Sandbox: in `--project` mode the agent should NOT be able to write
  `save_world` unless explicitly enabled.

### 7. Optional: visual planning

For debugging, replay tool-call traces as `Scene` sequences using the
existing `demo_living_scene.py` pattern — produces screenshots of
exactly what the agent decided.

## Open questions

- **Idle behaviour**: between LLM rounds, what does the character do?
  Probably a small handcrafted "idle/loiter/look_around" policy that
  ticks for free while the LLM is thinking.
- **Latency**: cloud LLMs are slow; we'll need a local fallback (e.g.
  Ollama / Goose already integrated) for moment-to-moment reactions.
- **Multi-character coordination**: do agents share a planner, or each
  have their own with negotiation through dialogue?
- **Hot-reload**: can tools be added without restarting the engine?
  (Probably yes — they're Python.)

## Loose ends from this session (worth tackling first)

- `try_push` reach probe didn't see the dynamic cube spawned 1.1 m
  forward — needs investigation. Reproduces in `demo_living_scene.py`
  scene E.
- Door animations exist as character clips (`open_door_in`,
  `open_door_out` in `humanoid.anim`) but are **not** triggered by the
  `/api/door/open` route — the door swings while the character stays
  Idle. Either:
  - call `playAnimation("open_door_in")` from inside
    `DoorManager::open` (simple, but couples door geometry to character
    animation), **or**
  - add a new `/api/interaction/use_door` route that plays the clip on
    the character AND drives the DoorManager swing in sync (cleaner).
  - Recommended path: option (b), with a 0.2 s grab pose before the
    door begins swinging.

## Already-built scaffolding to lean on

- `tools/interaction_pipeline/` — the calibration matrix system; its
  `EngineSession`, `Mode`, scene model, and report helpers are perfect
  for the agent runtime.
- `tools/demo_living_scene.py` — first multi-scene driver, template
  for `Tool` shape.
- `tools/smoke_door_matrix.py` — example of API-only smoke pattern.
- `scripts/mcp/phyxel_mcp_server.py` — already exposes the engine
  HTTP API to Claude/MCP-aware agents. The Tool registry can either
  layer on top of this or duplicate the subset of routes we care
  about.
- `engine/.../StoryEngine`, `gameEventLog`, `WorldClock` — persistence
  + scheduling primitives already present.
