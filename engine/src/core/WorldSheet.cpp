#include "core/WorldSheet.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {

SheetParams SheetParams::clamped(SheetParams p) {
    // Each clamp names the failure it prevents (FeatureDesignKeys: clamp at the boundary, say why).
    if (p.mesoCellU < 1.0f) p.mesoCellU = 1.0f;          // a sub-voxel cell has no column to serve
    if (p.mesoTileCells < 4) p.mesoTileCells = 4;        // below 4 the apron is the whole tile
    if (p.mesoTileCells > 1024) p.mesoTileCells = 1024;  // 1024^2 floats = 4 MB per tile; the LRU is sized for 256^2
    if (p.mesoSteps < 0) p.mesoSteps = 0;
    if (p.mesoSteps > 32) p.mesoSteps = 32;              // apron = steps + 2 extra cells per edge; 32 -> ~30 % extra area at 256^2
    if (p.diffusion < 0.0f) p.diffusion = 0.0f;
    if (p.diffusion > 0.24f) p.diffusion = 0.24f;        // the explicit 4-neighbour Laplacian is unstable at k >= 0.25 (von Neumann)
    if (p.talusSlope < 0.05f) p.talusSlope = 0.05f;      // a ~0 talus moves everything every step (flattens the tile)
    if (p.thermalRate < 0.0f) p.thermalRate = 0.0f;
    if (p.thermalRate > 0.5f) p.thermalRate = 0.5f;      // > 0.5 lets a pair swap past each other and oscillate
    return p;
}

MesoTile::MesoTile(int cellX0, int cellZ0, int cells, int apron, float cellU)
    : m_cellX0(cellX0), m_cellZ0(cellZ0), m_cells(cells), m_apron(apron), m_cellU(cellU),
      m_h(static_cast<size_t>(cells + 2 * apron) * static_cast<size_t>(cells + 2 * apron), 0.0f) {}

MesoBaker::MesoBaker(uint32_t seed, SheetParams params, HeightFieldFn macroHeightAt, HeightFieldFn detailAt)
    : m_seed(seed), m_params(SheetParams::clamped(params)), m_macro(std::move(macroHeightAt)),
      m_detail(std::move(detailAt)) {}

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

    // h0: the macro field + detail at each cell CENTRE, addressed by the ABSOLUTE cell index so the
    // float inputs are bit-identical whichever window the cell is baked in.
    for (int j = -apron; j < cells + apron; ++j) {
        for (int i = -apron; i < cells + apron; ++i) {
            const float cx = static_cast<float>(cellX0 + i) * cu + 0.5f * cu;
            const float cz = static_cast<float>(cellZ0 + j) * cu + 0.5f * cu;
            float h = m_macro ? m_macro(cx, cz) : 0.0f;
            if (m_detail) h += m_detail(cx, cz);
            t.at(i, j) = h;
        }
    }

    // Relaxation: thermal erosion (material past the talus slope moves downhill) + hillslope
    // diffusion, a fixed number of steps over the padded grid minus its outer ring. Cell at padded
    // index k is correct through step k, so the interior (k >= apron) is correct after every step
    // (apron = steps + bicubic reach, SheetParams::apronCells).
    const float talusU = m_params.talusSlope * cu;   // height difference between neighbour centres at the talus slope
    const float rate   = m_params.thermalRate;
    const float kd     = m_params.diffusion;
    // JACOBI (double-buffered): every cell of step s+1 reads only step-s values, so a cell's
    // value depends on nothing outside its (steps x radius) cone and tile-by-tile == whole-region
    // bit for bit. The in-place sweep tried first read already-updated W/N neighbours whose values
    // reached back across the whole sweep: 881 of 16384 cells differed between a tile and the
    // whole-region bake (docs/evidence/worldforge_v2/m1_meso_seam_and_column_cost_red.txt).
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

namespace {
inline float catmullRom(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}
}  // namespace

float mesoHeightAt(const MesoTile& t, float worldX, float worldZ) {
    // Absolute cell coordinate of the sample relative to cell CENTRES; the fractional part is
    // computed from the absolute value so it does not depend on which tile serves the sample.
    const float ua = worldX / t.cellU() - 0.5f;
    const float va = worldZ / t.cellU() - 0.5f;
    const float fi = std::floor(ua), fj = std::floor(va);
    const float tu = ua - fi, tv = va - fj;
    const int i0 = static_cast<int>(fi) - t.cellX0();
    const int j0 = static_cast<int>(fj) - t.cellZ0();
    float rows[4];
    for (int r = 0; r < 4; ++r) {
        const int j = j0 - 1 + r;
        rows[r] = catmullRom(t.at(i0 - 1, j), t.at(i0, j), t.at(i0 + 1, j), t.at(i0 + 2, j), tu);
    }
    return catmullRom(rows[0], rows[1], rows[2], rows[3], tv);
}

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

float MesoPager::heightAt(float worldX, float worldZ) {
    const float tileU = m_baker->params().tileU();
    const int tx = static_cast<int>(std::floor(worldX / tileU));
    const int tz = static_cast<int>(std::floor(worldZ / tileU));
    return mesoHeightAt(*get(tx, tz), worldX, worldZ);
}

size_t MesoPager::resident() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tiles.size();
}

} // namespace Phyxel
