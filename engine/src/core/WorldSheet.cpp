#include "core/WorldSheet.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {

SheetParams SheetParams::clamped(SheetParams p) {
    // Each clamp names the failure it prevents (FeatureDesignKeys: clamp at the boundary, say why).
    if (p.macroCellU < 32.0f) p.macroCellU = 32.0f;     // finer than a meso cell inverts the macro/meso roles
    if (p.continentCells < 4) p.continentCells = 4;      // below 4 the bicubic has no interior
    if (p.continentCells > 1024) p.continentCells = 1024; // 1024^2 cells x 8 B = 8 MB; a 512 km continent is the plan's stress ceiling
    if (p.mesoCellU < 1.0f) p.mesoCellU = 1.0f;          // a sub-voxel cell has no column to serve
    if (p.mesoTileCells < 4) p.mesoTileCells = 4;        // below 4 the apron is the whole tile
    if (p.mesoTileCells > 1024) p.mesoTileCells = 1024;  // 1024^2 x 3 planes = 12 MB per tile; the LRU is sized for 256^2
    if (p.mesoSteps < 0) p.mesoSteps = 0;
    if (p.mesoSteps > 32) p.mesoSteps = 32;              // apron = steps + 2 extra cells per edge; 32 -> ~30 % extra area at 256^2
    if (p.diffusion < 0.0f) p.diffusion = 0.0f;
    if (p.diffusion > 0.24f) p.diffusion = 0.24f;        // the explicit 4-neighbour Laplacian is unstable at k >= 0.25 (von Neumann)
    if (p.talusSlope < 0.05f) p.talusSlope = 0.05f;      // a ~0 talus moves everything every step (flattens the tile)
    if (p.thermalRate < 0.0f) p.thermalRate = 0.0f;
    if (p.thermalRate > 0.5f) p.thermalRate = 0.5f;      // > 0.5 lets a pair swap past each other and oscillate
    if (p.pagerTiles < 4) p.pagerTiles = 4;              // a column's climate warp can read a neighbouring tile: keep a 2x2 at least
    if (p.pagerTiles > 512) p.pagerTiles = 512;          // 512 x ~0.9 MB = ~460 MB resident: past the laptop's budget (laptop-memory-kill)
    if (p.erosionSteps < 0) p.erosionSteps = 0;
    if (p.erosionSteps > 500) p.erosionSteps = 500;      // each step is a full-grid flood + solve; 500 x 200^2 is the bake budget's ceiling
    if (p.erosionDt < 0.0f) p.erosionDt = 0.0f;
    if (p.erosionK < 0.0f) p.erosionK = 0.0f;            // negative erodibility deposits uphill
    if (p.erosionM < 0.35f) p.erosionM = 0.35f;          // m/n outside [0.35, 0.6] is outside Whipple & Tucker 1999's
    if (p.erosionM > 0.6f) p.erosionM = 0.6f;            // concavity band (§10 F4c, GROUNDED range; n = 1 here)
    if (p.erosionUpliftMax < 0.0f) p.erosionUpliftMax = 0.0f;
    return p;
}

// ---------------------------------------------------------------------------
// Macro sheet
// ---------------------------------------------------------------------------
MacroSheet::MacroSheet(int cells, float cellU, uint64_t hash)
    : m_cells(cells), m_cellU(cellU), m_origin(-0.5f * static_cast<float>(cells) * cellU), m_hash(hash),
      m_c(static_cast<size_t>(cells) * static_cast<size_t>(cells)) {}

namespace {
inline float catmullRom(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}
// d/dt of catmullRom at t (per cell).
inline float catmullRomD(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t;
    return 0.5f * ((-p0 + p2) + 2.0f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t +
                   3.0f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t2);
}
}  // namespace

MacroCell MacroSheet::sampleAt(float worldX, float worldZ) const {
    const float u = (worldX - m_origin) / m_cellU - 0.5f;
    const float v = (worldZ - m_origin) / m_cellU - 0.5f;
    const float fi = std::floor(u), fj = std::floor(v);
    const float tu = u - fi, tv = v - fj;
    const int i0 = static_cast<int>(fi), j0 = static_cast<int>(fj);
    float rh[4], rc[4];
    for (int r = 0; r < 4; ++r) {
        const int j = j0 - 1 + r;
        rh[r] = catmullRom(cell(i0 - 1, j).baseHeight, cell(i0, j).baseHeight,
                           cell(i0 + 1, j).baseHeight, cell(i0 + 2, j).baseHeight, tu);
        rc[r] = catmullRom(cell(i0 - 1, j).continentalness, cell(i0, j).continentalness,
                           cell(i0 + 1, j).continentalness, cell(i0 + 2, j).continentalness, tu);
    }
    MacroCell out;
    out.baseHeight = catmullRom(rh[0], rh[1], rh[2], rh[3], tv);
    out.continentalness = std::min(1.0f, std::max(0.0f, catmullRom(rc[0], rc[1], rc[2], rc[3], tv)));
    return out;
}

