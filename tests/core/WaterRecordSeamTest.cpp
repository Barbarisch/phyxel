// WaterCore plan A2a (32.2 item 1) - the record read is the same across chunk-column seams. Debris over a pond that slept into the
// record reads the water through WaterCoreManager::columnWater -> recordColumnWater, which caches the record PER 32 x 32 CHUNK COLUMN
// (one span-reader call per chunk column, floor division for negative coordinates). The chunk rule (FeatureDesignKeys: chunks must
// not be visible): the per-chunk read must equal each column's own spans, everywhere - here a pond straddling the four chunk columns
// that meet at (0, 0), so x and z both cross 31/32 and -1/0, negative coordinates included (FloraMarginTest's shape: chunked = whole).
#include <gtest/gtest.h>

#include "core/water/WaterCore.h"
#include "core/water/WaterCoreManager.h"

#include <algorithm>
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

// 32.4 (A2b): the record cache follows chunk residency - streaming loads and unloads chunks WITHOUT a span revision bump. Before, a
// pond whose chunk just loaded read dry until the cache's 2 s age ran out (and a static debris tile was never asked again at all).
TEST(WaterRecordSeam, TheRecordFollowsResidency) {
    Record all;   // every column's spans, resident or not
    for (int z = 0; z < 32; ++z) for (int x = 0; x < 32; ++x) {
        all.cols[{32 + x, 32 + z}] = {{0.0f, 1.25f, 1.25}};      // chunk column (1, 1)
        all.cols[{-32 + x, -32 + z}] = {{0.0f, 1.75f, 1.75}};    // chunk column (-1, -1): the negative seam
    }
    std::map<std::pair<int, int>, int> resident;   // chunk column -> its "object" (0 = not resident)
    auto residentRecord = [&](Record& r) {         // the reader sees only resident chunk columns (as the chunk spans do)
        r.cols.clear();
        for (const auto& [k, v] : all.cols) {
            const int cx = k.first >= 0 ? k.first / 32 : -((-k.first + 31) / 32), cz = k.second >= 0 ? k.second / 32 : -((-k.second + 31) / 32);
            if (resident[{cx, cz}]) r.cols[k] = v;
        }
    };
    int columnCalls = 0;
    auto install = [&](WaterCoreManager& m) {
        m.setRecordResidency([&] { uint64_t h = 0; for (const auto& [k, o] : resident) if (o) h += 1000003ull * (k.first + 7) + 31ull * (k.second + 7) + o; return h; },
                             [&](int cx, int cz) -> uint64_t { ++columnCalls; const int o = resident[{cx, cz}]; return o ? 977ull * o + 13ull : 0ull; });
    };
    WaterCoreManager mgr([](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; });
    WaterCoreManager plain([](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; });   // the RED CONTROL: no residency hooks
    Record rec, recPlain;
    rec.install(mgr); recPlain.install(plain); install(mgr);
    float s = 0.0f; glm::vec2 fl(0.0f);
    // (1) not resident: both ponds read dry
    residentRecord(rec); residentRecord(recPlain);
    EXPECT_FALSE(mgr.columnWater(40, 40, s, fl)); EXPECT_FALSE(mgr.columnWater(-20, -20, s, fl));
    EXPECT_FALSE(plain.columnWater(40, 40, s, fl));
    // (2) both chunk columns load - no span revision bump - then one update (realtime off)
    resident[{1, 1}] = 1; resident[{-1, -1}] = 2;
    residentRecord(rec); residentRecord(recPlain);
    mgr.update(0.0f); plain.update(0.0f);
    const auto changed = mgr.takeRecordChanged();
    const bool wetA = mgr.columnWater(40, 40, s, fl); const float la = s;
    const bool wetB = mgr.columnWater(-20, -20, s, fl); const float lb = s;
    const bool plainWet = plain.columnWater(40, 40, s, fl);
    std::printf("  after the load: (40, 40) %s %.3f, (-20, -20) %s %.3f, %zu columns queued; without the hooks (the old cache) %s\n",
                wetA ? "wet" : "dry", la, wetB ? "wet" : "dry", lb, changed.size(), plainWet ? "wet" : "dry");
    EXPECT_FALSE(plainWet) << "RED CONTROL: the old cache saw the load at once - the rig does not reproduce the defect";
    EXPECT_TRUE(wetA) << "a pond whose chunk just loaded read dry";
    EXPECT_NEAR(la, 1.25f, 1e-6f);
    EXPECT_TRUE(wetB) << "the negative chunk column's pond read dry after its load";
    EXPECT_NEAR(lb, 1.75f, 1e-6f);
    auto has = [&](const std::vector<glm::ivec2>& v, glm::ivec2 c) { return std::find(v.begin(), v.end(), c) != v.end(); };
    EXPECT_TRUE(has(changed, {1, 1}) && has(changed, {-1, -1})) << "the debris tiles of the loaded columns were not queued for a rebuild";
    // the control: nothing changed - no column checks, no reads
    const int callsBefore = rec.calls, colBefore = columnCalls;
    mgr.update(0.0f);
    EXPECT_TRUE(mgr.takeRecordChanged().empty());
    EXPECT_EQ(columnCalls, colBefore) << "a frame with no residency change checked columns";
    EXPECT_TRUE(mgr.columnWater(40, 40, s, fl));
    EXPECT_EQ(rec.calls, callsBefore) << "a frame with no residency change re-read the record";
    // (3) both unload: evicted, queued, dry
    const long evictedBefore = mgr.recordCacheStats().evicted;
    resident[{1, 1}] = 0; resident[{-1, -1}] = 0;
    residentRecord(rec);
    mgr.update(0.0f);
    const auto gone = mgr.takeRecordChanged();
    EXPECT_TRUE(has(gone, {1, 1}) && has(gone, {-1, -1})) << "the unloaded columns were not queued";
    EXPECT_GE(mgr.recordCacheStats().evicted - evictedBefore, 2);
    EXPECT_FALSE(mgr.columnWater(40, 40, s, fl)) << "a pond whose chunk left still read wet";
    EXPECT_FALSE(mgr.columnWater(-20, -20, s, fl));
}

// 34 (A2h, owner 2026-10-25: "keep the more chaotic feature as a turn on, but not by default"): debris displacement ships OFF. The
// editor's water_coupling {displace} starts from this constant; turning it on is the owner's opt-in, never a silent default flip.
TEST(WaterDisplaceDefault, ItIsOffByDefault) {
    EXPECT_FALSE(WaterCoreManager::kDisplaceByDefault) << "debris displacement is the owner's opt-in (WaterCore 34)";
}
