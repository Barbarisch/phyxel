#include "core/LodBrick.h"

#include <algorithm>
#include <map>

namespace Phyxel {
namespace Core {

const LodCell LodVolume::s_empty{};

LodVolume::LodVolume(glm::ivec3 dim, int level)
    : m_dim(glm::max(dim, glm::ivec3(0))), m_level(level) {
    m_cells.assign(static_cast<size_t>(m_dim.x) * m_dim.y * m_dim.z, LodCell{});
}

const LodCell& LodVolume::atClamped(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return s_empty;
    return m_cells[index(x, y, z)];
}

uint64_t LodVolume::totalCoverage() const {
    uint64_t t = 0;
    for (const auto& c : m_cells) t += c.coverage;
    return t;
}

size_t LodVolume::solidCellCount() const {
    return static_cast<size_t>(std::count_if(m_cells.begin(), m_cells.end(),
                                             [](const LodCell& c) { return c.solid(); }));
}

namespace {

/// Exposed faces of a cell against its 6 neighbours, weighted by how solid the
/// cell itself is. Used by the surface-area material vote.
uint32_t exposedFaceWeight(const LodVolume& v, int x, int y, int z) {
    static const int kOff[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    uint32_t faces = 0;
    for (const auto& o : kOff) {
        if (!v.atClamped(x + o[0], y + o[1], z + o[2]).solid()) ++faces;
    }
    return faces;
}

} // namespace

LodVolume squash(const LodVolume& src, const SquashConfig& cfg) {
    const glm::ivec3 d = src.dim();
    const glm::ivec3 nd((d.x + 1) / 2, (d.y + 1) / 2, (d.z + 1) / 2);
    LodVolume out(nd, src.level() + 1);
    if (src.empty()) return out;

    // Full coverage of ONE parent cell. MUST scale with the source level: a
    // level-L cell already holds ACCUMULATED coverage (729 * 8^L at full), so a
    // fixed 729*8 denominator only happens to be right for L=0 and silently
    // makes HalfThreshold pass 12.5%-solid volumes at deeper levels.
    // (Real defect found by solution-auditor 2026-07-29; pinned by
    // HalfThresholdRejectsSparseVolumeAtDepth.)
    const uint64_t parentFull =
        static_cast<uint64_t>(LodVolume::kFullCoverage) << (3u * (src.level() + 1));

    for (int x = 0; x < nd.x; ++x)
    for (int y = 0; y < nd.y; ++y)
    for (int z = 0; z < nd.z; ++z) {
        uint64_t cov = 0;
        uint64_t openCov = 0;
        bool anyOpening = false;
        // material -> accumulated weight, for both vote flavours
        std::map<uint16_t, uint64_t> volumeWeight;
        std::map<uint16_t, uint64_t> surfaceWeight;

        for (int dx = 0; dx < 2; ++dx)
        for (int dy = 0; dy < 2; ++dy)
        for (int dz = 0; dz < 2; ++dz) {
            const int cx = x * 2 + dx, cy = y * 2 + dy, cz = z * 2 + dz;
            if (!src.inBounds(cx, cy, cz)) continue;
            const LodCell& c = src.at(cx, cy, cz);
            anyOpening = anyOpening || c.preserveOpening;
            // Opening volume accumulates from EVERY child, solid or not: a fully-carved
            // child cell is exactly where the void lives.
            openCov += c.openingCoverage;
            if (!c.solid()) continue;
            cov += c.coverage;
            volumeWeight[c.bulkMaterial] += c.coverage;
            const uint32_t faces = exposedFaceWeight(src, cx, cy, cz);
            if (faces > 0) surfaceWeight[c.skinMaterial] += faces;
        }

        LodCell& p = out.at(x, y, z);
        p.preserveOpening = anyOpening;
        // Conserved upward regardless of the occupancy rule, so the mask survives even
        // when the rule itself would have blanked the cell.
        p.openingCoverage = openCov;

        bool solid = false;
        switch (cfg.occupancy) {
            case OccupancyRule::Or:
                solid = cov > 0;
                break;
            case OccupancyRule::HalfThreshold:
                solid = cov * 2 >= parentFull;
                break;
            case OccupancyRule::OrWithOpeningMask:
                // The cell stays solid; the opening is carried as `openingCoverage` for the
                // renderer to act on. Nothing is deleted, so over-carve is 0 by construction.
                solid = cov > 0;
                break;
            case OccupancyRule::OrPreserveOpenings:
                // OR, except a deliberate opening WINS over the merge. Without
                // this, any door/window narrower than a parent cell is filled in
                // by a solid sibling and the building reads as windowless at
                // distance. The cost is honest and deliberate: the hole becomes
                // one coarse cell wide (bigger than the real door). Preserving
                // the READ of an opening matters more than its exact width.
                solid = (cov > 0) && !anyOpening;
                break;
        }
        if (!solid) { p.coverage = 0; continue; }

        p.coverage = cov;   // uint64_t: a static_cast<uint32_t> here truncated past level ~8

        auto argmax = [](const std::map<uint16_t, uint64_t>& m) -> uint16_t {
            uint16_t best = 0; uint64_t bw = 0;
            for (const auto& kv : m) {
                // ties break toward the lower palette index for determinism
                if (kv.second > bw) { bw = kv.second; best = kv.first; }
            }
            return best;
        };

        p.bulkMaterial = argmax(volumeWeight);
        // The skin vote weights by EXPOSED FACE COUNT, not volume: a plaster-
        // skinned stone wall is mostly stone by volume but reads as plaster,
        // because the skin is the only part a viewer ever sees (plan §2.2b).
        // Fall back to the volume vote when nothing is exposed (fully buried).
        if (cfg.material == MaterialRule::SurfaceAreaMajority && !surfaceWeight.empty()) {
            p.skinMaterial = argmax(surfaceWeight);
        } else {
            p.skinMaterial = p.bulkMaterial;
        }
    }
    return out;
}

std::vector<LodVolume> buildPyramid(const LodVolume& src, const SquashConfig& cfg) {
    std::vector<LodVolume> levels;
    levels.push_back(src);
    while (true) {
        const glm::ivec3 d = levels.back().dim();
        if (d.x <= 1 && d.y <= 1 && d.z <= 1) break;
        levels.push_back(squash(levels.back(), cfg));
    }
    return levels;
}

size_t countWatertightViolations(const LodVolume& fine, const LodVolume& coarse) {
    const int ratio = coarse.cellSizeInCubes() / std::max(1, fine.cellSizeInCubes());
    if (ratio <= 0) return 0;
    size_t violations = 0;
    const glm::ivec3 d = fine.dim();
    for (int x = 0; x < d.x; ++x)
    for (int y = 0; y < d.y; ++y)
    for (int z = 0; z < d.z; ++z) {
        if (!fine.at(x, y, z).solid()) continue;
        const int cx = x / ratio, cy = y / ratio, cz = z / ratio;
        const LodCell& c = coarse.atClamped(cx, cy, cz);
        // A DELIBERATE opening (door/window carved by OrPreserveOpenings) is a
        // hole on purpose, not a crack. Counting it would put two invariants in
        // direct conflict: "openings survive" would always fail "watertight".
        if (c.preserveOpening) continue;
        if (!c.solid()) ++violations;
    }
    return violations;
}

} // namespace Core
} // namespace Phyxel