// ---------------------------------------------------------------------------
// Meso tile + baker
// ---------------------------------------------------------------------------
MesoTile::MesoTile(int cellX0, int cellZ0, int cells, int apron, float cellU)
    : m_cellX0(cellX0), m_cellZ0(cellZ0), m_cells(cells), m_apron(apron), m_cellU(cellU),
      m_h(static_cast<size_t>(cells + 2 * apron) * static_cast<size_t>(cells + 2 * apron), 0.0f),
      m_t(m_h.size(), 0.0f), m_m(m_h.size(), 0.0f) {}

MesoBaker::MesoBaker(uint32_t seed, SheetParams params, HeightFieldFn macroHeightAt, HeightFieldFn detailAt,
                     ClimateFn climateAt)
    : m_seed(seed), m_params(SheetParams::clamped(params)), m_macro(std::move(macroHeightAt)),
      m_detail(std::move(detailAt)), m_climate(std::move(climateAt)) {}

std::shared_ptr<const MesoTile> MesoBaker::bake(int tx, int tz) const {
    const int n = m_params.mesoTileCells;
    return bakeCells(tx * n, tz * n, n);
}

std::shared_ptr<const MesoTile> MesoBaker::bakeCells(int cellX0, int cellZ0, int cells) const {
    const int apron = m_params.apronCells();
    auto tile = std::make_shared<MesoTile>(cellX0, cellZ0, cells, apron, m_params.mesoCellU);
    MesoTile& t = *tile;
    const int P = t.padded();
    const float cu = m_params.mesoCellU;

    // h0 + climate: the fields at each cell CENTRE, addressed by the ABSOLUTE cell index so the
    // float inputs are bit-identical whichever window the cell is baked in.
    for (int j = -apron; j < cells + apron; ++j) {
        for (int i = -apron; i < cells + apron; ++i) {
            const float cx = static_cast<float>(cellX0 + i) * cu + 0.5f * cu;
            const float cz = static_cast<float>(cellZ0 + j) * cu + 0.5f * cu;
            float h = m_macro ? m_macro(cx, cz) : 0.0f;
            if (m_detail) h += m_detail(cx, cz);
            t.at(i, j) = h;
            if (m_climate) {
                float tr = 0.0f, mr = 0.0f;
                m_climate(cx, cz, tr, mr);
                t.temp(i, j) = tr;
                t.moist(i, j) = mr;
            }
        }
    }

    // Relaxation: thermal erosion (material past the talus slope moves downhill) + hillslope
    // diffusion, a fixed number of steps over the padded grid minus its outer ring. Cell at padded
    // index k is correct through step k, so the interior (k >= apron) is correct after every step
    // (apron = steps + bicubic reach, SheetParams::apronCells).
    //
    // JACOBI (double-buffered): every cell of step s+1 reads only step-s values, so a cell's
    // value depends on nothing outside its (steps x radius) cone and tile-by-tile == whole-region
    // bit for bit. The in-place sweep tried first read already-updated W/N neighbours whose values
    // reached back across the whole sweep: 881 of 16384 cells differed between a tile and the
    // whole-region bake (docs/evidence/worldforge_v2/m1_meso_seam_and_column_cost_red.txt).
    const float talusU = m_params.talusSlope * cu;   // height difference between neighbour centres at the talus slope
    const float rate   = m_params.thermalRate;
    const float kd     = m_params.diffusion;
    std::vector<float>& h = t.raw();
    std::vector<float> next(h.size());
    for (int step = 0; step < m_params.mesoSteps; ++step) {
        next = h;   // the untouched outer ring carries over (never read by a correct interior cell)
        for (int pj = 1; pj < P - 1; ++pj) {
            for (int pi = 1; pi < P - 1; ++pi) {
                const size_t c = static_cast<size_t>(pj) * P + pi;
                const float hc = h[c];
                const float nb[4] = {h[c - P], h[c + 1], h[c + P], h[c - 1]};   // N, E, S, W: fixed order
                float d = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    const float diff = hc - nb[k];
                    if (diff > talusU)       d -= rate * (diff - talusU) * 0.25f;   // we are the high side: lose
                    else if (diff < -talusU) d += rate * (-diff - talusU) * 0.25f;  // we are the low side: gain
                }
                const float lap = (nb[0] + nb[1] + nb[2] + nb[3]) - 4.0f * hc;
                next[c] = hc + kd * lap + d;
            }
        }
        h.swap(next);
    }
    return tile;
}

