// GpuTimingHistory: the per-scope GPU timing ring behind GET /api/debug/gpu_timing
// (docs/PerfProgram2026-09.md, P0 item I1).
//
// Before I1, gpu_scopes returned ONE frame, about two frames stale, with no history. Two defects would
// have followed a naive ring straight into the numbers, and each test below pins one:
//   * a NOT_READY readback left the previous frame's results in place, so a poller counted the
//     same frame again and again, inflating n and narrowing the confidence interval;
//   * scope names repeat within a frame ("Character Shadows" is recorded once per shadow cascade),
//     so a ring keyed by name would average two different passes.
#include <gtest/gtest.h>

#include "utils/GpuProfiler.h"
#include "utils/GpuTimingHistory.h"

using Phyxel::GpuTimingHistory;
using Phyxel::GpuTimingSample;

namespace {

GpuTimingSample sample(const std::string& key, double ms, uint32_t depth = 0) {
    GpuTimingSample s;
    s.key = key;
    s.name = key.substr(key.rfind('/') == std::string::npos ? 0 : key.rfind('/') + 1);
    s.depth = depth;
    s.ms = ms;
    return s;
}

const Phyxel::GpuTimingStats* find(const std::vector<Phyxel::GpuTimingStats>& v, const std::string& key) {
    for (const auto& s : v)
        if (s.key == key) return &s;
    return nullptr;
}

}  // namespace

// The median is taken over the requested window of accepted frames, not one frame.
TEST(GpuTimingHistoryTest, RingReportsMedianOverFrames) {
    GpuTimingHistory h(240);
    // 1..9 ms: median 5, mean 5.
    for (uint64_t f = 1; f <= 9; ++f) h.addFrame(f, {sample("Scene Pass", double(f))});

    const auto all = h.stats(240);
    const auto* s = find(all, "Scene Pass");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->n, 9u);
    EXPECT_DOUBLE_EQ(s->median, 5.0);
    EXPECT_DOUBLE_EQ(s->mean, 5.0);
    EXPECT_DOUBLE_EQ(s->last, 9.0);

    // A window of the last 3 frames sees 7, 8, 9 only.
    const auto recent = h.stats(3);
    const auto* r = find(recent, "Scene Pass");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->n, 3u);
    EXPECT_DOUBLE_EQ(r->median, 8.0);
}

// The ring holds `capacity` frames; older samples fall out.
TEST(GpuTimingHistoryTest, RingEvictsBeyondCapacity) {
    GpuTimingHistory h(4);
    for (uint64_t f = 1; f <= 10; ++f) h.addFrame(f, {sample("A", double(f))});
    const auto* s = find(h.stats(100), "A");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->n, 4u);                 // frames 7..10
    EXPECT_DOUBLE_EQ(s->median, 8.5);
    EXPECT_EQ(h.framesAccepted(), 10u);
}

// A readback that delivers a frame the ring has already seen (what the old NOT_READY path did by
// keeping the previous results) must not add samples. It is counted as stale instead.
TEST(GpuTimingHistoryTest, NotReadyDoesNotAddSamples) {
    GpuTimingHistory h(240);
    EXPECT_TRUE(h.addFrame(5, {sample("A", 1.0)}));
    for (int i = 0; i < 10; ++i)
        EXPECT_FALSE(h.addFrame(5, {sample("A", 1.0)}));   // the same frame, re-delivered
    EXPECT_FALSE(h.addFrame(4, {sample("A", 1.0)}));       // an older frame arriving late
    const auto* s = find(h.stats(240), "A");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->n, 1u);
    EXPECT_EQ(h.staleSkipped(), 11u);
}

