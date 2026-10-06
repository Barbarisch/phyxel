// DebrisContactOccupancyTest — debris contact through the packed pool equals the same contact
// through the source VoxelOccupancyGrids (DebrisInteractionPlan 1c step 6).
//
// The debris twin of the lighting equality test. GPU debris reads the packed micro pool; CPU
// physics reads the grids directly (queryAABB). If the packing ever disagrees with its source, a
// body rests on air the character walks through, or sinks into a floor the character stands on.
// The oracle below answers from the grid's own physics query, never from the packing code, so a
// packing bug cannot hide by being on both sides.
//
// Sampled where packing is hardest: across chunk seams (all three axes), around mixed cubes, at
// negative coordinates, and next to absent chunks (both sides must say UNKNOWN).

#include <gtest/gtest.h>

#include <bitset>
#include <map>
#include <random>

#include "core/DebrisContact.h"
#include "graphics/VoxelLightOccupancy.h"
#include "physics/VoxelOccupancyGrid.h"

using namespace Phyxel;
using DebrisContact::CubeState;
using Graphics::PackedOccupancyPool;
using Physics::VoxelOccupancyGrid;

namespace {

int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

struct KeyLess {
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    }
};

/// Source of truth: the grids themselves, queried the way CPU physics queries them (queryAABB).
/// Each cube is queried ONCE and its boxes rasterized to 729 micro bits (cached): per-sample
/// queries made the test crawl in Debug.
struct GridOracle {
    std::map<glm::ivec3, const VoxelOccupancyGrid*, KeyLess> byOrigin;
    struct CubeBits { bool known = false; std::bitset<729> bits; };
    mutable std::map<glm::ivec3, CubeBits, KeyLess> cache;

    const VoxelOccupancyGrid* gridFor(const glm::ivec3& wc) const {
        const glm::ivec3 o(floorDiv(wc.x, 32) * 32, floorDiv(wc.y, 32) * 32, floorDiv(wc.z, 32) * 32);
        auto it = byOrigin.find(o);
        return it == byOrigin.end() ? nullptr : it->second;
    }
    const CubeBits& bitsFor(const glm::ivec3& wc) const {
        auto it = cache.find(wc);
        if (it != cache.end()) return it->second;
        CubeBits cb;
        if (const VoxelOccupancyGrid* g = gridFor(wc)) {
            cb.known = true;
            std::vector<Physics::OccupiedBox> boxes;
            g->queryAABB(glm::vec3(wc) + 0.001f, glm::vec3(wc) + 0.999f, boxes);
            for (int m = 0; m < 729; ++m) {
                const glm::vec3 c = glm::vec3(wc) + (glm::vec3(m / 81, (m / 9) % 9, m % 9) + 0.5f) / 9.0f;
                for (const auto& b : boxes) {
                    const glm::vec3 lo = b.center - b.halfExtents, hi = b.center + b.halfExtents;
                    if (glm::all(glm::greaterThan(c, lo)) && glm::all(glm::lessThan(c, hi))) { cb.bits.set(m); break; }
                }
            }
        }
        return cache.emplace(wc, cb).first->second;
    }
    bool micro(const glm::ivec3& wm) const {
        const glm::ivec3 wc(floorDiv(wm.x, 9), floorDiv(wm.y, 9), floorDiv(wm.z, 9));
        const glm::ivec3 r = wm - wc * 9;
        return bitsFor(wc).bits.test(r.x * 81 + r.y * 9 + r.z);
    }
    CubeState cube(const glm::ivec3& wc) const {
        const CubeBits& cb = bitsFor(wc);
        if (!cb.known) return CubeState::Unknown;
        const size_t solid = cb.bits.count();
        return solid == 0 ? CubeState::Empty : solid == 729 ? CubeState::Solid : CubeState::Mixed;
    }
    DebrisContact::PointContact at(const glm::vec3& x, float margin) const {
        return DebrisContact::pointContact(
            x, margin, [&](const glm::ivec3& c) { return cube(c); },
            [&](const glm::ivec3& m) { return micro(m); });
    }
};