// ---------------------------------------------------------------------------
// Sampling: absolute cell coordinate relative to cell CENTRES; the fractional part is computed
// from the absolute value so it does not depend on which tile serves the sample.
// ---------------------------------------------------------------------------
namespace {
struct CellPos { int i0, j0; float tu, tv; };
inline CellPos cellPos(const MesoTile& t, float worldX, float worldZ) {
    const float ua = worldX / t.cellU() - 0.5f;
    const float va = worldZ / t.cellU() - 0.5f;
    const float fi = std::floor(ua), fj = std::floor(va);
    return {static_cast<int>(fi) - t.cellX0(), static_cast<int>(fj) - t.cellZ0(), ua - fi, va - fj};
}
template <class Get>
inline float bicubic(const MesoTile& t, const CellPos& p, Get get) {
    float rows[4];
    for (int r = 0; r < 4; ++r) {
        const int j = p.j0 - 1 + r;
        rows[r] = catmullRom(get(p.i0 - 1, j), get(p.i0, j), get(p.i0 + 1, j), get(p.i0 + 2, j), p.tu);
    }
    return catmullRom(rows[0], rows[1], rows[2], rows[3], p.tv);
}
}  // namespace

float mesoHeightAt(const MesoTile& t, float worldX, float worldZ) {
    const CellPos p = cellPos(t, worldX, worldZ);
    return bicubic(t, p, [&t](int i, int j) { return t.at(i, j); });
}

void mesoClimateAt(const MesoTile& t, float worldX, float worldZ, float& tempRaw, float& moistRaw) {
    const CellPos p = cellPos(t, worldX, worldZ);
    tempRaw  = bicubic(t, p, [&t](int i, int j) { return t.temp(i, j); });
    moistRaw = bicubic(t, p, [&t](int i, int j) { return t.moist(i, j); });
}

std::pair<float, float> mesoGradientAt(const MesoTile& t, float worldX, float worldZ) {
    const CellPos p = cellPos(t, worldX, worldZ);
    // d/dx: derivative along u in each row, then interpolate the rows along v.
    float rowsD[4], rows[4];
    for (int r = 0; r < 4; ++r) {
        const int j = p.j0 - 1 + r;
        const float a = t.at(p.i0 - 1, j), b = t.at(p.i0, j), c = t.at(p.i0 + 1, j), d = t.at(p.i0 + 2, j);
        rowsD[r] = catmullRomD(a, b, c, d, p.tu);
        rows[r]  = catmullRom(a, b, c, d, p.tu);
    }
    const float dhdu = catmullRom(rowsD[0], rowsD[1], rowsD[2], rowsD[3], p.tv);
    const float dhdv = catmullRomD(rows[0], rows[1], rows[2], rows[3], p.tv);
    return {dhdu / t.cellU(), dhdv / t.cellU()};
}

// ---------------------------------------------------------------------------
// Pager
// ---------------------------------------------------------------------------
MesoPager::MesoPager(std::shared_ptr<const MesoBaker> baker, size_t capacity)
    : m_baker(std::move(baker)), m_capacity(std::max<size_t>(1, capacity)) {}

std::shared_ptr<const MesoTile> MesoPager::get(int tx, int tz) {
    const Key key{tx, tz};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_tiles.find(key);
        if (it != m_tiles.end()) {
            m_lru.splice(m_lru.begin(), m_lru, it->second.second);
            return it->second.first;
        }
    }
    // Bake outside the lock (pure, may be slow); a concurrent duplicate bake yields an identical
    // tile and the first insert wins; no reader can observe a difference.
    auto tile = m_baker->bake(tx, tz);
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_bakes;
    auto it = m_tiles.find(key);
    if (it != m_tiles.end()) {
        m_lru.splice(m_lru.begin(), m_lru, it->second.second);
        return it->second.first;
    }
    m_lru.push_front(key);
    m_tiles.emplace(key, Entry(tile, m_lru.begin()));
    while (m_tiles.size() > m_capacity) {
        const Key victim = m_lru.back();
        m_tiles.erase(victim);
        m_lru.pop_back();
    }
    return tile;
}

std::shared_ptr<const MesoTile> MesoPager::tileFor(float worldX, float worldZ) {
    const float tileU = m_baker->params().tileU();
    return get(static_cast<int>(std::floor(worldX / tileU)), static_cast<int>(std::floor(worldZ / tileU)));
}

float MesoPager::heightAt(float worldX, float worldZ) {
    return mesoHeightAt(*tileFor(worldX, worldZ), worldX, worldZ);
}

void MesoPager::climateAt(float worldX, float worldZ, float& tempRaw, float& moistRaw) {
    mesoClimateAt(*tileFor(worldX, worldZ), worldX, worldZ, tempRaw, moistRaw);
}

std::pair<float, float> MesoPager::gradientAt(float worldX, float worldZ) {
    return mesoGradientAt(*tileFor(worldX, worldZ), worldX, worldZ);
}

size_t MesoPager::resident() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tiles.size();
}

} // namespace Phyxel
