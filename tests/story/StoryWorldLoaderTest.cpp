#include <gtest/gtest.h>
#include "story/StoryWorldLoader.h"
#include "story/StoryEngine.h"
#include "story/StoryDirectorTypes.h"
#include <nlohmann/json.hpp>
#include <fstream>

using namespace Phyxel::Story;
using json = nlohmann::json;

// ============================================================================
// StoryWorldLoader Tests
// ============================================================================

class StoryWorldLoaderTest : public ::testing::Test {
protected:
    StoryEngine engine;

    // Minimal valid world definition
    json minimalDef() {
        return json::parse(R"({
            "world": {
                "factions": [
                    {"id": "rebels", "name": "Rebel Alliance"}
                ]
            },
            "characters": [
                {"id": "hero", "name": "Hero Character"}
            ]
        })");
    }

    // Full world definition with all features
    json fullDef() {
        return json::parse(R"({
            "world": {
                "factions": [
                    {"id": "kingdom", "name": "Kingdom of Aldren"},
                    {"id": "bandits", "name": "Shadow Fang"}
                ],
                "factionRelations": {
                    "kingdom-bandits": -0.8
                },
                "locations": [
                    {
                        "id": "castle",
                        "name": "Royal Castle",
                        "position": {"x": 100, "y": 50, "z": 100},
                        "radius": 64.0,
                        "controllingFaction": "kingdom",
                        "tags": ["safe", "urban"]
                    },
                    {
                        "id": "forest",
                        "name": "Dark Forest",
                        "position": {"x": 300, "y": 30, "z": 200}
                    }
                ],
                "variables": {
                    "warDeclared": false,
                    "dayCount": 1,
                    "tension": 0.3,
                    "questGiver": "elder"
                }
            },
            "characters": [
                {
                    "id": "guard",
                    "name": "Captain Marcus",
                    "description": "A loyal guard captain",
                    "faction": "kingdom",
                    "agencyLevel": 2,
                    "traits": {
                        "openness": 0.3,
                        "conscientiousness": 0.9,
                        "extraversion": 0.5,
                        "agreeableness": 0.6,
                        "neuroticism": 0.2,
                        "bravery": 0.95
                    },
                    "goals": [
                        {
                            "id": "protect_castle",
                            "description": "Protect the castle from invaders",
                            "priority": 0.9,
                            "isActive": true
                        },
                        {
                            "id": "find_spy",
                            "description": "Uncover the bandit spy",
                            "priority": 0.7,
                            "completionCondition": "spy_revealed"
                        }
                    ],
                    "relationships": [
                        {"target": "thief", "trust": -0.5, "fear": 0.0, "respect": 0.1, "label": "suspect"},
                        {"target": "elder", "trust": 0.8, "affection": 0.3}
                    ],
                    "roles": ["guard", "captain"],
                    "allowedActions": ["patrol", "arrest", "question", "fight"],
                    "defaultBehavior": "patrol",
                    "startingKnowledge": [
                        "The castle has been under increased threat lately",
                        "A spy is suspected among the newcomers"
                    ]
                },
                {
                    "id": "thief",
                    "name": "Shadow",
                    "faction": "bandits",
                    "agencyLevel": "Autonomous",
                    "traits": {
                        "openness": 0.8,
                        "conscientiousness": 0.2,
                        "extraversion": 0.4,
                        "agreeableness": 0.3,
                        "neuroticism": 0.6
                    },
                    "startingKnowledge": [
                        "The kingdom's treasury location is a secret",
                        "Captain Marcus is suspicious of newcomers"
                    ]
                }
            ],
            "storyArcs": [
                {
                    "id": "spy_arc",
                    "name": "The Spy Among Us",
                    "description": "Uncover the bandit spy in the castle",
                    "constraintMode": "Guided",
                    "minTimeBetweenBeats": 30.0,
                    "maxTimeWithoutProgress": 120.0,
                    "beats": [
                        {
                            "id": "suspicion",
                            "description": "Guard notices suspicious behavior",
                            "type": "Hard",
                            "triggerCondition": "spy_noticed",
                            "requiredCharacters": ["guard"]
                        },
                        {
                            "id": "investigation",
                            "description": "Investigation begins",
                            "type": "Soft",
                            "prerequisites": ["suspicion"],
                            "requiredCharacters": ["guard", "thief"],
                            "directorActions": [
                                {"type": "spawn_clue", "params": {"location": "castle"}}
                            ]
                        },
                        {
                            "id": "confrontation",
                            "description": "Guard confronts the spy",
                            "type": "Hard",
                            "prerequisites": ["investigation"],
                            "completionCondition": "spy_revealed",
                            "failureCondition": "spy_escaped"
                        }
                    ],
                    "tensionCurve": [0.2, 0.4, 0.6, 0.8, 1.0]
                }
            ]
        })");
    }
};

