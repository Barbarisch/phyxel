// WaterCore §54.2 (W5.2): the reach's edge columns (RiverReach.h ringBoundaries) are a pure function of the grid, the record and
// the current - the design check's chunk-independence test (amendment A7): the record read whole, or read chunk by chunk (each
// 32 x 32 x 32 piece's runs, a run that crosses a vertical chunk border split in two) and fed in shuffled order, gives the SAME
// list. Plus the classes on a straight river: inflow across the upstream edge, level-only across the downstream edge and along it.
// Control: a list built from a record missing one chunk's piece differs (the comparison can see a change).
#include <gtest/gtest.h>
#include "core/water/RiverReach.h"
#include <algorithm>
#include <climits>
#include <map>
#include <memory>
#include <cstdio>
#include <random>

using namespace Phyxel::Core::Water;

namespace {
// a meandering river in the record: water [bed, 33.5] where |z - centre(x)| < 7, the bed deeper in the middle
std::vector<ColumnRuns> wholeRecord(int x0, int x1, int z0, int z1) {
    std::vector<ColumnRuns> out;
    for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
        const float c = 20.0f + 9.0f * std::sin(0.08f * static_cast<float>(x));
        const float d = std::abs(static_cast<float>(z) + 0.5f - c);
        if (d >= 7.0f) continue;
        ColumnRuns col; col.x = x; col.z = z;
        ColumnRun r; r.bottomY = 30.0f - 2.0f * (1.0f - d / 7.0f) - 12.0f; r.topY = 33.5f; r.mass = r.topY - r.bottomY;   // the top crosses the y = 32 chunk border: the chunk reader splits it
        col.runs.push_back(r); out.push_back(col);
    }
    return out;
}
// the same record as the chunk reader returns it: per 32^3 chunk, each column's run clipped to the chunk's height
std::vector<ColumnRuns> chunkedRecord(const std::vector<ColumnRuns>& whole, int skipChunkX = INT_MIN) {
    std::vector<ColumnRuns> out;
    for (const ColumnRuns& c : whole) {
        if (riverFloorDiv(c.x, 32) == skipChunkX) continue;
        for (const ColumnRun& r : c.runs)
            for (int cy = riverFloorDiv(static_cast<int>(std::floor(r.bottomY)), 32); cy * 32 < r.topY; ++cy) {
                ColumnRuns p; p.x = c.x; p.z = c.z;
                ColumnRun q; q.bottomY = std::max(r.bottomY, cy * 32.0f); q.topY = std::min(r.topY, cy * 32.0f + 32.0f);
                if (q.topY <= q.bottomY) continue;
                q.mass = q.topY - q.bottomY; p.runs.push_back(q); out.push_back(p);
            }
    }
    return out;
}
glm::vec2 current(int wx, int, float, float) { const float a = std::atan(0.72f * std::cos(0.08f * static_cast<float>(wx))); return 0.6f * glm::vec2(std::cos(a), std::sin(a)); }
bool same(const std::vector<RingColumn>& a, const std::vector<RingColumn>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].local != b[i].local || a[i].topY != b[i].topY || a[i].bottomY != b[i].bottomY || a[i].vel != b[i].vel || a[i].inflow != b[i].inflow) return false;
    return true;
}
}  // namespace

TEST(RiverReachTest, TheEdgeColumnsAreTheSameHoweverTheRecordIsRead) {
    GridSpec sp; sp.h = 1.0f / 9.0f; sp.origin = glm::ivec3(-40 * 9, 15 * 9, -8 * 9); sp.dims = glm::ivec3(56 * 9, 20 * 9, 56 * 9);   // x -40..15, y 15..34, z -8..47: spans chunk x -2..0 and y 0..1
    const auto whole = wholeRecord(-48, 24, -16, 56);
    const auto ref = ringBoundaries(sp, whole, current);
    auto chunked = chunkedRecord(whole);
    std::mt19937 rng(7);
    int differ = 0;
    for (int k = 0; k < 5; ++k) { std::shuffle(chunked.begin(), chunked.end(), rng); if (!same(ringBoundaries(sp, chunked, current), ref)) ++differ; }
    const auto missing = ringBoundaries(sp, chunkedRecord(whole, 0), current);
    int inflow = 0; for (const auto& c : ref) inflow += c.inflow ? 1 : 0;
    std::printf("  ring columns %zu (%d inflow); chunked + shuffled x5: %d differ; control (chunk x 0 - the downstream edge's - missing): %zu columns, %s\n",
                ref.size(), inflow, differ, missing.size(), same(missing, ref) ? "SAME (the test cannot see)" : "differs");
    EXPECT_GT(ref.size(), 0u);
    EXPECT_GT(inflow, 0);
    EXPECT_EQ(differ, 0);
    EXPECT_FALSE(same(missing, ref)) << "the control: a record missing a chunk must change the list";
}

