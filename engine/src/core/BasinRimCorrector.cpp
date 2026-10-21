#include "core/BasinRimCorrector.h"

#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <unordered_map>

namespace Phyxel {

namespace {
thread_local int t_inCorrection = 0;
struct CorrectionGuard {
    CorrectionGuard() { ++t_inCorrection; }
    ~CorrectionGuard() { --t_inCorrection; }
};
}  // namespace

bool BasinRimCorrector::inCorrection() { return t_inCorrection > 0; }

BasinRimCorrector::BasinRimCorrector(std::shared_ptr<const HydrologyMap> hydro,
                                     std::shared_ptr<const WaterBodyIndex> bodies, float seaLevel)
    : m_hydro(std::move(hydro)), m_bodies(std::move(bodies)), m_seaLevel(seaLevel) {
    const size_t n = m_bodies ? m_bodies->bodies().size() : 0;
    m_state.reserve(n);
    for (size_t i = 0; i < n; ++i) m_state.push_back(std::make_unique<State>());
}

bool BasinRimCorrector::levelIfComputed(int32_t bodyId, float& out) const {
    if (bodyId < 0 || static_cast<size_t>(bodyId) >= m_state.size()) return false;
    const State& s = *m_state[static_cast<size_t>(bodyId)];
    if (!s.done.load(std::memory_order_acquire)) return false;
    out = s.level;
    return true;
}

BasinRimCorrector::Stats BasinRimCorrector::stats() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_stats;
}

float BasinRimCorrector::level(int32_t bodyId, const SurfaceYFn& surfaceYAt) {
    if (!m_hydro || !m_bodies || bodyId < 0 || static_cast<size_t>(bodyId) >= m_state.size())
        return HydrologyMap::NO_WATER;
    State& s = *m_state[static_cast<size_t>(bodyId)];
    if (s.done.load(std::memory_order_acquire)) return s.level;

    std::lock_guard<std::mutex> lk(m_mutex);
    if (s.done.load(std::memory_order_relaxed)) return s.level;

    const WaterBodyIndex::Body& b = m_bodies->bodies()[static_cast<size_t>(bodyId)];
    float L = b.level;
    const float L0 = L;
    // The sea's level is the sea's business (and the inland sea's ring is the whole coast).
    if (b.cls == WaterBodyIndex::Class::Ocean || L <= m_seaLevel + 1.0f || !surfaceYAt) {
        s.level = L;
        s.done.store(true, std::memory_order_release);
        return L;
    }

    CorrectionGuard guard;
    const HydrologyMap& h = *m_hydro;
    const int   nx = h.cellsX(), nz = h.cellsZ();
    const float cs = h.cellSize();
    const int   csI = std::max(1, static_cast<int>(std::lround(cs)));
    const float ox = h.originX(), oz = h.originZ();
    auto wetOfThisBody = [&](int i, int j, float lvl) {
        if (i < 0 || j < 0 || i >= nx || j >= nz) return false;
        return m_bodies->bodyIdAtCell(i, j) == bodyId && h.baseAtCell(i, j) < lvl;
    };
    auto wetOfAnother = [&](int i, int j) {
        if (i < 0 || j < 0 || i >= nx || j >= nz) return true;   // outside the box: not a rim we can see
        const int32_t id = m_bodies->bodyIdAtCell(i, j);
        return id >= 0 && id != bodyId && h.levelAtCell(i, j) > HydrologyMap::NO_WATER * 0.5f;
    };
    std::unordered_map<long long, int> colCache;
    auto surfAt = [&](int x, int z) {
        const long long key = (static_cast<long long>(x) << 32) ^ static_cast<long long>(static_cast<unsigned int>(z));
        auto it = colCache.find(key);
        if (it != colCache.end()) return it->second;
        const int sy = surfaceYAt(x, z);
        colCache.emplace(key, sy);
        return sy;
    };
    static const int NI[4] = {1, -1, 0, 0}, NJ[4] = {0, 0, 1, -1};
    int iterations = 0;
    long cols = 0;
    bool anyWet = false;
    for (; iterations < kMaxIterations; ++iterations) {
        int ringMin = INT_MAX;
        anyWet = false;
        for (int j = b.bboxMin.y; j <= b.bboxMax.y; ++j)
            for (int i = b.bboxMin.x; i <= b.bboxMax.x; ++i) {
                if (!wetOfThisBody(i, j, L)) continue;
                anyWet = true;
                for (int k = 0; k < 4; ++k) {
                    const int ni = i + NI[k], nj = j + NJ[k];
                    if (wetOfThisBody(ni, nj, L) || wetOfAnother(ni, nj)) continue;
                    // The dry neighbour's row of columns along the shared edge (the validator's rim).
                    const int x0 = static_cast<int>(std::floor(ox)) + ni * csI, z0 = static_cast<int>(std::floor(oz)) + nj * csI;
                    for (int t = 0; t < csI; ++t) {
                        int x, z;
                        if (NI[k] == 1)       { x = x0;           z = z0 + t; }   // east neighbour: its west column
                        else if (NI[k] == -1) { x = x0 + csI - 1; z = z0 + t; }   // west neighbour: its east column
                        else if (NJ[k] == 1)  { x = x0 + t;       z = z0; }       // south neighbour: its north row
                        else                  { x = x0 + t;       z = z0 + csI - 1; }
                        ringMin = std::min(ringMin, surfAt(x, z));
                        ++cols;
                    }
                }
            }
        if (!anyWet || ringMin == INT_MAX) break;
        if (std::floor(L) <= static_cast<float>(ringMin)) break;   // no rim column below the level: done
        L = static_cast<float>(ringMin) + kRimMargin;               // floor(L) == ringMin
    }
    s.level = L;
    s.done.store(true, std::memory_order_release);
    ++m_stats.bodiesCorrected;
    if (L < L0) ++m_stats.bodiesLowered;
    if (!anyWet) ++m_stats.bodiesDried;
    m_stats.ringColumns += cols;
    m_stats.maxIterations = std::max(m_stats.maxIterations, iterations);
    m_stats.maxDrop = std::max(m_stats.maxDrop, L0 - L);
    return L;
}

float BasinRimCorrector::waterLevelAt(float worldX, float worldZ, const SurfaceYFn& surfaceYAt) {
    if (!m_hydro) return HydrologyMap::NO_WATER;
    const float raw = m_hydro->waterLevelAt(worldX, worldZ);
    if (raw <= HydrologyMap::NO_WATER * 0.5f || !m_bodies || inCorrection()) return raw;
    const int32_t id = m_bodies->bodyIdAt(worldX, worldZ);
    if (id < 0) return raw;
    const float L = level(id, surfaceYAt);
    if (L >= raw) return raw;
    return m_hydro->baseAt(worldX, worldZ) < L ? L : HydrologyMap::NO_WATER;
}

}  // namespace Phyxel
