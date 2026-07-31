#include <gtest/gtest.h>

#include "core/WorldGenerator.h"
#include "core/FlowField.h"
#include "core/HydrologyMap.h"
#include "core/Chunk.h"
#include "core/ChunkVoxelManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

// L3 integration validation for the P2 river carve + meander (docs/TerrainGenerationV2.md §P2).
// Drives the REAL WorldGenerator over the baked hydrology region and asserts the river network is
// wired into the generator's output: carving happens (non-vacuous), rivers MEANDER (the carved
// channel is displaced off the straight D8 cell centres), carved beds sit in valleys, drainage is
// acyclic (downhill to sinks), and it is deterministic.
//
// RED baselines (each invariant independently coupled to one feature):
//  - carve disabled  → riverOrder 0 everywhere → CarvingIsNonVacuous + DeterministicCarve fail.
//  - valley shaping disabled → BedsSitInValleys drops (~0.3) and fails.
//  - meander disabled → the channel sits ON the cell centres → RiversMeander's mean offset ≈ 0 fails.

namespace Phyxel {
namespace {

constexpr int kSeaLevel = 16;

// The region has thousands of order>=3 cells for seed 7 — far too many to window-scan all. The
// windowed carve/meander/sinuosity tests anchor on ONE river: the order-4 stream nearest the origin,
// which runs ~N-S near x=832 over z=-320..320 (found via the PrintRiverCellsSeed7 probe). The box
// bounds it with margin. (Deterministic per seed; re-derive from the probe if the terrain changes.)
constexpr int kBoxX0 = 712, kBoxX1 = 952, kBoxZ0 = -460, kBoxZ1 = 460;

// World-column centres of order>=3 river cells within [x0,x1]×[z0,z1] (default = whole baked region).
// Geometry is read from the baked FlowField itself (origin/cellSize/cell counts), so this is
// independent of the generator's region-size constants.
std::vector<std::pair<int, int>> riverCellCentres(WorldGenerator& g, int x0 = -(1 << 30),
                                                  int x1 = (1 << 30), int z0 = -(1 << 30), int z1 = (1 << 30)) {
    std::vector<std::pair<int, int>> out;
    const FlowField* rn = g.riverNetwork();
    if (!rn) return out;
    const float ox = rn->originX(), oz = rn->originZ(), cs = rn->cellSize();
    for (int j = 0; j < rn->cellsZ(); ++j)
        for (int i = 0; i < rn->cellsX(); ++i) {
            const float wx = ox + (i + 0.5f) * cs, wz = oz + (j + 0.5f) * cs;
            if (rn->orderAt(wx, wz) < 3) continue;
            const int ix = static_cast<int>(std::floor(wx)), iz = static_cast<int>(std::floor(wz));
            if (ix >= x0 && ix <= x1 && iz >= z0 && iz <= z1) out.emplace_back(ix, iz);
        }
    return out;
}

// Fingerprint of a baked river network: the Strahler order sampled at every 8th cell. Two generators
// with the same bake inputs produce identical fingerprints; different inputs must differ.
std::vector<int> orderFingerprint(WorldGenerator& g) {
    std::vector<int> f;
    const FlowField* rn = g.riverNetwork();
    if (!rn) return f;
    const float ox = rn->originX(), oz = rn->originZ(), cs = rn->cellSize();
    for (int j = 0; j < rn->cellsZ(); j += 8)
        for (int i = 0; i < rn->cellsX(); i += 8)
            f.push_back(rn->orderAt(ox + (i + 0.5f) * cs, oz + (j + 0.5f) * cs));
    return f;
}

// Scan a ±W window around each river-cell centre and collect every carved (riverOrder>=3) column of
// the actual (meandered) channel, deduplicated.
std::vector<std::pair<int, int>> carvedColumnsNear(WorldGenerator& g,
                                                   const std::vector<std::pair<int, int>>& cells, int W) {
    std::set<std::pair<int, int>> seen;
    for (const auto& c : cells)
        for (int dz = -W; dz <= W; ++dz)
            for (int dx = -W; dx <= W; ++dx) {
                const int x = c.first + dx, z = c.second + dz;
                if (g.sampleSurface(x, z).riverOrder >= 3) seen.emplace(x, z);
            }
    return {seen.begin(), seen.end()};
}

// The bigger bake region grows the drainage network deep enough to produce high Strahler-order
// trunk rivers (wide, deeper channels), not just order-3 headwater streams. Probe several seeds.
TEST(TerrainRiverTest, HigherOrderRiversExist) {
    int best = 0;
    for (uint32_t s : {7u, 99u, 4242u, 1234u, 2026u}) {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, s);
        const FlowField* rn = g.riverNetwork();
        ASSERT_NE(rn, nullptr);
        std::printf("[river] seed=%u region=%dx%d maxAccum=%d maxOrder=%d\n",
                    s, rn->cellsX(), rn->cellsZ(), rn->maxAccum(), rn->maxOrder());
        best = std::max(best, rn->maxOrder());
    }
    EXPECT_GE(best, 5) << "no order>=5 trunk river in any seed — continents too small for big rivers";
}

// Carving is wired and non-vacuous: the meandered channel carves a substantial set of riverbed
// columns near the network. RED (carve disabled): riverOrder is 0 everywhere → 0 carved.
TEST(TerrainRiverTest, CarvingIsNonVacuous) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const FlowField* rn = g.riverNetwork();
    ASSERT_NE(rn, nullptr);
    EXPECT_GE(rn->maxOrder(), 3) << "no carving-order river baked — test would be vacuous";

