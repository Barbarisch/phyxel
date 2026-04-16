#include <gtest/gtest.h>
#include "core/SceneManager.h"
#include "core/SceneDefinition.h"
#include "core/GameEventLog.h"
#include "graphics/Camera.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <string>

using namespace Phyxel::Core;
using json = nlohmann::json;

// ============================================================================
// SceneIntegrationTest
//
// Tests the SceneManager with wired callbacks that track invocation order,
// multi-transition flows, re-entry state persistence, and event emission.
// Does NOT require Vulkan — uses mock subsystems.
// ============================================================================

namespace {

/// Tracks callback invocations in order for assertion.
struct CallbackLog {
    std::vector<std::string> calls;
    void record(const std::string& name) { calls.push_back(name); }
    void clear() { calls.clear(); }
    bool contains(const std::string& name) const {
        for (const auto& c : calls) if (c == name) return true;
        return false;
    }
    size_t indexOf(const std::string& name) const {
        for (size_t i = 0; i < calls.size(); ++i)
            if (calls[i] == name) return i;
        return std::string::npos;
    }
};

SceneManifest buildTestManifest() {
    json scene1Def = {{"world", {{"type", "Flat"}, {"seed", 1}}},
                      {"camera", {{"position", {10, 20, 10}}}}};
    json scene2Def = {{"world", {{"type", "Perlin"}, {"seed", 2}}},
                      {"camera", {{"position", {50, 30, 50}}}}};
    json scene3Def = {{"camera", {{"position", {5, 5, 5}}}}};

    json manifest = {
        {"startScene", "scene_a"},
        {"scenes", json::array({
            {{"id", "scene_a"}, {"name", "Scene A"}, {"worldDatabase", "a.db"},
             {"transitionStyle", "cut"}, {"world", scene1Def["world"]}, {"camera", scene1Def["camera"]}},
            {{"id", "scene_b"}, {"name", "Scene B"}, {"worldDatabase", "b.db"},
             {"transitionStyle", "fade"}, {"world", scene2Def["world"]}, {"camera", scene2Def["camera"]}},
            {{"id", "scene_c"}, {"name", "Scene C"}, {"worldDatabase", "c.db"},
             {"transitionStyle", "loading_screen"}, {"camera", scene3Def["camera"]}}
        })}
    };
    return SceneManifest::fromJson(manifest);
}

} // anonymous namespace

class SceneIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        log.clear();
        loadingScreenState.clear();
        camera = std::make_unique<Phyxel::Graphics::Camera>();
        eventLog = std::make_unique<GameEventLog>();

        subsystems = {};
        subsystems.camera = camera.get();
        subsystems.gameEventLog = eventLog.get();
        // No chunkManager — we're testing the orchestration, not real I/O

        sm.setSubsystems(&subsystems);
        sm.setWorldsDir("worlds");

        SceneCallbacks cb;
        cb.endDialogue = [this]() { log.record("endDialogue"); };
        cb.clearEntities = [this]() { log.record("clearEntities"); };
        cb.clearNPCs = [this]() { log.record("clearNPCs"); };
        cb.clearSceneStoryCharacters = [this]() { log.record("clearSceneStoryCharacters"); };
        cb.resetLighting = [this]() { log.record("resetLighting"); };
        cb.clearMusic = [this]() { log.record("clearMusic"); };
        cb.clearObjectives = [this]() { log.record("clearObjectives"); };
        cb.setLoadingScreen = [this](bool show, const std::string& name) {
            loadingScreenState.push_back({show, name});
            log.record(show ? "loadingScreen_on" : "loadingScreen_off");
        };
        cb.rebuildNavGrid = [this]() { log.record("rebuildNavGrid"); };
        cb.runScript = [this](const std::string& path) { log.record("runScript:" + path); };
        cb.onSceneReady = [this](const std::string& id) { log.record("ready:" + id); };
        sm.setCallbacks(cb);

        // Load manifest
        auto manifest = buildTestManifest();
        sm.loadManifest(manifest);
    }

    SceneManager sm;
    CallbackLog log;
    std::vector<std::pair<bool, std::string>> loadingScreenState;
    std::unique_ptr<Phyxel::Graphics::Camera> camera;
    std::unique_ptr<GameEventLog> eventLog;
    GameSubsystems subsystems;
};

// ============================================================================
// Initial scene load
// ============================================================================

TEST_F(SceneIntegrationTest, LoadStartSceneTransitionsToReady) {
    ASSERT_TRUE(sm.loadStartScene());
    EXPECT_EQ(sm.getState(), SceneState::Loading);

    sm.update(0.0f);

    EXPECT_EQ(sm.getState(), SceneState::Ready);
    EXPECT_EQ(sm.getActiveSceneId(), "scene_a");
    EXPECT_TRUE(sm.getLastTransitionResult().success);
}

