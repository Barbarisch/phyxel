# Engine Architecture & Complexity Audit

Goal: understand why adding features has gotten harder, and lay out an *incremental*
path to make components simpler and easier to plug in. Companion to
`EngineRobustnessAudit.md` — several robustness bugs are symptoms of the structural
issues below.

Guiding principle: **reduce coupling and establish single ownership of state.** Prefer
clearer ownership boundaries over more abstraction layers — "easy plugging" comes from
knowing where state lives and who mutates it, not from generic interfaces.

---

## Metrics (2026-05-25, engine + editor, excl. deprecated)

- Total: ~116k LOC.
- **God-files:** `editor/src/Application.cpp` **15,275** · `engine/src/scene/AnimatedVoxelCharacter.cpp` **4,244** · `engine/src/core/EngineAPIServer.cpp` **4,007**. Top 3 ≈ 20% of the codebase.
- `Application` is the coupling hub: **41** owned subsystem members, **78** includes in the .cpp.
- `EngineAPIServer.cpp`: **233** `queueAndWait` handlers + **272** route registrations in one file.
- **~30+ distinct `*Manager` classes**, wired together by hand.

---

## Structural problems

### A. God-files / mega-methods
`Application.cpp` (15k) mixes composition-root wiring + ~hundreds of MCP handlers + game
logic + per-frame update. `EngineAPIServer.cpp` (4k) holds all 233 action handlers.
`AnimatedVoxelCharacter::resolveKinematicMovement` is one ~700-line method with ~6
interacting state flags (caused the subtle fall-through). Effect: every feature edits a
hub file → merge friction, hard to test, hard to reason about.

