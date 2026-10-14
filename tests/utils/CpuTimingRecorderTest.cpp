// CpuTimingRecorder: CPU scopes for the render path (docs/PerfProgram2026-09.md, I7).
#include <gtest/gtest.h>

#include <thread>

#include "utils/CpuTimingRecorder.h"

using Phyxel::CpuTimingRecorder;

namespace {
const Phyxel::GpuTimingStats* find(const std::vector<Phyxel::GpuTimingStats>& v, const std::string& key) {
    for (const auto& s : v)
        if (s.key == key) return &s;
    return nullptr;
}
}  // namespace

// Nested scopes get parent/child paths and depths; a child never outlasts its parent.
TEST(CpuTimingRecorderTest, NestedScopesKeyedByPath) {
    CpuTimingRecorder rec(16);
    rec.beginFrame();
    {
        CPU_PROFILE_SCOPE(&rec, "drawFrame");
        {
            CPU_PROFILE_SCOPE(&rec, "Record");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    rec.endFrame(1);
    const auto st = rec.history().stats(16);
    const auto* frame = find(st, "drawFrame");
    const auto* record = find(st, "drawFrame/Record");
    ASSERT_NE(frame, nullptr);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(frame->depth, 0u);
    EXPECT_EQ(record->depth, 1u);
    EXPECT_EQ(record->name, "Record");
    EXPECT_GE(record->last, 1.5);
    EXPECT_GE(frame->last, record->last);
}

// The same path twice in one frame gets distinct keys; a scope left open by an early return is
// closed at endFrame instead of being lost or corrupting the next frame.
TEST(CpuTimingRecorderTest, RepeatsAndUnclosedScopes) {
    CpuTimingRecorder rec(16);
    rec.beginFrame();
    rec.push("A");
    rec.pop();
    rec.push("A");
    rec.pop();
    rec.push("Open");   // never popped
    rec.endFrame(1);
    const auto st = rec.history().stats(16);
    EXPECT_NE(find(st, "A"), nullptr);
    EXPECT_NE(find(st, "A#1"), nullptr);
    EXPECT_NE(find(st, "Open"), nullptr);

    rec.beginFrame();
    rec.push("B");
    rec.pop();
    rec.endFrame(2);
    EXPECT_NE(find(rec.history().stats(1), "B"), nullptr);   // not "Open/B"
}

// A frame is accepted once (the history's serial rule applies to CPU frames too).
TEST(CpuTimingRecorderTest, FrameAcceptedOnce) {
    CpuTimingRecorder rec(16);
    rec.beginFrame();
    rec.push("X");
    rec.pop();
    rec.endFrame(7);
    rec.beginFrame();
    rec.push("X");
    rec.pop();
    rec.endFrame(7);
    EXPECT_EQ(rec.history().framesAccepted(), 1u);
    EXPECT_EQ(rec.history().staleSkipped(), 1u);
}