TEST_F(SceneIntegrationTest, LoadStartSceneCallsOnSceneReady) {
    sm.loadStartScene();
    sm.update(0.0f);

    EXPECT_TRUE(log.contains("ready:scene_a"));
}

TEST_F(SceneIntegrationTest, LoadStartSceneShowsAndHidesLoadingScreen) {
    sm.loadStartScene();
    sm.update(0.0f);

    // Loading screen should have been shown then hidden
    ASSERT_GE(loadingScreenState.size(), 2u);
    EXPECT_TRUE(loadingScreenState[0].first);   // shown
    EXPECT_EQ(loadingScreenState[0].second, "Scene A");
    EXPECT_FALSE(loadingScreenState.back().first); // hidden
}

// ============================================================================
// Full transition (unload → load)
// ============================================================================

TEST_F(SceneIntegrationTest, TransitionUnloadsAndLoadsCorrectly) {
    // Load initial scene
    sm.loadStartScene();
    sm.update(0.0f);
    ASSERT_EQ(sm.getActiveSceneId(), "scene_a");

    log.clear();
    loadingScreenState.clear();

    // Transition to scene_b
    ASSERT_TRUE(sm.transitionTo("scene_b"));
    EXPECT_EQ(sm.getState(), SceneState::Unloading);

    sm.update(0.0f); // unload
    EXPECT_EQ(sm.getState(), SceneState::Loading);

    sm.update(0.0f); // load
    EXPECT_EQ(sm.getState(), SceneState::Ready);
    EXPECT_EQ(sm.getActiveSceneId(), "scene_b");
}

TEST_F(SceneIntegrationTest, UnloadCallbackOrder) {
    sm.loadStartScene();
    sm.update(0.0f);
    log.clear();

    sm.transitionTo("scene_b");
    sm.update(0.0f); // triggers unload

    // endDialogue should come first during unload
    EXPECT_TRUE(log.contains("endDialogue"));
    EXPECT_TRUE(log.contains("clearNPCs"));
    EXPECT_TRUE(log.contains("clearEntities"));

    // endDialogue before clearNPCs
    size_t dialog = log.indexOf("endDialogue");
    size_t npcs = log.indexOf("clearNPCs");
    size_t entities = log.indexOf("clearEntities");
    EXPECT_LT(dialog, npcs);
    EXPECT_LT(npcs, entities);
}

