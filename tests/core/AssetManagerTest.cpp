#include <gtest/gtest.h>
#include "core/EngineConfig.h"
#include "core/AssetManager.h"

using namespace Phyxel;

class AssetManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Core::EngineConfig cfg;
        Core::AssetManager::instance().initialize(cfg);
    }
};

// ============================================================================
// Initialization
// ============================================================================

TEST_F(AssetManagerTest, IsInitialized) {
    EXPECT_TRUE(Core::AssetManager::instance().isInitialized());
}

// ============================================================================
// Typed Resolvers (default config)
// ============================================================================

TEST_F(AssetManagerTest, ResolveShader) {
    EXPECT_EQ(Core::AssetManager::instance().resolveShader("test.vert.spv"),
              "shaders/test.vert.spv");
}

TEST_F(AssetManagerTest, ResolveTemplate) {
    EXPECT_EQ(Core::AssetManager::instance().resolveTemplate("tree.voxel"),
              "resources/templates/tree.voxel");
}

TEST_F(AssetManagerTest, ResolveTexture) {
    EXPECT_EQ(Core::AssetManager::instance().resolveTexture("brick.png"),
              "resources/textures/brick.png");
}

TEST_F(AssetManagerTest, ResolveDialogue) {
    EXPECT_EQ(Core::AssetManager::instance().resolveDialogue("intro.json"),
              "resources/dialogues/intro.json");
}

TEST_F(AssetManagerTest, ResolveSound) {
    EXPECT_EQ(Core::AssetManager::instance().resolveSound("place.wav"),
              "resources/sounds/place.wav");
}

TEST_F(AssetManagerTest, ResolveAnimatedChar) {
    EXPECT_EQ(Core::AssetManager::instance().resolveAnimatedChar("wolf.anim"),
              "resources/animated_characters/wolf.anim");
}

TEST_F(AssetManagerTest, ResolveRecipe) {
    EXPECT_EQ(Core::AssetManager::instance().resolveRecipe("characters/guard.yaml"),
              "resources/ai/characters/guard.yaml");
}

TEST_F(AssetManagerTest, ResolveWorld) {
    EXPECT_EQ(Core::AssetManager::instance().resolveWorld("save1.db"),
              "worlds/save1.db");
}

TEST_F(AssetManagerTest, ResolveScript) {
    EXPECT_EQ(Core::AssetManager::instance().resolveScript("startup.py"),
              "scripts/startup.py");
}

// ============================================================================
// Directory Accessors
// ============================================================================

TEST_F(AssetManagerTest, DirectoryAccessors) {
    auto& am = Core::AssetManager::instance();
    EXPECT_EQ(am.shadersDir(), "shaders");
    EXPECT_EQ(am.templatesDir(), "resources/templates");
    EXPECT_EQ(am.texturesDir(), "resources/textures");
    EXPECT_EQ(am.dialoguesDir(), "resources/dialogues");
    EXPECT_EQ(am.soundsDir(), "resources/sounds");
    EXPECT_EQ(am.animatedCharsDir(), "resources/animated_characters");
    EXPECT_EQ(am.recipesDir(), "resources/ai");
    EXPECT_EQ(am.worldsDir(), "worlds");
    EXPECT_EQ(am.scriptsDir(), "scripts");
}

// ============================================================================
// Special Files
// ============================================================================

TEST_F(AssetManagerTest, SpecialFiles) {
    auto& am = Core::AssetManager::instance();
    EXPECT_EQ(am.textureAtlasPath(), "resources/textures/cube_atlas.png");
    EXPECT_EQ(am.worldDatabasePath(), "worlds/default.db");
    EXPECT_EQ(am.loggingConfigPath(), "logging.ini");
    EXPECT_EQ(am.defaultAnimFile(), "resources/animated_characters/humanoid.anim");
}

// ============================================================================
// Custom Config
// ============================================================================

TEST_F(AssetManagerTest, CustomConfig) {
    Core::EngineConfig cfg;
    cfg.assetsDir = "game_assets";
    cfg.shadersDir = "game_shaders";
    cfg.worldsDir = "saves";
    cfg.scriptsDir = "lua";
    cfg.templatesSubdir = "objects";
    cfg.soundsSubdir = "audio";
    cfg.defaultWorldFile = "world1.db";
    cfg.textureAtlasFile = "atlas_hd.png";

    Core::AssetManager::instance().initialize(cfg);
    auto& am = Core::AssetManager::instance();

    EXPECT_EQ(am.resolveShader("test.spv"), "game_shaders/test.spv");
    EXPECT_EQ(am.resolveTemplate("tree.voxel"), "game_assets/objects/tree.voxel");
    EXPECT_EQ(am.resolveSound("boom.wav"), "game_assets/audio/boom.wav");
    EXPECT_EQ(am.resolveScript("init.lua"), "lua/init.lua");
    EXPECT_EQ(am.worldDatabasePath(), "saves/world1.db");
    EXPECT_EQ(am.textureAtlasPath(), "game_assets/textures/atlas_hd.png");
}

// ============================================================================
// Custom Search Paths
// ============================================================================

TEST_F(AssetManagerTest, RegisterAndResolveSearchPath) {
    auto& am = Core::AssetManager::instance();
    am.registerSearchPath("mods", "mods/textures");
    EXPECT_EQ(am.resolve("mods", "custom.png"), "mods/textures/custom.png");
}