TEST(RiverReachTest, AStraightRiverIsFedUpstreamAndHeldDownstream) {
    GridSpec sp; sp.h = 1.0f / 9.0f; sp.origin = glm::ivec3(0, -9, 0); sp.dims = glm::ivec3(24 * 9, 6 * 9, 24 * 9);
    std::vector<ColumnRuns> rec;
    for (int x = -8; x < 32; ++x) for (int z = 6; z <= 17; ++z) { ColumnRuns c; c.x = x; c.z = z; ColumnRun r; r.bottomY = 0.0f; r.topY = 2.0f; r.mass = 2.0; c.runs.push_back(r); rec.push_back(c); }
    const auto ring = ringBoundaries(sp, rec, [](int, int, float, float) { return glm::vec2(0.6f, 0.0f); });
    int inUp = 0, inOther = 0, levelDown = 0, levelOther = 0;
    for (const auto& c : ring) {
        EXPECT_FLOAT_EQ(c.topY, 2.0f);
        if (c.local.x == 0 && c.local.y > 0 && c.local.y < sp.dims.z - 1) { if (c.inflow) ++inUp; else ++levelOther; }
        else if (c.local.x == sp.dims.x - 1 && c.local.y > 0 && c.local.y < sp.dims.z - 1) { if (!c.inflow) ++levelDown; else ++inOther; }
        else { if (c.inflow) ++inOther; else ++levelOther; }
    }
    double qIn, qOut; ringDischarge(ring, sp.h, qIn, qOut);
    std::printf("  upstream inflow %d, downstream level-only %d, other inflow %d / level-only %d; record Q in %.3f out %.3f m^3/s\n", inUp, levelDown, inOther, levelOther, qIn, qOut);
    EXPECT_EQ(inUp, 12 * 9);          // 12 m of river across the upstream edge, 9 grid columns a metre
    EXPECT_EQ(levelDown, 12 * 9);
    EXPECT_EQ(inOther, 0);
    EXPECT_NEAR(qIn, 0.6 * 2.0 * 12.0, 1e-3);   // the record's discharge: v h width
    EXPECT_NEAR(qOut, qIn, 1e-3);
}

// WaterCore §54.3 (W5.2b): the follow trigger's search (nearestRiverPoint) is a pure function of world position and the record - the
// design check's equality test: the nearest river point for 40 viewers is the SAME with the record read whole or chunk by chunk
// (runs split at the y = 32 chunk border) and fed shuffled. Control: with the chunk holding a viewer's point missing, the answer
// changes (the comparison can see a change).
TEST(RiverReachTest, TheNearestRiverPointIsTheSameHoweverTheRecordIsRead) {
    const auto whole = wholeRecord(-80, 80, -40, 80);
    auto lookup = [](const std::vector<ColumnRuns>& rec) {
        auto col = std::make_shared<std::map<std::pair<int, int>, std::pair<float, float>>>(joinColumnRuns(rec, -1e9f, 1e9f));
        return RecordColumnFn([col](int wx, int wz, float& t, float& b) {
            const auto it = col->find({wx, wz});
            if (it == col->end()) return false;
            t = it->second.first; b = it->second.second; return true;
        });
    };
    const RecordColumnFn wholeFn = lookup(whole);
    auto chunked = chunkedRecord(whole);
    std::mt19937 rng(11);
    std::shuffle(chunked.begin(), chunked.end(), rng);
    const RecordColumnFn chunkFn = lookup(chunked);
    auto eq = [](const RiverPoint& a, const RiverPoint& b) { return a.found == b.found && (!a.found || (a.wx == b.wx && a.wz == b.wz && a.top == b.top && a.bottom == b.bottom)); };
    int found = 0, differ = 0;
    RiverPoint probe;
    for (int k = 0; k < 40; ++k) {
        const glm::vec2 v(-60.0f + 3.1f * static_cast<float>(k), -20.0f + 6.9f * static_cast<float>(k % 17));
        const RiverPoint a = nearestRiverPoint(v, 24.0f, wholeFn, current), b = nearestRiverPoint(v, 24.0f, chunkFn, current);
        if (a.found) { ++found; if (!probe.found) probe = a; }
        if (!eq(a, b)) ++differ;
        if (a.found) { EXPECT_EQ(a.wx % 2, 0); EXPECT_EQ(a.wz % 2, 0); EXPECT_LE(a.distM, 24.0f); }
    }
    // the control: the chunk holding the first found point removed - the same viewer reads another point (or none)
    const glm::vec2 pv(static_cast<float>(probe.wx) + 0.5f, static_cast<float>(probe.wz) + 0.5f);
    const RiverPoint ref = nearestRiverPoint(pv, 24.0f, wholeFn, current);
    const RiverPoint ctl = nearestRiverPoint(pv, 24.0f, lookup(chunkedRecord(whole, riverFloorDiv(probe.wx, 32))), current);
    const RiverPoint none = nearestRiverPoint(glm::vec2(0.0f, 300.0f), 24.0f, wholeFn, current);   // far from any river: none
    std::printf("  40 viewers: %d found a river point, %d differ read chunked + shuffled; control (the point's chunk missing): (%d, %d) -> %s (%d, %d); far viewer: %s\n",
                found, differ, ref.wx, ref.wz, ctl.found ? "found" : "none", ctl.wx, ctl.wz, none.found ? "FOUND" : "none");
    EXPECT_GT(found, 20);
    EXPECT_EQ(differ, 0);
    EXPECT_TRUE(ref.found);
    EXPECT_EQ(ref.distM, 0.0f) << "a viewer on a river lattice column reads that column";
    EXPECT_FALSE(eq(ctl, ref)) << "the control: a record missing the point's chunk must change the answer";
    EXPECT_FALSE(none.found);
}