struct PoolView {
    PackedOccupancyPool packed;
    CubeState cube(const glm::ivec3& wc) const {
        if (Graphics::packedPoolOccupancyState(packed, wc * 9) == Graphics::OccupancyState::Unknown)
            return CubeState::Unknown;
        switch (Graphics::packedPoolCubeOccupancy(packed, wc)) {
            case Graphics::CubeOccupancy::Solid: return CubeState::Solid;
            case Graphics::CubeOccupancy::Mixed: return CubeState::Mixed;
            default:                             return CubeState::Empty;
        }
    }
    bool micro(const glm::ivec3& wm) const { return Graphics::packedPoolSolidAt(packed, wm); }
    DebrisContact::PointContact at(const glm::vec3& x, float margin) const {
        return DebrisContact::pointContact(
            x, margin, [&](const glm::ivec3& c) { return cube(c); },
            [&](const glm::ivec3& m) { return micro(m); });
    }
};

/// Fill micro cells of world cube wc (in grid g) from a predicate over in-cube micro coords.
template <class Pred>
void microCells(VoxelOccupancyGrid& g, const glm::ivec3& wc, Pred pred) {
    const glm::ivec3 lp = wc - g.getChunkOrigin();
    for (int mx = 0; mx < 9; ++mx) for (int my = 0; my < 9; ++my) for (int mz = 0; mz < 9; ++mz) {
        if (!pred(mx, my, mz)) continue;
        const glm::ivec3 sp(mx / 3, my / 3, mz / 3), mp(mx % 3, my % 3, mz % 3);
        g.setCube(lp, true);
        g.markSubdivided(lp, true);
        g.setSubcube(lp, sp, true);
        g.markSubcubeSubdivided(lp, sp, true);
        g.setMicrocube(lp, sp, mp, true);
    }
}

/// A 2x2x2 block of chunks around the world origin (so x,y,z all cross 0 and a chunk seam),
/// minus one chunk left ABSENT (unknown occupancy), with a floor, sub-voxel steps and fences
/// straddling every seam.
struct World {
    std::vector<std::unique_ptr<VoxelOccupancyGrid>> grids;
    GridOracle oracle;
    PoolView pool;

    World() {
        std::mt19937 rng(11);
        std::uniform_int_distribution<int> pick(0, 5);
        for (int cx = -1; cx <= 0; ++cx) for (int cy = -1; cy <= 0; ++cy) for (int cz = -1; cz <= 0; ++cz) {
            if (cx == 0 && cy == 0 && cz == -1) continue;             // the absent chunk
            auto g = std::make_unique<VoxelOccupancyGrid>();
            g->setChunkOrigin({cx * 32, cy * 32, cz * 32});
            grids.push_back(std::move(g));
        }
        auto gridAt = [&](const glm::ivec3& wc) -> VoxelOccupancyGrid* {
            for (auto& g : grids) {
                const glm::ivec3 o = g->getChunkOrigin();
                if (glm::all(glm::greaterThanEqual(wc, o)) && glm::all(glm::lessThan(wc, o + 32))) return g.get();
            }
            return nullptr;
        };
        // The region every sample sits in: world cubes -6..5 on each axis, straddling the seams.
        for (int x = -6; x < 6; ++x) for (int y = -6; y < 6; ++y) for (int z = -6; z < 6; ++z) {
            const glm::ivec3 wc(x, y, z);
            VoxelOccupancyGrid* g = gridAt(wc);
            if (!g) continue;
            const glm::ivec3 lp = wc - g->getChunkOrigin();
            if (y < -2) { g->setCube(lp, true); continue; }               // solid floor below y=-2
            switch (pick(rng)) {
                case 0: g->setCube(lp, true); break;                      // full cube
                case 1: microCells(*g, wc, [](int, int my, int) { return my < 3; }); break;   // 1/3 slab
                case 2: microCells(*g, wc, [](int mx, int, int) { return mx < 2; }); break;   // fence
                case 3: microCells(*g, wc, [](int mx, int my, int mz) { return (mx + my + mz) % 4 == 0; }); break;
                default: break;                                           // air
            }
        }
        std::vector<std::pair<glm::ivec3, Graphics::ChunkLightOccupancy>> blobs;
        for (auto& g : grids) {
            oracle.byOrigin[g->getChunkOrigin()] = g.get();
            blobs.push_back({g->getChunkOrigin(), Graphics::buildLightOccupancy(*g)});
        }
        pool.packed = Graphics::packOccupancyPool(blobs, PackedOccupancyPool::boxMinChunkFor(glm::vec3(0.0f)));
    }
};

}  // namespace