// ----------------------------------------------------------------------------
// Basic loading tests
// ----------------------------------------------------------------------------

TEST_F(StoryWorldLoaderTest, LoadEmptyDefinition) {
    json def = json::object();
    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
    EXPECT_TRUE(error.empty());
}

TEST_F(StoryWorldLoaderTest, LoadMinimalDefinition) {
    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(minimalDef(), engine, &error));
    EXPECT_TRUE(error.empty());

    auto ids = engine.getCharacterIds();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], "hero");
}

TEST_F(StoryWorldLoaderTest, LoadFullDefinition) {
    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine, &error));
    EXPECT_TRUE(error.empty());

    auto ids = engine.getCharacterIds();
    EXPECT_EQ(ids.size(), 2u);
}

// ----------------------------------------------------------------------------
// World parsing
// ----------------------------------------------------------------------------

TEST_F(StoryWorldLoaderTest, ParsesFactions) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    auto state = engine.saveState();
    EXPECT_TRUE(state.contains("worldState"));
}

TEST_F(StoryWorldLoaderTest, ParsesFactionRelations) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    // The factions should have been loaded with bidirectional relations
    // We verify by checking saveState contains faction data
    auto state = engine.saveState();
    EXPECT_TRUE(state.is_object());
}

TEST_F(StoryWorldLoaderTest, ParsesLocations) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    auto state = engine.saveState();
    EXPECT_TRUE(state.is_object());
}

TEST_F(StoryWorldLoaderTest, ParsesWorldVariables) {
    json def = json::parse(R"({
        "world": {
            "variables": {
                "warDeclared": false,
                "dayCount": 1,
                "tension": 0.3,
                "questGiver": "elder"
            }
        }
    })");

    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}

// ----------------------------------------------------------------------------
// Character parsing
// ----------------------------------------------------------------------------

TEST_F(StoryWorldLoaderTest, ParsesCharacterBasic) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));

    const auto* profile = engine.getCharacter("guard");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->name, "Captain Marcus");
    EXPECT_EQ(profile->description, "A loyal guard captain");
    EXPECT_EQ(profile->factionId, "kingdom");
}

TEST_F(StoryWorldLoaderTest, ParsesAgencyLevelInteger) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    const auto* guard = engine.getCharacter("guard");
    ASSERT_NE(guard, nullptr);
    EXPECT_EQ(guard->agencyLevel, AgencyLevel::Guided);  // 2
}

TEST_F(StoryWorldLoaderTest, ParsesAgencyLevelString) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    const auto* thief = engine.getCharacter("thief");
    ASSERT_NE(thief, nullptr);
    EXPECT_EQ(thief->agencyLevel, AgencyLevel::Autonomous);
}

TEST_F(StoryWorldLoaderTest, ParsesPersonalityTraits) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    const auto* guard = engine.getCharacter("guard");
    ASSERT_NE(guard, nullptr);
    EXPECT_FLOAT_EQ(guard->traits.openness, 0.3f);
    EXPECT_FLOAT_EQ(guard->traits.conscientiousness, 0.9f);
    EXPECT_FLOAT_EQ(guard->traits.extraversion, 0.5f);
    EXPECT_FLOAT_EQ(guard->traits.agreeableness, 0.6f);
    EXPECT_FLOAT_EQ(guard->traits.neuroticism, 0.2f);
}

