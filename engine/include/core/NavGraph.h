#pragma once
#include "core/AgentProfile.h"

#include "core/NavGrid.h"   // for Core::VoxelQueryFunc (shared during NavGrid -> NavGraph migration)
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <climits>
#include <functional>
#include <shared_mutex>

namespace Phyxel {

class ChunkManager;

namespace Core {

// ============================================================================
// NavGraph — Layer 1 of the navigation architecture (docs/NavigationArchitecture.md).
//
// A voxel-native, TRUE-3D walkable-surface graph: unlike the flat 2.5D NavGrid
// (one surface per XZ column), it keeps EVERY standable level in a column — so a
// bridge and the ground beneath it, or floor 1 and floor 2 of a building, are
// distinct nodes at the same XZ. Built alongside NavGrid during migration.
//
// SUB-CUBE AWARE (2026-09-08, Ravenmere G-54): generated buildings are built from
// subcubes and microcubes — a framed door is two 1-micro jambs + a 2-micro lintel
// inside otherwise-open cubes, with a 7-micro (0.78 m) clear reveal. The original
// column model classified a cube as solid whenever it was not EMPTY, which made
// every framed door in every generated building a wall to NPC navigation
// (measured live: `navgraph_path` street -> tavern interior `found:false`).
// The graph now has two occupancy models:
//   * CUBE mode  — `VoxelQueryFunc` (tests / legacy): a cube is solid or air.
//   * MICRO mode — `CellFillFunc` + `MicroQueryFunc` (the ChunkManager-backed
//     runtime): each cube is Empty / Solid / Partial, and Partial cubes are sampled
//     at 1/9 m against the AGENT'S footprint (centre + 4 corners, the TraversalProbe
//     model), so a door reveal wide and tall enough for the character box is
//     passable and a 6-micro wall band is not. Standing heights and headroom are
//     kept in micro, so the grounded 1.78 m door clears the 1.75 m agent.
//     Edges between adjacent cells are swept (centre to centre, 1-micro steps)
//     at build time — no world reads happen on the PathService worker thread.
// ============================================================================

/// Coarse classification of one cube for the micro model's fast path.
enum class CellFill : uint8_t { Empty, Solid, Partial };
using CellFillFunc  = std::function<CellFill(const glm::ivec3& cube)>;
/// True if the 1/9-m cell at world MICRO coordinates (cube * 9 + 0..8) is solid.
using MicroQueryFunc = std::function<bool(const glm::ivec3& micro)>;

/// One walkable surface within an XZ column. Multiple may stack per column.
struct NavSurface {
    int      x = 0;
    int      z = 0;
    int      floorY = -1;     ///< Y of the cube holding the top solid the agent stands ON.
    uint16_t headroom = 0;    ///< empty cubes above the floor (clamped to MAX_HEADROOM).
    bool     nearEdge = false;///< adjacent to a drop / non-walkable column (steering hint).
    /// Feet height in MICRO (1/9 m): the first free micro layer above the floor's top solid.
    /// Cube mode: (floorY+1)*9. Micro mode: exact (a 1/3 floor slab gives cube*9+3).
    int      floorTopMicro = INT_MIN;
    uint16_t headroomMicro = 0;   ///< free micro layers above the feet (clamped).
    /// Micro mode only: the neighbour level reachable in each 4-direction
    /// (+x, -x, +z, -z), or -1. Computed by the build-time edge sweep.
    int8_t   edge[4] = { -1, -1, -1, -1 };
    /// Micro mode only: lateral slack of each edge — the largest offset o (0..4 micro)
    /// such that the centre-to-centre sweep still fits at every lateral offset in
    /// [-o, +o]. 4 = the whole 9-micro face is passable (open ground); 0..3 = a tight
    /// crossing (a door reveal) that the mover must take through the cell centre.
    /// Path smoothing only string-pulls across slack-4 crossings and open cells.
    int8_t   slack[4] = { -1, -1, -1, -1 };
    /// Every 4-direction crossing exists with full slack: a straight line may pass
    /// through this cell at any offset without clipping sub-cube geometry.
    bool openCell() const {
        return slack[0] == 4 && slack[1] == 4 && slack[2] == 4 && slack[3] == 4;
    }
    /// A cell some route must thread precisely: at least one EXISTING crossing was
    /// swept with less than full lateral slack. A missing edge (wall, region boundary)
    /// is not tightness - it just is not a way out.
    bool tightCell() const {
        for (int d = 0; d < 4; ++d) if (edge[d] >= 0 && slack[d] < 4) return true;
        return false;
    }
};

/// Identity of a surface node: its column plus the level index within that column.
struct NavNodeId {
    int x = 0;
    int z = 0;
    int level = -1;           ///< index into the column's surface list; < 0 = invalid.
    bool valid() const { return level >= 0; }
    bool operator==(const NavNodeId& o) const { return x == o.x && z == o.z && level == o.level; }
    bool operator!=(const NavNodeId& o) const { return !(*this == o); }
};

/// Movement capabilities — paths depend on the agent, so queries take a profile.
///
/// The micro fields are GROUNDED in the engine character (AnimatedVoxelCharacter):
/// half-width 0.25 m = 2 micro (`m_originalHalfWidth`), height ~1.75 m = 16 micro,
/// auto step-up 4/9 m = 4 micro (`m_maxStepHeight`) — the same AgentBox the
/// settlement walkability gate (TraversalProbe) uses, so "proven walkable by the
/// generator" and "pathable at runtime" are the same statement. The cube fields
/// remain for CUBE-mode graphs (legacy tests / the NavGrid migration).
struct NavAgentProfile {
    int  height     = 2;      ///< CUBE mode: empty voxels needed above the floor to stand/walk.
    int  stepHeight = 1;      ///< CUBE mode: max upward floorY difference traversable as a step.
    int  maxFallY   = 4;      ///< max downward drop traversable (down only), cubes.
    int  jumpHeight = 1;      ///< reserved for jump edges (later slice).
    bool canClimb   = true;   ///< reserved for climb edges (later slice).
    // --- MICRO mode (sub-cube occupancy) ---
    int  halfWidthMicro = 2;  ///< footprint half-extent, micro (<= 4: the footprint stays in its cell)
    int  heightMicro    = kAgentHeightMicro; ///< clearance the body needs above the feet, micro (AgentProfile.h)
    int  stepUpMicro    = 4;  ///< max auto step-up, micro
    int  maxFallMicro() const { return maxFallY * 9; }
};

class NavGraph {
public:
    static constexpr int MIN_SCAN_Y   = -64;  ///< lowest Y scanned per column.
    static constexpr int MAX_SCAN_Y   = 256;  ///< highest Y scanned per column.
    static constexpr int MAX_HEADROOM = 32;   ///< headroom is clamped to this (cubes).
    static constexpr int MICRO        = 9;    ///< micro cells per cube edge.

