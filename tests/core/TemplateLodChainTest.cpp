// World Rendering v2, M1 (docs/WorldRenderV2Plan.md) — the template voxel mip chain and its
// tree-aware downsample operator. Every invariant here exists because the OR-occupancy squash
// was rejected by the user ("weird floating voxels"): these tests make the failure modes
// impossible by construction, not by tuning.
//
// RED before the feature: TemplateLodChain::build returns {}.

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>

#include "core/ObjectTemplateManager.h"
#include "core/TemplateLodChain.h"
#include "core/PlacedObjectManager.h"   // full InteractionPointDef (VoxelTemplate holds a vector of it)
#include "core/VoxelTemplate.h"

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

/// Synthetic oak: a 12-cube Log trunk with a 7x5x7 Leaf canopy of SUBCUBES (matching how
/// real templates author canopies at sub-voxel resolution), plus a couple of stray leaf
/// microcubes far from the tree — deliberate debris the island cull must remove.
VoxelTemplate syntheticOak() {
    VoxelTemplate t;
    t.name = "test_oak";
    for (int y = 0; y < 12; ++y)
        t.cubes.push_back({glm::ivec3(0, y, 0), "Log", 0, 0xFFFFFFu});
    for (int dx = -3; dx <= 3; ++dx)
        for (int dy = 0; dy < 5; ++dy)
            for (int dz = -3; dz <= 3; ++dz) {
                if (dx * dx + dz * dz > 11) continue;      // rounded canopy
                // ~50% sub-voxel foliage fill — representative of authored forge canopies.
                for (int s = 0; s < 3; ++s)
                    for (int sy = 0; sy < 3; ++sy)
                        for (int sz = 0; sz < 3; ++sz) {
                            if ((s + sy + sz + dx + dz) % 2 == 0) continue;
                            t.subcubes.push_back({glm::ivec3(dx, 10 + dy, dz),
                                                  glm::ivec3(s, sy, sz), "Leaf", 0,
                                                  0xFFFFFFu, 0});
                        }
            }
    // Debris: two isolated microcubes floating far from the tree.
    t.microcubes.push_back({glm::ivec3(6, 20, 6), glm::ivec3(0, 0, 0), glm::ivec3(1, 1, 1),
                            "Leaf", 0, 0xFFFFFFu, 0});
    t.microcubes.push_back({glm::ivec3(-6, 22, -6), glm::ivec3(2, 2, 2), glm::ivec3(0, 0, 0),
                            "Leaf", 0, 0xFFFFFFu, 0});
    return t;
}

size_t cellCount(const TemplateLodChain::Level& l) { return l.cells.size(); }

} // namespace

// THE POINT: a real chain exists and steps down smoothly — every coarser level has fewer
// cells, and no level collapses to nothing for a normal tree.
TEST(TemplateLodChainTest, LevelsShrinkMonotonicallyAndNeverVanish) {
    auto levels = TemplateLodChain::build(syntheticOak());
    ASSERT_EQ(levels.size(), size_t(TemplateLodChain::kLevelCount));
    for (size_t i = 0; i < levels.size(); ++i) {
        EXPECT_GT(cellCount(levels[i]), 0u) << "level " << i << " lost the tree entirely";
        if (i > 0)
            EXPECT_LT(cellCount(levels[i]), cellCount(levels[i - 1]))
                << "level " << i << " did not decimate";
    }
    // The finest derived level should still be recognisably dense (hundreds of cells for
    // this canopy), the coarsest a handful — the "1000 -> ... -> 50" arc the user asked for.
    EXPECT_GT(cellCount(levels.front()), 100u);
    EXPECT_LT(cellCount(levels.back()), 80u);
}