### B. No single source of truth per entity
One logical "chair" exists simultaneously as: voxels baked in a chunk · a
`PlacedObjectManager` entry · (optionally) a `DynamicFurnitureManager` body · a
`KinematicVoxelManager` render object · a `VoxelRigidBody` · a world-DB row. They are
kept consistent by hand-written cross-calls — which were incomplete (→ chairs reappear,
robustness #2) and have no migration/versioning (→ stale worlds, #5). This is the
single highest-leverage problem: it generates a whole class of "X out of sync with Y"
bugs and makes every entity feature touch many systems.

### C. Manager web + raw-pointer wiring
30+ managers connected via `setX()` dependency injection and registration-by-raw-address
(e.g. `registerGrid(&m_occupancyGrid)` → the dangling-pointer footgun #3). No composition
root owns the graph; lifetimes and wiring order are implicit and fragile.

### D. No extension seam
Adding a feature today = (1) register a route + a `queueAndWait` handler in
`EngineAPIServer.cpp`, (2) add logic to one or more managers, (3) wire dependencies in
`Application.cpp`, (4) maybe add a panel. There is no module/registry boundary, so
"plugging in" always means editing the hub files.

---

## Phased decomplexification plan (incremental — keep shippable each step)

**Phase 0 — Guardrails (cheap, immediate).**
Stop the bleeding: agree that no new logic goes into `Application.cpp` /
`EngineAPIServer.cpp` directly. New MCP actions register through a per-domain module
(see Phase 2 seam). Add a CI/size check that flags growth of the god-files.

**Phase 1 — Pilot: single owner for placed objects + furniture.** (recommended first)
Make one component the authoritative owner of "objects in the world." Render
(`KinematicVoxelManager`), physics (`VoxelRigidBody`), and persistence (world DB) become
*derived views* that subscribe to it, not parallel stores. Fixes robustness #2 and #5
for this domain and proves the single-source-of-truth pattern on code we already
understand.

**Phase 2 — Define the extension seam + split the MCP layer.**
Introduce a `Module`/`Feature` interface that registers its MCP actions, its systems,
and its panels through one entry point. Move the 233 handlers out of
`EngineAPIServer.cpp` into per-domain handler files that self-register. This is the
concrete "plug a component in" mechanism — adding a feature becomes adding one module
file, not editing 3 hubs.

**Phase 3 — Decompose `Application.cpp`.**
Extract a thin composition root (owns + wires subsystems) from the game/editor logic and
per-frame loop. Pull editor panels and game controllers into their own units. Target:
no single file > ~1.5k lines.

**Phase 4 — Tame mega-methods + wiring/lifecycle.**
`resolveKinematicMovement` → explicit movement state machine / strategy objects so each
mode (ground, glide, jump, stair, climb) is isolated and testable. Replace
registration-by-address with stable handles/IDs; reduce `setX()` injection via the
composition root.

---

## Sequencing & risk

- **No big-bang rewrite.** Each phase ships independently and leaves the engine working.
- Phase 1 (pilot) is the proof of concept and also closes two open robustness bugs —
  best ROI and lowest risk to start.
- Success metric: LOC in the three god-files trends down; adding a feature touches one
  module file instead of three hubs; the "X out of sync with Y" bug class disappears in
  refactored domains.

## Decisions (2026-05-25)
- Entity state model: **full ECS** (north star), introduced incrementally one domain at
  a time. ECS core + world-object components already built & tested.
- Cadence: incremental, interleaved with feature work — no big-bang rewrite.
- Guiding philosophy: UNIX / KISS — small single-purpose modules, composed through
  narrow contracts; pluggability from clear ownership, not from a framework.

---

# Target architecture: the Feature-module seam

**North-star goal: make feature work hard to get wrong.** Adding a feature should touch
ONE module, not the hub files.

## Principles (UNIX / KISS, each tied to a real failure this session)
1. **One owner per piece of state; everyone else derives.** (The chair + stale-world bugs
   were sync failures — one "chair" lived in 5 stores kept consistent by hand.)
2. **One responsibility per module, small enough to hold in your head.** (The fall-through
   hid in a 700-line method with 6 interacting flags.)
3. **Features self-register; a thin core composes them.** (Adding a feature today edits
   `EngineAPIServer.cpp` + `Application.cpp` + several managers.)
4. **Talk through narrow contracts, not internals.** (30+ managers wired by raw `setX()`
   pointers → the dangling-grid footgun.)
5. **Fail loud at boundaries; make illegal states unrepresentable.**

## Litmus test (definition of "less error-prone")
> To add feature X: create one module + one registration line, and touch nothing in
> `Application.cpp`, `EngineAPIServer.cpp`, or the other managers. Coordination happens
> via events/ECS, so you cannot *forget a cross-wire*.

## The seam — four small pieces

**1. `Feature` — the pluggable unit.** Owns its ECS components + systems + MCP commands +
optional panels (e.g. `FurnitureFeature`, `DoorFeature`, `DialogueFeature`).
```cpp
class Feature {
public:
  virtual ~Feature() = default;
  virtual const char* name() const = 0;
  virtual void init(EngineContext&)            {}  // register systems, commands, event subs
  virtual void update(EngineContext&, float dt){}
  virtual void shutdown(EngineContext&)        {}
};
```

**2. `EngineContext` — the narrow contract** (replaces the `setX()` pointer web). One handle
passed to every feature, exposing the shared substrate + the seams. Features depend on
the context, never on each other directly.
```cpp
struct EngineContext {
  Ecs::Registry&               ecs;       // shared state
  CommandRegistry&             commands;  // MCP/HTTP seam
  EventBus&                    events;    // decoupled coordination
  // shared substrate (the stable core — NOT features):
  ChunkManager&                chunks;
  Physics::PhysicsWorld&       physics;
  Graphics::RenderCoordinator& render;
  Core::AudioSystem&           audio;
  // ...
};
```

**3. `CommandRegistry` — the MCP seam** (replaces 233 hand-written handlers in
`EngineAPIServer.cpp`). `EngineAPIServer` becomes a thin dispatcher: name → handler,
marshaled via `queueAndWait`. A new MCP tool is a registration line in your feature.
```cpp
ctx.commands.add("activate_furniture", [](const json& p) -> json { /* ... */ });
```

**4. `EventBus` — decoupled coordination** (retires the chair-bug class). The chair bug was
a *missing direct call* between two managers. With events the furniture feature reacts to
removal without `PlacedObject` knowing it exists — you can't forget the wire.
```cpp
ctx.events.publish(ObjectRemoved{ id });                       // placed-object feature
ctx.events.subscribe<ObjectRemoved>([](auto& e){ /*teardown*/ }); // furniture feature
```

**The core (`FeatureHost`) — thin.** Owns `vector<unique_ptr<Feature>>`, the ECS registry,
command registry, event bus, and the shared substrate. Lifecycle: init all → loop
{ input; ECS/feature updates; render } → shutdown all. Absorbs `Application`'s
wiring/dispatch role; the editor's own bits become features too.

**Registration — explicit, no magic (KISS).**
```cpp
host.add<WorldObjectFeature>();
host.add<DoorFeature>();
// adding a feature = one line + the module file
```
(No static-init self-registration yet — an explicit list is easier to reason about. Add
auto-registration only if the list ever becomes a burden.)

## Before / after — adding "activate furniture"
- **Before:** edit `EngineAPIServer.cpp` (route + handler), edit `Application.cpp` (member +
  include + `setX` wiring), add/modify a manager, hand-wire cross-manager notifications
  (miss one → the chair bug). ~3 hub files, easy to break.
- **After:** add `features/furniture/FurnitureFeature.{h,cpp}`; in `init()` register its
  systems, its `activate_furniture` command, and an `ObjectRemoved` subscription; add one
  line to the feature list. **1 module + 1 line.**

## How the seam dissolves the four problems
- **God-files** → logic moves into features; core + `EngineAPIServer` become thin.
- **No single source of truth** → state lives in ECS components owned by one feature.
- **Manager web / raw pointers** → `EngineContext` + events.
- **No extension seam** → the `Feature` interface *is* the seam.

## Migration (incremental, KISS)
The `FeatureHost` hosts the legacy `Application` during transition — no rewrite at once.
Convert one domain at a time; `Application` thins as each manager becomes a feature.
**First module: placed-object / furniture** (single-owner ECS + self-registered commands +
`ObjectRemoved` event) — exercises all four seam pieces and retires the chair-bug class.

## Keep-it-simple guardrails
- Don't build `EventBus`/`CommandRegistry` as generic frameworks — a typed map + a callback
  list is enough until ~3 real features exist.
- The hot loop, GPU, and chunk store stay a **shared substrate**, not features — don't
  decompose the performance path chasing purity.
- Prefer compile-time wiring (the explicit list) over runtime magic.
