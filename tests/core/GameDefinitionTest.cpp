#include <gtest/gtest.h>
#include "core/GameDefinitionLoader.h"

using namespace Phyxel::Core;
using json = nlohmann::json;

// =============================================================================
// GameDefinitionLoader tests — verify game definition validation, parsing,
// and result reporting without requiring Vulkan or running subsystems.
// =============================================================================

// --- Validation Tests ---

TEST(GameDefinitionTest, ValidateEmptyDefinitionSucceeds) {
    auto [valid, err] = GameDefinitionLoader::validate(json::object());
    EXPECT_TRUE(valid);
    EXPECT_TRUE(err.empty());
}

TEST(GameDefinitionTest, ValidateNonObjectFails) {
    auto [valid, err] = GameDefinitionLoader::validate(json::array());
    EXPECT_FALSE(valid);
    EXPECT_NE(err.find("JSON object"), std::string::npos);
}

TEST(GameDefinitionTest, ValidateWorldTypePerlin) {
    json def = {{"world", {{"type", "Perlin"}}}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_TRUE(valid);
}

TEST(GameDefinitionTest, ValidateWorldTypeMountains) {
    json def = {{"world", {{"type", "Mountains"}}}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_TRUE(valid);
}

TEST(GameDefinitionTest, ValidateWorldTypeInvalid) {
    json def = {{"world", {{"type", "Lava"}}}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid);
    EXPECT_NE(err.find("Lava"), std::string::npos);
}

TEST(GameDefinitionTest, ValidateWorldNotObject) {
    json def = {{"world", "flat"}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid);
}

TEST(GameDefinitionTest, ValidateNPCsMissingName) {
    json def = {{"npcs", json::array({{{"position", {{"x", 0}, {"y", 0}, {"z", 0}}}}})}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid);
    EXPECT_NE(err.find("name"), std::string::npos);
}

TEST(GameDefinitionTest, ValidateNPCsWithName) {
    json def = {{"npcs", json::array({{{"name", "Guard"}}})}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_TRUE(valid);
}

TEST(GameDefinitionTest, ValidateNPCsNotArray) {
    json def = {{"npcs", "Guard"}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid);
}

TEST(GameDefinitionTest, ValidateStructuresInvalidType) {
    json def = {{"structures", json::array({{{"type", "explode"}}})}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid);
    EXPECT_NE(err.find("explode"), std::string::npos);
}

TEST(GameDefinitionTest, ValidateStructuresFill) {
    json def = {{"structures", json::array({{{"type", "fill"}}})}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_TRUE(valid);
}

TEST(GameDefinitionTest, ValidateStructuresTemplate) {
    json def = {{"structures", json::array({{{"type", "template"}}})}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_TRUE(valid);
}

TEST(GameDefinitionTest, ValidateStructuresMissingType) {
    json def = {{"structures", json::array({{{"material", "Stone"}}})}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid);
}

TEST(GameDefinitionTest, ValidatePlayerNotObject) {
    json def = {{"player", "physics"}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid);
}

TEST(GameDefinitionTest, ValidateAllWorldTypes) {
    for (const auto& type : {"Random", "Perlin", "Flat", "Mountains", "Caves", "City"}) {
        json def = {{"world", {{"type", type}}}};
        auto [valid, err] = GameDefinitionLoader::validate(def);
        EXPECT_TRUE(valid) << "Type: " << type;
    }
}

// --- Result Serialization Tests ---

TEST(GameDefinitionTest, ResultToJsonSuccess) {
    GameDefinitionResult result;
    result.success = true;
    result.chunksGenerated = 3;
    result.structuresPlaced = 2;
    result.npcsSpawned = 4;
    result.playerSpawned = true;
    result.cameraSet = true;
    result.storyLoaded = true;

    auto j = result.toJson();
    EXPECT_TRUE(j["success"].get<bool>());
    EXPECT_EQ(j["chunks_generated"].get<int>(), 3);
    EXPECT_EQ(j["structures_placed"].get<int>(), 2);
    EXPECT_EQ(j["npcs_spawned"].get<int>(), 4);
    EXPECT_TRUE(j["player_spawned"].get<bool>());
    EXPECT_TRUE(j["camera_set"].get<bool>());
    EXPECT_TRUE(j["story_loaded"].get<bool>());
}

TEST(GameDefinitionTest, ResultToJsonFailure) {
    GameDefinitionResult result;
    result.success = false;
    result.error = "Something went wrong";

    auto j = result.toJson();
    EXPECT_FALSE(j["success"].get<bool>());
    EXPECT_EQ(j["error"].get<std::string>(), "Something went wrong");
}

TEST(GameDefinitionTest, ResultDefaultValues) {
    GameDefinitionResult result;
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.chunksGenerated, 0);
    EXPECT_EQ(result.structuresPlaced, 0);
    EXPECT_EQ(result.npcsSpawned, 0);
    EXPECT_FALSE(result.playerSpawned);
    EXPECT_FALSE(result.cameraSet);
    EXPECT_FALSE(result.storyLoaded);
}

// --- Load with null subsystems ---

TEST(GameDefinitionTest, LoadEmptyDefinitionWithNullSubsystems) {
    GameSubsystems subs;  // all null
    auto result = GameDefinitionLoader::load(json::object(), subs);
    EXPECT_TRUE(result.success);
}

TEST(GameDefinitionTest, LoadWorldWithoutChunkManagerFails) {
    GameSubsystems subs;
    json def = {{"world", {{"type", "Flat"}}}};
    auto result = GameDefinitionLoader::load(def, subs);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("ChunkManager"), std::string::npos);
}

TEST(GameDefinitionTest, LoadNPCsWithoutNPCManagerFails) {
    GameSubsystems subs;
    json def = {{"npcs", json::array({{{"name", "Guard"}}})}};
    auto result = GameDefinitionLoader::load(def, subs);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("NPCManager"), std::string::npos);
}

TEST(GameDefinitionTest, LoadPlayerWithoutSpawnerSkips) {
    GameSubsystems subs;
    json def = {{"player", {{"type", "physics"}, {"position", {{"x", 10}, {"y", 20}, {"z", 10}}}}}};
    auto result = GameDefinitionLoader::load(def, subs);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.playerSpawned);
}

TEST(GameDefinitionTest, LoadCameraWithoutCameraSkips) {
    GameSubsystems subs;
    json def = {{"camera", {{"position", {{"x", 50}, {"y", 50}, {"z", 50}}}}}};
    auto result = GameDefinitionLoader::load(def, subs);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.cameraSet);
}