    const auto cells = riverCellCentres(g, kBoxX0, kBoxX1, kBoxZ0, kBoxZ1);
    ASSERT_GT(cells.size(), 3u) << "too few order>=3 river cells in the anchor box to validate";
    const auto carved = carvedColumnsNear(g, cells, 22);
    std::printf("[river] seed=7 anchor box: %zu river cells, %zu carved channel columns\n",
                cells.size(), carved.size());
    EXPECT_GT(carved.size(), 200u) << "the meandered channel barely carves — carve not wired";
}

// Rivers MEANDER: the meander warp displaces the carved channel OFF the straight D8 cell centres.
// Without the warp the channel segments run cell-centre → downstream-cell-centre, so every river-cell
// centre lies exactly ON the channel (carved). The warp swings the channel a full channel-width+ to
// the side, so most centres fall off it. We measure the fraction of river-cell centres still carved.
// (This isolates the WARP: a metric like "centreline excursion" fails because the D8 path itself
// wanders cell-to-cell.) RED (meander disabled): the channel IS the cell path → fraction ≈ 1.0.
TEST(TerrainRiverTest, RiversMeander) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const auto cells = riverCellCentres(g, kBoxX0, kBoxX1, kBoxZ0, kBoxZ1);
    ASSERT_GT(cells.size(), 3u);
    int onCentre = 0;
    for (const auto& c : cells)
        if (g.sampleSurface(c.first, c.second).riverOrder >= 3) ++onCentre;
    const double frac = static_cast<double>(onCentre) / cells.size();
    std::printf("[river] river-cell centres still on the carved channel = %.2f (%d/%zu) — low ⇒ meandered\n",
                frac, onCentre, cells.size());
    // Baseline (warp off): the D8 channel runs through every cell centre → 1.00. The warp swings it
    // off them → 0.50 for this order-4 river (wider channels retain more centres than the order-3
    // case, so this bound is looser than the sinuosity check but still separates 0.50 from 1.00).
    EXPECT_LT(frac, 0.75)
        << "channel still runs through the cell centres — river is the straight D8 path, not meandering";
}