// WaterCore §54.4 (W5.3): the river ripple layer's record part (riverRippleFromRecord - wet, current, depth, surface per 1/9 m cell
// of the window) is a pure function of world position and the record: the record read whole or chunk by chunk (split at y 32,
// shuffled) gives the same arrays, bit for bit; the window placed anywhere (negative origins, a world column split by the window's
// edge) reads each cell from its own world column. Control: the record missing one chunk's piece changes the arrays.
TEST(RiverReachTest, TheRiverRippleRecordIsTheSameHoweverTheRecordIsRead) {
    const auto whole = wholeRecord(-80, 80, -40, 80);
    auto lookup = [](const std::vector<ColumnRuns>& rec) {
        auto col = std::make_shared<std::map<std::pair<int, int>, std::pair<float, float>>>(joinColumnRuns(rec, -1e9f, 1e9f));
        return RecordColumnFn([col](int wx, int wz, float& t, float& b) {
            const auto it = col->find({wx, wz});
            if (it == col->end()) return false;
            t = it->second.first; b = it->second.second; return true;
        });
    };
    auto chunked = chunkedRecord(whole);
    std::mt19937 rng(5);
    std::shuffle(chunked.begin(), chunked.end(), rng);
    const RecordColumnFn wholeFn = lookup(whole), chunkFn = lookup(chunked);
    auto eq = [](const RiverRippleRecord& a, const RiverRippleRecord& b) {
        return a.wet == b.wet && a.vel == b.vel && a.depth == b.depth && a.top == b.top && a.riverCells == b.riverCells;
    };
    int differ = 0, windows = 0, riverCells = 0;
    for (const glm::ivec2 o : {glm::ivec2(-500, -100), glm::ivec2(-229, 13), glm::ivec2(4, 50), glm::ivec2(-77, -31)}) {
        RiverRippleRecord a, b;
        riverRippleFromRecord(o, 458, 458, wholeFn, current, a);
        riverRippleFromRecord(o, 458, 458, chunkFn, current, b);
        ++windows; riverCells += a.riverCells;
        if (!eq(a, b)) ++differ;
        // each cell reads its own world column (the floor division at negative lattice coordinates)
        for (int z = 0; z < a.nz; z += 37) for (int x = 0; x < a.nx; x += 41) {
            const int wx = riverFloorDiv(o.x + x, 9), wz = riverFloorDiv(o.y + z, 9);
            float t = 0.0f, bb = 0.0f;
            const bool wet = wholeFn(wx, wz, t, bb) && t > bb;
            const size_t i = static_cast<size_t>(x) + static_cast<size_t>(a.nx) * z;
            ASSERT_EQ(a.wet[i] != 0, wet) << "cell (" << x << ", " << z << ") of the window at (" << o.x << ", " << o.y << ")";
            if (wet) { EXPECT_EQ(a.top[i], t); EXPECT_EQ(a.depth[i], t - bb); EXPECT_EQ(a.vel[i], current(wx, wz, t, bb)); }
        }
    }
    RiverRippleRecord ref, ctl;
    riverRippleFromRecord(glm::ivec2(-229, 13), 458, 458, wholeFn, current, ref);
    riverRippleFromRecord(glm::ivec2(-229, 13), 458, 458, lookup(chunkedRecord(whole, -1)), current, ctl);
    std::printf("  %d windows (458 x 458 cells): %d differ read chunked + shuffled; %d river cells; control (chunk x -1 missing): %s (%d vs %d wet)\n",
                windows, differ, riverCells, eq(ref, ctl) ? "SAME" : "differs", ctl.wetCells, ref.wetCells);
    EXPECT_EQ(differ, 0);
    EXPECT_GT(riverCells, 50000);
    EXPECT_FALSE(eq(ref, ctl)) << "the control: a record missing a chunk must change the arrays";
}

// 54.4: the bed-shear forcing is ONE formula (the volume's ripple forcing and the record river's), pinned
TEST(RiverReachTest, TheBedShearForcingIsTheVolumesFormula) {
    const float speed = 0.6f, depth = 2.0f, n = kRiverBedManning, cf = 200.0f, dt = 1.0f / 60.0f;
    const float ustar = speed * n * std::sqrt(9.81f) / std::pow(depth, 1.0f / 6.0f);
    EXPECT_FLOAT_EQ(bedShearRippleImpulse(speed, depth, n, cf, dt), cf * 1000.0f * ustar * ustar * dt);
    EXPECT_EQ(bedShearRippleImpulse(0.0f, depth, n, cf, dt), 0.0f);
    EXPECT_EQ(kRiverBedManning, 0.035f);
}