// Trunk preservation: every level keeps a Log-class stem from the base up to the canopy —
// a decimated tree must never become a floating canopy with no stick.
TEST(TemplateLodChainTest, TrunkSurvivesEveryLevel) {
    auto levels = TemplateLodChain::build(syntheticOak());
    ASSERT_FALSE(levels.empty());
    for (size_t i = 0; i < levels.size(); ++i) {
        const auto& l = levels[i];
        ASSERT_FALSE(l.cells.empty());
        int minY = INT_MAX, maxLogY = INT_MIN, minLogY = INT_MAX;
        for (const auto& c : l.cells) {
            minY = std::min(minY, c.pos.y);
            if (c.material.rfind("Log", 0) == 0) {
                maxLogY = std::max(maxLogY, c.pos.y);
                minLogY = std::min(minLogY, c.pos.y);
            }
        }
        ASSERT_NE(maxLogY, INT_MIN) << "level " << i << " has no trunk cells at all";
        EXPECT_EQ(minLogY, minY) << "level " << i << ": trunk does not reach the tree's base";
        // The trunk must span up to where the canopy starts (10 of 17 cubes ≈ 55% height).
        const float trunkTopWorld = float(maxLogY + 1) * float(l.cellSizeMicros) / 9.0f;
        EXPECT_GE(trunkTopWorld, 8.0f)
            << "level " << i << ": stem truncated (top at " << trunkTopWorld << " voxels)";
    }
}

// Island culling: no tiny disconnected debris — the deliberate stray microcubes in the
// fixture must NOT survive as isolated floating cells (the exact rejected defect).
TEST(TemplateLodChainTest, NoTinyIslandsSurvive) {
    auto levels = TemplateLodChain::build(syntheticOak());
    ASSERT_FALSE(levels.empty());
    for (size_t li = 0; li < levels.size(); ++li) {
        const auto& l = levels[li];
        std::set<std::tuple<int, int, int>> occ;
        for (const auto& c : l.cells) occ.insert({c.pos.x, c.pos.y, c.pos.z});
        // Flood-fill 26-connected components.
        std::set<std::tuple<int, int, int>> seen;
        for (const auto& start : occ) {
            if (seen.count(start)) continue;
            std::vector<std::tuple<int, int, int>> stack{start};
            size_t size = 0;
            while (!stack.empty()) {
                auto cur = stack.back(); stack.pop_back();
                if (seen.count(cur)) continue;
                seen.insert(cur); ++size;
                auto [x, y, z] = cur;
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dz = -1; dz <= 1; ++dz) {
                            if (!dx && !dy && !dz) continue;
                            auto n = std::make_tuple(x + dx, y + dy, z + dz);
                            if (occ.count(n) && !seen.count(n)) stack.push_back(n);
                        }
            }
            const size_t minComponent =
                std::max<size_t>(2, occ.size() / 50);   // 2% debris threshold
            EXPECT_GE(size, minComponent)
                << "level " << li << " kept a floating island of " << size << " cells";
        }
    }
}

// Silhouette bound: decimation must not inflate the tree. The CANOPY (non-trunk) volume of
// every level stays within a modest factor of the input's occupied micro volume; the trunk is
// a deliberate exemption but must stay STICK-shaped (a thin column, never a slab).
TEST(TemplateLodChainTest, SilhouetteIsBounded) {
    auto t = syntheticOak();
    auto levels = TemplateLodChain::build(t);
    ASSERT_FALSE(levels.empty());
    // Input occupied micro volume: cubes 9^3 each, subcubes 3^3, microcubes 1.
    const size_t inputMicro = t.cubes.size() * 729 + t.subcubes.size() * 27 + t.microcubes.size();
    const int templateHeightMicros = 17 * 9;   // 12 trunk + 5 canopy voxels
    for (size_t i = 0; i < levels.size(); ++i) {
        const auto& l = levels[i];
        const size_t cellVol = size_t(l.cellSizeMicros) * l.cellSizeMicros * l.cellSizeMicros;
        size_t logCells = 0;
        for (const auto& c : l.cells)
            if (c.material.rfind("Log", 0) == 0) ++logCells;
        const size_t canopyMicro = (l.cells.size() - logCells) * cellVol;
        EXPECT_LT(canopyMicro, inputMicro * 2)
            << "level " << i << " canopy fattened to " << canopyMicro
            << " micro-volume vs input " << inputMicro << " — the OR-squash defect is back";
        // Stick bound: the trunk is 9 micros (1 voxel) wide, so at cell size c it honestly
        // spans ceil(9/c)^2 cells of cross-section times height/c rows, margined 2x. A trunk
        // that grows past that is spreading like an area (slab) — the exemption leaked.
        const size_t cross = size_t((9 + l.cellSizeMicros - 1) / l.cellSizeMicros);
        const size_t stickBound =
            2 * cross * cross * (size_t(templateHeightMicros / l.cellSizeMicros) + 2);
        EXPECT_LE(logCells, stickBound)
            << "level " << i << " trunk exemption produced " << logCells
            << " cells — no longer stick-shaped";
    }
}