// Rivers are SINUOUS (not merely displaced): the carved channel's centreline is meaningfully longer
// than the straight line between its endpoints. Near the origin the seed-7 river runs ~N-S; we build
// the channel centreline meanX(z) (mean x of carved columns per z-row), subsample it to suppress
// per-row noise, and measure sinuosity = arc-length / straight-line distance (Leopold-Wolman-Miller:
// straight <1.05, sinuous 1.05-1.5). This is STRONGER than RiversMeander: a constant lateral offset
// (a straight but shifted channel) leaves the centreline straight → sinuosity ≈ 1.0 → fails here,
// whereas it would pass RiversMeander. Only a genuine periodic warp lengthens the path.
TEST(TerrainRiverTest, RiversAreSinuous) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const int zLo = kBoxZ0, zHi = kBoxZ1, xLo = kBoxX0, xHi = kBoxX1;
    std::vector<std::pair<double, double>> centre;  // (meanX, z) per z-row that has channel
    for (int z = zLo; z <= zHi; ++z) {
        long sumX = 0;
        int n = 0;
        for (int x = xLo; x <= xHi; ++x)
            if (g.sampleSurface(x, z).riverOrder >= 3) { sumX += x; ++n; }
        if (n > 0) centre.emplace_back(static_cast<double>(sumX) / n, static_cast<double>(z));
    }
    ASSERT_GT(centre.size(), 80u) << "river does not span the window — cannot judge sinuosity";
    // Subsample every 6 z-rows: at ~55 m meander wavelength this keeps the bends while averaging out
    // the ±0.5-column quantisation noise that would otherwise inflate arc length on a straight channel.
    const int step = 6;
    double arc = 0.0;
    std::pair<double, double> prev = centre.front();
    for (size_t i = step; i < centre.size(); i += step) {
        const double dx = centre[i].first - prev.first, dz = centre[i].second - prev.second;
        arc += std::sqrt(dx * dx + dz * dz);
        prev = centre[i];
    }
    const double straight = std::sqrt(std::pow(prev.first - centre.front().first, 2.0) +
                                      std::pow(prev.second - centre.front().second, 2.0));
    const double sinuosity = arc / straight;
    std::printf("[river] channel sinuosity (arc/straight) = %.3f over %zu centreline points\n",
                sinuosity, centre.size());
    // The anchor river's D8 path runs dead straight N-S (sinuosity 1.000; a constant lateral offset
    // reproduces exactly that — translation preserves arc/straight); the fbm meander warp lengthens
    // the centreline to ~1.20. Threshold 1.10 sits between, so it fails on the bare/constant-offset
    // straight channel and passes only when the meander warp adds real sinuosity.
    EXPECT_GT(sinuosity, 1.10) << "channel centreline is ~straight — rivers not sinuous/meandering";
}

// Carved beds sit in valleys, not on ridges: across the channel the bed is strictly lower than the
// terrain a channel-width to either side. Sampled on the ACTUAL carved (meandered) columns (not cell
// centres, which the meander no longer lands on). RED (valley shaping disabled): drops to ~0.3.
TEST(TerrainRiverTest, BedsSitInValleysNotOnRidges) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const auto cells = riverCellCentres(g, kBoxX0, kBoxX1, kBoxZ0, kBoxZ1);
    const auto carved = carvedColumnsNear(g, cells, 22);
    ASSERT_GT(carved.size(), 200u);

    const int d = 6;   // banks well outside the order-3 half-width (2.5)
    int trough = 0, tested = 0;
    for (const auto& c : carved) {
        const int bed = g.sampleSurface(c.first, c.second).surfaceY;
        const int xl = g.sampleSurface(c.first - d, c.second).surfaceY;
        const int xr = g.sampleSurface(c.first + d, c.second).surfaceY;
        const int zl = g.sampleSurface(c.first, c.second - d).surfaceY;
        const int zr = g.sampleSurface(c.first, c.second + d).surfaceY;
        if ((bed < xl && bed < xr) || (bed < zl && bed < zr)) ++trough;
        ++tested;
    }
    const double rate = static_cast<double>(trough) / tested;
    std::printf("[river] beds in a cross-channel trough: %d/%d (%.2f)\n", trough, tested, rate);
    EXPECT_GT(rate, 0.70) << "carved beds are not in valleys — rivers on ridges (wrong height field?)";
}

// Drainage is acyclic: every flow path terminates at a sink (no uphill loop). With Priority-Flood's
// downhill-by-construction downstream, this means rivers flow monotonically downhill to sea/lake.
TEST(TerrainRiverTest, DrainageIsAcyclicFlowsToSinks) {
    for (uint32_t s : {7u, 99u, 4242u}) {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, s);
        ASSERT_NE(g.riverNetwork(), nullptr);
        EXPECT_TRUE(g.riverNetwork()->drainageComplete())
            << "drainage graph has a cycle at seed " << s << " — a river would flow uphill/pond forever";
    }
}