TEST_F(SceneIntegrationTest, TransitionEmitsSceneLoadedEvent) {
    sm.loadStartScene();
    sm.update(0.0f);

    sm.transitionTo("scene_b");
    sm.update(0.0f); // unload
    sm.update(0.0f); // load

    auto result = eventLog->pollSince(0);
    bool found = false;
    for (const auto& e : result.events) {
        if (e.type == "scene_loaded" && e.data.contains("scene_id") &&
            e.data["scene_id"] == "scene_b") {
            found = true;
            EXPECT_EQ(e.data["from_scene"], "scene_a");
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected scene_loaded event for scene_b";
}

// ============================================================================
// Re-entry state (player position persistence)
// ============================================================================

TEST_F(SceneIntegrationTest, ReentryStateSavesPlayerPosition) {
    sm.loadStartScene();
    sm.update(0.0f);

    // Move camera to a known position
    camera->setPosition(glm::vec3(100.0f, 50.0f, 75.0f));

    // Transition away — should save position
    sm.transitionTo("scene_b");
    sm.update(0.0f); // unload (saves camera pos)
    sm.update(0.0f); // load

    auto* reentry = sm.getReentryState("scene_a");
    ASSERT_NE(reentry, nullptr);
    EXPECT_TRUE(reentry->visited);
    EXPECT_FLOAT_EQ(reentry->lastPlayerX, 100.0f);
    EXPECT_FLOAT_EQ(reentry->lastPlayerY, 50.0f);
    EXPECT_FLOAT_EQ(reentry->lastPlayerZ, 75.0f);
}

TEST_F(SceneIntegrationTest, ReentryStateRestoresOnReturn) {
    sm.loadStartScene();
    sm.update(0.0f);

    // Mark a position in scene_a
    camera->setPosition(glm::vec3(100.0f, 50.0f, 75.0f));

    // Go to scene_b
    sm.transitionTo("scene_b");
    sm.update(0.0f);
    sm.update(0.0f);
    ASSERT_EQ(sm.getActiveSceneId(), "scene_b");

    // Return to scene_a — should restore camera position
    sm.transitionTo("scene_a");
    sm.update(0.0f);
    sm.update(0.0f);
    ASSERT_EQ(sm.getActiveSceneId(), "scene_a");

    auto pos = camera->getPosition();
    EXPECT_FLOAT_EQ(pos.x, 100.0f);
    EXPECT_FLOAT_EQ(pos.y, 50.0f);
    EXPECT_FLOAT_EQ(pos.z, 75.0f);
}

// ============================================================================
// Multi-hop transitions
// ============================================================================

TEST_F(SceneIntegrationTest, MultiHopTransition_A_B_C_A) {
    sm.loadStartScene();
    sm.update(0.0f);
    EXPECT_EQ(sm.getActiveSceneId(), "scene_a");

    camera->setPosition(glm::vec3(10, 20, 30));

    // A → B
    sm.transitionTo("scene_b");
    sm.update(0.0f); sm.update(0.0f);
    EXPECT_EQ(sm.getActiveSceneId(), "scene_b");

    camera->setPosition(glm::vec3(40, 50, 60));

    // B → C
    sm.transitionTo("scene_c");
    sm.update(0.0f); sm.update(0.0f);
    EXPECT_EQ(sm.getActiveSceneId(), "scene_c");

    // C → A (re-entry)
    sm.transitionTo("scene_a");
    sm.update(0.0f); sm.update(0.0f);
    EXPECT_EQ(sm.getActiveSceneId(), "scene_a");

    // Camera should be restored to scene_a's saved position
    auto pos = camera->getPosition();
    EXPECT_FLOAT_EQ(pos.x, 10.0f);
    EXPECT_FLOAT_EQ(pos.y, 20.0f);
    EXPECT_FLOAT_EQ(pos.z, 30.0f);

    // All three scenes should be marked as visited
    EXPECT_TRUE(sm.getReentryState("scene_a")->visited);
    EXPECT_TRUE(sm.getReentryState("scene_b")->visited);
    EXPECT_TRUE(sm.getReentryState("scene_c")->visited);
}

TEST_F(SceneIntegrationTest, TransitionTimingIsRecorded) {
    sm.loadStartScene();
    sm.update(0.0f);

    sm.transitionTo("scene_b");
    sm.update(0.0f);
    sm.update(0.0f);

    auto result = sm.getLastTransitionResult();
    EXPECT_TRUE(result.success);
    EXPECT_GE(result.transitionTimeMs, 0.0f);
    EXPECT_EQ(result.fromScene, "scene_a");
    EXPECT_EQ(result.toScene, "scene_b");
}

// ============================================================================
// Runtime scene management during transitions
// ============================================================================

TEST_F(SceneIntegrationTest, AddSceneThenTransitionToIt) {
    sm.loadStartScene();
    sm.update(0.0f);

    // Add a new scene at runtime
    SceneDefinition newScene;
    newScene.id = "bonus";
    newScene.name = "Bonus Level";
    newScene.worldDatabase = "bonus.db";
    sm.addScene(newScene);

    EXPECT_NE(sm.findScene("bonus"), nullptr);

    // Transition to the new scene
    ASSERT_TRUE(sm.transitionTo("bonus"));
    sm.update(0.0f); sm.update(0.0f);
    EXPECT_EQ(sm.getActiveSceneId(), "bonus");
}

TEST_F(SceneIntegrationTest, RemoveNonActiveSceneSucceeds) {
    sm.loadStartScene();
    sm.update(0.0f);
    ASSERT_EQ(sm.getActiveSceneId(), "scene_a");

    EXPECT_TRUE(sm.removeScene("scene_c"));
    EXPECT_EQ(sm.findScene("scene_c"), nullptr);
}

TEST_F(SceneIntegrationTest, CannotRemoveActiveScene) {
    sm.loadStartScene();
    sm.update(0.0f);

    EXPECT_FALSE(sm.removeScene("scene_a"));
    EXPECT_NE(sm.findScene("scene_a"), nullptr);
}

// ============================================================================
// Callback completeness during full transitions
// ============================================================================

TEST_F(SceneIntegrationTest, AllUnloadCallbacksFire) {
    sm.loadStartScene();
    sm.update(0.0f);
    log.clear();

    sm.transitionTo("scene_b");
    sm.update(0.0f); // unload

    EXPECT_TRUE(log.contains("endDialogue"));
    EXPECT_TRUE(log.contains("clearNPCs"));
    EXPECT_TRUE(log.contains("clearEntities"));
    EXPECT_TRUE(log.contains("clearSceneStoryCharacters"));
    EXPECT_TRUE(log.contains("resetLighting"));
    EXPECT_TRUE(log.contains("clearMusic"));
    EXPECT_TRUE(log.contains("clearObjectives"));
}

TEST_F(SceneIntegrationTest, LoadCallbacksIncludeNavGridRebuild) {
    sm.loadStartScene();
    sm.update(0.0f);
    log.clear();

    sm.transitionTo("scene_b");
    sm.update(0.0f); // unload
    sm.update(0.0f); // load

    EXPECT_TRUE(log.contains("rebuildNavGrid"));
    EXPECT_TRUE(log.contains("ready:scene_b"));

    // rebuildNavGrid should come before onSceneReady
    EXPECT_LT(log.indexOf("rebuildNavGrid"), log.indexOf("ready:scene_b"));
}
