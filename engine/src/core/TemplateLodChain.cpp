#include "core/TemplateLodChain.h"

#include "core/PlacedObjectManager.h"   // full InteractionPointDef for VoxelTemplate's vector
#include "core/VoxelTemplate.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Phyxel {
namespace Core {

constexpr int TemplateLodChain::kCellSizesMicros[6];

namespace {

/// Packed 3D key: 21 bits per axis, offset so template-local negatives fit.
inline uint64_t key3(int x, int y, int z) {
    constexpr int kOff = 1 << 20;
    return (uint64_t(uint32_t(x + kOff)) << 42) |
           (uint64_t(uint32_t(y + kOff)) << 21) |
            uint64_t(uint32_t(z + kOff));
}

inline int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

struct Accum {
    int total = 0;                       ///< occupied input micros in this cell
    int trunk = 0;                       ///< of which trunk-class (Log*)
    int exposed = 0;                     ///< of which air-exposed (>=1 of 6 neighbors empty)
    std::unordered_map<uint16_t, int> perMat;
};

/// Decode helper (keys back to coords).
inline void decode3(uint64_t k, int& x, int& y, int& z) {
    constexpr int kOff = 1 << 20;
    x = int((k >> 42) & 0x1FFFFF) - kOff;
    y = int((k >> 21) & 0x1FFFFF) - kOff;
    z = int(k & 0x1FFFFF) - kOff;
}

/// The shared operator core: micro raster -> levels, per the config.
std::vector<TemplateLodChain::Level> buildCore(
    const std::unordered_map<uint64_t, uint16_t>& micro,
    const std::vector<std::string>& matTable,
    const TemplateLodChain::Config& cfg) {

    std::vector<uint8_t> isTrunk(matTable.size(), 0);
    for (size_t i = 0; i < matTable.size(); ++i)
        if (matTable[i].rfind("Log", 0) == 0) isTrunk[i] = 1;

    // Exposure (computed ONCE on the input raster): a micro with any of its 6 face
    // neighbors empty is part of the subject's visible surface. Cells carrying exposed
    // micros are what shell protection keeps and what interior hollowing spares.
    std::unordered_set<uint64_t> exposedSet;
    if (cfg.protectExposedShell || cfg.hollowInterior) {
        exposedSet.reserve(micro.size() / 2);
        static const int kD[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const auto& [k, m] : micro) {
            int x, y, z;
            decode3(k, x, y, z);
            for (const auto& d : kD) {
                if (!micro.count(key3(x + d[0], y + d[1], z + d[2]))) {
                    exposedSet.insert(k);
                    break;
                }
            }
        }
    }

    std::vector<TemplateLodChain::Level> levels;
    levels.reserve(cfg.cellSizesMicros.size());

    for (int c : cfg.cellSizesMicros) {
        const int cellVol = c * c * c;

        std::unordered_map<uint64_t, Accum> cells;
        cells.reserve(micro.size() / std::max(1, cellVol / 4));
        for (const auto& [k, m] : micro) {
            int x, y, z;
            decode3(k, x, y, z);
            Accum& a = cells[key3(floorDiv(x, c), floorDiv(y, c), floorDiv(z, c))];
            a.total += 1;
            a.trunk += isTrunk[m];
            a.exposed += exposedSet.count(k) ? 1 : 0;
            a.perMat[m] += 1;
        }

        // Emission is VOLUME-CONSERVING: protected cells always emit (trunk stems for
        // trees, exposed-shell cells for structures — the surfaces that DEFINE the
        // subject must survive), then ordinary candidates in descending-coverage order
        // until the level's represented volume reaches input x volumeBudgetFactor.
        // Sparse fringe cells are shed first, so coarse levels cannot fatten (the
        // OR-squash defect). Material is the MAJORITY of the cell's occupied micros —
        // never OR-promotion; trunk-rescued cells render as their trunk material.
        struct Cand {
            uint64_t k;
            const Accum* a;
            float coverage;
            bool protected_;    ///< exempt from the volume budget
            bool subFloor;      ///< below the ordinary coverage floor (trunk-rescued only)
        };
        std::vector<Cand> cands;
        cands.reserve(cells.size());
        for (const auto& [k, a] : cells) {
            // Interior hollowing: a cell with NO exposed micros is invisible from outside
            // (room fill, solid cores). Dropping it wholesale is the structure-LOD win —
            // walls/roofs survive as a shell while the box empties out.
            if (cfg.hollowInterior && a.exposed == 0) continue;

            const float coverage = float(a.total) / float(cellVol);
            const float trunkCov = float(a.trunk) / float(cellVol);
            const bool ordinary  = coverage >= cfg.coverageFloor;
            // PROTECTED: trunk mass (a thin stem loses every coverage contest — measured
            // amputation at c=18 without this) or exposed shell content (a 1-voxel wall
            // in a 2-3 voxel cell is 33-11% coverage — below any sane floor — yet it IS
            // the building; eroding it opens holes in facades).
            const bool protected_ =
                (cfg.protectTrunk && trunkCov >= cfg.trunkFloor) ||
                (cfg.protectExposedShell && a.exposed > 0);
            if (!ordinary && !protected_) continue;
            cands.push_back({k, &a, coverage, protected_, !ordinary});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            if (a.protected_ != b.protected_) return a.protected_;   // protected first, always
            if (a.coverage != b.coverage) return a.coverage > b.coverage;
            return a.k < b.k;                                        // deterministic tie-break
        });

        const size_t volumeBudget =
            size_t(double(micro.size()) * double(cfg.volumeBudgetFactor));

        TemplateLodChain::Level level;
        level.cellSizeMicros = c;
        std::unordered_map<uint64_t, size_t> emitted;   // key -> index into level.cells
        size_t levelVolume = 0;
        for (const Cand& cand : cands) {
            if (!cand.protected_ && levelVolume + size_t(cellVol) > volumeBudget) break;

            uint16_t best = 0;
            int bestCount = -1;
            for (const auto& [m, count] : cand.a->perMat) {
                // A trunk-rescued sub-floor cell exists only to carry the stem: it must
                // render as trunk. (Shell-protected cells use plain majority — a window
                // cell renders as glass, not as the wall around it.)
                if (cand.subFloor && cfg.protectTrunk && !cfg.protectExposedShell &&
                    !isTrunk[m]) continue;
                if (count > bestCount || (count == bestCount && m < best)) {
                    best = m;
                    bestCount = count;
                }
            }
            if (bestCount < 0) continue;

            int x, y, z;
            decode3(cand.k, x, y, z);
            emitted.emplace(cand.k, level.cells.size());
            level.cells.push_back({glm::ivec3(x, y, z), matTable[best]});
            level.occupiedMicroVolume += size_t(cand.a->total);
            // Protected cells don't charge the budget: at extreme coarseness a thin
            // trunk's (or a large facade's) cells can exceed the whole subject's input
            // volume, and charging them starved the ordinary mass to zero (a bare stick
            // at 2 km, measured). The budget exists to stop INTERIOR mass fattening,
            // and that it still does.
            if (!cand.protected_) levelVolume += size_t(cellVol);
        }

        // ---- Island cull: no floating debris, by construction. ---------------------------
        // 26-connected components below the debris threshold are dropped — the exact defect
        // class the user rejected in the chunk-squash ("weird floating voxels").
        if (!level.cells.empty()) {
            const size_t minComponent = std::max<size_t>(
                2, cfg.islandCullDivisor > 0 ? level.cells.size() / cfg.islandCullDivisor : 0);
            std::vector<int> comp(level.cells.size(), -1);
            int nComp = 0;
            std::vector<size_t> compSize;
            for (size_t i = 0; i < level.cells.size(); ++i) {
                if (comp[i] >= 0) continue;
                std::vector<size_t> stack{i};
                comp[i] = nComp;
                size_t size = 0;
                while (!stack.empty()) {
                    const size_t cur = stack.back();
                    stack.pop_back();
                    ++size;
                    const glm::ivec3 p = level.cells[cur].pos;
                    for (int dx = -1; dx <= 1; ++dx)
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!dx && !dy && !dz) continue;
                                auto it = emitted.find(key3(p.x + dx, p.y + dy, p.z + dz));
                                if (it == emitted.end()) continue;
                                if (comp[it->second] >= 0) continue;
                                comp[it->second] = nComp;
                                stack.push_back(it->second);
                            }
                }
                compSize.push_back(size);
                ++nComp;
            }
            std::vector<TemplateLodChain::Cell> kept;
            kept.reserve(level.cells.size());
            for (size_t i = 0; i < level.cells.size(); ++i)
                if (compSize[size_t(comp[i])] >= minComponent)
                    kept.push_back(std::move(level.cells[i]));
            level.cells = std::move(kept);
        }

