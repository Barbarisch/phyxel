#include <gtest/gtest.h>

#include "core/water/WaterSpanWriteBack.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

// WaterCore Phase D slice D1 (docs/WaterCore.md 16.2): the pure clip/assemble/merge half of the
// write-back, and the chunk-seam equality test of 14.2 (test 1): an active volume straddling a
// chunk border in X and a vertical border in Y writes clips that, assembled, equal the world-space
// runs - and the two clips of one run can never disagree, because they are produced from the same
// world-space object.

namespace Phyxel::Core::Water {
namespace {

ColumnRun run(float lo, float hi) { ColumnRun r; r.bottomY = lo; r.topY = hi; r.mass = hi - lo; return r; }

TEST(WaterSpanWriteBackTest, ClipAcrossAVerticalBorderAndReassemble) {
    // one run 20.5 .. 40.25 in column (35, 7): chunk (1, 0, 0) holds 20.5..32, chunk (1, 1, 0) holds 32..40.25
    WorldSpan s; s.x = 35; s.z = 7; s.bottomY = 20.5f; s.topY = 40.25f;
    std::map<int, std::vector<Chunk::WaterSpanLocal>> clips;
    clipSpansToChunks({s}, 1, 0, 0, 1, clips);
    ASSERT_EQ(clips.size(), 2u);
    ASSERT_EQ(clips[0].size(), 1u);
    ASSERT_EQ(clips[1].size(), 1u);
    EXPECT_EQ(clips[0][0].x, 3); EXPECT_EQ(clips[0][0].z, 7);
    EXPECT_FLOAT_EQ(clips[0][0].bottom, 20.5f); EXPECT_FLOAT_EQ(clips[0][0].top, 32.0f) << "continues above";
    EXPECT_FLOAT_EQ(clips[1][0].bottom, 0.0f);  EXPECT_FLOAT_EQ(clips[1][0].top, 8.25f);
    std::map<int, const std::vector<Chunk::WaterSpanLocal>*> chunks{{0, &clips[0]}, {1, &clips[1]}};
    const auto back = assembleColumnSpans(chunks, 1, 0);
    ASSERT_EQ(back.size(), 1u) << "the two clips are one run again";
    EXPECT_EQ(back[0].x, 35); EXPECT_EQ(back[0].z, 7);
    EXPECT_FLOAT_EQ(back[0].bottomY, 20.5f); EXPECT_FLOAT_EQ(back[0].topY, 40.25f);
}

TEST(WaterSpanWriteBackTest, MergeReplacesOnlyTheBoxRange) {
    // a tall column 10..60 stored; a volume covering y 30..50 writes a run 30..44.2 (it drained 5.8 m)
    WorldSpan e; e.x = 4; e.z = 4; e.bottomY = 10.0f; e.topY = 60.0f;
    const auto m = mergeColumnRuns({e}, 4, 4, 30.0f, 50.0f, {run(30.0f, 44.2f)});
    ASSERT_EQ(m.size(), 2u) << "the part below the box joins the new run; the part above stays separate";
    EXPECT_FLOAT_EQ(m[0].bottomY, 10.0f); EXPECT_FLOAT_EQ(m[0].topY, 44.2f);
    EXPECT_FLOAT_EQ(m[1].bottomY, 50.0f); EXPECT_FLOAT_EQ(m[1].topY, 60.0f);
    // the volume found NO water in its range: the stored middle is removed, nothing is created
    const auto d = mergeColumnRuns({e}, 4, 4, 30.0f, 50.0f, {});
    ASSERT_EQ(d.size(), 2u);
    EXPECT_FLOAT_EQ(d[0].topY, 30.0f); EXPECT_FLOAT_EQ(d[1].bottomY, 50.0f);
    // another column is untouched
    EXPECT_TRUE(mergeColumnRuns({e}, 5, 4, 30.0f, 50.0f, {run(30.0f, 31.0f)}).size() == 1u);
}

TEST(WaterSpanWriteBackTest, DrainedChunkIsClearedNotKept) {
    std::map<int, std::vector<Chunk::WaterSpanLocal>> clips;
    clipSpansToChunks({}, 0, 0, 0, 2, clips);
    ASSERT_EQ(clips.size(), 3u) << "every chunk of the range is present so the caller clears it";
    for (auto& [cy, l] : clips) EXPECT_TRUE(l.empty());
}

TEST(WaterSpanWriteBackTest, SpansToRunsClipsToTheBox) {
    WorldSpan a; a.x = 1; a.z = 2; a.bottomY = 3.0f; a.topY = 9.5f;
    WorldSpan b; b.x = 1; b.z = 2; b.bottomY = 12.0f; b.topY = 13.0f;   // a second run (a cave lake over a pool)
    std::vector<ColumnRuns> out;
    spansToColumnRuns({a, b}, 5.0f, 12.5f, out);
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0].runs.size(), 2u);
    EXPECT_FLOAT_EQ(out[0].runs[0].bottomY, 5.0f); EXPECT_FLOAT_EQ(out[0].runs[0].topY, 9.5f);
    EXPECT_NEAR(out[0].runs[0].mass, 4.5, 1e-6);
    EXPECT_FLOAT_EQ(out[0].runs[1].bottomY, 12.0f); EXPECT_FLOAT_EQ(out[0].runs[1].topY, 12.5f);
}