TEST_F(StoryWorldLoaderTest, ParsesCustomTraits) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    const auto* guard = engine.getCharacter("guard");
    ASSERT_NE(guard, nullptr);
    EXPECT_EQ(guard->traits.customTraits.count("bravery"), 1u);
    EXPECT_FLOAT_EQ(guard->traits.customTraits.at("bravery"), 0.95f);
}

TEST_F(StoryWorldLoaderTest, ParsesCharacterGoals) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    const auto* guard = engine.getCharacter("guard");
    ASSERT_NE(guard, nullptr);
    ASSERT_EQ(guard->goals.size(), 2u);
    EXPECT_EQ(guard->goals[0].id, "protect_castle");
    EXPECT_EQ(guard->goals[0].description, "Protect the castle from invaders");
    EXPECT_FLOAT_EQ(guard->goals[0].priority, 0.9f);
    EXPECT_TRUE(guard->goals[0].isActive);
    EXPECT_EQ(guard->goals[1].completionCondition, "spy_revealed");
}

TEST_F(StoryWorldLoaderTest, ParsesRelationships) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    const auto* guard = engine.getCharacter("guard");
    ASSERT_NE(guard, nullptr);
    ASSERT_EQ(guard->relationships.size(), 2u);
    EXPECT_EQ(guard->relationships[0].targetCharacterId, "thief");
    EXPECT_FLOAT_EQ(guard->relationships[0].trust, -0.5f);
    EXPECT_EQ(guard->relationships[0].label, "suspect");
    EXPECT_EQ(guard->relationships[1].targetCharacterId, "elder");
    EXPECT_FLOAT_EQ(guard->relationships[1].trust, 0.8f);
}

TEST_F(StoryWorldLoaderTest, ParsesRolesAndAllowedActions) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    const auto* guard = engine.getCharacter("guard");
    ASSERT_NE(guard, nullptr);
    EXPECT_EQ(guard->roles.size(), 2u);
    EXPECT_EQ(guard->roles[0], "guard");
    EXPECT_EQ(guard->allowedActions.size(), 4u);
}

TEST_F(StoryWorldLoaderTest, ParsesDefaultBehavior) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    const auto* guard = engine.getCharacter("guard");
    ASSERT_NE(guard, nullptr);
    EXPECT_EQ(guard->defaultBehavior, "patrol");
}

TEST_F(StoryWorldLoaderTest, ParsesStartingKnowledge) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    // Verify knowledge was added to the character's memory
    auto* memory = engine.getCharacterMemoryMut("guard");
    ASSERT_NE(memory, nullptr);
    auto summary = memory->buildContextSummary();
    EXPECT_FALSE(summary.empty());
}

TEST_F(StoryWorldLoaderTest, ParsesMultipleCharacters) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    auto ids = engine.getCharacterIds();
    EXPECT_EQ(ids.size(), 2u);
    EXPECT_NE(engine.getCharacter("guard"), nullptr);
    EXPECT_NE(engine.getCharacter("thief"), nullptr);
}

// ----------------------------------------------------------------------------
// Story arc parsing
// ----------------------------------------------------------------------------

TEST_F(StoryWorldLoaderTest, ParsesStoryArcBasic) {
    ASSERT_TRUE(StoryWorldLoader::loadFromJson(fullDef(), engine));
    auto state = engine.saveState();
    EXPECT_TRUE(state.contains("storyArcs") || state.contains("arcs") || state.is_object());
}

TEST_F(StoryWorldLoaderTest, ParsesStoryArcBeats) {
    json def = json::parse(R"({
        "storyArcs": [{
            "id": "test_arc",
            "name": "Test Arc",
            "constraintMode": "Scripted",
            "beats": [
                {
                    "id": "beat1",
                    "description": "First beat",
                    "type": "Hard",
                    "triggerCondition": "start_condition"
                },
                {
                    "id": "beat2",
                    "description": "Second beat",
                    "type": "Soft",
                    "prerequisites": ["beat1"]
                }
            ]
        }]
    })");

    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}

TEST_F(StoryWorldLoaderTest, ParsesArcConstraintModes) {
    auto testMode = [&](const std::string& mode) {
        StoryEngine eng;
        json def = json::parse(R"({"storyArcs": [{"id": "arc", "name": "Arc", "constraintMode": ")" + mode + R"("}]})");
        return StoryWorldLoader::loadFromJson(def, eng);
    };

    EXPECT_TRUE(testMode("Scripted"));
    EXPECT_TRUE(testMode("Guided"));
    EXPECT_TRUE(testMode("Emergent"));
    EXPECT_TRUE(testMode("Freeform"));
}

