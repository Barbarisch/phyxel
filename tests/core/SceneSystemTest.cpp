#include <gtest/gtest.h>
#include "core/SceneDefinition.h"
#include "core/SceneManager.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::Core;
using json = nlohmann::json;

// ============================================================================
// SceneDefinition Tests
// ============================================================================

TEST(SceneDefinitionTest, FromJsonBasic) {
    json j = {
        {"id", "overworld"},
        {"name", "The Overworld"},
        {"worldDatabase", "overworld.db"},
        {"description", "Main game area"},
        {"world", {{"type", "Perlin"}, {"seed", 42}}}
    };

    auto scene = SceneDefinition::fromJson(j);
    EXPECT_EQ(scene.id, "overworld");
    EXPECT_EQ(scene.name, "The Overworld");
    EXPECT_EQ(scene.worldDatabase, "overworld.db");
    EXPECT_EQ(scene.description, "Main game area");
    // The definition should contain "world" but not "id", "name", etc.
    EXPECT_TRUE(scene.definition.contains("world"));
    EXPECT_FALSE(scene.definition.contains("id"));
    EXPECT_FALSE(scene.definition.contains("name"));
    EXPECT_FALSE(scene.definition.contains("worldDatabase"));
}

TEST(SceneDefinitionTest, FromJsonDefaultName) {
    json j = {{"id", "dungeon"}};
    auto scene = SceneDefinition::fromJson(j);
    EXPECT_EQ(scene.id, "dungeon");
    EXPECT_EQ(scene.name, "dungeon"); // defaults to id
}

TEST(SceneDefinitionTest, ToJsonRoundTrip) {
    json j = {
        {"id", "cave"},
        {"name", "Dark Cave"},
        {"worldDatabase", "cave.db"},
        {"onEnterScript", "scripts/enter_cave.py"},
        {"transitionStyle", "fade"},
        {"world", {{"type", "Caves"}}}
    };

    auto scene = SceneDefinition::fromJson(j);
    json out = scene.toJson();

    EXPECT_EQ(out["id"], "cave");
    EXPECT_EQ(out["name"], "Dark Cave");
    EXPECT_EQ(out["worldDatabase"], "cave.db");
    EXPECT_EQ(out["onEnterScript"], "scripts/enter_cave.py");
    EXPECT_EQ(out["transitionStyle"], "fade");
    EXPECT_TRUE(out.contains("world"));
    EXPECT_EQ(out["world"]["type"], "Caves");
}

TEST(SceneDefinitionTest, TransitionStyleParsing) {
    json j1 = {{"id", "a"}, {"transitionStyle", "cut"}};
    EXPECT_EQ(SceneDefinition::fromJson(j1).transitionStyle, SceneTransitionStyle::Cut);

    json j2 = {{"id", "b"}, {"transitionStyle", "fade"}};
    EXPECT_EQ(SceneDefinition::fromJson(j2).transitionStyle, SceneTransitionStyle::Fade);

    json j3 = {{"id", "c"}, {"transitionStyle", "loading_screen"}};
    EXPECT_EQ(SceneDefinition::fromJson(j3).transitionStyle, SceneTransitionStyle::LoadingScreen);

    // Default
    json j4 = {{"id", "d"}};
    EXPECT_EQ(SceneDefinition::fromJson(j4).transitionStyle, SceneTransitionStyle::LoadingScreen);
}

// ============================================================================
// SceneManifest Tests
// ============================================================================

TEST(SceneManifestTest, IsMultiScene) {
    json single = {{"world", {{"type", "Flat"}}}, {"player", {{"type", "animated"}}}};
    EXPECT_FALSE(SceneManifest::isMultiScene(single));

    json multi = {
        {"startScene", "a"},
        {"scenes", json::array({
            {{"id", "a"}},
            {{"id", "b"}}
        })}
    };
    EXPECT_TRUE(SceneManifest::isMultiScene(multi));

    // Empty scenes array still counts as multi-scene format
    json emptyScenes = {{"scenes", json::array()}};
    EXPECT_TRUE(SceneManifest::isMultiScene(emptyScenes));
}

TEST(SceneManifestTest, FromJsonBasic) {
    json j = {
        {"startScene", "overworld"},
        {"playerDefaults", {{"type", "animated"}}},
        {"scenes", json::array({
            {{"id", "overworld"}, {"name", "The Overworld"}, {"worldDatabase", "overworld.db"}},
            {{"id", "dungeon"}, {"name", "The Dungeon"}, {"worldDatabase", "dungeon.db"}}
        })}
    };

    auto manifest = SceneManifest::fromJson(j);
    EXPECT_EQ(manifest.startScene, "overworld");
    EXPECT_EQ(manifest.scenes.size(), 2u);
    EXPECT_EQ(manifest.scenes[0].id, "overworld");
    EXPECT_EQ(manifest.scenes[1].id, "dungeon");
    EXPECT_EQ(manifest.playerDefaults["type"], "animated");
}

TEST(SceneManifestTest, DefaultStartSceneToFirst) {
    json j = {
        {"scenes", json::array({
            {{"id", "level1"}},
            {{"id", "level2"}}
        })}
    };

    auto manifest = SceneManifest::fromJson(j);
    EXPECT_EQ(manifest.startScene, "level1");
}

TEST(SceneManifestTest, FindScene) {
    json j = {
        {"scenes", json::array({
            {{"id", "a"}, {"name", "Scene A"}},
            {{"id", "b"}, {"name", "Scene B"}}
        })}
    };

    auto manifest = SceneManifest::fromJson(j);
    EXPECT_NE(manifest.findScene("a"), nullptr);
    EXPECT_EQ(manifest.findScene("a")->name, "Scene A");
    EXPECT_NE(manifest.findScene("b"), nullptr);
    EXPECT_EQ(manifest.findScene("c"), nullptr);
}

