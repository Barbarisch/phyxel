#include "core/water/ShoreBand.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace Phyxel::Core::Water {

namespace {
inline int floorToInt(float v) { return static_cast<int>(std::floor(v)); }
}

bool ShoreBand::site(const glm::vec2& centreXZ, const ShoreBandParams& pIn, BedQuery bed, StoredTopQuery stored, std::string* err) {
    const auto t0 = std::chrono::steady_clock::now();
    ShoreBandParams p = pIn;
    p.radius = std::clamp(p.radius, 4.0f, 64.0f);
    p.inner = std::max(p.inner, 4);
    p.cellSize = p.cellSize < 0.5f ? 1.0f / 3.0f : 1.0f;   // 1 or 1/3: the voxel grid's own sizes
    const float h = p.cellSize;
    const int n = static_cast<int>(std::ceil(2.0f * p.radius / h));
    if (static_cast<long>(n) * n > kMaxColumns) { if (err) *err = "band of " + std::to_string(static_cast<long>(n) * n) + " columns exceeds the cap of " + std::to_string(kMaxColumns) + " (radius / cellSize)"; return false; }
    if (!bed || !stored) { if (err) *err = "no bed / stored-top query installed"; return false; }
    // the seaward bias: the centroid of the stored water within 2 R pulls the centre up to R / 2 toward it
    glm::vec2 centre = centreXZ;
    if (p.seawardBias) {
        double sx = 0.0, sz = 0.0; long cnt = 0;
        const int R2 = static_cast<int>(2.0f * p.radius);
        for (int dz = -R2; dz <= R2; dz += 2) for (int dx = -R2; dx <= R2; dx += 2) {
            const int wx = floorToInt(centreXZ.x) + dx, wz = floorToInt(centreXZ.y) + dz;
            if (stored(wx, wz).wet) { sx += wx + 0.5; sz += wz + 0.5; ++cnt; }
        }
        if (cnt > 0) {
            const glm::vec2 toWater(static_cast<float>(sx / cnt) - centreXZ.x, static_cast<float>(sz / cnt) - centreXZ.y);
            const float len = glm::length(toWater);
            if (len > 1e-3f) centre += toWater * (std::min(len, 0.5f * p.radius) / len);
        }
    }
    // the box on the column grid: origin = the multiple of h at or below centre - radius
    const glm::vec2 origin(std::floor((centre.x - p.radius) / h) * h, std::floor((centre.y - p.radius) / h) * h);

    // 1. the stored tops; the still level = the ring's most common stored top (1 cm bins)
    std::vector<StoredTop> tops(static_cast<size_t>(n) * n);
    std::map<int, int> bins;
    auto inRing = [&](int x, int z) { return x < p.inner || z < p.inner || x >= n - p.inner || z >= n - p.inner; };
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
        const glm::vec2 c = origin + glm::vec2((x + 0.5f) * h, (z + 0.5f) * h);
        const StoredTop s = stored(floorToInt(c.x), floorToInt(c.y));
        tops[static_cast<size_t>(x) + static_cast<size_t>(n) * z] = s;
        if (s.wet && inRing(x, z)) ++bins[static_cast<int>(std::lround(s.top * 100.0f))];
    }
    if (bins.empty()) { if (err) *err = "the band's outer ring holds no stored water: no ocean to drive the shore (move closer to the water, or the chunks are not resident)"; return false; }
    int bestBin = 0, bestCount = -1;
    for (const auto& [b, c] : bins) if (c > bestCount) { bestCount = c; bestBin = b; }
    const float still = static_cast<float>(bestBin) / 100.0f;

    // 2. the new solver: bed per column, roles, initial surface
    auto solver = std::make_unique<ShoreSolver>(origin, n, n, h, p.solver);
    std::vector<ShoreColumnRole> roles(static_cast<size_t>(n) * n, ShoreColumnRole::Free);
    std::vector<float> stillOf(roles.size(), still);
    std::vector<float> weights(roles.size(), 1.0f);
    auto edgeDistance = [&](int x, int z) { return std::min(std::min(x, z), std::min(n - 1 - x, n - 1 - z)); };
    long walls = 0, prescribed = 0, wet = 0, sponge = 0;
    const float yLo = still - p.maxDepth, yHi = still + p.runUp + 1.0f;
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n) * z;
        ShoreColumn& c = solver->col(x, z);
        const glm::vec2 wc = origin + glm::vec2((x + 0.5f) * h, (z + 0.5f) * h);
        const BedSample b = bed(wc.x, wc.y, yLo, yHi);
        const StoredTop& s = tops[i];
        if (!b.known || b.top > still + p.runUp) {   // unknown ground holds (5.1); land above the band's reach reflects
            c.wall = 1; c.bed = b.known ? b.top : yHi; c.eta = c.bed; roles[i] = ShoreColumnRole::Wall; ++walls; continue;
        }
        c.bed = b.top;
        c.eta = (s.wet && s.top > c.bed) ? s.top : c.bed;
        if (s.wet && inRing(x, z) && std::abs(s.top - still) < 0.05f && c.bed < still) {
            const bool deep = still - c.bed >= p.minOceanDepth;
            c.prescribed = 1; c.eta = still; roles[i] = deep ? ShoreColumnRole::Prescribed : ShoreColumnRole::Sponge;
            if (deep) ++prescribed; else ++sponge;
            const float q = static_cast<float>(p.inner - edgeDistance(x, z)) * h;   // m into the ring from the free region (h at the first ring column)
            const float rr = std::min(q / std::max(p.ramp, h), 1.0f);
            c.weight = rr * rr;                                                    // ~0 beside the free region, 1 at `ramp` m and beyond (hard ocean)
            weights[i] = c.weight;
        }
        if (c.eta - c.bed > p.solver.dryDepth) ++wet;
    }
    // 3. carry the overlapping columns' state over from the previous siting (same h only)
    if (m_solver && m_rec.h == h) {
        for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
            const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n) * z;
            if (roles[i] != ShoreColumnRole::Free) continue;
            const glm::vec2 wc = origin + glm::vec2((x + 0.5f) * h, (z + 0.5f) * h);
            const int ox = floorToInt((wc.x - m_rec.originXZ.x) / h), oz = floorToInt((wc.y - m_rec.originXZ.y) / h);
            if (ox < 0 || oz < 0 || ox >= m_rec.n || oz >= m_rec.n) continue;
            if (role(ox, oz) == ShoreColumnRole::Wall) continue;
            const ShoreColumn& o = m_solver->col(ox, oz);
            ShoreColumn& c = solver->col(x, z);
            if (std::abs(o.bed - c.bed) > 1e-3f) continue;   // the bed changed under it: start from the stored state
            c.eta = std::max(o.eta, static_cast<double>(c.bed)); c.u = o.u; c.w = o.w; c.foam = o.foam;
        }
    }
    m_solver = std::move(solver);
    m_params = p;
    m_roles = std::move(roles);
    m_stillOfColumn = std::move(stillOf);
    m_weights = std::move(weights);
    m_bed = std::move(bed);
    m_centre = centreXZ;   // the CAMERA's position at siting: the resite trigger measures from it
    m_dirty.clear();
    const double exch = m_rec.exchanged; const int sit = m_rec.sitings; const uint64_t rev = m_rec.revision; const float peak = m_rec.runUpPeak;
    m_rec = ShoreBandRecord{};
    m_rec.on = true; m_rec.originXZ = origin; m_rec.n = n; m_rec.h = h; m_rec.still = still; m_rec.centreXZ = centre;
    m_rec.columns = static_cast<long>(n) * n; m_rec.prescribed = prescribed; m_rec.walls = walls; m_rec.wet = wet; m_rec.sponge = sponge;
    m_rec.free = m_rec.columns - prescribed - walls - sponge;
    m_rec.exchanged = exch; m_rec.sitings = sit + 1; m_rec.revision = rev + 1; m_rec.runUpPeak = peak;
    m_rec.mass = m_solver->totalMass();
    m_rec.siteMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    rebuildField();
    return true;
}