        // Deterministic order.
        std::sort(level.cells.begin(), level.cells.end(),
                  [](const TemplateLodChain::Cell& a, const TemplateLodChain::Cell& b) {
                      if (a.pos.y != b.pos.y) return a.pos.y < b.pos.y;
                      if (a.pos.z != b.pos.z) return a.pos.z < b.pos.z;
                      return a.pos.x < b.pos.x;
                  });

        levels.push_back(std::move(level));
    }

    return levels;
}

} // namespace

TemplateLodChain::Config TemplateLodChain::treeConfig() {
    Config cfg;
    cfg.cellSizesMicros.assign(std::begin(kCellSizesMicros), std::end(kCellSizesMicros));
    cfg.coverageFloor      = kCoverageFloor;
    cfg.trunkFloor         = kTrunkFloor;
    cfg.volumeBudgetFactor = kVolumeBudgetFactor;
    cfg.protectTrunk        = true;
    cfg.protectExposedShell = false;
    cfg.hollowInterior      = false;
    cfg.islandCullDivisor   = 50;
    return cfg;
}

TemplateLodChain::Config TemplateLodChain::structureConfig() {
    Config cfg;
    // Full 6-level ladder (~1.4-1.5x per step), starting at SUBCUBE resolution so trim and
    // roof courses stay readable at the handoff and stopping at 3-voxel cells — a house at
    // 2 km is a card/far-tile concern, not a mesh one. Densified from {3,9,18,27} on
    // 2026-08-05 (user: "way more than 3 levels of LOD") — the old 4-level ladder skipped
    // 3x cell-volume jumps that read as visible pops even under the dither.
    cfg.cellSizesMicros = {3, 6, 9, 13, 18, 27};
    cfg.coverageFloor      = 0.10f;
    cfg.volumeBudgetFactor = 1.30f;
    cfg.protectTrunk        = false;   // no stems in buildings
    cfg.protectExposedShell = true;    // walls/roofs must never erode into holes
    cfg.hollowInterior      = true;    // rooms are invisible at range — drop the fill
    cfg.islandCullDivisor   = 0;       // fences/wells are legitimate small islands;
                                       // only single-cell floaters die
    return cfg;
}