TEST(SceneManifestTest, ToJsonRoundTrip) {
    json j = {
        {"startScene", "forest"},
        {"playerDefaults", {{"type", "physics"}}},
        {"globalStory", {{"arcs", json::array()}}},
        {"scenes", json::array({
            {{"id", "forest"}, {"name", "The Forest"}, {"worldDatabase", "forest.db"},
             {"world", {{"type", "Perlin"}}}},
            {{"id", "castle"}, {"name", "The Castle"}, {"worldDatabase", "castle.db"}}
        })}
    };

    auto manifest = SceneManifest::fromJson(j);
    json out = manifest.toJson();

    EXPECT_EQ(out["startScene"], "forest");
    EXPECT_EQ(out["playerDefaults"]["type"], "physics");
    EXPECT_TRUE(out.contains("globalStory"));
    ASSERT_EQ(out["scenes"].size(), 2u);
    EXPECT_EQ(out["scenes"][0]["id"], "forest");
    EXPECT_EQ(out["scenes"][1]["id"], "castle");
}

// ============================================================================
// SceneManager Tests
// ============================================================================

class SceneManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager = std::make_unique<SceneManager>();

        json manifestJson = {
            {"startScene", "level1"},
            {"scenes", json::array({
                {{"id", "level1"}, {"name", "Level 1"}, {"worldDatabase", "level1.db"}},
                {{"id", "level2"}, {"name", "Level 2"}, {"worldDatabase", "level2.db"}},
                {{"id", "level3"}, {"name", "Level 3"}, {"worldDatabase", "level3.db"}}
            })}
        };
        manifest = SceneManifest::fromJson(manifestJson);
    }

    std::unique_ptr<SceneManager> manager;
    SceneManifest manifest;
};

TEST_F(SceneManagerTest, InitialState) {
    EXPECT_EQ(manager->getState(), SceneState::Idle);
    EXPECT_FALSE(manager->isTransitioning());
    EXPECT_TRUE(manager->getActiveSceneId().empty());
    EXPECT_EQ(manager->getActiveScene(), nullptr);
    EXPECT_FALSE(manager->hasManifest());
}

TEST_F(SceneManagerTest, LoadManifest) {
    manager->loadManifest(manifest);
    EXPECT_TRUE(manager->hasManifest());
    EXPECT_EQ(manager->getSceneIds().size(), 3u);
    EXPECT_NE(manager->findScene("level1"), nullptr);
    EXPECT_NE(manager->findScene("level2"), nullptr);
    EXPECT_NE(manager->findScene("level3"), nullptr);
    EXPECT_EQ(manager->findScene("nonexistent"), nullptr);
}

TEST_F(SceneManagerTest, TransitionToUnknownSceneFails) {
    manager->loadManifest(manifest);
    EXPECT_FALSE(manager->transitionTo("nonexistent"));
    EXPECT_EQ(manager->getState(), SceneState::Idle);
}

TEST_F(SceneManagerTest, TransitionToFirstScene) {
    manager->loadManifest(manifest);
    EXPECT_TRUE(manager->transitionTo("level1"));
    // No active scene yet, so goes straight to Loading
    EXPECT_EQ(manager->getState(), SceneState::Loading);
    EXPECT_TRUE(manager->isTransitioning());
}

TEST_F(SceneManagerTest, LoadStartScene) {
    manager->loadManifest(manifest);
    EXPECT_TRUE(manager->loadStartScene());
    EXPECT_EQ(manager->getState(), SceneState::Loading);
}

TEST_F(SceneManagerTest, CannotTransitionWhileTransitioning) {
    manager->loadManifest(manifest);
    manager->transitionTo("level1");
    EXPECT_TRUE(manager->isTransitioning());
    EXPECT_FALSE(manager->transitionTo("level2"));
}

TEST_F(SceneManagerTest, AddScene) {
    manager->loadManifest(manifest);
    EXPECT_EQ(manager->getSceneIds().size(), 3u);

    SceneDefinition newScene;
    newScene.id = "bonus";
    newScene.name = "Bonus Level";
    manager->addScene(newScene);

    EXPECT_EQ(manager->getSceneIds().size(), 4u);
    EXPECT_NE(manager->findScene("bonus"), nullptr);
}

TEST_F(SceneManagerTest, RemoveScene) {
    manager->loadManifest(manifest);
    EXPECT_TRUE(manager->removeScene("level3"));
    EXPECT_EQ(manager->getSceneIds().size(), 2u);
    EXPECT_EQ(manager->findScene("level3"), nullptr);
}

TEST_F(SceneManagerTest, CannotRemoveNonexistentScene) {
    manager->loadManifest(manifest);
    EXPECT_FALSE(manager->removeScene("nonexistent"));
}

TEST_F(SceneManagerTest, GetManifest) {
    manager->loadManifest(manifest);
    const auto& m = manager->getManifest();
    EXPECT_EQ(m.startScene, "level1");
    EXPECT_EQ(m.scenes.size(), 3u);
}

TEST_F(SceneManagerTest, TransitionToSameSceneFails) {
    manager->loadManifest(manifest);
    // Simulate that we're already in level1 by directly setting state
    // The only way to get there normally is through update(), which needs callbacks.
    // Test the guard: transitionTo same scene shouldn't work
    // First transition starts
    manager->transitionTo("level1");
    // Can't transition to level1 again while already transitioning
    EXPECT_FALSE(manager->transitionTo("level1"));
}