TEST(GameDefinitionTest, LoadInvalidDefinitionRejected) {
    GameSubsystems subs;
    auto result = GameDefinitionLoader::load(json::array(), subs);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("JSON object"), std::string::npos);
}

// --- Export Tests ---

TEST(GameDefinitionTest, ExportWithNullSubsystems) {
    GameSubsystems subs;
    auto exported = GameDefinitionLoader::exportDefinition(subs);
    EXPECT_TRUE(exported.contains("name"));
    EXPECT_TRUE(exported.contains("version"));
}

// --- Full Definition Validation ---

TEST(GameDefinitionTest, ValidateCompleteDefinition) {
    json def = {
        {"name", "Test Game"},
        {"description", "A test game"},
        {"version", "1.0"},
        {"world", {
            {"type", "Perlin"},
            {"seed", 42},
            {"from", {{"x", 0}, {"y", 0}, {"z", 0}}},
            {"to", {{"x", 1}, {"y", 0}, {"z", 1}}}
        }},
        {"structures", json::array({
            {{"type", "fill"}, {"from", {{"x", 0}, {"y", 0}, {"z", 0}}},
             {"to", {{"x", 10}, {"y", 5}, {"z", 10}}}, {"material", "Stone"}},
            {{"type", "template"}, {"template", "tree.txt"},
             {"position", {{"x", 5}, {"y", 6}, {"z", 5}}}}
        })},
        {"player", {
            {"type", "physics"},
            {"position", {{"x", 16}, {"y", 20}, {"z", 16}}},
            {"id", "player1"}
        }},
        {"camera", {
            {"position", {{"x", 50}, {"y", 50}, {"z", 50}}},
            {"yaw", -135.0},
            {"pitch", -30.0}
        }},
        {"npcs", json::array({
            {{"name", "Guard"},
             {"animFile", "character.anim"},
             {"position", {{"x", 10}, {"y", 20}, {"z", 10}}},
             {"behavior", "patrol"},
             {"waypoints", json::array({{{"x", 10}, {"y", 20}, {"z", 10}}, {{"x", 20}, {"y", 20}, {"z", 10}}})},
             {"dialogue", {
                 {"id", "guard_talk"},
                 {"startNodeId", "start"},
                 {"nodes", json::array({{{"id", "start"}, {"speaker", "Guard"}, {"text", "Hello!"}}})}
             }},
             {"storyCharacter", {
                 {"id", "guard"},
                 {"faction", "town_guard"},
                 {"agencyLevel", 1},
                 {"traits", {{"openness", 0.3}, {"extraversion", 0.7}}},
                 {"goals", json::array({{{"id", "protect"}, {"description", "Protect the town"}, {"priority", 0.9}}})}
             }}}
        })},
        {"story", {
            {"arcs", json::array({
                {{"id", "main_quest"}, {"name", "The Main Quest"},
                 {"constraintMode", "Guided"},
                 {"beats", json::array({
                     {{"id", "beat1"}, {"description", "Meet the guard"}, {"type", "Hard"}}
                 })}}
            })}
        }}
    };

    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_TRUE(valid) << err;
}