std::vector<TemplateLodChain::Level> TemplateLodChain::build(const VoxelTemplate& t) {
    return build(t, treeConfig());
}

std::vector<TemplateLodChain::Level> TemplateLodChain::build(const VoxelTemplate& t,
                                                             const Config& cfg) {
    // ---- Rasterize the template to a micro-resolution occupancy map. ---------------------
    // Finest authored unit is the microcube: 9 per voxel axis (3 subcubes x 3 micros).
    // Later writes override earlier ones (cube -> subcube -> microcube), matching stamping.
    std::vector<std::string> matTable;
    std::unordered_map<std::string, uint16_t> matIndex;
    auto matId = [&](const std::string& m) -> uint16_t {
        auto it = matIndex.find(m);
        if (it != matIndex.end()) return it->second;
        const uint16_t id = uint16_t(matTable.size());
        matTable.push_back(m);
        matIndex.emplace(m, id);
        return id;
    };

    std::unordered_map<uint64_t, uint16_t> micro;
    micro.reserve(t.cubes.size() * 729 + t.subcubes.size() * 27 + t.microcubes.size());

    for (const auto& c : t.cubes) {
        const glm::ivec3 base = c.relativePos * 9;
        const uint16_t m = matId(c.material);
        for (int x = 0; x < 9; ++x)
            for (int y = 0; y < 9; ++y)
                for (int z = 0; z < 9; ++z)
                    micro[key3(base.x + x, base.y + y, base.z + z)] = m;
    }
    for (const auto& s : t.subcubes) {
        const glm::ivec3 base = s.parentRelativePos * 9 + s.subcubePos * 3;
        const uint16_t m = matId(s.material);
        for (int x = 0; x < 3; ++x)
            for (int y = 0; y < 3; ++y)
                for (int z = 0; z < 3; ++z)
                    micro[key3(base.x + x, base.y + y, base.z + z)] = m;
    }
    for (const auto& mc : t.microcubes) {
        const glm::ivec3 base =
            mc.parentRelativePos * 9 + mc.subcubePos * 3 + mc.microcubePos;
        micro[key3(base.x, base.y, base.z)] = matId(mc.material);
    }

    return buildCore(micro, matTable, cfg);
}

std::vector<TemplateLodChain::Level> TemplateLodChain::buildFromSoup(const MicroSoup& soup,
                                                                     const Config& cfg) {
    std::unordered_map<uint64_t, uint16_t> micro;
    micro.reserve(soup.micros.size());
    for (const auto& [p, m] : soup.micros) micro[key3(p.x, p.y, p.z)] = m;
    return buildCore(micro, soup.materials, cfg);
}

} // namespace Core
} // namespace Phyxel