bool ShoreBand::needsResite(const glm::vec2& centreXZ) const {
    if (!m_solver) return false;
    const glm::vec2 d = centreXZ - m_centre;
    return std::max(std::abs(d.x), std::abs(d.y)) > 0.5f * m_params.radius;
}

void ShoreBand::noteEdit(int worldX, int worldZ) {
    if (!m_solver) return;
    const int x = floorToInt((static_cast<float>(worldX) + 0.5f - m_rec.originXZ.x) / m_rec.h);
    const int z = floorToInt((static_cast<float>(worldZ) + 0.5f - m_rec.originXZ.y) / m_rec.h);
    // a voxel column covers 1 / h^2 band columns; mark every one of them
    const int per = std::max(1, static_cast<int>(std::lround(1.0f / m_rec.h)));
    const int bx = floorToInt((static_cast<float>(worldX) - m_rec.originXZ.x) / m_rec.h), bz = floorToInt((static_cast<float>(worldZ) - m_rec.originXZ.y) / m_rec.h);
    (void)x; (void)z;
    for (int dz = 0; dz < per; ++dz) for (int dx = 0; dx < per; ++dx) {
        const int cx = bx + dx, cz = bz + dz;
        if (cx < 0 || cz < 0 || cx >= m_rec.n || cz >= m_rec.n) continue;
        m_dirty.emplace_back(cx, cz);
    }
}