    /// Runtime graph over the live world: MICRO mode (cube type + sub-voxel occupancy).
    explicit NavGraph(ChunkManager* chunkManager);
    /// CUBE mode for tests / custom worlds: every cube is solid or air.
    explicit NavGraph(VoxelQueryFunc queryFunc);
    /// MICRO mode over an injected occupancy (tests, canvases, composite runtime samplers).
    NavGraph(CellFillFunc fill, MicroQueryFunc micro);

    bool microMode() const { return static_cast<bool>(m_microFunc); }

    /// Build all columns in an inclusive XZ region for the given agent.
    void buildRegion(const glm::ivec2& minXZ, const glm::ivec2& maxXZ, const NavAgentProfile& agent);

    /// Rebuild a single column after a voxel change (incremental update entry point).
    /// Micro mode also re-sweeps the edges of the column and its four neighbours.
    void rebuildColumn(int x, int z, const NavAgentProfile& agent);

    /// Surfaces in a column (bottom-to-top). Empty ref if the column has none / unbuilt.
    const std::vector<NavSurface>& columnSurfaces(int x, int z) const;

    /// Resolve a node id to its surface (nullptr if invalid / not present).
    const NavSurface* surface(const NavNodeId& id) const;

    /// Walkable neighbors reachable from a node for this agent (step up/down within limits).
    std::vector<NavNodeId> neighbors(const NavNodeId& id, const NavAgentProfile& agent) const;

    /// The surface an agent at worldPos is standing on (highest floor at/just below the
    /// agent's feet). Returns an invalid id if the column has no surface there.
    NavNodeId surfaceAt(const glm::vec3& worldPos) const;

    /// Result of a path query. `waypoints` are world positions at each node's standing
    /// point (cell center, feet height). `nodes` is the corresponding node path.
    struct PathResult {
        bool found = false;
        std::vector<glm::vec3> waypoints;
        std::vector<NavNodeId> nodes;
        int nodesExpanded = 0;
        /// Per-waypoint arrival radius (metres), parallel to `waypoints` when filled
        /// (PathService fills it after smoothing). A waypoint kept at a TIGHT crossing
        /// (its cell is not fully open - a door reveal, a fence/wall residual) gets
        /// kArriveTight; open ground gets kArriveLoose. Empty = every waypoint loose.
        std::vector<float> arriveRadius;
    };
    static constexpr float kArriveLoose = 0.5f;    ///< open ground: the mover's classic radius
    static constexpr float kArriveTight = 0.15f;   ///< a crossing the sweep proved only near the centre

    /// Arrival radius for one waypoint: tight when the cell under it is not fully open
    /// (Ravenmere G-79: movers advanced at 0.5 m through a crossing with 0.11 m of slack).
    float arrivalRadiusAt(const glm::vec3& worldPos) const;
    std::vector<float> arrivalRadii(const std::vector<glm::vec3>& waypoints) const;