// 14.2 test 1: an AV straddling a chunk border in X (columns 30..33) and a vertical border in Y
// (box y 28..36) holds TWO bodies at different levels (west 31.4, east 34.75) so a coincidence
// cannot pass. Written through per-chunk clips, then assembled per chunk column, the result equals
// the world-space runs exactly, on both sides of both borders.
TEST(WaterCoreSeamTest, WriteBackIdenticalAcrossChunkSeams) {
    std::vector<ColumnRuns> cols;
    for (int x = 30; x <= 33; ++x) for (int z = 0; z <= 1; ++z) {
        ColumnRuns c; c.x = x; c.z = z;
        c.runs.push_back(x <= 31 ? run(28.0f, 31.4f) : run(28.0f, 34.75f));
        cols.push_back(c);
    }
    // write: per chunk column, merge into (empty) existing spans, clip to the chunks the box crosses
    std::map<std::pair<int, int>, std::map<int, std::vector<Chunk::WaterSpanLocal>>> stored;   // (cx,cz) -> cy -> spans
    for (const auto& c : cols) {
        const int cx = c.x / 32, cz = c.z / 32;
        auto& chunkCol = stored[{cx, cz}];
        std::map<int, const std::vector<Chunk::WaterSpanLocal>*> have;
        for (auto& [cy, l] : chunkCol) have[cy] = &l;
        auto world = assembleColumnSpans(have, cx, cz);
        // keep other columns, replace this one
        std::vector<WorldSpan> others;
        for (const auto& w : world) if (!(w.x == c.x && w.z == c.z)) others.push_back(w);
        auto merged = mergeColumnRuns(world, c.x, c.z, 28.0f, 37.0f, c.runs);
        others.insert(others.end(), merged.begin(), merged.end());
        std::map<int, std::vector<Chunk::WaterSpanLocal>> clips;
        clipSpansToChunks(others, cx, cz, 0, 1, clips);
        chunkCol = clips;
    }
    ASSERT_EQ(stored.size(), 2u) << "two chunk columns: cx 0 and cx 1";
    // the east run crosses y = 32: its lower clip ends at 32, its upper clip starts at 0
    const auto& east = stored[{1, 0}];
    ASSERT_EQ(east.at(0).size(), 4u); ASSERT_EQ(east.at(1).size(), 4u);
    for (const auto& s : east.at(0)) { EXPECT_FLOAT_EQ(s.top, 32.0f); EXPECT_FLOAT_EQ(s.bottom, 28.0f); }
    for (const auto& s : east.at(1)) { EXPECT_FLOAT_EQ(s.bottom, 0.0f); EXPECT_FLOAT_EQ(s.top, 2.75f); }
    const auto& west = stored[{0, 0}];
    ASSERT_EQ(west.at(0).size(), 4u); EXPECT_TRUE(west.at(1).empty()) << "the west body ends below the vertical border";
    // read back per chunk column and compare with the world-space runs
    for (const auto& c : cols) {
        const int cx = c.x / 32, cz = c.z / 32;
        std::map<int, const std::vector<Chunk::WaterSpanLocal>*> have;
        for (auto& [cy, l] : stored[{cx, cz}]) have[cy] = &l;
        const auto world = assembleColumnSpans(have, cx, cz);
        int found = 0;
        for (const auto& w : world) if (w.x == c.x && w.z == c.z) { ++found; EXPECT_FLOAT_EQ(w.bottomY, c.runs[0].bottomY); EXPECT_FLOAT_EQ(w.topY, c.runs[0].topY) << "column " << c.x; }
        EXPECT_EQ(found, 1) << "column " << c.x << "," << c.z;
    }
}

// 58 item 1 (no stutter): the record's span writer cost 4.7-5.6 ms per 8 m tile live (and the same in every tile wake): each
// written column was merged against the WHOLE chunk column's assembled spans (cols x spans) and all of it re-clipped. The writer is
// now writeRunsIntoChunkColumn; this pins (a) it writes exactly what the old path wrote and (b) its work is linear in the chunk
// column's spans - RED before the rewrite (the old path visits ~65x them for a 64-column tile).
namespace {
using Locals = std::vector<Chunk::WaterSpanLocal>;
// the old path, kept as the reference
std::map<int, Locals> referenceWrite(const std::map<int, const Locals*>& have, int cx, int cz, const std::vector<const ColumnRuns*>& cols, float yLo, float yHi) {
    std::vector<WorldSpan> world = assembleColumnSpans(have, cx, cz), result;
    std::set<std::pair<int, int>> written;
    for (const ColumnRuns* c : cols) written.insert({c->x, c->z});
    for (const auto& w : world) if (!written.count({w.x, w.z})) result.push_back(w);
    for (const ColumnRuns* c : cols) { auto m = mergeColumnRuns(world, c->x, c->z, yLo, yHi, c->runs); result.insert(result.end(), m.begin(), m.end()); }
    std::map<int, Locals> clips;
    clipSpansToChunks(result, cx, cz, have.begin()->first, have.rbegin()->first, clips);
    return clips;
}
}  // namespace