// Determinism: the baked hydrology + meandered carve are a pure function of seed. Two independent
// generators agree column-for-column over the actual carved channel (non-vacuous on riverbeds).
TEST(TerrainRiverTest, DeterministicCarve) {
    WorldGenerator a(WorldGenerator::GenerationType::Mountains, 7u);
    const auto cells = riverCellCentres(a, kBoxX0, kBoxX1, kBoxZ0, kBoxZ1);
    const auto carved = carvedColumnsNear(a, cells, 22);
    ASSERT_GT(carved.size(), 200u);
    // Force b to bake INDEPENDENTLY (not share a's cached backing), so this genuinely tests that two
    // separate bakes of the same seed agree — not that one object equals itself.
    WorldGenerator::clearHydroBakeCache();
    WorldGenerator b(WorldGenerator::GenerationType::Mountains, 7u);
    EXPECT_NE(a.riverNetwork(), b.riverNetwork()) << "b reused a's cached bake — determinism not actually tested";
    int riverbeds = 0;
    for (const auto& c : carved) {
        const WorldGenerator::ColumnSample ca = a.sampleSurface(c.first, c.second);
        const WorldGenerator::ColumnSample cb = b.sampleSurface(c.first, c.second);
        EXPECT_EQ(ca.surfaceY, cb.surfaceY) << "surfaceY differs at (" << c.first << "," << c.second << ")";
        EXPECT_EQ(ca.riverOrder, cb.riverOrder) << "riverOrder differs at (" << c.first << "," << c.second << ")";
        if (ca.riverOrder >= 3) ++riverbeds;
    }
    std::printf("[river] deterministic over %d carved columns\n", riverbeds);
    EXPECT_GT(riverbeds, 200) << "vacuous — no carved columns compared";
}

// The in-process hydrology-bake cache must be keyed on ALL bake inputs. This guards the cache KEY:
// same (seed, type) → identical network; a DIFFERENT seed or generation type → a different network.
// A key that dropped seed or genType would silently hand the second generator the first's cached bake,
// producing an identical fingerprint here (RED). (Determinism/HigherOrder alone would NOT catch that.)
TEST(TerrainRiverTest, HydroBakeCacheKeyDiscriminates) {
    WorldGenerator a(WorldGenerator::GenerationType::Mountains, 7u);
    WorldGenerator a2(WorldGenerator::GenerationType::Mountains, 7u);
    const auto fa = orderFingerprint(a);
    ASSERT_GT(fa.size(), 100u);
    EXPECT_EQ(fa, orderFingerprint(a2)) << "same seed+type gave different bakes — non-deterministic";

    WorldGenerator b(WorldGenerator::GenerationType::Mountains, 20260710u);
    EXPECT_NE(fa, orderFingerprint(b)) << "different seed gave the SAME bake — cache key omits seed?";

    WorldGenerator p(WorldGenerator::GenerationType::Perlin, 7u);
    EXPECT_NE(fa, orderFingerprint(p)) << "different generation type gave the SAME bake — cache key omits genType?";
}

// Diagnostic (not a quality assertion): print carved channel columns nearest the origin for seed 7,
// so the L4 runtime demo can aim the camera at an actual meandering river.
TEST(TerrainRiverTest, PrintRiverCellsSeed7) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const FlowField* rn = g.riverNetwork();
    auto cells = riverCellCentres(g);  // unclamped
    std::printf("[river] seed7 has %zu order>=3 cells; nearest-to-origin + highest-order:\n", cells.size());
    std::sort(cells.begin(), cells.end(), [](auto a, auto b) {
        return (a.first * a.first + a.second * a.second) < (b.first * b.first + b.second * b.second);
    });
    for (size_t i = 0; i < cells.size() && i < 6; ++i)
        std::printf("[river]   near #%zu (%d,%d) order=%d accum=%d\n", i, cells[i].first, cells[i].second,
                    rn->orderAt(cells[i].first, cells[i].second), rn->accumAt(cells[i].first, cells[i].second));
    // Highest-order cell (the trunk).
    auto trunk = *std::max_element(cells.begin(), cells.end(), [&](auto a, auto b) {
        return rn->orderAt(a.first, a.second) < rn->orderAt(b.first, b.second);
    });
    std::printf("[river]   TRUNK (%d,%d) order=%d accum=%d\n", trunk.first, trunk.second,
                rn->orderAt(trunk.first, trunk.second), rn->accumAt(trunk.first, trunk.second));
}