void ShoreBand::tick(float dt, float waveTime, const SeaSwellParams& swell) {
    if (!m_solver) return;
    const auto t0 = std::chrono::steady_clock::now();
    // edits: the bed moves, the surface follows (never below the bed); counted, never silent
    if (!m_dirty.empty() && m_bed) {
        const float yLo = m_rec.still - m_params.maxDepth, yHi = m_rec.still + m_params.runUp + 1.0f;
        for (const auto& [x, z] : m_dirty) {
            ShoreColumn& c = m_solver->col(x, z);
            const glm::vec2 wc = columnCentre(x, z);
            const BedSample b = m_bed(wc.x, wc.y, yLo, yHi);
            if (!b.known) continue;
            c.bed = b.top;
            if (c.eta < c.bed) c.eta = c.bed;   // the ground rose through the water: that water is gone (an edit never creates water; it can bury it)
            ++m_rec.bedUpdates;
        }
        m_dirty.clear();
    }
    // the ocean ring: the sheet's swell about the still level, the depth-averaged Airy velocity
    const int n = m_rec.n;
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n) * z;
        if (m_roles[i] == ShoreColumnRole::Sponge) { m_solver->prescribe(x, z, m_stillOfColumn[i], glm::vec2(0.0f), m_weights[i]); continue; }
        if (m_roles[i] != ShoreColumnRole::Prescribed) continue;
        const ShoreColumn& c = m_solver->col(x, z);
        const glm::vec2 wc = columnCentre(x, z);
        const float still = m_stillOfColumn[i];
        const SeaSwellColumn s = seaSwellColumn(swell, wc.x, wc.y, waveTime, still - c.bed);
        m_solver->prescribe(x, z, still + s.height, s.uAvg, m_weights[i]);
    }
    const ShoreStepReport r = m_solver->step(dt);
    m_rec.exchanged += r.exchanged;
    m_rec.mass = r.mass;
    m_rec.substeps = r.substeps;
    m_rec.maxSpeed = r.maxSpeed;
    m_rec.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    rebuildField();
}

void ShoreBand::rebuildField() {
    const auto t0 = std::chrono::steady_clock::now();
    const int n = m_rec.n;
    const float h = m_rec.h;
    m_field.origin = glm::ivec3(static_cast<int>(std::lround(m_rec.originXZ.x / h)), 0, static_cast<int>(std::lround(m_rec.originXZ.y / h)));
    m_field.nx = n; m_field.nz = n; m_field.h = h;
    m_field.cols.assign(static_cast<size_t>(n) * n, SurfaceColumn{});
    long wet = 0; float runUp = 0.0f, foam = 0.0f; double riseSum = 0.0; long riseN = 0; long swash = 0; float swashEta = 0.0f;
    constexpr float kDrawDepth = 0.005f;   // thinner than 5 mm is a film the mesh does not draw
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n) * z;
        const ShoreColumn& c = m_solver->col(x, z);
        SurfaceColumn& s = m_field.cols[i];
        s.solidTopY = c.bed;
        if (m_roles[i] == ShoreColumnRole::Wall) continue;
        const float d = static_cast<float>(c.eta - c.bed);
        if (d < kDrawDepth) continue;
        s.runs = 1.0f; s.bottom[0] = c.bed; s.top[0] = static_cast<float>(c.eta);
        s.foam = c.foam; s.u = c.u; s.w = c.w;   // G2
        ++wet;
        foam = std::max(foam, c.foam);
        if (m_roles[i] == ShoreColumnRole::Free && c.bed > m_rec.still) runUp = std::max(runUp, c.bed - m_rec.still);
        if (m_roles[i] == ShoreColumnRole::Free && c.bed >= m_rec.still - 1e-3f) { ++swash; swashEta = std::max(swashEta, static_cast<float>(c.eta - m_rec.still)); }
        if (m_roles[i] == ShoreColumnRole::Free && c.bed < m_rec.still) { riseSum += c.eta - m_rec.still; ++riseN; }
    }
    m_rec.wet = wet; m_rec.runUpMax = runUp; m_rec.swashColumns = swash; m_rec.swashEtaMax = swashEta; m_rec.meanFreeRise = riseN ? static_cast<float>(riseSum / riseN) : 0.0f; m_rec.runUpPeak = std::max(m_rec.runUpPeak, runUp); m_rec.foamMax = foam;
    m_rec.fieldMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void ShoreBand::clear() {
    m_solver.reset();
    m_roles.clear(); m_stillOfColumn.clear(); m_weights.clear(); m_dirty.clear();
    m_field = WaterSurfaceField{};
    const uint64_t rev = m_rec.revision;
    m_rec = ShoreBandRecord{};
    m_rec.revision = rev + 1;
}

std::pair<glm::ivec3, glm::ivec3> ShoreBand::maskBox() const {
    const int x0 = floorToInt(m_rec.originXZ.x), z0 = floorToInt(m_rec.originXZ.y);
    const int x1 = floorToInt(m_rec.originXZ.x + m_rec.n * m_rec.h - 1e-3f), z1 = floorToInt(m_rec.originXZ.y + m_rec.n * m_rec.h - 1e-3f);
    return {glm::ivec3(x0, 0, z0), glm::ivec3(x1, 0, z1)};
}

}  // namespace Phyxel::Core::Water
