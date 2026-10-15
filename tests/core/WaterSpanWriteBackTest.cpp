#include <gtest/gtest.h>

#include "core/water/WaterSpanWriteBack.h"

#include <cmath>
#include <map>
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

}  // namespace
}  // namespace Phyxel::Core::Water
