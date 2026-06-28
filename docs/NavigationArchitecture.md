# Navigation Architecture (Design)

> Status: **Layer-1 foundation implemented on main.** `Core::NavGraph`
> (`engine/include/core/NavGraph.h`) — the voxel-native TRUE-3D walkable-surface graph
> (every standable level per XZ column, built alongside the legacy `NavGrid`) — and
> `Core::PathService` (`engine/include/core/PathService.h`) — async worker-thread A*
> query owner — are on main. The legacy flat 2.5D `Core::NavGrid` + `Core::AStarPathfinder`
> still drive `PatrolBehavior` during migration. Deferred / **TODO**: Layer-2 HPA*
> hierarchical routing, path caching + voxel-change dirtying, smoothing, ORCA/RVO local
> avoidance, and full NPC-behavior integration. The sections below remain the
> authoritative design rationale.

## Goals
- **True 3D navigation** — multi-floor building interiors, bridges over paths,
  towers, tunnels, overhangs. Multiple walkable surfaces may share one XZ column.
- **World-aware routing** — NPCs reason about *which locations connect* and pick
  routes by more than raw distance (danger, faction territory, roads, time of day,
  temporary blockages).
- **Destructible-voxel friendly** — terrain changes constantly; nav data must
  update incrementally and cheaply, not via global rebuilds.
- **Scales** — bounded towns now, but the layering must let large/streamed worlds
  slot in without a rewrite.
- **Many NPCs without stutter** — pathfinding off the main thread, cached, prioritized.

## Non-goals (for the first iterations)
- Flying/swimming agents (design leaves room; not built first).
- *Large-scale* crowd simulation / flow fields (a later complement). Per-agent
  **local avoidance (ORCA/RVO) is in scope** — see PathService.
- Perfectly optimal paths — "good and cheap and reactive" beats "optimal and slow."

## Current state (what we build on / replace)
`Core::NavGrid` (`engine/include/core/NavGrid.h`):
- **2.5D**: one `NavCell` per XZ column (`surfaceY`, `walkable`, `nearWall`). **One
  surface per column** — the core limitation for 3D.
- Sparse hashmap storage (multi-chunk capable), incremental `rebuildCell`/`rebuildRegion`,
  `NavLink` jump/climb links, path-intersection invalidation.
- `Core::AStarPathfinder` runs A* over cells + links. `PatrolBehavior` consumes it;
  `StoryDrivenBehavior` currently does *direct-line* movement (no pathfinding).

Verdict: the surface-detection, headroom, link, incremental-update, and invalidation
machinery is sound and reusable. The **data model** (one cell per column, flat graph,
synchronous) is what must change.

## Design stance: state-of-the-art *techniques*, voxel-native representation

Shipping AAA engines (Unreal, Unity) standardize on **Recast/Detour navmesh + Detour
crowd avoidance + tiled dynamic regeneration + hierarchical pathfinding + nav-area
costs**. That is the quality bar. But a triangle navmesh assumes geometry that is
stable or changes rarely/locally — the opposite of this engine, whose defining feature
is **pervasive voxel destruction**. Re-extracting a mesh and rebuilding navmesh tiles on
every break is the classic friction, and the reason voxel/destructible games don't ship
classic navmesh.

So this design matches the SOTA *quality* by adopting the SOTA *techniques* on a
**voxel-native** representation that updates incrementally under destruction:

| SOTA technique (Recast/Detour world) | Where it lives here |
|---|---|
| Navmesh + off-mesh / smart links | Layer 1 surface graph + step/jump/climb/fall links |
| Hierarchical pathfinding | Layer 2 HPA* portal graph |
| Nav areas + cost modifiers | Layer 3 weighted, dynamic route edges |
| Detour Crowd (ORCA/RVO) avoidance | Local-avoidance layer (PathService, below) |
| Tiled dynamic regeneration | Chunk-tiled incremental rebuild — *cheaper* here (rebuild a voxel column vs re-mesh a tile) |
| Path string-pulling / smoothing | PathService post-process |

Net: we get for free the thing AAA engines spend the most effort on under destruction
(cheap, local updates) by being voxel-native, while meeting their navigation *quality*
(true 3D, hierarchy, crowd avoidance, cost-aware routing). This is the better-engineered
choice for a destructible voxel engine, not a compromise. Recast/Detour remains the
recommended primary **only if** destruction in NPC-traversed areas turns out rare and
localized (see Alternatives).

## Architecture: three layers + an async service