// Two scopes with the same name under different parents keep separate histories.
TEST(GpuTimingHistoryTest, SameNameDistinctScopes) {
    GpuTimingHistory h(240);
    for (uint64_t f = 1; f <= 5; ++f) {
        h.addFrame(f, {sample("Shadow Pass/Shadow Mid/Character Shadows", 1.0, 2),
                       sample("Shadow Pass/Shadow Near/Character Shadows", 3.0, 2)});
    }
    const auto all = h.stats(240);
    const auto* mid = find(all, "Shadow Pass/Shadow Mid/Character Shadows");
    const auto* nr = find(all, "Shadow Pass/Shadow Near/Character Shadows");
    ASSERT_NE(mid, nullptr);
    ASSERT_NE(nr, nullptr);
    EXPECT_DOUBLE_EQ(mid->median, 1.0);
    EXPECT_DOUBLE_EQ(nr->median, 3.0);
    EXPECT_EQ(mid->name, "Character Shadows");
}

// Repeats of one path inside a single frame get distinct keys (#1, #2, ...), in recording order.
TEST(GpuTimingHistoryTest, RepeatedPathInOneFrameGetsOccurrenceKeys) {
    std::vector<std::string> paths = {"Scene Pass/Water", "Scene Pass/Water", "Scene Pass/Grass"};
    const auto keys = GpuTimingHistory::occurrenceKeys(paths);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], "Scene Pass/Water");
    EXPECT_EQ(keys[1], "Scene Pass/Water#1");
    EXPECT_EQ(keys[2], "Scene Pass/Grass");
}

// Percentiles use nearest-rank on the sorted window.
TEST(GpuTimingHistoryTest, PercentilesNearestRank) {
    GpuTimingHistory h(240);
    for (uint64_t f = 1; f <= 100; ++f) h.addFrame(f, {sample("A", double(f))});
    const auto* s = find(h.stats(240), "A");
    ASSERT_NE(s, nullptr);
    EXPECT_DOUBLE_EQ(s->p90, 90.0);
    EXPECT_DOUBLE_EQ(s->p99, 99.0);
}

// GPU_PROFILE_SCOPE must be usable twice in one block. It pasted _gpu_timer_##__LINE__ without an
// expansion step, so every use declared the same variable and a second scope did not compile.
TEST(GpuTimingHistoryTest, TwoScopesInOneBlockCompile) {
    Phyxel::GpuProfiler* none = nullptr;       // a null profiler makes the timer a no-op
    GPU_PROFILE_SCOPE(none, VK_NULL_HANDLE, "first");
    GPU_PROFILE_SCOPE(none, VK_NULL_HANDLE, "second");
    SUCCEED();
}

// V2 (docs/PerfProgram2026-09.md section 16, I11): frame pacing needs WHICH frame was slow, not only
// how often. series() returns the raw values behind stats(), frame by frame, oldest first.
TEST(GpuTimingHistoryTest, SeriesReturnsEachFramesValuesInOrder) {
    GpuTimingHistory h(4);
    for (uint64_t s = 1; s <= 6; ++s) {
        std::vector<GpuTimingSample> f{sample("Frame", 10.0 + s)};
        if (s == 5) f.push_back(sample("Frame/Streaming Pump", 30.0, 1));   // a hitch in frame 5 only
        ASSERT_TRUE(h.addFrame(s, f));
    }
    const auto all = h.series(100);                 // clamped to the 4 frames held
    ASSERT_EQ(all.size(), 4u);
    EXPECT_EQ(all.front().serial, 3u);
    EXPECT_EQ(all.back().serial, 6u);
    ASSERT_EQ(all[2].serial, 5u);
    ASSERT_EQ(all[2].values.size(), 2u);
    EXPECT_EQ(all[2].values[1].first, "Frame/Streaming Pump");
    EXPECT_DOUBLE_EQ(all[2].values[1].second, 30.0);
    EXPECT_EQ(all[1].values.size(), 1u);            // frame 4 had no pump sample
    EXPECT_DOUBLE_EQ(all[3].values[0].second, 16.0);

    const auto two = h.series(2);
    ASSERT_EQ(two.size(), 2u);
    EXPECT_EQ(two[0].serial, 5u);

    const auto st = h.stats(4);
    const auto* frame = find(st, "Frame");
    ASSERT_NE(frame, nullptr);
    EXPECT_DOUBLE_EQ(frame->max, 16.0);
    EXPECT_TRUE(h.series(0).empty());
}
