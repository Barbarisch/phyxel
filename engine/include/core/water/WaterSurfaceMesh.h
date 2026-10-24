#pragma once

#include "core/water/WaterCore.h"

#include <cstdint>
#include <glm/glm.hpp>
#include "core/water/WaterLook.h"
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

// WaterCore Phase F (docs/WaterCore.md 17): the simulated water's own surface, at the volume's own
// resolution, every frame.
//
//   grid cells  --extractSurfaceField-->  WaterSurfaceField  --buildWaterSurfaceMesh-->  mesh
//
// The FIELD is one record per sub-column (the volume's own (x, z) lattice): up to kMaxRuns runs of
// water [bottomY, topY) in world units, bottom-up, plus the top face of the highest solid in the
// column (what a neighbour's lateral face stops at). The CPU extracts it from the grid; the GPU
// kernel wc_surface.comp writes the same layout into a buffer that is read back each step - never
// the whole grid. The MESH is a pure function of the field: a top quad per run with corner heights
// averaged over the neighbouring sub-columns that hold an overlapping run (C0-continuous inside the
// volume), and a lateral face wherever a run meets a neighbour that holds no overlapping run and is
// not solid up to the run's top (a step, an overhang, the lip of a fall). Beside a solid the top edge
// ends at the solid: the voxel is the wall.
namespace Phyxel::Core::Water {

constexpr int kSurfaceMaxRuns = 4;
/// 19.7: a part-full cell UNDER water is an air pocket the projection keeps (a liquid cell is projected as
/// full, so nothing closes it) - the plain height function (19.5) drew each one as a 16 cm dip, with the
/// displaced water piled +3.5 cm elsewhere. A run's surface = its bottom + the water in it + each cell's
/// missing water x pocketWeight(fill of the cell directly ABOVE it): 0 up to kSurfacePocketA0 (a film or a
/// thin sheet over a part-full cell - that cell IS the surface, or a sheet climbing a wall), 1 from
/// kSurfacePocketA1 (a liquid cell sits on it - it is a pocket), linear between, so the surface stays
/// continuous in time (a hard 'highest cell >= 0.5' rule jumped 23 cm; weighting by ALL the water above
/// drew wall sheets as +12 cm bumps). Same rule in wc_surface.comp and WaterGrid::surfaceWorldY.
constexpr float kSurfacePocketA0 = 0.2f, kSurfacePocketA1 = 0.7f;
inline float surfacePocketWeight(float fillAbove) {
    const float t = (fillAbove - kSurfacePocketA0) / (kSurfacePocketA1 - kSurfacePocketA0);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

/// One sub-column of the field. Matches wc_surface.comp's layout exactly (16 floats = 64 B).
/// Phase G2: foam (0..1) and the surface velocity (m/s) ride along - the band fills both from its
/// solver; the 3-D volumes export their top cell's horizontal velocity (E2, WaterCore.md 19) and
/// foam 0.
struct SurfaceColumn {
    float top[kSurfaceMaxRuns] = {0, 0, 0, 0};      ///< world Y of each run's surface (bottom-up)
    float bottom[kSurfaceMaxRuns] = {0, 0, 0, 0};   ///< world Y of each run's base
    float solidTopY = -1e30f;                        ///< top face of the highest solid cell in the column (-1e30 = none)
    float runs = 0.0f;                               ///< run count (float for the GPU layout)
    float foam = 0.0f;                               ///< 0..1 whitewater on the top run (G2)
    float u = 0.0f, w = 0.0f;                        ///< surface velocity, x and z (m/s) (G2)
    float bodyAtSurface = 0.0f;                      ///< 20: a moving solid's fraction at the top run's top cell or the cell above it (max) -
                                                     ///< the level here is the body's, not the water's: a floater must not read it (columnWater skips it)
    float pad1 = 0.0f, pad2 = 0.0f;
};
static_assert(sizeof(SurfaceColumn) == 64, "wc_surface.comp writes 16 floats per column");

struct WaterSurfaceField {
    glm::ivec3 origin{0};     ///< grid origin in cells (world = origin * h)
    int   nx = 0, nz = 0;
    float h = 1.0f;
    std::vector<SurfaceColumn> cols;   ///< nx * nz, index x + nx * z
    WaterLookPacked look;              ///< G3: the body's look, resolved at the field's centre by its producer
    int rippleLayer = -1;              ///< 22: the ripple layer drawn on this field (an index the renderer's atlas uses); -1 = none
    int source = 0;                    ///< 26.30: who made it (a volume's id; 0 = unknown) - the tiler's key for its tiles
    uint64_t version = 0;              ///< 26.30: the producer's step counter; same source + version = same columns (0 = unknown: compared)
    bool gpuDrawn = false;             ///< 29 S1: the device draws this field (WaterCoreManager::gpuSurfaceDraws) - the host's tiler skips it
    const SurfaceColumn& at(int x, int z) const { return cols[static_cast<size_t>(x) + static_cast<size_t>(nx) * z]; }
};

/// CPU extraction from the grid (runs of layers with f * h >= 1 mm; the top of a run is its highest
/// wet cell's bottom + fill * h; solidTopY from the grid's occupancy).
void extractSurfaceField(const WaterGrid& grid, WaterSurfaceField& out);

/// §27 W1a: the pond surface of RECORDED water - the record's runs woken into `scratch` (the level-fill a wake makes, 27.9) and
/// the surface extracted from it, so a recorded tile draws what the same water drew awake at rest. `scratch` must hold the
/// world's occupancy over the tile; its fills and velocities are overwritten. Returns the m^3 placed.
double recordSurfaceField(WaterGrid& scratch, const std::vector<ColumnRuns>& runs, WaterSurfaceField& out);

/// 32 B, the vertex layout of water_surface.vert.
struct WaterSurfaceVertex {
    glm::vec3 pos;       ///< world
    float     depth;     ///< the run's thickness (m), the shader's thickness floor
    glm::vec3 normal;    ///< world
    float     side;      ///< 0 = top face, 1 = lateral face
    float     foam;      ///< 0..1 whitewater (G2)
    glm::vec2 flow;      ///< surface velocity, x and z (m/s) (G2)
    float     pad = 0.0f;
};
static_assert(sizeof(WaterSurfaceVertex) == 48, "water_surface.vert expects 48-byte vertices");

/// G3: one draw per field, so each body's look reaches the shader in the push block.
struct WaterSurfaceRange { uint32_t firstIndex = 0, indexCount = 0; WaterLookPacked look; };

struct WaterSurfaceMesh {
    std::vector<WaterSurfaceVertex> vertices;
    std::vector<uint32_t> indices;
    long topQuads = 0, sideQuads = 0;
    std::vector<WaterSurfaceRange> ranges;   ///< G3: one per appended field (its look)
    void clear() { vertices.clear(); indices.clear(); ranges.clear(); topQuads = sideQuads = 0; }
};

/// Pure. Appends the volume's mesh to `out`.
void buildWaterSurfaceMesh(const WaterSurfaceField& field, WaterSurfaceMesh& out);
/// 26.30: the quads of the columns [x0, x1) x [z0, z1) only (field-local), exactly as the whole mesh has them - corners and side
/// faces still read the neighbours outside the rect.
void buildWaterSurfaceMeshRect(const WaterSurfaceField& field, int x0, int z0, int x1, int z1, WaterSurfaceMesh& out);

/// 26.30: the surface in TILES - world-aligned squares of kSurfaceTileCols x kSurfaceTileCols columns of a field's lattice (4 m at
/// 1/9 m). A COST structure: a tile's quads are the whole mesh's quads for its columns, nothing else (WaterSurfaceTileTest). A tile
/// is rebuilt only when what it reads changed: its columns plus a one-column ring (corners and side faces read the neighbours),
/// the field's lattice, look and ripple layer. A field whose source and version are unchanged is skipped without reading it.
constexpr int kSurfaceTileCols = 36;
struct SurfaceTileKey {
    int source = 0, tx = 0, tz = 0;   ///< the field's source (or -(1000 + its index) when it has none); the tile, in world tiles
    bool operator<(const SurfaceTileKey& o) const { return source != o.source ? source < o.source : (tx != o.tx ? tx < o.tx : tz < o.tz); }
    bool operator==(const SurfaceTileKey& o) const { return source == o.source && tx == o.tx && tz == o.tz; }
};
struct SurfaceTile {
    WaterSurfaceMesh mesh;                 ///< its own vertices from 0 (one range, the field's look)
    glm::vec3 lo{0.0f}, hi{0.0f};          ///< world bounds of its vertices (empty mesh: lo > hi)
    uint64_t generation = 0;               ///< bumped on every rebuild - the renderer re-uploads when it changes
    // what it was built from
    glm::ivec3 origin{0}; int nx = 0, nz = 0; float h = 0.0f; int rippleLayer = -1; WaterLookPacked look;
    int rx0 = 0, rz0 = 0, rx1 = 0, rz1 = 0;   ///< the snapshot rect (its columns + the ring, clipped to the field)
    std::vector<SurfaceColumn> input;
    bool seen = false;
};
class WaterSurfaceTiler {
public:
    struct Stats {
        long tiles = 0, rebuilt = 0, kept = 0, removed = 0, fieldsSkipped = 0; double ms = 0.0;
        double buildMs = 0.0; int threads = 1;   ///< 26.30 step 3: the rebuilds' share of ms (wall), and the threads they ran on
        // 26.30 step 3 (measurement): how much a rebuilt tile's columns moved since its snapshot - the largest change of
        // any height (top/bottom/solid, m), of the flow (u/w, m/s), of foam and the body fraction; tiles whose run COUNT
        // changed; and the rebuilt tiles binned by their largest height change: < 1e-5, < 1e-4, < 1e-3, < 1e-2, larger (m)
        float maxDHeight = 0.0f, maxDFlow = 0.0f, maxDFoam = 0.0f, maxDBody = 0.0f;
        long runsChanged = 0;
        long keptWithin = 0;   ///< kept although not bit-equal: only flow/foam moved, within the tolerances
        long heightBins[5] = {0, 0, 0, 0, 0};
    };
    /// Brings the tiles up to date with this frame's fields. Pure CPU; no GPU state.
    void update(const std::vector<const WaterSurfaceField*>& fields);
    void update(const std::vector<WaterSurfaceField>& fields) { std::vector<const WaterSurfaceField*> p; for (const auto& f : fields) p.push_back(&f); update(p); }
    const std::map<SurfaceTileKey, SurfaceTile>& tiles() const { return m_tiles; }
    const Stats& stats() const { return m_stats; }
    void clear() { m_tiles.clear(); m_fieldVersion.clear(); m_stats = Stats{}; }
    /// 26.30 step 3: a tile whose heights are bit-equal is kept while its surface flow (m/s) and foam (0..1) moved no more than
    /// these since it was built (0 = exact). 1 mm/s and 0.001 of foam are far below what the shading can show.
    void setTolerances(float flow, float foam) { m_flowTolerance = std::max(0.0f, flow); m_foamTolerance = std::max(0.0f, foam); }
    float flowTolerance() const { return m_flowTolerance; }
    float foamTolerance() const { return m_foamTolerance; }
    static constexpr float kDefaultFlowTolerance = 1e-3f, kDefaultFoamTolerance = 1e-3f;
    /// 26.30 step 3: the rebuilds of one update are shared over this many threads (1 = the calling thread alone, the control;
    /// 0 = the default: the hardware's threads less two, at most 8). Each tile is built from the field alone, so the result is
    /// the same for any count (WaterSurfaceTileTest pins it).
    void setThreads(int n);
    /// 26.30 step 3: fill the stats' movement figures (maxD*, runsChanged, heightBins) - a full scan of every changed tile; off,
    /// the keep test stops at the first column that rebuilds the tile
    void setMeasure(bool on) { m_measure = on; }
    bool measure() const { return m_measure; }
    int threads() const { return m_threads; }
    WaterSurfaceTiler();
    ~WaterSurfaceTiler();
    WaterSurfaceTiler(const WaterSurfaceTiler&) = delete;
    WaterSurfaceTiler& operator=(const WaterSurfaceTiler&) = delete;
private:
    float m_flowTolerance = kDefaultFlowTolerance, m_foamTolerance = kDefaultFoamTolerance;
    int m_threads = 1;
    bool m_measure = false;
    struct Pool;
    std::unique_ptr<Pool> m_pool;
    std::map<SurfaceTileKey, SurfaceTile> m_tiles;
    struct FieldMark { uint64_t version = 0; glm::ivec3 origin{0}; int nx = 0, nz = 0; float h = 0.0f; int rippleLayer = -1; WaterLookPacked look; };
    std::map<int, FieldMark> m_fieldVersion;
    Stats m_stats;
};
/// G3: appends the field's mesh AND a draw range carrying the field's look (empty fields add no range).
void appendFieldToMesh(const WaterSurfaceField& field, WaterSurfaceMesh& out);
/// 21: the droplet crown as cubes (xyz centre, w edge) - six outward quads each, side = 2 (the shader's droplet
/// flag: spray, its thickness its own edge), one draw range with the derived look.
void appendDropletsToMesh(const std::vector<glm::vec4>& droplets, WaterSurfaceMesh& out);

/// Phase F render mode of the simulated water (docs/WaterCore.md 17.2 key 4).
enum class WaterCoreRenderMode : int { Off = 0, Cells = 1, Mesh = 2 };
constexpr WaterCoreRenderMode kWaterCoreRenderModeDefault = WaterCoreRenderMode::Mesh;
inline const char* waterCoreRenderModeName(WaterCoreRenderMode m) { return m == WaterCoreRenderMode::Mesh ? "mesh" : (m == WaterCoreRenderMode::Cells ? "cells" : "off"); }

}  // namespace Phyxel::Core::Water
