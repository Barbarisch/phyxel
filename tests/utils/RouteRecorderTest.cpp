// RouteRecorderTest -- V7 of docs/PerfProgram2026-09.md section 16 (I15). A benchmark route is
// ~6,000 frames; the 240-frame timing rings wrap, and polling them mid-route perturbs the frames being
// measured. The recorder must keep EVERY frame of a route from its first, stop (never wrap) when full,
// and attach late-resolving GPU timings to the frame that rendered them.
#include <gtest/gtest.h>

#include <cmath>

#include "utils/RouteRecorder.h"

using Phyxel::GpuTimingSample;
using Phyxel::RouteRecorder;

namespace {

GpuTimingSample s(const std::string& key, double ms) {
    GpuTimingSample x;
    x.key = key;
    x.name = key;
    x.ms = ms;
    return x;
}

RouteRecorder::FrameInput frame(uint64_t n, const std::vector<GpuTimingSample>* phases) {
    RouteRecorder::FrameInput in;
    in.frame = n;
    in.gpuSerial = 1000 + n;
    in.frameMs = static_cast<float>(n);
    in.phases = phases;
    in.cameraPos = glm::vec3(static_cast<float>(n), 0.0f, 0.0f);
    return in;
}

}  // namespace

TEST(RouteRecorderTest, StopsWhenFullKeepingTheFirstFramesNeverWraps) {
    RouteRecorder r;
    ASSERT_TRUE(r.start(100));
    const std::vector<GpuTimingSample> ph{s("Frame", 10.0)};
    for (uint64_t n = 1; n <= 300; ++n) r.addFrame(frame(n, &ph));
    EXPECT_EQ(r.frames(), 100u);
    EXPECT_TRUE(r.truncated());
    EXPECT_FALSE(r.recording());
    EXPECT_EQ(r.row(0).frame, 1u);     // the route's START is what a wrapping ring would have lost
    EXPECT_EQ(r.row(99).frame, 100u);
}

TEST(RouteRecorderTest, ShortRunIsCompleteAndContiguous) {
    RouteRecorder r;
    ASSERT_TRUE(r.start(1000));
    const std::vector<GpuTimingSample> ph{s("Frame", 10.0), s("Frame/API Drain", 0.5)};
    for (uint64_t n = 1; n <= 50; ++n) r.addFrame(frame(n, &ph));
    r.stop();
    EXPECT_EQ(r.frames(), 50u);
    EXPECT_FALSE(r.truncated());
    for (size_t i = 1; i < r.frames(); ++i) EXPECT_EQ(r.row(i).frame, r.row(i - 1).frame + 1);
    ASSERT_EQ(r.phaseKeys().size(), 2u);
    EXPECT_FLOAT_EQ(r.row(7).phaseMs[1], 0.5f);
    EXPECT_FLOAT_EQ(r.row(7).cameraPos.x, 8.0f);
}

TEST(RouteRecorderTest, GpuTimingsAttachToTheFrameThatRenderedThemEvenAfterStop) {
    RouteRecorder r;
    ASSERT_TRUE(r.start(10));
    for (uint64_t n = 1; n <= 5; ++n) r.addFrame(frame(n, nullptr));
    r.onGpuFrame(1002, {s("Scene Pass", 7.0)});   // frame 2's GPU work resolves late
    r.stop();
    r.onGpuFrame(1005, {s("Scene Pass", 9.0)});   // ...and frame 5's after the route ended
    r.onGpuFrame(4242, {s("Scene Pass", 1.0)});   // a serial nobody recorded: ignored
    EXPECT_TRUE(r.row(1).gpuResolved);
    EXPECT_FLOAT_EQ(r.row(1).gpuMs[0], 7.0f);
    EXPECT_TRUE(r.row(4).gpuResolved);
    EXPECT_FLOAT_EQ(r.row(4).gpuMs[0], 9.0f);
    EXPECT_FALSE(r.row(0).gpuResolved);            // never resolved: reported pending, not guessed
    EXPECT_TRUE(std::isnan(r.row(0).gpuMs[0]));
}

TEST(RouteRecorderTest, StartIsRefusedWhileRecordingAndCapacityIsClamped) {
    RouteRecorder r;
    ASSERT_TRUE(r.start(0));                        // clamped up to 1
    EXPECT_EQ(r.framesCapacity(), 1u);
    EXPECT_FALSE(r.start(10));                      // refused: already recording
    r.stop();
    ASSERT_TRUE(r.start(RouteRecorder::kMaxFrames + 5));
    EXPECT_EQ(r.framesCapacity(), RouteRecorder::kMaxFrames);
}

TEST(RouteRecorderTest, ScopesBeyondTheTableAreCountedNotSilentlyDropped) {
    RouteRecorder r;
    ASSERT_TRUE(r.start(4));
    std::vector<GpuTimingSample> many;
    for (size_t i = 0; i < RouteRecorder::kMaxPhases + 3; ++i) many.push_back(s("P" + std::to_string(i), 1.0));
    r.addFrame(frame(1, &many));
    EXPECT_EQ(r.phaseKeys().size(), RouteRecorder::kMaxPhases);
    EXPECT_EQ(r.droppedKeys(), 3u);
}