// Cell-level agreement first: if this fails, the contact comparison below is moot.
TEST(DebrisContactOccupancy, PoolCellStatesEqualTheGridsAcrossSeamsAndNegatives) {
    World w;
    int mixed = 0, unknown = 0;
    for (int x = -7; x < 7; ++x) for (int y = -7; y < 7; ++y) for (int z = -7; z < 7; ++z) {
        const glm::ivec3 wc(x, y, z);
        const CubeState want = w.oracle.cube(wc), got = w.pool.cube(wc);
        ASSERT_EQ(static_cast<int>(got), static_cast<int>(want))
            << "cube (" << x << "," << y << "," << z << ")";
        mixed += want == CubeState::Mixed;
        unknown += want == CubeState::Unknown;
        if (want != CubeState::Mixed) continue;
        for (int m = 0; m < 729; ++m) {
            const glm::ivec3 wm = wc * 9 + glm::ivec3(m / 81, (m / 9) % 9, m % 9);
            ASSERT_EQ(w.pool.micro(wm), w.oracle.micro(wm))
                << "micro (" << wm.x << "," << wm.y << "," << wm.z << ") in cube (" << x << "," << y << "," << z << ")";
        }
    }
    EXPECT_GT(mixed, 100) << "the world must exercise mixed cubes";
    EXPECT_GT(unknown, 100) << "the world must exercise the absent chunk";
}

// THE contract: every contact the debris solver would compute is the same through either source.
TEST(DebrisContactOccupancy, ContactThroughThePoolEqualsContactThroughTheGrids) {
    World w;
    std::mt19937 rng(23);
    std::uniform_real_distribution<float> pos(-5.0f, 5.0f);
    int hits = 0, micro = 0, unknown = 0;
    for (int i = 0; i < 6000; ++i) {
        const glm::vec3 x(pos(rng), pos(rng), pos(rng));
        for (float margin : {0.02f, 0.0f}) {
            const auto want = w.oracle.at(x, margin), got = w.pool.at(x, margin);
            const std::string where = "at (" + std::to_string(x.x) + "," + std::to_string(x.y) + "," +
                                      std::to_string(x.z) + ") margin " + std::to_string(margin);
            ASSERT_EQ(got.unknown, want.unknown) << where;
            ASSERT_EQ(got.hit, want.hit) << where;
            ASSERT_EQ(got.micro, want.micro) << where;
            unknown += want.unknown;
            if (!want.hit || want.unknown) continue;
            ASSERT_EQ(got.dirIdx, want.dirIdx) << where;
            ASSERT_EQ(got.pen, want.pen) << where;
            ASSERT_EQ(got.n, want.n) << where;
            ++hits;
            micro += want.micro;
        }
    }
    EXPECT_GT(hits, 1000) << "the sample must exercise contacts";
    EXPECT_GT(micro, 200) << "the sample must exercise the micro search";
    EXPECT_GT(unknown, 100) << "the sample must reach the absent chunk";
}

// CONTROL: a pool packed from a DIFFERENT world must disagree, or the comparison above would pass
// for any pool at all.
TEST(DebrisContactOccupancy, ControlAPoolFromAnotherWorldDisagrees) {
    World w;
    VoxelOccupancyGrid other;
    other.setChunkOrigin({0, 0, 0});
    std::vector<std::pair<glm::ivec3, Graphics::ChunkLightOccupancy>> blobs;
    for (auto& g : w.grids) {
        if (g->getChunkOrigin() == glm::ivec3(0, 0, 0))
            blobs.push_back({g->getChunkOrigin(), Graphics::buildLightOccupancy(other)});   // emptied
        else
            blobs.push_back({g->getChunkOrigin(), Graphics::buildLightOccupancy(*g)});
    }
    PoolView wrong{Graphics::packOccupancyPool(blobs, PackedOccupancyPool::boxMinChunkFor(glm::vec3(0.0f)))};
    int differ = 0;
    for (int x = 0; x < 6; ++x) for (int y = 0; y < 6; ++y) for (int z = 0; z < 6; ++z)
        differ += wrong.cube({x, y, z}) != w.oracle.cube({x, y, z});
    EXPECT_GT(differ, 20);
}