// Majority material, not OR: a cell dominated by leaves is a leaf cell even when a sliver of
// trunk passes through it (OR-promotion painted whole canopies with whatever won the merge).
TEST(TemplateLodChainTest, MajorityMaterialWinsPerCell) {
    auto levels = TemplateLodChain::build(syntheticOak());
    ASSERT_FALSE(levels.empty());
    // In the canopy band (world y >= 11), leaf cells must dominate log cells at every level:
    // the trunk sliver crossing the canopy must not convert canopy cells to Log.
    for (size_t i = 0; i < levels.size(); ++i) {
        const auto& l = levels[i];
        int leaf = 0, log = 0;
        for (const auto& c : l.cells) {
            const float worldY = float(c.pos.y) * float(l.cellSizeMicros) / 9.0f;
            if (worldY < 11.0f) continue;
            if (c.material.rfind("Leaf", 0) == 0) ++leaf;
            else if (c.material.rfind("Log", 0) == 0) ++log;
        }
        EXPECT_GT(leaf, log * 3)
            << "level " << i << " canopy band is not leaf-dominated (leaf " << leaf
            << " vs log " << log << ")";
    }
}

// Smoke test on a REAL authored tree — the actual "1000 -> ... -> 50" arc the user asked
// for, on the template the forests actually stamp. Prints the arc so contact-sheet reviews
// have the numbers next to them.
TEST(TemplateLodChainTest, RealOakTemplateProducesAFullChain) {
    ObjectTemplateManager mgr(nullptr, nullptr);   // headless: load/parse only, no stamping
    if (!mgr.loadTemplate("resources/templates/forge_oak_m.voxel"))
        GTEST_SKIP() << "forge_oak_m.voxel not found (run from repo root)";
    const VoxelTemplate* oak = mgr.getTemplate("forge_oak_m");
    ASSERT_NE(oak, nullptr);

    auto levels = TemplateLodChain::build(*oak);
    ASSERT_EQ(levels.size(), size_t(TemplateLodChain::kLevelCount));
    std::string arc;
    for (size_t i = 0; i < levels.size(); ++i) {
        EXPECT_GT(levels[i].cells.size(), 0u) << "level " << i << " lost the real oak";
        if (i > 0)
            EXPECT_LT(levels[i].cells.size(), levels[i - 1].cells.size());
        arc += (i ? " -> " : "") + std::to_string(levels[i].cells.size());
    }
    // Every level keeps both wood and foliage — a real tree at every distance.
    for (size_t i = 0; i < levels.size(); ++i) {
        bool log = false, leaf = false;
        for (const auto& c : levels[i].cells) {
            log  |= c.material.rfind("Log", 0) == 0;
            leaf |= c.material.rfind("Leaf", 0) == 0;
        }
        EXPECT_TRUE(log) << "level " << i << " lost its trunk";
        EXPECT_TRUE(leaf) << "level " << i << " lost its canopy";
    }
    std::cout << "[ forge_oak_m LOD arc: " << arc << " cells ]" << std::endl;
}

// Deterministic and stably ordered — two builds are byte-identical.
TEST(TemplateLodChainTest, DeterministicAcrossBuilds) {
    auto a = TemplateLodChain::build(syntheticOak());
    auto b = TemplateLodChain::build(syntheticOak());
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        ASSERT_EQ(a[i].cells.size(), b[i].cells.size()) << "level " << i;
        for (size_t j = 0; j < a[i].cells.size(); ++j) {
            EXPECT_EQ(a[i].cells[j].pos, b[i].cells[j].pos);
            EXPECT_EQ(a[i].cells[j].material, b[i].cells[j].material);
        }
    }
}