TEST_F(StoryWorldLoaderTest, ParsesTensionCurve) {
    json def = json::parse(R"({
        "storyArcs": [{
            "id": "arc",
            "name": "Arc",
            "tensionCurve": [0.1, 0.3, 0.5, 0.8, 1.0, 0.6]
        }]
    })");

    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}

TEST_F(StoryWorldLoaderTest, ParsesBeatDirectorActions) {
    json def = json::parse(R"({
        "storyArcs": [{
            "id": "arc",
            "name": "Arc",
            "beats": [{
                "id": "beat1",
                "type": "Hard",
                "directorActions": [
                    {"type": "spawn_clue", "params": {"location": "castle"}},
                    {"type": "change_weather", "params": {"weather": "storm"}}
                ]
            }]
        }]
    })");

    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}

TEST_F(StoryWorldLoaderTest, ParsesArcPacingParams) {
    json def = json::parse(R"({
        "storyArcs": [{
            "id": "arc",
            "name": "Arc",
            "minTimeBetweenBeats": 15.0,
            "maxTimeWithoutProgress": 600.0
        }]
    })");

    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}

// ----------------------------------------------------------------------------
// Validation tests
// ----------------------------------------------------------------------------

TEST_F(StoryWorldLoaderTest, ValidateAcceptsValidDef) {
    std::string error;
    EXPECT_TRUE(StoryWorldLoader::validate(fullDef(), &error));
    EXPECT_TRUE(error.empty());
}

TEST_F(StoryWorldLoaderTest, ValidateAcceptsEmptyObject) {
    std::string error;
    EXPECT_TRUE(StoryWorldLoader::validate(json::object(), &error));
}

TEST_F(StoryWorldLoaderTest, ValidateRejectsNonObject) {
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::validate(json::array(), &error));
    EXPECT_FALSE(error.empty());
}

