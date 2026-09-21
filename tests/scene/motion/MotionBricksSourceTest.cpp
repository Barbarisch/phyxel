#include <gtest/gtest.h>

#include "scene/motion/MotionBricksSource.h"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace Phyxel::Scene::Motion;

TEST(MotionBricksSource, MissingLibraryFailsCleanly) {
    std::string error;
    EXPECT_FALSE(MotionBricksRuntime::probeLibrary("definitely_missing_motionbricks.dll", error));
    EXPECT_FALSE(error.empty());

    MotionBricksConfig config;
    config.libraryPath = "definitely_missing_motionbricks.dll";
    EXPECT_EQ(MotionBricksRuntime::create(config, error), nullptr);
    EXPECT_FALSE(error.empty());
}

TEST(MotionBricksSource, PinnedAbiLibraryLoadsWhenProvided) {
    const char* path = std::getenv("PHYXEL_MOTIONBRICKS_TEST_DLL");
    if (!path || !path[0]) GTEST_SKIP() << "set PHYXEL_MOTIONBRICKS_TEST_DLL for native smoke";
    std::string error;
    EXPECT_TRUE(MotionBricksRuntime::probeLibrary(path, error)) << error;
}

TEST(MotionBricksSource, PinnedAbiLibraryCanBeProbedRepeatedly) {
    const char* path = std::getenv("PHYXEL_MOTIONBRICKS_TEST_DLL");
    if (!path || !path[0]) GTEST_SKIP() << "set PHYXEL_MOTIONBRICKS_TEST_DLL for native smoke";
    for (int i = 0; i < 100; ++i) {
        std::string error;
        ASSERT_TRUE(MotionBricksRuntime::probeLibrary(path, error)) << "iteration " << i << ": " << error;
    }
}

TEST(MotionBricksSource, RealCpuBackendReturnsFiniteMotionWhenAssetsProvided) {
    const char* library = std::getenv("PHYXEL_MOTIONBRICKS_TEST_DLL");
    const char* assets = std::getenv("PHYXEL_MOTIONBRICKS_TEST_ASSETS");
    if (!library || !library[0] || !assets || !assets[0])
        GTEST_SKIP() << "set MotionBricks DLL and asset environment variables";

    const std::filesystem::path root(assets);
    MotionBricksConfig config;
    config.libraryPath = library;
    config.modelDirectory = (root / "g1-f32").string();
    config.stylePath = (root / "styles" / "walk.mbstyle").string();
    config.device = MotionBricksDevice::Cpu;
    config.threads = 2;
    std::string error;
    auto runtime = MotionBricksRuntime::create(config, error);
    ASSERT_NE(runtime, nullptr) << error;
    EXPECT_EQ(runtime->jointNames().size(), 34u);
    auto source = runtime->createSource(error);
    ASSERT_NE(source, nullptr) << error;

    MotionIntent intent;
    intent.movementDirection = {0.0f, 0.0f, 1.0f};
    intent.facingDirection = intent.movementDirection;
    intent.targetSpeed = 1.0f;
    intent.seed = 0x50585958454cULL;
    source->submitIntent(intent);

    LocalPoseFrame pose;
    bool sampled = false;
    for (int i = 0; i < 1200 && !sampled; ++i) {
        sampled = source->sample(i * 0.01, pose);
        if (!sampled) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(sampled) << source->status().error;
    EXPECT_TRUE(pose.structurallyValid());
    EXPECT_EQ(pose.localRotations.size(), 34u);
    EXPECT_EQ(pose.provenance, MotionProvenance::MotionBricks);
    EXPECT_EQ(source->status().effectiveProvider, "motionbricks");
}

TEST(MotionBricksSource, RealCpuMultiAgentStressWhenRequested) {
    const char* countText = std::getenv("PHYXEL_MOTIONBRICKS_STRESS_AGENTS");
    const char* library = std::getenv("PHYXEL_MOTIONBRICKS_TEST_DLL");
    const char* assets = std::getenv("PHYXEL_MOTIONBRICKS_TEST_ASSETS");
    if (!countText || !library || !assets) GTEST_SKIP() << "stress test is explicit opt-in";
    const int count = std::stoi(countText);
    ASSERT_GT(count, 0);
    ASSERT_LE(count, 100);

    const std::filesystem::path root(assets);
    MotionBricksConfig config{library, (root / "g1-f32").string(),
                              (root / "styles" / "walk.mbstyle").string(),
                              MotionBricksDevice::Cpu, 2};
    std::string error;
    auto runtime = MotionBricksRuntime::create(config, error);
    ASSERT_NE(runtime, nullptr) << error;
    std::vector<std::shared_ptr<MotionBricksSource>> sources;
    for (int i = 0; i < count; ++i) {
        auto source = runtime->createSource(error);
        ASSERT_NE(source, nullptr) << "agent " << i << ": " << error;
        MotionIntent intent;
        intent.movementDirection = {0, 0, 1};
        intent.facingDirection = {0, 0, 1};
        intent.targetSpeed = 1.0f;
        intent.seed = static_cast<std::uint64_t>(i + 1);
        source->submitIntent(intent);
        sources.push_back(std::move(source));
    }
    const auto start = std::chrono::steady_clock::now();
    std::vector<bool> ready(static_cast<std::size_t>(count), false);
    int readyCount = 0;
    for (int tick = 0; tick < 18000 && readyCount < count; ++tick) {
        for (int i = 0; i < count; ++i) {
            if (ready[static_cast<std::size_t>(i)]) continue;
            LocalPoseFrame pose;
            if (sources[static_cast<std::size_t>(i)]->sample(tick * 0.01, pose)) {
                ASSERT_TRUE(pose.structurallyValid());
                ready[static_cast<std::size_t>(i)] = true;
                ++readyCount;
            }
        }
        if (readyCount < count) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    EXPECT_EQ(readyCount, count) << "ready in " << elapsed << " seconds";
    RecordProperty("agents", count);
    RecordProperty("seconds", elapsed);
}