```
 Layer 3  Location/Route graph     "go from the shop to the tavern, avoiding the swamp"
            (nodes = named locations; weighted, dynamic edges; cached routes)
                | refines each leg via
 Layer 2  Abstract portal graph (HPA*)   "which regions/portals to traverse"
            (region partition; border portals; precomputed intra-region cost)
                | refines each leg via
 Layer 1  3D walkable-surface graph       "the exact footsteps"
            (multi-level nav nodes per column + inter-level links; chunk-tiled)

 (cross-cutting) PathService — async request queue + worker, caching, priority,
                 dynamic-update dirtying. Mirrors TTSService's threading model.
```

Build order: **Layer 1 (3D) + Layer 3 + async service** first (bounded worlds);
**Layer 2 (HPA*)** when world scale demands it. The interfaces below reserve the
Layer-2 seam so it slots in later.

---

### Layer 1 — 3D walkable-surface graph (replaces the flat grid)

Voxel-native navmesh: instead of one cell per XZ column, store **a list of walkable
surfaces per column**. A surface is a standable spot (solid voxel below + enough
headroom above for the agent). A bridge and the ground beneath it are two surfaces
at the same XZ; floor 1 and floor 2 of a building are two surfaces.

```cpp
struct NavSurface {            // one walkable level within a column
    int   x, z;                // world XZ
    int   floorY;              // Y the agent stands ON (top of the solid voxel)
    uint8_t headroom;          // empty voxels above (clamped); gates agent height
    bool  nearEdge;            // adjacent to a drop / non-walkable (steering hint)
    // region/cluster id for Layer 2 added later
};
struct NavNodeId { int x, z; int16_t level; };  // level = index into the column's surfaces
```

Edges (neighbors) between surface nodes:
- **Step** — adjacent XZ, |ΔfloorY| ≤ stepHeight (stairs/ramps as small steps).
- **Jump** — horizontal/vertical gap within the agent's jump envelope (reuse `NavLink`).
- **Climb** — ladders/climb volumes (tagged voxels → vertical links).
- **Fall** — safe drops (down only, within fall-damage budget).

Why this over Recast/Detour: it's **voxel-native** (no mesh extraction), updates
**incrementally** on destruction (rebuild only the changed columns + their links —
exactly the existing `rebuildRegion` pattern, extended to multi-level), and is simple
to reason about on a grid world. Recast remains the fallback if free-form geometry or
mature crowd avoidance is ever needed (see Alternatives).

**Agent capability profile** — paths depend on the agent, so queries take a profile:
```cpp
struct NavAgentProfile { int height = 2; int stepHeight = 1; int jumpHeight = 1;
                         int maxFallY = 4; bool canClimb = true; };
```
(height gates headroom; capabilities gate which jump/climb/fall edges are usable).

**Chunk-tiling** — each chunk owns the surfaces/links for its columns, built when the
chunk loads and discarded when it unloads (fits `ChunkStreamingManager`). Cross-chunk
neighbor edges are resolved at borders.

**Incremental updates** — on a voxel change at (x,y,z): rebuild that column's surface
list + the 8 neighbor columns' border edges + nearby jump/climb links; mark affected
abstract portals (Layer 2) dirty; invalidate cached routes crossing the cell (Layer 3).
Hooks already exist: `NPCManager::onVoxelChanged` / `onRegionChanged`.

---

### Layer 2 — Abstract portal graph (HPA*) — *deferred until scale needs it*

Partition Layer 1 into regions (natural unit: one chunk, or a chunk's NxN sub-blocks
per floor). On region borders, group contiguous traversable surface nodes into
**portals**. Build an abstract graph: nodes = portals, edges = precomputed
intra-region traversal cost (including inter-floor links so portals can be vertical).

