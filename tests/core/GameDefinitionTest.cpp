#include <gtest/gtest.h>
#include "core/GameDefinitionLoader.h"
#include "graphics/Camera.h"

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

TEST(GameDefinitionTest, LoadCameraMode) {
    Phyxel::Graphics::Camera cam;
    GameSubsystems subs;
    subs.camera = &cam;

    json def = {{"camera", {{"position", {{"x", 1}, {"y", 2}, {"z", 3}}},
                            {"mode", "first_person"}}}};
    auto result = GameDefinitionLoader::load(def, subs);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.cameraSet);
    EXPECT_TRUE(result.cameraModeSet);
    EXPECT_EQ(cam.getMode(), Phyxel::Graphics::CameraMode::FirstPerson);

    // Unknown mode: warn + leave mode alone, cameraModeSet stays false.
    cam.setMode(Phyxel::Graphics::CameraMode::Free);
    def["camera"]["mode"] = "cinematic";
    result = GameDefinitionLoader::load(def, subs);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.cameraModeSet);
    EXPECT_EQ(cam.getMode(), Phyxel::Graphics::CameraMode::Free);

    // No mode key: mode untouched, cameraModeSet false (hosts may then default).
    def["camera"].erase("mode");
    result = GameDefinitionLoader::load(def, subs);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.cameraModeSet);
    EXPECT_EQ(cam.getMode(), Phyxel::Graphics::CameraMode::Free);
}

// =============================================================================
// G-93 + run 49: an inline `world` block on a scene whose DB already holds the
// chunks must NOT regenerate (it replaced Ravenmere's built town with flat terrain
// twice) - and the refused path must still LOAD the saved region, because nothing
// else does for a fixed-region scene (run 49: the second farm visit had 2 of 9
// chunks in memory and the player walked into nothing). Headless: a ChunkManager
// with a temp world DB, no Vulkan.
// =============================================================================
#include "core/ChunkManager.h"
#include "core/Cube.h"
#include "core/Chunk.h"
#include <filesystem>

TEST(GameDefinitionTest, InlineWorldBlockLoadsSavedChunksInsteadOfRegenerating) {
    const std::string db = (std::filesystem::temp_directory_path() / "gamedef_regen_guard.db").string();
    { std::error_code ec; std::filesystem::remove(db, ec); }
    const json def = json::parse(R"({"world":{"type":"Flat","seed":3,"from":{"x":0,"y":0,"z":0},"to":{"x":1,"y":0,"z":0},"flora":false}})");
    {
        Phyxel::ChunkManager cm;
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        ASSERT_TRUE(cm.initializeWorldStorage(db));
        GameSubsystems subs; subs.chunkManager = &cm;
        const auto r = GameDefinitionLoader::load(def, subs);
        ASSERT_TRUE(r.success) << r.error;
        EXPECT_EQ(r.chunksGenerated, 2);
        EXPECT_FALSE(r.worldGenerationRefused);
        // a deliberate edit the DB must keep
        cm.addCubeWithMaterial(glm::ivec3(40, 20, 5), "Gold");
        ASSERT_TRUE(cm.saveAllChunks());
        cm.disconnectWorldStorage();
    }
    Phyxel::ChunkManager cm2;
    cm2.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    ASSERT_TRUE(cm2.initializeWorldStorage(db));
    GameSubsystems subs2; subs2.chunkManager = &cm2;
    const auto r2 = GameDefinitionLoader::load(def, subs2);
    ASSERT_TRUE(r2.success) << r2.error;
    EXPECT_TRUE(r2.worldGenerationRefused) << "the DB holds the chunks: the inline block must be ignored";
    EXPECT_EQ(r2.chunksGenerated, 0);
    // ...and the whole region is IN MEMORY, from the DB, edits included.
    EXPECT_NE(cm2.getChunkAtCoord(glm::ivec3(0, 0, 0)), nullptr);
    EXPECT_NE(cm2.getChunkAtCoord(glm::ivec3(1, 0, 0)), nullptr) << "every saved chunk of the region loads, not just the ones something touched";
    ASSERT_NE(cm2.getCubeAt(glm::ivec3(40, 20, 5)), nullptr) << "the saved edit survived (no regeneration)";
    EXPECT_EQ(cm2.getCubeAt(glm::ivec3(40, 20, 5))->getMaterialName(), "Gold");
    // ...FINALIZED like generated chunks: faces built and the collision occupancy grid filled.
    // The streaming load defers both to a bulk pass this path never ran - the second farm
    // visit had terrain data and no collision, and every NPC fell through (2026-09-10).
    const Phyxel::Chunk* c1 = cm2.getChunkAtCoord(glm::ivec3(1, 0, 0));
    EXPECT_GT(c1->getFaces().size(), 0u) << "loaded chunks must have their faces rebuilt";
    EXPECT_TRUE(c1->getOccupancyGrid().isCubeFilled(glm::ivec3(8, 20, 5)))
        << "loaded chunks must have their collision occupancy built (characters fall through otherwise)";
    std::error_code ec; std::filesystem::remove(db, ec);
}

