// WaterCore plan A2a (32.2 item 1) - the record read is the same across chunk-column seams. Debris over a pond that slept into the
// record reads the water through WaterCoreManager::columnWater -> recordColumnWater, which caches the record PER 32 x 32 CHUNK COLUMN
// (one span-reader call per chunk column, floor division for negative coordinates). The chunk rule (FeatureDesignKeys: chunks must
// not be visible): the per-chunk read must equal each column's own spans, everywhere - here a pond straddling the four chunk columns
// that meet at (0, 0), so x and z both cross 31/32 and -1/0, negative coordinates included (FloraMarginTest's shape: chunked = whole).
#include <gtest/gtest.h>

#include "core/water/WaterCore.h"
#include "core/water/WaterCoreManager.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace Phyxel::Core::Water;

namespace {

// the chunk spans, stood in for: per world column, its runs (bottom-up)
struct Record {
    std::map<std::pair<int, int>, std::vector<ColumnRun>> cols;
    uint64_t revision = 1;
    int calls = 0;
    void install(WaterCoreManager& mgr) {
        mgr.setSpanIo([](const std::vector<ColumnRuns>&, float, float, long* u, long* c) { if (u) *u = 0; if (c) *c = 0; },
            [this](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& out) {
                ++calls;
                out.clear();
                for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                    const auto it = cols.find({x, z});
                    if (it == cols.end()) continue;
                    ColumnRuns c; c.x = x; c.z = z; c.runs = it->second;
                    out.push_back(std::move(c));
                }
            });
        mgr.setSpanRevisionQuery([this] { return revision; });
    }
    // the column's own answer, read from nothing but its own spans: today's rule is the highest run top (A2e will change it)
    bool own(int x, int z, float& top) const {
        const auto it = cols.find({x, z});
        if (it == cols.end()) return false;
        top = -1e30f;
        for (const ColumnRun& r : it->second) if (r.topY > r.bottomY + 1e-4f) top = std::max(top, r.topY);
        return top > -1e29f;
    }
};

// a level that differs in every column (a seam that swapped or shifted columns cannot pass by accident)
float levelAt(int x, int z) { return 1.0f + 0.001f * static_cast<float>(((x * 73856093) ^ (z * 19349663)) & 511); }

}  // namespace

TEST(WaterRecordSeam, ThePerChunkReadEqualsEachColumnsOwnSpansAcrossSeams) {
    WaterCoreManager mgr([](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; });
    Record rec;
    rec.install(mgr);
    // the pond: x, z in -40..39 (four chunk columns meet at 0, 0; the -32 / 32 seams are inside it too), its level per column;
    // every 7th column also holds a second, lower run (a cave pool under the pond) and a hole every 11th (a dry column inside)
    const int lo = -40, hi = 39;
    for (int z = lo; z <= hi; ++z) for (int x = lo; x <= hi; ++x) {
        if (((x + 1000) * 3 + (z + 1000)) % 11 == 0) continue;
        std::vector<ColumnRun> runs;
        if (((x + 1000) + (z + 1000) * 5) % 7 == 0) runs.push_back({-20.0f, -18.5f, 1.5});
        const float l = levelAt(x, z);
        runs.push_back({0.0f, l, static_cast<double>(l)});
        rec.cols[{x, z}] = runs;
    }
    int checked = 0, wet = 0, mismatched = 0, truncDiffers = 0;
    for (int z = lo - 8; z <= hi + 8; ++z) for (int x = lo - 8; x <= hi + 8; ++x) {   // 8 dry columns beyond the pond each side
        float want = 0.0f, got = 0.0f; glm::vec2 flow(1.0f);
        const bool ownWet = rec.own(x, z, want);
        const bool gotWet = mgr.columnWater(x, z, got, flow);
        ++checked; wet += ownWet;
        if (ownWet != gotWet || (ownWet && std::abs(got - want) > 1e-6f)) {
            if (++mismatched <= 8) ADD_FAILURE() << "column (" << x << ", " << z << "): own spans " << (ownWet ? "wet " : "dry ") << want
                                                 << ", the per-chunk read " << (gotWet ? "wet " : "dry ") << got;
        }
        if (gotWet) EXPECT_EQ(flow, glm::vec2(0.0f)) << "the record has no current";
        // the control: the range holds columns a truncating chunk index (x / 32 for negative x) would put in the wrong chunk
        truncDiffers += (x / 32 != static_cast<int>(std::floor(x / 32.0f))) || (z / 32 != static_cast<int>(std::floor(z / 32.0f)));
    }
    std::printf("  %d columns checked (%d wet), %d mismatched; %d sit where truncation picks the wrong chunk; %d reader calls\n", checked, wet,
                mismatched, truncDiffers, rec.calls);
    EXPECT_EQ(mismatched, 0);
    EXPECT_GT(truncDiffers, 1000) << "CONTROL: the rig does not reach the negative seams";
    EXPECT_GT(wet, 5000);
    // one reader call per chunk column touched (the cache's grain): -48..47 spans chunk columns -2..1 on each axis = 16
    EXPECT_EQ(rec.calls, 16) << "the record cache read more than once per chunk column, or per column";
}