// ── Water-as-terrain-stage P2: creeks shaped by the terrain ─────────────────────────────────────
//
// User directive (2026-07-31): "a creek should be shaped by the terrain around it — not generated
// and just exist independent of the land." Two mechanisms, each with its own red baseline:
//  - SWALE: orders 1-2 attenuate Layer-1 relief in a narrow band (~40% floor at the centreline).
//    RED (no swale): creek columns average the same height as their surroundings.
//  - BED RECESS: inner-band creek columns emit their surface voxel as a 2-layer subcube shelf
//    (floor 2/3), so the runtime's fractional ribbon rests 1/3 voxel below the banks.
//    RED (no shelf): the surface voxel is a full cube (subVoxelFloor == 1).

// World-column positions of creek (order 1-2) channel columns near order 1-2 cells, found by the
// generator's own recorded riverOrder (so the meander warp is accounted for).
std::vector<std::pair<int, int>> creekColumns(WorldGenerator& g, size_t maxCount) {
    std::vector<std::pair<int, int>> out;
    const FlowField* rn = g.riverNetwork();
    if (!rn) return out;
    const float ox = rn->originX(), oz = rn->originZ(), cs = rn->cellSize();
    for (int j = 0; j < rn->cellsZ() && out.size() < maxCount; ++j)
        for (int i = 0; i < rn->cellsX() && out.size() < maxCount; ++i) {
            const float wx = ox + (i + 0.5f) * cs, wz = oz + (j + 0.5f) * cs;
            // Keep the scan affordable: only cells in the seed-7 anchor box neighbourhood.
            if (wx < kBoxX0 - 800 || wx > kBoxX1 + 800 || wz < kBoxZ0 - 800 || wz > kBoxZ1 + 800) continue;
            const int ord = rn->orderAt(wx, wz);
            if (ord < 1 || ord > 2) continue;
            const int cx = static_cast<int>(std::floor(wx)), cz = static_cast<int>(std::floor(wz));
            for (int dz = -3; dz <= 3 && out.size() < maxCount; ++dz)
                for (int dx = -3; dx <= 3 && out.size() < maxCount; ++dx) {
                    const auto c = g.sampleSurface(cx + dx, cz + dz);
                    if (c.riverOrder >= 1 && c.riverOrder <= 2) out.emplace_back(cx + dx, cz + dz);
                }
        }
    return out;
}

// Creek columns sit LOWER than their immediate (off-channel) surroundings on average — the swale.
// RED (no swale): fine relief is uncorrelated with the channel line at a 5-voxel offset, so the
// mean height difference is ~0.
TEST(TerrainRiverTest, CreeksSitInASwale) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const auto creeks = creekColumns(g, 300);
    ASSERT_GT(creeks.size(), 50u) << "too few creek columns found to measure a swale";

    double sumDiff = 0.0;
    int n = 0;
    for (const auto& c : creeks) {
        const int creekY = g.sampleSurface(c.first, c.second).surfaceY;
        static const int OX[4] = {5, -5, 0, 0}, OZ[4] = {0, 0, 5, -5};
        double off = 0.0;
        int m = 0;
        for (int k = 0; k < 4; ++k) {
            const auto o = g.sampleSurface(c.first + OX[k], c.second + OZ[k]);
            if (o.riverOrder != 0) continue;   // offset landed on a channel — not a bank sample
            off += o.surfaceY;
            ++m;
        }
        if (m < 2) continue;
        sumDiff += off / m - creekY;
        ++n;
    }
    ASSERT_GT(n, 50) << "too few usable creek/bank pairs";
    const double meanDiff = sumDiff / n;
    std::printf("[creek] swale: mean(bank - creek) = %.3f voxels over %d columns\n", meanDiff, n);
    EXPECT_GT(meanDiff, 0.5) << "creek columns do not sit in a swale — terrain ignores the creek";
}