// =============================================================================
// "dayNight" block (G-165). RED-BEFORE-GREEN: DayNightCycle is OFF by default and, before this
// block existed, nothing in a shipped game could turn it on - only the editor's ImGui panel and
// its HTTP route. Ravenmere therefore sat at a fixed noon (user, 2026-09-23: "i dont see
// day/night cycle going"). These pin the authored contract; the shell applies the result.
// =============================================================================

TEST(GameDefinitionTest, ValidateDayNightRejectsAnHourThatIsNotOnTheClock) {
    json def = {{"dayNight", {{"timeOfDay", 25.0}}}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid) << "25 o'clock is a typo, not a time - it must be refused, not wrapped";
    EXPECT_NE(err.find("timeOfDay"), std::string::npos) << "the error must name the field: " << err;
}

TEST(GameDefinitionTest, ValidateDayNightRejectsAZeroLengthDay) {
    json def = {{"dayNight", {{"dayLengthSeconds", 0}}}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid) << "a zero-second day divides by zero in update(); the cycle clamps to 1 s, "
                           "which turns an authoring mistake into a strobing sky";
    EXPECT_NE(err.find("dayLengthSeconds"), std::string::npos) << err;
}

TEST(GameDefinitionTest, ValidateDayNightRejectsANonObject) {
    json def = {{"dayNight", true}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_FALSE(valid);
    EXPECT_NE(err.find("dayNight"), std::string::npos) << err;
}

TEST(GameDefinitionTest, ValidateDayNightAcceptsAWellFormedBlock) {
    json def = {{"dayNight", {{"enabled", true}, {"timeOfDay", 9.0}, {"dayLengthSeconds", 1200},
                              {"timeScale", 1.0}, {"paused", false}, {"dayNumber", 1}}}};
    auto [valid, err] = GameDefinitionLoader::validate(def);
    EXPECT_TRUE(valid) << err;
}

TEST(GameDefinitionTest, LoadParsesTheDayNightBlockIntoTheResult) {
    GameSubsystems subs;  // no renderer here on purpose: core must not need graphics to parse it
    json def = {{"dayNight", {{"enabled", true}, {"timeOfDay", 9.0}, {"dayLengthSeconds", 1200}}}};
    auto result = GameDefinitionLoader::load(def, subs);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.dayNightLoaded) << "the block was present and must be reported as loaded";
    ASSERT_TRUE(result.dayNight.enabled.has_value());
    EXPECT_TRUE(*result.dayNight.enabled);
    ASSERT_TRUE(result.dayNight.timeOfDay.has_value());
    EXPECT_FLOAT_EQ(*result.dayNight.timeOfDay, 9.0f);
    ASSERT_TRUE(result.dayNight.dayLengthSeconds.has_value());
    EXPECT_FLOAT_EQ(*result.dayNight.dayLengthSeconds, 1200.0f);
    EXPECT_FALSE(result.dayNight.paused.has_value()) << "an unset field stays unset, not defaulted";
}

TEST(GameDefinitionTest, LoadWithoutADayNightBlockLeavesItUnloaded) {
    GameSubsystems subs;
    auto result = GameDefinitionLoader::load(json::object(), subs);
    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.dayNightLoaded);
    EXPECT_FALSE(result.dayNight.any());
}

TEST(GameDefinitionTest, LoadRefusesAnInvalidDayNightBlockInsteadOfClampingIt) {
    GameSubsystems subs;
    json def = {{"dayNight", {{"dayLengthSeconds", -5}}}};
    auto result = GameDefinitionLoader::load(def, subs);
    EXPECT_FALSE(result.success) << "a bad block must fail the load with a message, not load and clamp";
    EXPECT_NE(result.error.find("dayLengthSeconds"), std::string::npos) << result.error;
}

TEST(GameDefinitionTest, ParseDayNightReadsTheTopLevelOfAMultiSceneDocument) {
    // A multi-scene game never passes its top-level document through load(); the shell reads
    // the world-level block with parseDayNight and applies it once.
    json def = {{"scenes", json::array()}, {"dayNight", {{"enabled", true}, {"timeOfDay", 21.5}}}};
    DayNightSettings dn;
    ASSERT_TRUE(GameDefinitionLoader::parseDayNight(def, dn));
    EXPECT_TRUE(dn.enabled.value_or(false));
    EXPECT_FLOAT_EQ(dn.timeOfDay.value_or(0.0f), 21.5f);
    DayNightSettings none;
    EXPECT_FALSE(GameDefinitionLoader::parseDayNight(json{{"scenes", json::array()}}, none));
}