TEST(WaterSpanWriteBackTest, ATileWriteTouchesOnlyItsColumnsAndWritesWhatTheOldPathWrote) {
    const int cx = 2, cz = -1;
    // a lake over the whole chunk column: every column wet from 40.1 to 75.37 (crosses cy 1 / 2); every 3rd column a second,
    // perched run 90.2-93.4 (cy 2); cy 0 and 3 resident and dry
    std::map<int, Locals> store;
    for (int cy = 0; cy <= 3; ++cy) store[cy];
    std::vector<WorldSpan> lake;
    for (int x = 0; x < 32; ++x) for (int z = 0; z < 32; ++z) {
        WorldSpan w; w.x = cx * 32 + x; w.z = cz * 32 + z; w.bottomY = 40.1f; w.topY = 75.37f; lake.push_back(w);
        if ((x + z) % 3 == 0) { w.bottomY = 90.2f; w.topY = 93.4f; lake.push_back(w); }
    }
    { std::map<int, Locals> c; clipSpansToChunks(lake, cx, cz, 0, 3, c); for (auto& [cy, l] : c) store[cy] = l; }
    long total = 0; for (const auto& [cy, l] : store) total += static_cast<long>(l.size());
    // the write: one 8 m tile (local x, z 8..15), the band [38, 80) - new levels, two columns drained, one column with two runs
    const float yLo = 38.0f, yHi = 80.0f;
    std::vector<ColumnRuns> runs;
    for (int x = 8; x < 16; ++x) for (int z = 8; z < 16; ++z) {
        ColumnRuns c; c.x = cx * 32 + x; c.z = cz * 32 + z;
        if (!(x == 9 && (z == 9 || z == 10))) c.runs.push_back(run(40.1f, 74.0f + 0.1f * static_cast<float>(x - 8)));
        if (x == 12 && z == 12) c.runs.push_back(run(76.0f, 78.5f));
        runs.push_back(c);
    }
    std::vector<const ColumnRuns*> cols; for (const auto& c : runs) cols.push_back(&c);
    long writtenSpans = 0;
    for (const auto& [cy, l] : store) for (const auto& sp : l) if (sp.x >= 8 && sp.x < 16 && sp.z >= 8 && sp.z < 16) ++writtenSpans;
    std::map<int, const Locals*> have; for (auto& [cy, l] : store) have[cy] = &l;
    const std::map<int, Locals> ref = referenceWrite(have, cx, cz, cols, yLo, yHi);
    std::map<int, Locals> out; ChunkColumnWriteStats st;
    writeRunsIntoChunkColumn(have, cx, cz, cols, yLo, yHi, out, &st);
    // the store after the write (unchanged chunks keep their list) against the reference's lists
    int differ = 0, changedRef = 0; float worst = 0.0f;
    for (const auto& [cy, l] : store) {
        const Locals& got = out.count(cy) ? out.at(cy) : l;
        const Locals& want = ref.count(cy) ? ref.at(cy) : l;
        bool chg = want.size() != l.size();
        for (size_t i = 0; !chg && i < l.size(); ++i) chg = std::abs(want[i].top - l[i].top) > 1e-4f || std::abs(want[i].bottom - l[i].bottom) > 1e-4f || want[i].x != l[i].x || want[i].z != l[i].z;
        changedRef += chg ? 1 : 0;
        if (got.size() != want.size()) { ++differ; continue; }
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i].x != want[i].x || got[i].z != want[i].z) { ++differ; break; }
            worst = std::max({worst, std::abs(got[i].bottom - want[i].bottom), std::abs(got[i].top - want[i].top)});
        }
    }
    std::printf("[spans] chunk column: %ld spans, the tile's %ld; written: %ld spans visited, %d chunks changed (the reference changed %d); "
                "%d chunks differ from the old path, worst %.6f m\n", total, writtenSpans, st.spansVisited, st.chunksChanged, changedRef, differ, worst);
    EXPECT_EQ(differ, 0);
    EXPECT_LE(worst, 1e-4f);
    EXPECT_EQ(st.chunksChanged, changedRef) << "only the chunks whose list changed are written";
    EXPECT_GT(changedRef, 0) << "the control: the write changes something";
    // the cost: one pass over the chunk column's spans plus the tile's own a few times - not the tile's columns times all of them
    EXPECT_LE(st.spansVisited, total + 8 * writtenSpans) << "the writer's work grew with columns x spans";
}

}  // namespace
}  // namespace Phyxel::Core::Water
