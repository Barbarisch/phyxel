#include <gtest/gtest.h>

#include "core/WorldGenerator.h"
#include "core/FlowField.h"
#include "core/HydrologyMap.h"

#include <algorithm>
#include <cstdio>
#include <vector>

// L3 integration validation for the P2 river carve (docs/TerrainGenerationV2.md §P2). Drives the
// REAL WorldGenerator over the baked hydrology region and asserts the river network is wired into
// the generator's output: carving actually happens (non-vacuous), the drainage is acyclic (flows to
// sinks, never uphill loops), carved beds sit in valleys (local minima across the channel), and it
// is deterministic. RED baseline (each invariant independently coupled to one feature): disabling the
// carve in sampleColumn leaves riverOrder 0, so CarveIsWiredToTheRiverNetwork + DeterministicCarve fail
// while BedsSitInValleysNotOnRidges still passes (valley shaping alone troughs the beds); disabling
// valley shaping drops BedsSitInValleysNotOnRidges to ~0.28 and fails it while CarveIsWired still passes.

namespace Phyxel {
namespace {

constexpr int   kSeaLevel   = 16;
constexpr float kHydroCell  = 32.0f;
constexpr float kHydroOrigin = -4096.0f;   // must mirror WorldGenerator's kHydroOrigin/kHydroCells
constexpr int   kHydroCells  = 256;

// Cell-centre world coord for cell (i,j) of the baked region.
float cellCentreX(int i) { return kHydroOrigin + (i + 0.5f) * kHydroCell; }
float cellCentreZ(int j) { return kHydroOrigin + (j + 0.5f) * kHydroCell; }

// Collect the world-column centres of every order>=3 river cell in the baked region (these are the
// cells that carve — orders 1-2 are sub-voxel). Uses only public queries.
std::vector<std::pair<int, int>> riverCellCentres(WorldGenerator& g) {
    std::vector<std::pair<int, int>> out;
    const FlowField* rn = g.riverNetwork();
    if (!rn) return out;
    for (int j = 0; j < kHydroCells; ++j)
        for (int i = 0; i < kHydroCells; ++i) {
            const float wx = cellCentreX(i), wz = cellCentreZ(j);
            if (rn->orderAt(wx, wz) >= 3)
                out.emplace_back(static_cast<int>(std::floor(wx)), static_cast<int>(std::floor(wz)));
        }
    return out;
}

// The carve is wired: every place the network runs an order>=3 channel, the generator's column
// there is marked a riverbed (riverOrder>=3). This is the red-before-green discriminator — with the
// carve removed from sampleColumn, riverOrder is 0 everywhere and this fails on the first river cell.
TEST(TerrainRiverTest, CarveIsWiredToTheRiverNetwork) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const FlowField* rn = g.riverNetwork();
    ASSERT_NE(rn, nullptr);
    EXPECT_GE(rn->maxOrder(), 3) << "no carving-order river baked — test would be vacuous";

    const auto cells = riverCellCentres(g);
    ASSERT_GT(cells.size(), 50u) << "too few order>=3 river cells to validate carving";

    int carved = 0;
    for (const auto& c : cells) {
        const WorldGenerator::ColumnSample col = g.sampleSurface(c.first, c.second);
        if (col.riverOrder >= 3) ++carved;
    }
    std::printf("[river] seed=7: %zu order>=3 river cells, %d carved at their centres\n",
                cells.size(), carved);
    // Every river-cell centre must read as a carved bed. (Allow none to be missed.)
    EXPECT_EQ(carved, static_cast<int>(cells.size()))
        << "the generator did not carve at river-network cells — carve not wired to channelAt";
}

// Carved beds sit in valleys, not on ridges/columns: across the channel the bed is strictly lower
// than the terrain a channel-width to either side. Checked on both axes; a trough passes on at least
// the cross-flow axis. A "river carved onto a ridge" bug (baking on the wrong height field, or a
// coordinate mismatch between the bake and sampleColumn) makes the bed HIGHER than its banks → fails.
TEST(TerrainRiverTest, BedsSitInValleysNotOnRidges) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const auto cells = riverCellCentres(g);
    ASSERT_GT(cells.size(), 50u);

