#pragma once
// ============================================================================
// M1 World Sheet — docs/WorldForgeV2Plan.md §3 (L0) / §5 M1 / "Design-check resolutions".
//
// Meso tiles of height cells (32 u) baked on demand from a macro height field + a per-cell
// detail term, then relaxed by a FIXED number of Jacobi (double-buffered) thermal-erosion +
// hillslope-diffusion steps inside the tile and its apron. Tiles bound COST only: every cell
// is a pure function of (seed, params, the absolute cell index, the macro field), and the
// apron is DERIVED from the step count so a cell's whole dependency cone lies inside the
// tile. MesoSeamTest pins "tile-by-tile == whole-region" bit-exactly (resolution 2).
// ============================================================================
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Phyxel {

constexpr int kMesoStencilRadius = 1;  // both Jacobi ops read the 4-neighbourhood
constexpr int kMesoBicubicReach  = 2;  // Catmull-Rom reads two cells past the sample's cell

struct SheetParams {
    float mesoCellU        = 32.0f;   // world units per meso cell (plan §5 M1)
    int   mesoTileCells    = 256;     // interior cells per tile edge (256 x 32 u = 8192 u)
    int   mesoSteps        = 4;       // Jacobi thermal + diffusion iterations per tile
    float diffusion        = 0.10f;   // hillslope diffusion per step; the explicit 4-neighbour stencil
                                      // is stable only for k < 0.25 (von Neumann), clamped there
    float talusSlope       = 0.70f;   // thermal threshold, rise/run = tan 35 deg = WorldGenerator's
                                      // kRockSlope (angle of repose of loose scree, TerrainGenerationV2 P0)
    float thermalRate      = 0.50f;   // fraction of the over-talus excess moved per step, (0, 0.5] so a
                                      // pair never overshoots (each side moves at most half the excess)
    /// Clamped copy, reasons at each clamp; a pager echoes the copy it runs with.
    static SheetParams clamped(SheetParams p);
    /// DERIVED, never set: steps x stencil radius so every interior cell's cone is inside the
    /// tile, + the bicubic reach so samples up to the interior edge read correct apron cells.
    int apronCells() const { return mesoSteps * kMesoStencilRadius + kMesoBicubicReach; }
    float tileU() const { return mesoTileCells * mesoCellU; }
};

/// A baked window of height cells with its apron. Cell (i, j) for i, j in [-apron, cells+apron)
/// sits at absolute cell index (cellX0 + i, cellZ0 + j); its CENTRE is at
/// ((cellX0 + i) + 0.5) x cellU in world units. Immutable once baked; shared by every reader.
class MesoTile {
public:
    MesoTile(int cellX0, int cellZ0, int cells, int apron, float cellU);
    int   cells()  const { return m_cells; }
    int   apron()  const { return m_apron; }
    int   padded() const { return m_cells + 2 * m_apron; }
    float cellU()  const { return m_cellU; }
    int   cellX0() const { return m_cellX0; }
    int   cellZ0() const { return m_cellZ0; }
    float at(int i, int j) const { return m_h[idx(i, j)]; }
    float& at(int i, int j)      { return m_h[idx(i, j)]; }
    std::vector<float>&       raw()       { return m_h; }
    const std::vector<float>& raw() const { return m_h; }
private:
    size_t idx(int i, int j) const {
        return static_cast<size_t>(j + m_apron) * static_cast<size_t>(padded()) +
               static_cast<size_t>(i + m_apron);
    }
    int m_cellX0, m_cellZ0, m_cells, m_apron;
    float m_cellU;
    std::vector<float> m_h;
};

/// Pure position-only inputs: the macro height (bicubic over the macro sheet, M1b) and the
/// per-cell detail term (ridged/warped noise at wavelength <= 512 u, supplied by the generator).
/// Both MUST be pure functions of world position: they are evaluated per cell at the cell centre.
using HeightFieldFn = std::function<float(float worldX, float worldZ)>;

class MesoBaker {
public:
    MesoBaker(uint32_t seed, SheetParams params, HeightFieldFn macroHeightAt,
              HeightFieldFn detailAt = nullptr);
    /// Tile (tx, tz): interior cells [tx*N, (tx+1)*N) x [tz*N, (tz+1)*N) plus the derived apron.
    std::shared_ptr<const MesoTile> bake(int tx, int tz) const;
    /// Any window in absolute cell coordinates: the seam test's whole-region reference
    /// (bakeCells(0, 0, 2N) covers tiles (0,0)..(1,1) in ONE bake).
    std::shared_ptr<const MesoTile> bakeCells(int cellX0, int cellZ0, int cells) const;
    const SheetParams& params() const { return m_params; }
    uint32_t seed() const { return m_seed; }
private:
    uint32_t m_seed;
    SheetParams m_params;
    HeightFieldFn m_macro, m_detail;
};

/// Catmull-Rom bicubic height at a world position from ONE tile. The sample's cell is found
/// from the ABSOLUTE cell coordinate (so the fractional offset is bit-identical whichever
/// tile serves it); reads up to kMesoBicubicReach cells past the interior into the apron.
float mesoHeightAt(const MesoTile& t, float worldX, float worldZ);

/// Shared, synchronized LRU of immutable tiles (one per process, read-mostly; the generator's
/// worker copies all point at the same pager, plan 13.1). Eviction + re-bake is bit-identical
/// because the bake is pure (pinned by MesoSeamTest.PagerEvictsAndRebakesIdentically).
class MesoPager {
public:
    MesoPager(std::shared_ptr<const MesoBaker> baker, size_t capacity);
    std::shared_ptr<const MesoTile> get(int tx, int tz);
    float heightAt(float worldX, float worldZ);
    size_t   resident() const;
    uint64_t bakes()    const { return m_bakes; }
    size_t   capacity() const { return m_capacity; }
    const MesoBaker& baker() const { return *m_baker; }
private:
    struct Key {
        int tx, tz;
        bool operator==(const Key& o) const { return tx == o.tx && tz == o.tz; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return std::hash<long long>()((static_cast<long long>(k.tx) << 32) ^
                                          static_cast<unsigned int>(k.tz));
        }
    };
    using Entry = std::pair<std::shared_ptr<const MesoTile>, std::list<Key>::iterator>;
    std::shared_ptr<const MesoBaker> m_baker;
    size_t m_capacity;
    mutable std::mutex m_mutex;
    std::list<Key> m_lru;   // front = most recent
    std::unordered_map<Key, Entry, KeyHash> m_tiles;
    uint64_t m_bakes = 0;
};

} // namespace Phyxel