    // Thread-safety: findPath() is safe to call from a worker thread (e.g. PathService)
    // concurrently with other findPath() calls — it takes a shared (read) lock. buildRegion()
    // and rebuildColumn() take an exclusive (write) lock, so a graph rebuild on the main
    // thread is correctly serialized against in-flight queries. The granular readers
    // (neighbors/surface/surfaceAt/columnSurfaces) are NOT internally locked: they are reached
    // either under findPath()'s lock or from single-threaded (main/test) code, so call them
    // off-thread only via findPath(). The occupancy callbacks are only invoked from the
    // build paths (main thread) — queries read the stored surfaces/edges only.

    /// A* between two surface nodes for the given agent (step-up/fall/headroom costs).
    PathResult findPath(const NavNodeId& start, const NavNodeId& goal, const NavAgentProfile& agent) const;

    /// A* between world positions (resolves the standing surface at each end first).
    PathResult findPath(const glm::vec3& from, const glm::vec3& to, const NavAgentProfile& agent) const;

    /// True if an agent can walk the straight XZ line from `a` to `b` staying on one
    /// level: every sampled column along the segment must have a walkable surface at the
    /// same floor (within a step) with enough headroom, and in micro mode every cell
    /// crossing must be a swept edge (so a shortcut never clips a sub-cube wall corner).
    /// Conservative — returns false across level changes, walls, or gaps. The primitive
    /// behind path smoothing. Thread-safe (shared lock).
    bool hasClearWalk(const glm::vec3& a, const glm::vec3& b, const NavAgentProfile& agent) const;

    /// String-pull a waypoint list: greedily drop intermediate points whenever the span
    /// between kept points passes hasClearWalk(), so flat 4-connected zig-zags collapse to
    /// straight diagonals. Endpoints and any point at a level change are preserved. Returns
    /// the simplified path (>= 2 points unless the input had fewer). Thread-safe.
    std::vector<glm::vec3> smoothWaypoints(const std::vector<glm::vec3>& raw, const NavAgentProfile& agent) const;

    size_t columnCount() const { return m_columns.size(); }
    size_t surfaceCount() const;

private:
    static int64_t packKey(int x, int z);
    static int64_t packCell(const glm::ivec3& c);
    bool hasVoxel(const glm::ivec3& p) const;

    // --- cube mode ---
    std::vector<NavSurface> buildColumnCube(int x, int z, const NavAgentProfile& agent) const;

    // --- micro mode ---
    /// Per-build memo of cube classifications (the world is only read on the build thread).
    struct FillCache {
        std::unordered_map<int64_t, CellFill> cells;
    };
    CellFill fillAt(const glm::ivec3& cube, FillCache& cache) const;
    /// Is the micro cell solid? Uses the cube fast path, samples micro only for Partial cubes.
    bool microSolid(const glm::ivec3& micro, FillCache& cache) const;
    /// The agent box with feet at micro (fx,fy,fz) overlaps no solid, for `heightMicro` layers.
    bool boxFits(int fx, int fy, int fz, int halfWidth, int heightMicro, FillCache& cache) const;
    /// Any solid directly under the footprint at feet height fy (i.e. at layer fy-1)?
    bool boxSupported(int fx, int fy, int fz, int halfWidth, FillCache& cache) const;
    std::vector<NavSurface> buildColumnMicro(int x, int z, const NavAgentProfile& agent, FillCache& cache) const;
    /// Sweep the agent from the centre of `from` to the centre of the neighbour `to`
    /// (1-micro steps along the shared axis), with the footprint shifted `lateral`
    /// micro perpendicular to the axis. True if the box fits at every step.
    bool sweepEdge(const NavSurface& from, const NavSurface& to, const NavAgentProfile& agent,
                   FillCache& cache, int lateral = 0) const;
    /// Recompute the outgoing edges of every surface in column (x,z).
    void linkColumn(int x, int z, const NavAgentProfile& agent, FillCache& cache);

    /// A* core that assumes the caller already holds m_mutex (shared). Both public
    /// findPath() overloads lock then delegate here, so the vec3 overload doesn't
    /// recursively re-lock when it resolves the endpoint surfaces.
    PathResult findPathCore(const NavNodeId& start, const NavNodeId& goal, const NavAgentProfile& agent) const;

    /// Non-locking line-of-walk test; caller must already hold m_mutex (shared).
    /// smoothWaypoints() and hasClearWalk() both delegate here under one lock.
    bool hasClearWalkCore(const glm::vec3& a, const glm::vec3& b, const NavAgentProfile& agent) const;

    ChunkManager*  m_chunkManager = nullptr;
    VoxelQueryFunc m_queryFunc;
    CellFillFunc   m_fillFunc;
    MicroQueryFunc m_microFunc;
    std::unordered_map<int64_t, std::vector<NavSurface>> m_columns;
    glm::ivec2 m_minBounds{0, 0};
    glm::ivec2 m_maxBounds{0, 0};
    mutable std::shared_mutex m_mutex;   ///< guards m_columns/bounds; see findPath() note above.
};

} // namespace Core
} // namespace Phyxel
