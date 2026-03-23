#include <gtest/gtest.h>
#include "core/JobSystem.h"
#include <chrono>
#include <thread>

using namespace Phyxel::Core;
using json = nlohmann::json;

// ============================================================================
// Basic lifecycle tests
// ============================================================================

TEST(JobSystemTest, ConstructAndDestroy) {
    JobSystem js;
    EXPECT_FALSE(js.hasActiveJobs());
    EXPECT_EQ(js.activeJobCount(), 0u);
}

TEST(JobSystemTest, SubmitReturnsUniqueIds) {
    JobSystem js;
    auto id1 = js.submitJob({"test", {}, [](JobContext&) { return json{{"ok", true}}; }});
    auto id2 = js.submitJob({"test", {}, [](JobContext&) { return json{{"ok", true}}; }});
    EXPECT_NE(id1, id2);
    EXPECT_GT(id1, 0u);
    EXPECT_GT(id2, 0u);
    // Let jobs complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// ============================================================================
// Execution tests
// ============================================================================

TEST(JobSystemTest, SimpleJobCompletesSuccessfully) {
    JobSystem js;

    auto id = js.submitJob({
        "test_simple",
        json{{"input", 42}},
        [](JobContext&) -> json {
            return {{"result", 42}, {"success", true}};
        }
    });

    // Wait for completion
    for (int i = 0; i < 100; ++i) {
        js.processCompletedJobs();
        auto status = js.getJobStatus(id);
        ASSERT_TRUE(status.has_value());
        if (status->state == JobState::Complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto status = js.getJobStatus(id);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->state, JobState::Complete);
    EXPECT_EQ(status->type, "test_simple");
    EXPECT_EQ(status->result["result"], 42);
    EXPECT_TRUE(status->result["success"].get<bool>());
    EXPECT_GT(status->elapsedSeconds, 0.0);
}

TEST(JobSystemTest, JobWithMainThreadFinalize) {
    JobSystem js;
    bool finalized = false;

    auto id = js.submitJob({
        "test_finalize",
        {},
        [](JobContext&) -> json {
            return {{"bg_result", "done"}};
        },
        [&finalized](json& result) {
            // This runs on main thread
            result["finalized"] = true;
            finalized = true;
        }
    });

    // Wait until Completing state
    for (int i = 0; i < 100; ++i) {
        auto status = js.getJobStatus(id);
        if (status && status->state == JobState::Completing) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Should not be finalized yet
    EXPECT_FALSE(finalized);

    // Process finalization on "main thread"
    size_t count = js.processCompletedJobs();
    EXPECT_EQ(count, 1u);
    EXPECT_TRUE(finalized);

    auto status = js.getJobStatus(id);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->state, JobState::Complete);
    EXPECT_EQ(status->result["bg_result"], "done");
    EXPECT_TRUE(status->result["finalized"].get<bool>());
}

TEST(JobSystemTest, JobFailsOnException) {
    JobSystem js;

    auto id = js.submitJob({
        "test_fail",
        {},
        [](JobContext&) -> json {
            throw std::runtime_error("intentional failure");
        }
    });

    // Wait for failure
    for (int i = 0; i < 100; ++i) {
        js.processCompletedJobs();
        auto status = js.getJobStatus(id);
        if (status && status->state == JobState::Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto status = js.getJobStatus(id);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->state, JobState::Failed);
    EXPECT_TRUE(status->result.contains("error"));
}

// ============================================================================
// Progress reporting tests
// ============================================================================

TEST(JobSystemTest, ProgressReporting) {
    JobSystem js;

    std::atomic<bool> started{false};
    std::atomic<bool> canFinish{false};

    auto id = js.submitJob({
        "test_progress",
        {},
        [&started, &canFinish](JobContext& ctx) -> json {
            started = true;
            ctx.setProgress(0.5f, "Halfway");
            // Wait until test has had a chance to read progress
            while (!canFinish.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (ctx.cancelled) return {{"cancelled", true}};
            }
            ctx.setProgress(1.0f, "Done");
            return {{"success", true}};
        }
    });

    // Wait for the job to actually start and set progress
    while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Give it a moment to set the 0.5 progress
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto status = js.getJobStatus(id);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->state, JobState::Running);
    EXPECT_FLOAT_EQ(status->progress, 0.5f);
    EXPECT_EQ(status->message, "Halfway");

    // Let the job finish
    canFinish = true;

    // Wait for completion
    for (int i = 0; i < 500; ++i) {
        js.processCompletedJobs();
        status = js.getJobStatus(id);
        if (status && status->state == JobState::Complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    status = js.getJobStatus(id);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->state, JobState::Complete);
}

// ============================================================================
// Cancellation tests
// ============================================================================

TEST(JobSystemTest, CancelRunningJob) {
    JobSystem js;
    std::atomic<bool> started{false};

    auto id = js.submitJob({
        "test_cancel",
        {},
        [&started](JobContext& ctx) -> json {
            started = true;
            // Long-running work that checks cancellation
            for (int i = 0; i < 1000; ++i) {
                if (ctx.cancelled) return {{"partial", i}};
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return {{"complete", true}};
        }
    });

    // Wait until running
    for (int i = 0; i < 100 && !started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(started.load());

    // Cancel
    bool cancelled = js.cancelJob(id);
    EXPECT_TRUE(cancelled);

    // Wait for cancellation to take effect
    for (int i = 0; i < 100; ++i) {
        js.processCompletedJobs();
        auto status = js.getJobStatus(id);
        if (status && status->state == JobState::Cancelled) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto status = js.getJobStatus(id);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->state, JobState::Cancelled);
}

TEST(JobSystemTest, CancelPendingJob) {
    JobSystem js;
    // Submit a slow job first to block the worker
    js.submitJob({
        "blocker",
        {},
        [](JobContext& ctx) -> json {
            for (int i = 0; i < 100; ++i) {
                if (ctx.cancelled) return {};
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return {};
        }
    });

    // Submit second job — should be pending while first runs
    auto id2 = js.submitJob({
        "pending_cancel",
        {},
        [](JobContext&) -> json { return {{"done", true}}; }
    });

    // Give first job time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Cancel the pending job
    bool cancelled = js.cancelJob(id2);
    EXPECT_TRUE(cancelled);

    auto status = js.getJobStatus(id2);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->state, JobState::Cancelled);
}

TEST(JobSystemTest, CancelCompletedJobReturnsFalse) {
    JobSystem js;

    auto id = js.submitJob({
        "fast",
        {},
        [](JobContext&) -> json { return {{"ok", true}}; }
    });

    // Wait for completion
    for (int i = 0; i < 100; ++i) {
        js.processCompletedJobs();
        auto status = js.getJobStatus(id);
        if (status && status->state == JobState::Complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_FALSE(js.cancelJob(id));
}

// ============================================================================
// List / query tests
// ============================================================================

TEST(JobSystemTest, ListJobsShowsAllActive) {
    JobSystem js;

    // Submit a slow job
    js.submitJob({
        "slow_list_test",
        {},
        [](JobContext& ctx) -> json {
            for (int i = 0; i < 50; ++i) {
                if (ctx.cancelled) return {};
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return {};
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto jobs = js.listJobs();
    EXPECT_GE(jobs.size(), 1u);

    bool found = false;
    for (auto& j : jobs) {
        if (j.type == "slow_list_test") found = true;
    }
    EXPECT_TRUE(found);
}

TEST(JobSystemTest, UnknownJobReturnsNullopt) {
    JobSystem js;
    EXPECT_FALSE(js.getJobStatus(99999).has_value());
    EXPECT_TRUE(js.getJobResult(99999).empty());
}

// ============================================================================
// Ordering tests
// ============================================================================

TEST(JobSystemTest, JobsExecuteInFIFOOrder) {
    JobSystem js;
    std::vector<int> executionOrder;
    std::mutex orderMutex;

    // Submit a blocking job first
    std::atomic<bool> gate{false};
    auto blockerId = js.submitJob({
        "gate",
        {},
        [&gate](JobContext& ctx) -> json {
            while (!gate.load() && !ctx.cancelled) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return {};
        }
    });

    // Wait for blocker to start
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Queue 3 jobs while blocked
    for (int i = 1; i <= 3; ++i) {
        js.submitJob({
            "ordered_" + std::to_string(i),
            {},
            [i, &executionOrder, &orderMutex](JobContext&) -> json {
                std::lock_guard lock(orderMutex);
                executionOrder.push_back(i);
                return {{"order", i}};
            }
        });
    }

    // Release gate
    gate = true;

    // Wait for all to complete
    for (int i = 0; i < 200; ++i) {
        js.processCompletedJobs();
        if (js.activeJobCount() == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::lock_guard lock(orderMutex);
    ASSERT_EQ(executionOrder.size(), 3u);
    EXPECT_EQ(executionOrder[0], 1);
    EXPECT_EQ(executionOrder[1], 2);
    EXPECT_EQ(executionOrder[2], 3);
}

// ============================================================================
// JobStatus serialization
// ============================================================================

TEST(JobSystemTest, StatusToJson) {
    JobStatus status;
    status.id = 42;
    status.state = JobState::Running;
    status.type = "fill_region";
    status.progress = 0.65f;
    status.message = "Placing voxels (32500/50000)";
    status.elapsedSeconds = 1.5;

    auto j = status.toJson();
    EXPECT_EQ(j["id"], 42u);
    EXPECT_EQ(j["state"], "running");
    EXPECT_EQ(j["type"], "fill_region");
    EXPECT_FLOAT_EQ(j["progress"].get<float>(), 0.65f);
    EXPECT_EQ(j["message"], "Placing voxels (32500/50000)");
    // Result not included when running
    EXPECT_FALSE(j.contains("result"));
}

TEST(JobSystemTest, CompletedStatusIncludesResult) {
    JobStatus status;
    status.id = 1;
    status.state = JobState::Complete;
    status.type = "test";
    status.result = {{"placed", 5000}};

    auto j = status.toJson();
    EXPECT_TRUE(j.contains("result"));
    EXPECT_EQ(j["result"]["placed"], 5000);
}

// ============================================================================
// JobState string conversion
// ============================================================================

TEST(JobSystemTest, JobStateStrings) {
    EXPECT_STREQ(jobStateToString(JobState::Pending), "pending");
    EXPECT_STREQ(jobStateToString(JobState::Running), "running");
    EXPECT_STREQ(jobStateToString(JobState::Completing), "completing");
    EXPECT_STREQ(jobStateToString(JobState::Complete), "complete");
    EXPECT_STREQ(jobStateToString(JobState::Failed), "failed");
    EXPECT_STREQ(jobStateToString(JobState::Cancelled), "cancelled");
}

// ============================================================================
// Active count tracking
// ============================================================================

TEST(JobSystemTest, ActiveJobCountTracking) {
    JobSystem js;

    EXPECT_EQ(js.activeJobCount(), 0u);
    EXPECT_FALSE(js.hasActiveJobs());

    std::atomic<bool> gate{false};
    js.submitJob({
        "held",
        {},
        [&gate](JobContext& ctx) -> json {
            while (!gate.load() && !ctx.cancelled) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return {};
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_TRUE(js.hasActiveJobs());
    EXPECT_GE(js.activeJobCount(), 1u);

    gate = true;

    for (int i = 0; i < 100; ++i) {
        js.processCompletedJobs();
        if (!js.hasActiveJobs()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_FALSE(js.hasActiveJobs());
    EXPECT_EQ(js.activeJobCount(), 0u);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(JobSystemTest, ProcessCompletedJobsWithNoPendingReturnsZero) {
    JobSystem js;
    EXPECT_EQ(js.processCompletedJobs(), 0u);
}

TEST(JobSystemTest, MultipleRapidSubmissions) {
    JobSystem js;
    constexpr int N = 20;
    std::atomic<int> completed{0};

    for (int i = 0; i < N; ++i) {
        js.submitJob({
            "rapid_" + std::to_string(i),
            {},
            [&completed](JobContext&) -> json {
                completed++;
                return {{"ok", true}};
            }
        });
    }

    // Wait for all to complete
    for (int i = 0; i < 300; ++i) {
        js.processCompletedJobs();
        if (completed.load() >= N) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(completed.load(), N);
}