    const int d = 6;   // banks well outside the order-3 half-width (2.5)
    int trough = 0, tested = 0;
    for (const auto& c : cells) {
        const int bed = g.sampleSurface(c.first, c.second).surfaceY;
        const int xl = g.sampleSurface(c.first - d, c.second).surfaceY;
        const int xr = g.sampleSurface(c.first + d, c.second).surfaceY;
        const int zl = g.sampleSurface(c.first, c.second - d).surfaceY;
        const int zr = g.sampleSurface(c.first, c.second + d).surfaceY;
        const bool troughX = bed < xl && bed < xr;
        const bool troughZ = bed < zl && bed < zr;
        if (troughX || troughZ) ++trough;
        ++tested;
    }
    const double rate = static_cast<double>(trough) / tested;
    std::printf("[river] beds in a cross-channel trough: %d/%d (%.2f)\n", trough, tested, rate);
    // The vast majority of carved beds must be local minima across the channel. (Confluences, the
    // sea mouth, and flat spills legitimately fail, so this is a majority — not all — bound.)
    EXPECT_GT(rate, 0.70) << "carved beds are not in valleys — rivers on ridges (wrong height field?)";
}

// Drainage is acyclic: every flow path terminates at a sink (no uphill loop). Combined with
// Priority-Flood's downhill-by-construction downstream (proven in FlowFieldTest), this means every
// river flows monotonically downhill to the sea/a lake within the baked region.
TEST(TerrainRiverTest, DrainageIsAcyclicFlowsToSinks) {
    for (uint32_t s : {7u, 99u, 4242u}) {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, s);
        ASSERT_NE(g.riverNetwork(), nullptr);
        EXPECT_TRUE(g.riverNetwork()->drainageComplete())
            << "drainage graph has a cycle at seed " << s << " — a river would flow uphill/pond forever";
    }
}

// Determinism: the baked hydrology + carve are a pure function of seed (safe under the streaming
// worker's generator copy). Two independent generators agree column-for-column.
TEST(TerrainRiverTest, DeterministicCarve) {
    WorldGenerator a(WorldGenerator::GenerationType::Mountains, 4242u);
    WorldGenerator b(WorldGenerator::GenerationType::Mountains, 4242u);
    // Sample the actual river-cell centres so the check is NON-VACUOUS on carved beds (not just dry
    // land) — a nondeterministic carve/valley pass would diverge here.
    const auto cells = riverCellCentres(a);
    ASSERT_GT(cells.size(), 50u);
    int riverbeds = 0;
    for (const auto& c : cells) {
        const WorldGenerator::ColumnSample ca = a.sampleSurface(c.first, c.second);
        const WorldGenerator::ColumnSample cb = b.sampleSurface(c.first, c.second);
        EXPECT_EQ(ca.surfaceY, cb.surfaceY) << "surfaceY differs at (" << c.first << "," << c.second << ")";
        EXPECT_EQ(ca.riverOrder, cb.riverOrder) << "riverOrder differs at (" << c.first << "," << c.second << ")";
        if (ca.riverOrder >= 3) ++riverbeds;
    }
    std::printf("[river] deterministic sample hit %d riverbed columns\n", riverbeds);
    EXPECT_GT(riverbeds, 50) << "determinism check never landed on a carved bed — vacuous";
}

// Diagnostic (not an assertion of quality): print the order>=3 river-cell centres nearest the world
// origin for seed 7, so the L4 runtime demo can aim the camera at an actual carved valley.
TEST(TerrainRiverTest, PrintNearOriginRiverCellsSeed7) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    auto cells = riverCellCentres(g);
    ASSERT_GT(cells.size(), 0u);
    std::sort(cells.begin(), cells.end(), [](auto a, auto b) {
        return (a.first * a.first + a.second * a.second) < (b.first * b.first + b.second * b.second);
    });
    for (size_t i = 0; i < cells.size() && i < 8; ++i) {
        const auto col = g.sampleSurface(cells[i].first, cells[i].second);
        std::printf("[river] seed7 near-origin river #%zu (%d,%d) surfaceY=%d order=%d\n",
                    i, cells[i].first, cells[i].second, col.surfaceY, col.riverOrder);
    }
}

}  // namespace
}  // namespace Phyxel