A long query becomes: A* over the abstract graph (tens–hundreds of nodes) → for each
chosen leg, a *bounded* Layer-1 A* inside one region. This caps per-query cost
independent of world size and makes streaming/dynamic updates local (only the edited
region's portals + touching abstract edges recompute).

The Layer-1 API is designed so a query can be "give me a path within this region to
this border portal," which is exactly what HPA* refinement needs — so adding Layer 2
later doesn't disturb Layer 1.

---

### Layer 3 — Location/route graph (the "world-aware" brain)

Nodes = `Core::LocationRegistry` locations (shop, home, tavern, gate…). Edges =
routes between locations, **computed once via Layer 2/1 and cached**, with a cost that
is **more than distance**:

```
edgeCost = baseTravelCost
         * dangerMultiplier(region)      // avoid the swamp / monster den
         * factionMultiplier(territory)  // avoid rival faction land
         + roadBonus / shortcutPenalty   // prefer roads; shortcuts cost stamina/risk
         + timeOfDayModifier             // city gates shut at night, etc.
         + blockedPenalty (∞ if cut)     // destruction severed the route
```

NPCs "choose paths between locations" by A* on this small graph; personality/role
tune the weights (a brave NPC discounts danger; a guard ignores faction penalties on
patrol). This is where higher-level reasoning lives and where the autonomy/schedule
work plugs in: the schedule says *which location*; Layer 3 decides *which route*;
Layers 2/1 produce the footsteps.

Dynamic: terrain edits that sever/restore a route flip `blockedPenalty` and force
re-route; time-of-day and world events adjust weights live.

---

### Cross-cutting — `PathService` (async)

A single owner of pathfinding, mirroring `TTSService`'s worker model:
- **Request queue** off the main thread; `requestPath(agentProfile, from, to, priority)`
  returns a handle; result delivered next frame(s). Nearby/visible NPCs get priority;
  far NPCs can wait or use cached/abstract-only paths.
- **Caching** — location-pair routes (Layer 3) and recent grid paths; reused across NPCs.
- **Dynamic dirtying** — voxel-change hooks invalidate the right cache entries and
  re-queue affected agents.
- **Smoothing** — string-pull grid paths so agents cut corners instead of stair-stepping
  along cell boundaries.
- **Local avoidance (ORCA/RVO)** — the Detour-Crowd equivalent: a reciprocal
  velocity-obstacle layer that adjusts each agent's *desired* velocity each frame so
  agents flow around one another and small dynamic obstacles between path nodes. Replaces
  the current ad-hoc NPC separation. Cheap, local, and independent of the path layers, so
  it works the same whether a path came from Layer 1, 2, or 3. (Large-scale crowds →
  flow fields later.)

Behaviors (`StoryDrivenBehavior`, `PatrolBehavior`) become thin consumers: request a
path to the target location, then each frame follow the waypoints through the
local-avoidance layer; re-request on invalidation.

---

## Alternatives considered
- **Recast/Detour** — true navmesh + Detour crowd avoidance; the most capable option.
  Rejected as the *primary* approach because: heavy external dependency, requires
  voxel→triangle-mesh extraction, and re-tiling on destructible voxels + streaming is
  significant ongoing work. Kept as a fallback if free-form (non-grid) geometry or
  mature crowd avoidance becomes a hard requirement.
- **Flow fields** — excellent for *many agents → one destination*; a future complement
  for crowds/fleeing, layered over Layer 1. Not a replacement for individual routing.
- **Keep flat 2.5D grid** — rejected: cannot represent multi-floor/bridges (the stated
  requirement) and doesn't scale.

## Phased implementation
1. **Quick win (pre-overhaul):** route `StoryDrivenBehavior` movement through the
   *existing* `AStarPathfinder` so routines stop walking off cliffs while the overhaul
   lands. (Throwaway-ish glue; optional.)
2. **Layer 1 — 3D surface graph:** multi-level `NavSurface` model + step/jump/climb/fall
   edges + agent profiles + chunk-tiled incremental build. Port `AStarPathfinder` onto it.
3. **PathService (async):** request queue + worker + caching + dynamic dirtying; behaviors
   become consumers.
4. **Local avoidance (ORCA/RVO):** reciprocal velocity-obstacle layer so agents flow
   around each other; replaces ad-hoc separation.
5. **Layer 3 — location/route graph:** cached weighted location→location routing with
   dynamic costs; wire schedule/personality into the weights.
6. **Layer 2 — HPA*:** region partition + portals + abstract A*, when world scale needs it.
7. **Optional later:** flow fields (large crowds), Recast (only if free-form 3D geometry demands).

## Open questions
- Region size for Layer 2 (per chunk vs sub-blocks per floor) — pick when building Layer 2.
- How are climb volumes / ladders / "road" tiles tagged — material flags, a nav-hint
  voxel type, or authored volumes? (Affects Layer 1 edge generation and Layer 3 roadBonus.)
- Memory budget for multi-level surfaces + caches on large worlds (drives chunk-tiling
  aggressiveness).

## Key integration points
`NavGrid`→new surface graph; `AStarPathfinder` (port query); `NPCManager`
(owns nav + the new `PathService`, already has the voxel-change hooks); `StoryDrivenBehavior`
/ `PatrolBehavior` (become PathService consumers); `LocationRegistry` (Layer 3 nodes);
`DayNightCycle` + faction/danger data (Layer 3 edge weights); `ChunkStreamingManager`
(tile build/discard).
