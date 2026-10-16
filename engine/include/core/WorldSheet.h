#pragma once
// ============================================================================
// M1 World Sheet — docs/WorldForgeV2Plan.md §3 (L0) / §5 M1 / "Design-check resolutions".
//
// MACRO sheet: a bounded square grid (512 u cells, the continent) of base elevation +
// continentalness, baked once per recipe and shared by every generator copy. MESO tiles: 32 u
// height cells baked on demand from the macro field + a per-cell detail term, relaxed by a FIXED
// number of Jacobi (double-buffered) thermal-erosion + hillslope-diffusion steps, plus raw climate
// planes (temperature / moisture noise at the cell centre). Tiles bound COST only: every cell is
// a pure function of (seed, params, the absolute cell index, the macro field), and the apron is
// DERIVED from the step count so a cell's whole dependency cone lies inside the tile.
// MesoSeamTest pins "tile-by-tile == whole-region" bit-exactly (resolution 2).
// ============================================================================
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Phyxel {

constexpr int kMesoStencilRadius = 1;  // both Jacobi ops read the 4-neighbourhood
constexpr int kMesoBicubicReach  = 2;  // Catmull-Rom reads two cells past the sample's cell

struct SheetParams {
    float macroCellU       = 512.0f;  // world units per macro cell (plan §5 M1)
    int   continentCells   = 200;     // macro cells per edge, centred on the world origin (200 x 512 u = 102 km)
    float mesoCellU        = 32.0f;   // world units per meso cell
    int   mesoTileCells    = 256;     // interior cells per tile edge (256 x 32 u = 8192 u)
    int   mesoSteps        = 4;       // Jacobi thermal + diffusion iterations per tile
    float diffusion        = 0.10f;   // hillslope diffusion per step; the explicit 4-neighbour stencil
                                      // is stable only for k < 0.25 (von Neumann), clamped there
    float talusSlope       = 0.70f;   // thermal threshold, rise/run = tan 35 deg = WorldGenerator's
                                      // kRockSlope (angle of repose of loose scree, TerrainGenerationV2 P0)
    float thermalRate      = 0.50f;   // fraction of the over-talus excess moved per step, (0, 0.5] so a
                                      // pair never overshoots (each side moves at most half the excess)
    int   pagerTiles       = 64;      // resident meso tiles in the shared LRU (a 256-cell tile with its
                                      // apron is ~0.9 MB over three planes: 64 tiles ~ 55 MB)
    // M1c macro stream-power erosion (core/MacroErosion.h). The constants are Cordonnier et al.
    // 2016's published configuration (plan §10 F4e, quoted): n = 1, m = 0.5 ("as in most
    // geomorphological studies"), k = 5.61e-7 /yr, U = 5.0e-4 m/yr ("the average uplift among
    // earth mountains"), dt = 2.5e5 yr, convergence ~200 steps. They are a published DESIGN
    // configuration, not measurements (K is calibrated, never measured: W&T 1999). Steps default to
    // 0 (off) until the lab L4 experiment has run; the lab recipe turns it on.
    int   erosionSteps     = 0;        // Cordonnier: ~200 to converge (50 for shape, 100-300 to stop evolving)
    float erosionDt        = 2.5e5f;   // years per step (Cordonnier §6: "to ensure a fast convergence")
    float erosionK         = 5.61e-7f; // /yr at m = 0.5 with A in m² (units m^(1-2m)/yr: re-derive if m changes)
    float erosionM         = 0.5f;     // drainage-area exponent; n = 1; clamped to the measured concavity band
    float erosionUpliftMax = 5.0e-4f;  // m/yr at the uplift field's maximum (the field = normalised base elevation)
    float erosionMaxSlope  = 0.577f;   // tan 30°: Cordonnier §5 reduces the elevation change so slopes stay below
                                       // the talus angle (regularises valleys; their 6°-54° noise modulation is M2)
    /// Clamped copy, reasons at each clamp; a pager echoes the copy it runs with.
    static SheetParams clamped(SheetParams p);
    /// DERIVED, never set: steps x stencil radius so every interior cell's cone is inside the
    /// tile, + the bicubic reach so samples up to the interior edge read correct apron cells.
    int apronCells() const { return mesoSteps * kMesoStencilRadius + kMesoBicubicReach; }
    float tileU() const { return mesoTileCells * mesoCellU; }
    bool operator==(const SheetParams& o) const {
        return macroCellU == o.macroCellU && continentCells == o.continentCells && mesoCellU == o.mesoCellU &&
               mesoTileCells == o.mesoTileCells && mesoSteps == o.mesoSteps && diffusion == o.diffusion &&
               talusSlope == o.talusSlope && thermalRate == o.thermalRate && pagerTiles == o.pagerTiles &&
               erosionSteps == o.erosionSteps && erosionDt == o.erosionDt && erosionK == o.erosionK &&
               erosionM == o.erosionM && erosionUpliftMax == o.erosionUpliftMax && erosionMaxSlope == o.erosionMaxSlope;
    }
};

struct MacroCell {
    float baseHeight      = 0.0f;   // Layer-0 base elevation (world Y) at the cell centre
    float continentalness = 0.5f;   // [0,1]
};

/// The continent: a square grid of macro cells centred on the world origin (cell (0,0)'s corner at
/// -cells/2 x cellU). Immutable once baked; sampled bicubically over cell CENTRES and clamped to the
/// edge cells outside the grid (a bounded world is the same sheet with bounds, WorldModel.md).
class MacroSheet {
public:
    MacroSheet(int cells, float cellU, uint64_t hash);
    int      cells()   const { return m_cells; }
    float    cellU()   const { return m_cellU; }
    float    originU() const { return m_origin; }   // world coordinate of cell (0,0)'s corner (x and z)
    uint64_t hash()    const { return m_hash; }
    MacroCell&       cell(int i, int j)       { return m_c[idx(i, j)]; }
    const MacroCell& cell(int i, int j) const { return m_c[idx(i, j)]; }
    /// Catmull-Rom over cell centres; indices clamp to the grid edge.
    MacroCell sampleAt(float worldX, float worldZ) const;
private:
    size_t idx(int i, int j) const {
        i = i < 0 ? 0 : (i >= m_cells ? m_cells - 1 : i);
        j = j < 0 ? 0 : (j >= m_cells ? m_cells - 1 : j);
        return static_cast<size_t>(j) * static_cast<size_t>(m_cells) + static_cast<size_t>(i);
    }
    int m_cells;
    float m_cellU, m_origin;
    uint64_t m_hash;
    std::vector<MacroCell> m_c;
};

/// A baked window of cells with its apron. Cell (i, j) for i, j in [-apron, cells+apron) sits at
/// absolute cell index (cellX0 + i, cellZ0 + j); its CENTRE is at ((cellX0 + i) + 0.5) x cellU in
/// world units. Three planes: height (relaxed), raw temperature and raw moisture noise (not
/// relaxed; the column applies its domain warp and contrast expansion). Immutable once baked.
class MesoTile {
public:
    MesoTile(int cellX0, int cellZ0, int cells, int apron, float cellU);
    int   cells()  const { return m_cells; }
    int   apron()  const { return m_apron; }
    int   padded() const { return m_cells + 2 * m_apron; }
    float cellU()  const { return m_cellU; }
    int   cellX0() const { return m_cellX0; }
    int   cellZ0() const { return m_cellZ0; }
    float  at(int i, int j) const { return m_h[idx(i, j)]; }
    float& at(int i, int j)       { return m_h[idx(i, j)]; }
    float  temp(int i, int j)  const { return m_t[idx(i, j)]; }
    float& temp(int i, int j)        { return m_t[idx(i, j)]; }
    float  moist(int i, int j) const { return m_m[idx(i, j)]; }
    float& moist(int i, int j)       { return m_m[idx(i, j)]; }
    std::vector<float>&       raw()       { return m_h; }
    const std::vector<float>& raw() const { return m_h; }
    size_t idx(int i, int j) const {
        return static_cast<size_t>(j + m_apron) * static_cast<size_t>(padded()) +
               static_cast<size_t>(i + m_apron);
    }
private:
    int m_cellX0, m_cellZ0, m_cells, m_apron;
    float m_cellU;
    std::vector<float> m_h, m_t, m_m;
};

/// Pure position-only inputs, evaluated per cell at the cell centre: the macro height (bicubic
/// over the macro sheet), the detail term (the generator's broad ridged band), and the raw
/// climate noise pair. All MUST be pure functions of world position.
using HeightFieldFn = std::function<float(float worldX, float worldZ)>;
using ClimateFn     = std::function<void(float worldX, float worldZ, float& tempRaw, float& moistRaw)>;

class MesoBaker {
public:
    MesoBaker(uint32_t seed, SheetParams params, HeightFieldFn macroHeightAt,
              HeightFieldFn detailAt = nullptr, ClimateFn climateAt = nullptr);
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
    ClimateFn m_climate;
};

/// Catmull-Rom bicubic height at a world position from ONE tile. The sample's cell is found
/// from the ABSOLUTE cell coordinate (so the fractional offset is bit-identical whichever
/// tile serves it); reads up to kMesoBicubicReach cells past the interior into the apron.
float mesoHeightAt(const MesoTile& t, float worldX, float worldZ);
/// Same interpolation over the raw climate planes.
void  mesoClimateAt(const MesoTile& t, float worldX, float worldZ, float& tempRaw, float& moistRaw);
/// Analytic gradient of the bicubic height (dh/dx, dh/dz per world unit): the slope the material
/// override reads instead of re-sampling four neighbour columns (resolution 3).
std::pair<float, float> mesoGradientAt(const MesoTile& t, float worldX, float worldZ);

/// Shared, synchronized LRU of immutable tiles (one per recipe, read-mostly; the generator's
/// worker copies all point at the same pager, plan 13.1). Eviction + re-bake is bit-identical
/// because the bake is pure (pinned by MesoSeamTest.PagerEvictsAndRebakesIdentically).
class MesoPager {
public:
    MesoPager(std::shared_ptr<const MesoBaker> baker, size_t capacity);
    std::shared_ptr<const MesoTile> get(int tx, int tz);
    std::shared_ptr<const MesoTile> tileFor(float worldX, float worldZ);
    float heightAt(float worldX, float worldZ);
    void  climateAt(float worldX, float worldZ, float& tempRaw, float& moistRaw);
    std::pair<float, float> gradientAt(float worldX, float worldZ);
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