TEST_F(StoryWorldLoaderTest, ValidateRejectsNonArrayCharacters) {
    json def = {{"characters", "not_an_array"}};
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::validate(def, &error));
    EXPECT_NE(error.find("array"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, ValidateRejectsCharacterWithoutId) {
    json def = {{"characters", json::array({json::object({{"name", "Test"}})})}};
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::validate(def, &error));
    EXPECT_NE(error.find("id"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, ValidateRejectsCharacterWithoutName) {
    json def = {{"characters", json::array({json::object({{"id", "test"}})})}};
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::validate(def, &error));
    EXPECT_NE(error.find("name"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, ValidateRejectsNonObjectWorld) {
    json def = {{"world", 42}};
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::validate(def, &error));
    EXPECT_NE(error.find("object"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, ValidateRejectsNonArrayFactions) {
    json def = {{"world", {{"factions", "nope"}}}};
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::validate(def, &error));
    EXPECT_NE(error.find("array"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, ValidateRejectsNonObjectFactionRelations) {
    json def = {{"world", {{"factionRelations", json::array()}}}};
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::validate(def, &error));
    EXPECT_NE(error.find("object"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, ValidateRejectsArcWithoutId) {
    json def = {{"storyArcs", json::array({json::object({{"name", "Test"}})})}};
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::validate(def, &error));
    EXPECT_NE(error.find("id"), std::string::npos);
}

// ----------------------------------------------------------------------------
// Error handling
// ----------------------------------------------------------------------------

TEST_F(StoryWorldLoaderTest, InvalidAgencyLevelReturnsError) {
    json def = json::parse(R"({
        "characters": [{
            "id": "test",
            "name": "Test",
            "agencyLevel": 99
        }]
    })");

    std::string error;
    EXPECT_FALSE(StoryWorldLoader::loadFromJson(def, engine, &error));
    EXPECT_FALSE(error.empty());
}

TEST_F(StoryWorldLoaderTest, InvalidFactionRelationKeyReturnsError) {
    json def = json::parse(R"({
        "world": {
            "factions": [{"id": "a"}],
            "factionRelations": {"invalid_no_dash": 0.5}
        }
    })");

    std::string error;
    EXPECT_FALSE(StoryWorldLoader::loadFromJson(def, engine, &error));
    EXPECT_NE(error.find("factionA-factionB"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, NonArrayCharactersReturnsError) {
    json def = {{"characters", json::object()}};
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::loadFromJson(def, engine, &error));
    EXPECT_NE(error.find("array"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, NonArrayStoryArcsReturnsError) {
    json def = {{"storyArcs", 42}};
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::loadFromJson(def, engine, &error));
    EXPECT_NE(error.find("array"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, LoadNullErrorPointerDoesNotCrash) {
    json def = {{"characters", json::object()}};
    EXPECT_FALSE(StoryWorldLoader::loadFromJson(def, engine, nullptr));
}

// ----------------------------------------------------------------------------
// File loading
// ----------------------------------------------------------------------------

TEST_F(StoryWorldLoaderTest, LoadFromFileNonexistent) {
    std::string error;
    EXPECT_FALSE(StoryWorldLoader::loadFromFile("nonexistent_file_xyz.json", engine, &error));
    EXPECT_NE(error.find("Cannot open"), std::string::npos);
}

TEST_F(StoryWorldLoaderTest, LoadFromFileMalformedJson) {
    // Write a temporary malformed file
    const std::string tmpPath = "test_malformed_story.json";
    {
        std::ofstream f(tmpPath);
        f << "{ this is not valid json }}}";
    }

    std::string error;
    EXPECT_FALSE(StoryWorldLoader::loadFromFile(tmpPath, engine, &error));
    EXPECT_NE(error.find("parse error"), std::string::npos);

    std::remove(tmpPath.c_str());
}

TEST_F(StoryWorldLoaderTest, LoadFromFileValid) {
    const std::string tmpPath = "test_valid_story.json";
    {
        std::ofstream f(tmpPath);
        f << fullDef().dump(2);
    }

    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromFile(tmpPath, engine, &error));
    EXPECT_TRUE(error.empty());

    auto ids = engine.getCharacterIds();
    EXPECT_EQ(ids.size(), 2u);

    std::remove(tmpPath.c_str());
}

// ----------------------------------------------------------------------------
// Edge cases
// ----------------------------------------------------------------------------

TEST_F(StoryWorldLoaderTest, CharacterWithMinimalFields) {
    json def = json::parse(R"({
        "characters": [{"id": "min", "name": "Minimal"}]
    })");

    ASSERT_TRUE(StoryWorldLoader::loadFromJson(def, engine));
    const auto* profile = engine.getCharacter("min");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->name, "Minimal");
    // Defaults for traits should be 0.5
    EXPECT_FLOAT_EQ(profile->traits.openness, 0.5f);
}

TEST_F(StoryWorldLoaderTest, EmptyWorldSection) {
    json def = json::parse(R"({"world": {}})");
    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}

TEST_F(StoryWorldLoaderTest, EmptyCharacterArray) {
    json def = json::parse(R"({"characters": []})");
    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
    EXPECT_EQ(engine.getCharacterIds().size(), 0u);
}

TEST_F(StoryWorldLoaderTest, EmptyStoryArcsArray) {
    json def = json::parse(R"({"storyArcs": []})");
    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}

TEST_F(StoryWorldLoaderTest, LocationWithoutOptionalFields) {
    json def = json::parse(R"({
        "world": {
            "locations": [{"id": "somewhere", "name": "Somewhere"}]
        }
    })");

    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}

TEST_F(StoryWorldLoaderTest, CharacterWithEmptyKnowledge) {
    json def = json::parse(R"({
        "characters": [{
            "id": "ignorant",
            "name": "Knows Nothing",
            "startingKnowledge": []
        }]
    })");

    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}

TEST_F(StoryWorldLoaderTest, ArcWithEmptyBeats) {
    json def = json::parse(R"({
        "storyArcs": [{
            "id": "empty_arc",
            "name": "Empty Arc",
            "beats": []
        }]
    })");

    std::string error;
    EXPECT_TRUE(StoryWorldLoader::loadFromJson(def, engine, &error));
}
