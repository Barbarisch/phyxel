#include <gtest/gtest.h>
#include "core/EngineConfig.h"
#include <fstream>
#include <filesystem>

using namespace Phyxel;

// ============================================================================
// Default Values
// ============================================================================

TEST(EngineConfigTest, DefaultValues) {
    Core::EngineConfig cfg;
    EXPECT_EQ(cfg.windowTitle, "Phyxel");
    EXPECT_EQ(cfg.windowWidth, 1600);
    EXPECT_EQ(cfg.windowHeight, 900);
    EXPECT_EQ(cfg.assetsDir, "resources");
    EXPECT_EQ(cfg.shadersDir, "shaders");
    EXPECT_EQ(cfg.worldsDir, "worlds");
    EXPECT_EQ(cfg.scriptsDir, "scripts");
    EXPECT_EQ(cfg.templatesSubdir, "templates");
    EXPECT_EQ(cfg.texturesSubdir, "textures");
    EXPECT_EQ(cfg.dialoguesSubdir, "dialogues");
    EXPECT_EQ(cfg.soundsSubdir, "sounds");
    EXPECT_EQ(cfg.animatedCharsSubdir, "animated_characters");
    EXPECT_EQ(cfg.recipesSubdir, "ai");
    EXPECT_EQ(cfg.textureAtlasFile, "cube_atlas.png");
    EXPECT_EQ(cfg.defaultWorldFile, "default.db");
    EXPECT_EQ(cfg.loggingConfigFile, "logging.ini");
    EXPECT_EQ(cfg.defaultAnimFile, "resources/animated_characters/humanoid.anim");
    EXPECT_EQ(cfg.apiPort, 8090);
    EXPECT_TRUE(cfg.enableHTTPAPI);
    EXPECT_TRUE(cfg.enablePython);
    EXPECT_TRUE(cfg.enableAudio);
    EXPECT_FLOAT_EQ(cfg.cameraStartX, 50.0f);
    EXPECT_FLOAT_EQ(cfg.cameraStartY, 50.0f);
    EXPECT_FLOAT_EQ(cfg.cameraStartZ, 50.0f);
    EXPECT_FLOAT_EQ(cfg.cameraYaw, -135.0f);
    EXPECT_FLOAT_EQ(cfg.cameraPitch, -30.0f);
}

// ============================================================================
// Path Helpers
// ============================================================================

TEST(EngineConfigTest, PathHelpers) {
    Core::EngineConfig cfg;
    EXPECT_EQ(cfg.templatesPath(), "resources/templates");
    EXPECT_EQ(cfg.texturesPath(), "resources/textures");
    EXPECT_EQ(cfg.dialoguesPath(), "resources/dialogues");
    EXPECT_EQ(cfg.soundsPath(), "resources/sounds");
    EXPECT_EQ(cfg.animatedCharsPath(), "resources/animated_characters");
    EXPECT_EQ(cfg.recipesPath(), "resources/ai");
    EXPECT_EQ(cfg.textureAtlasPath(), "resources/textures/cube_atlas.png");
    EXPECT_EQ(cfg.worldDatabasePath(), "worlds/default.db");
}

TEST(EngineConfigTest, PathHelpersCustom) {
    Core::EngineConfig cfg;
    cfg.assetsDir = "my_game/assets";
    cfg.worldsDir = "my_game/saves";
    cfg.templatesSubdir = "objects";
    cfg.defaultWorldFile = "save1.db";

    EXPECT_EQ(cfg.templatesPath(), "my_game/assets/objects");
    EXPECT_EQ(cfg.worldDatabasePath(), "my_game/saves/save1.db");
}

// ============================================================================
// JSON Serialization
// ============================================================================

TEST(EngineConfigTest, ToJsonRoundTrip) {
    Core::EngineConfig original;
    original.windowTitle = "MyGame";
    original.windowWidth = 1920;
    original.windowHeight = 1080;
    original.apiPort = 9090;
    original.enablePython = false;
    original.cameraStartX = 100.0f;

    auto json = original.toJson();

    Core::EngineConfig loaded;
    Core::EngineConfig::fromJson(json, loaded);

    EXPECT_EQ(loaded.windowTitle, "MyGame");
    EXPECT_EQ(loaded.windowWidth, 1920);
    EXPECT_EQ(loaded.windowHeight, 1080);
    EXPECT_EQ(loaded.apiPort, 9090);
    EXPECT_FALSE(loaded.enablePython);
    EXPECT_FLOAT_EQ(loaded.cameraStartX, 100.0f);
}

TEST(EngineConfigTest, FromJsonPartial) {
    // Only supply a few keys — others should keep defaults
    nlohmann::json j = {
        {"window", {{"title", "TestGame"}, {"width", 800}}},
        {"network", {{"api_port", 7777}}}
    };

    Core::EngineConfig cfg;
    Core::EngineConfig::fromJson(j, cfg);

    EXPECT_EQ(cfg.windowTitle, "TestGame");
    EXPECT_EQ(cfg.windowWidth, 800);
    EXPECT_EQ(cfg.windowHeight, 900);  // default
    EXPECT_EQ(cfg.apiPort, 7777);
    EXPECT_EQ(cfg.assetsDir, "resources");  // default
}

TEST(EngineConfigTest, FromJsonEmpty) {
    nlohmann::json j = nlohmann::json::object();
    Core::EngineConfig cfg;
    Core::EngineConfig::fromJson(j, cfg);

    // All defaults
    EXPECT_EQ(cfg.windowTitle, "Phyxel");
    EXPECT_EQ(cfg.windowWidth, 1600);
    EXPECT_EQ(cfg.apiPort, 8090);
}

// ============================================================================
// File Load / Save
// ============================================================================

class EngineConfigFileTest : public ::testing::Test {
protected:
    std::string tempPath;

    void SetUp() override {
        tempPath = "test_engine_config_" + std::to_string(std::hash<std::string>{}(::testing::UnitTest::GetInstance()->current_test_info()->name())) + ".json";
    }

    void TearDown() override {
        std::filesystem::remove(tempPath);
    }
};

TEST_F(EngineConfigFileTest, LoadNonExistentFileReturnsDefaults) {
    Core::EngineConfig cfg;
    bool ok = Core::EngineConfig::loadFromFile("__nonexistent_config__.json", cfg);
    EXPECT_TRUE(ok);
    EXPECT_EQ(cfg.windowTitle, "Phyxel");  // defaults
}

TEST_F(EngineConfigFileTest, SaveAndLoad) {
    Core::EngineConfig original;
    original.windowTitle = "SaveTest";
    original.apiPort = 5555;
    original.enableAudio = false;

    ASSERT_TRUE(original.saveToFile(tempPath));

    Core::EngineConfig loaded;
    ASSERT_TRUE(Core::EngineConfig::loadFromFile(tempPath, loaded));

    EXPECT_EQ(loaded.windowTitle, "SaveTest");
    EXPECT_EQ(loaded.apiPort, 5555);
    EXPECT_FALSE(loaded.enableAudio);
}

TEST_F(EngineConfigFileTest, LoadInvalidJsonReturnsFalse) {
    // Write garbage to file
    {
        std::ofstream ofs(tempPath);
        ofs << "not valid json {{{";
    }

    Core::EngineConfig cfg;
    bool ok = Core::EngineConfig::loadFromFile(tempPath, cfg);
    EXPECT_FALSE(ok);
}