// The water runtime's channel queries must follow the MEANDERED line the terrain actually carves
// (WorldGenerator::channelHitAt), not FlowField::channelAt on raw coordinates. The warp displaces
// the channel by up to ~kMeanderAmp (55 u), so the raw query provably disagrees with the carve —
// binding it put ribbons beside their beds (measured live: creek pins with floor 0.0 under them).
TEST(TerrainRiverTest, RuntimeChannelQueryFollowsTheMeanderedCarve) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const auto cells = riverCellCentres(g, kBoxX0, kBoxX1, kBoxZ0, kBoxZ1);
    const auto carved = carvedColumnsNear(g, cells, 22);
    ASSERT_GT(carved.size(), 200u);

    // Query at the SAME coordinates sampleColumn recorded riverOrder from (integer corners):
    // a half-voxel offset flips hit/miss on the parabolic band edge and only muddies the metric.
    int warpedHits = 0, rawHits = 0;
    for (const auto& c : carved) {
        const float wx = static_cast<float>(c.first), wz = static_cast<float>(c.second);
        if (g.channelHitAt(wx, wz).hit) ++warpedHits;
        if (g.riverNetwork()->channelAt(wx, wz).hit) ++rawHits;
    }
    const double n = static_cast<double>(carved.size());
    std::printf("[river] carved columns: %zu; warped-query hits %.1f%%, raw-query hits %.1f%%\n",
                carved.size(), 100.0 * warpedHits / n, 100.0 * rawHits / n);
    EXPECT_GT(warpedHits / n, 0.99)
        << "channelHitAt does not cover the carved channel — warp mismatch between query and carve";
    EXPECT_LT(rawHits, warpedHits)
        << "raw channelAt covers the carve as well as the warped query — either the meander is "
           "gone or this guard is vacuous";
}

// The inner-band creek bed generates as a 2/3 subcube shelf the water ribbon can rest IN; ordinary
// ground stays a full cube. This is the L1 gate for generation-time sub-voxel terrain.
TEST(TerrainRiverTest, CreekBedIsARecessedSubcubeShelf) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 7u);
    const auto creeks = creekColumns(g, 300);

    // Find a creek column flagged as inner-band bed.
    int bx = 0, bz = 0;
    bool found = false;
    WorldGenerator::ColumnSample bed;
    for (const auto& c : creeks) {
        const auto s = g.sampleSurface(c.first, c.second);
        if (s.creekBed) { bx = c.first; bz = c.second; bed = s; found = true; break; }
    }
    ASSERT_TRUE(found) << "no inner-band creek bed column flagged (creekBed never set)";

    const glm::ivec3 chunkCoord(static_cast<int>(std::floor(bx / 32.0)),
                                static_cast<int>(std::floor(bed.surfaceY / 32.0)),
                                static_cast<int>(std::floor(bz / 32.0)));
    Chunk chunk(chunkCoord * 32);
    chunk.initializeForLoading();
    g.generateChunk(chunk, chunkCoord);

    const glm::ivec3 local(bx - chunkCoord.x * 32, bed.surfaceY - chunkCoord.y * 32,
                           bz - chunkCoord.z * 32);
    EXPECT_NEAR(chunk.subVoxelFloor(local), 2.0f / 3.0f, 1e-4f)
        << "creek bed surface voxel at (" << bx << "," << bed.surfaceY << "," << bz
        << ") is not a 2/3 shelf";

    // Control: a nearby non-channel column's surface voxel stays fully solid.
    for (int r = 4; r <= 8; ++r) {
        const auto o = g.sampleSurface(bx + r, bz);
        if (o.riverOrder != 0) continue;
        const glm::ivec3 oChunk(static_cast<int>(std::floor((bx + r) / 32.0)),
                                static_cast<int>(std::floor(o.surfaceY / 32.0)),
                                static_cast<int>(std::floor(bz / 32.0)));
        Chunk oc(oChunk * 32);
        oc.initializeForLoading();
        g.generateChunk(oc, oChunk);
        const glm::ivec3 ol(bx + r - oChunk.x * 32, o.surfaceY - oChunk.y * 32,
                            bz - oChunk.z * 32);
        EXPECT_NEAR(oc.subVoxelFloor(ol), ChunkVoxelManager::kSolidFloor, 1e-4f)
            << "plain bank column is not a plain solid cube — shelf leaked off the creek line";
        break;
    }
}

}  // namespace
}  // namespace Phyxel
