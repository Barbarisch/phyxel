#include "story/StoryWorldLoader.h"
#include "story/StoryDirectorTypes.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace Phyxel {
namespace Story {

void StoryWorldLoader::setError(std::string* outError, const std::string& msg) {
    if (outError) *outError = msg;
}

bool StoryWorldLoader::loadFromJson(const nlohmann::json& definition,
                                      StoryEngine& engine,
                                      std::string* outError) {
    // Parse world state
    if (definition.contains("world")) {
        WorldState state;
        if (!parseWorld(definition["world"], state, outError)) return false;
        engine.defineWorld(std::move(state));
    }

    // Parse characters
    if (definition.contains("characters")) {
        if (!definition["characters"].is_array()) {
            setError(outError, "'characters' must be an array");
            return false;
        }
        for (const auto& charJson : definition["characters"]) {
            CharacterProfile profile;
            std::vector<std::pair<std::string, std::string>> knowledge;
            if (!parseCharacter(charJson, profile, knowledge, outError)) return false;

            std::string charId = profile.id;
            engine.addCharacter(std::move(profile));

            for (const auto& [factId, summary] : knowledge) {
                engine.addStartingKnowledge(charId, factId, summary);
            }
        }
    }

    // Parse story arcs
    if (definition.contains("storyArcs")) {
        if (!definition["storyArcs"].is_array()) {
            setError(outError, "'storyArcs' must be an array");
            return false;
        }
        for (const auto& arcJson : definition["storyArcs"]) {
            StoryArc arc;
            if (!parseStoryArc(arcJson, arc, outError)) return false;
            engine.addStoryArc(std::move(arc));
        }
    }

    return true;
}

bool StoryWorldLoader::loadFromFile(const std::string& filePath,
                                      StoryEngine& engine,
                                      std::string* outError) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        setError(outError, "Cannot open file: " + filePath);
        return false;
    }

    nlohmann::json definition;
    try {
        file >> definition;
    } catch (const nlohmann::json::exception& e) {
        setError(outError, "JSON parse error: " + std::string(e.what()));
        return false;
    }

    return loadFromJson(definition, engine, outError);
}

bool StoryWorldLoader::validate(const nlohmann::json& definition,
                                  std::string* outError) {
    if (!definition.is_object()) {
        setError(outError, "Root must be a JSON object");
        return false;
    }

    // Validate world section
    if (definition.contains("world")) {
        if (!definition["world"].is_object()) {
            setError(outError, "'world' must be an object");
            return false;
        }
        const auto& world = definition["world"];
        if (world.contains("factions") && !world["factions"].is_array()) {
            setError(outError, "'world.factions' must be an array");
            return false;
        }
        if (world.contains("factionRelations") && !world["factionRelations"].is_object()) {
            setError(outError, "'world.factionRelations' must be an object");
            return false;
        }
    }

    // Validate characters section
    if (definition.contains("characters")) {
        if (!definition["characters"].is_array()) {
            setError(outError, "'characters' must be an array");
            return false;
        }
        for (size_t i = 0; i < definition["characters"].size(); ++i) {
            const auto& c = definition["characters"][i];
            if (!c.contains("id") || !c["id"].is_string()) {
                setError(outError, "Character at index " + std::to_string(i) + " missing 'id' string");
                return false;
            }
            if (!c.contains("name") || !c["name"].is_string()) {
                setError(outError, "Character '" + c.value("id", "?") + "' missing 'name' string");
                return false;
            }
        }
    }

    // Validate story arcs section
    if (definition.contains("storyArcs")) {
        if (!definition["storyArcs"].is_array()) {
            setError(outError, "'storyArcs' must be an array");
            return false;
        }
        for (size_t i = 0; i < definition["storyArcs"].size(); ++i) {
            const auto& a = definition["storyArcs"][i];
            if (!a.contains("id") || !a["id"].is_string()) {
                setError(outError, "Story arc at index " + std::to_string(i) + " missing 'id' string");
                return false;
            }
        }
    }

    return true;
}

// ============================================================================
// Private parsers
// ============================================================================

bool StoryWorldLoader::parseWorld(const nlohmann::json& worldJson,
                                    WorldState& state,
                                    std::string* outError) {
    // Parse factions
    if (worldJson.contains("factions")) {
        for (const auto& fj : worldJson["factions"]) {
            Faction faction;
            faction.id = fj.at("id").get<std::string>();
            faction.name = fj.value("name", faction.id);
            state.factions[faction.id] = std::move(faction);
        }
    }

    // Parse faction relations (format: "factionA-factionB": value)
    if (worldJson.contains("factionRelations")) {
        for (auto& [key, val] : worldJson["factionRelations"].items()) {
            auto dashPos = key.find('-');
            if (dashPos == std::string::npos) {
                setError(outError, "Invalid faction relation key '" + key + "' — expected 'factionA-factionB'");
                return false;
            }
            std::string factionA = key.substr(0, dashPos);
            std::string factionB = key.substr(dashPos + 1);
            float relation = val.get<float>();

            // Set bidirectional
            if (state.factions.count(factionA))
                state.factions[factionA].relations[factionB] = relation;
            if (state.factions.count(factionB))
                state.factions[factionB].relations[factionA] = relation;
        }
    }

    // Parse locations
    if (worldJson.contains("locations")) {
        for (const auto& lj : worldJson["locations"]) {
            Location loc;
            loc.id = lj.at("id").get<std::string>();
            loc.name = lj.value("name", loc.id);
            if (lj.contains("position")) {
                loc.worldPosition = glm::vec3(
                    lj["position"].value("x", 0.0f),
                    lj["position"].value("y", 0.0f),
                    lj["position"].value("z", 0.0f)
                );
            }
            if (lj.contains("radius"))
                loc.radius = lj["radius"].get<float>();
            if (lj.contains("controllingFaction"))
                loc.controllingFaction = lj["controllingFaction"].get<std::string>();
            if (lj.contains("tags")) {
                for (const auto& tag : lj["tags"])
                    loc.tags.push_back(tag.get<std::string>());
            }
            state.locations[loc.id] = std::move(loc);
        }
    }

    // Parse world variables
    if (worldJson.contains("variables")) {
        for (auto& [key, val] : worldJson["variables"].items()) {
            if (val.is_boolean()) {
                state.setVariable(key, val.get<bool>());
            } else if (val.is_number_integer()) {
                state.setVariable(key, val.get<int>());
            } else if (val.is_number_float()) {
                state.setVariable(key, val.get<float>());
            } else if (val.is_string()) {
                state.setVariable(key, val.get<std::string>());
            }
        }
    }

    return true;
}

bool StoryWorldLoader::parseCharacter(const nlohmann::json& charJson,
                                        CharacterProfile& profile,
                                        std::vector<std::pair<std::string, std::string>>& startingKnowledge,
                                        std::string* outError) {
    profile.id = charJson.at("id").get<std::string>();
    profile.name = charJson.at("name").get<std::string>();

    if (charJson.contains("description"))
        profile.description = charJson["description"].get<std::string>();
    if (charJson.contains("faction"))
        profile.factionId = charJson["faction"].get<std::string>();

    // Agency level (integer 0-3 or string)
    if (charJson.contains("agencyLevel")) {
        const auto& al = charJson["agencyLevel"];
        if (al.is_number_integer()) {
            int level = al.get<int>();
            if (level < 0 || level > 3) {
                setError(outError, "Character '" + profile.id + "' agencyLevel must be 0-3");
                return false;
            }
            profile.agencyLevel = static_cast<AgencyLevel>(level);
        } else if (al.is_string()) {
            std::string levelStr = al.get<std::string>();
            std::transform(levelStr.begin(), levelStr.end(), levelStr.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            profile.agencyLevel = agencyLevelFromString(levelStr);
        }
    }

    // Personality traits (Big Five + custom)
    if (charJson.contains("traits")) {
        const auto& t = charJson["traits"];
        profile.traits.openness = t.value("openness", 0.5f);
        profile.traits.conscientiousness = t.value("conscientiousness", 0.5f);
        profile.traits.extraversion = t.value("extraversion", 0.5f);
        profile.traits.agreeableness = t.value("agreeableness", 0.5f);
        profile.traits.neuroticism = t.value("neuroticism", 0.5f);

        // Any non-Big-Five key is a custom trait
        for (auto& [key, val] : t.items()) {
            if (key != "openness" && key != "conscientiousness" &&
                key != "extraversion" && key != "agreeableness" &&
                key != "neuroticism" && val.is_number()) {
                profile.traits.customTraits[key] = val.get<float>();
            }
        }
    }

    // Goals
    if (charJson.contains("goals")) {
        for (const auto& gj : charJson["goals"]) {
            CharacterGoal goal;
            goal.id = gj.at("id").get<std::string>();
            goal.description = gj.value("description", "");
            goal.priority = gj.value("priority", 0.5f);
            goal.isActive = gj.value("isActive", true);
            if (gj.contains("completionCondition"))
                goal.completionCondition = gj["completionCondition"].get<std::string>();
            if (gj.contains("failureCondition"))
                goal.failureCondition = gj["failureCondition"].get<std::string>();
            profile.goals.push_back(std::move(goal));
        }
    }

    // Relationships
    if (charJson.contains("relationships")) {
        for (const auto& rj : charJson["relationships"]) {
            Relationship rel;
            rel.targetCharacterId = rj.at("target").get<std::string>();
            rel.trust = rj.value("trust", 0.0f);
            rel.affection = rj.value("affection", 0.0f);
            rel.respect = rj.value("respect", 0.0f);
            rel.fear = rj.value("fear", 0.0f);
            rel.label = rj.value("label", "");
            profile.relationships.push_back(std::move(rel));
        }
    }

    // Roles
    if (charJson.contains("roles")) {
        for (const auto& r : charJson["roles"]) {
            profile.roles.push_back(r.get<std::string>());
        }
    }

    // Allowed actions
    if (charJson.contains("allowedActions")) {
        for (const auto& a : charJson["allowedActions"]) {
            profile.allowedActions.push_back(a.get<std::string>());
        }
    }

    // Default behavior / dialogue file
    if (charJson.contains("defaultBehavior"))
        profile.defaultBehavior = charJson["defaultBehavior"].get<std::string>();
    if (charJson.contains("defaultDialogueFile"))
        profile.defaultDialogueFile = charJson["defaultDialogueFile"].get<std::string>();

    // Starting knowledge (list of strings → auto-generate fact IDs)
    if (charJson.contains("startingKnowledge")) {
        int factIdx = 0;
        for (const auto& sk : charJson["startingKnowledge"]) {
            std::string summary = sk.get<std::string>();
            std::string factId = profile.id + "_know_" + std::to_string(factIdx++);
            startingKnowledge.emplace_back(factId, summary);
        }
    }

    return true;
}

bool StoryWorldLoader::parseStoryArc(const nlohmann::json& arcJson,
                                       StoryArc& arc,
                                       std::string* outError) {
    arc.id = arcJson.at("id").get<std::string>();
    arc.name = arcJson.value("name", arc.id);
    if (arcJson.contains("description"))
        arc.description = arcJson["description"].get<std::string>();

    // Constraint mode
    if (arcJson.contains("constraintMode")) {
        arc.constraintMode = arcConstraintModeFromString(
            arcJson["constraintMode"].get<std::string>());
    }

    // Pacing parameters
    arc.minTimeBetweenBeats = arcJson.value("minTimeBetweenBeats", 60.0f);
    arc.maxTimeWithoutProgress = arcJson.value("maxTimeWithoutProgress", 300.0f);

    // Beats
    if (arcJson.contains("beats")) {
        for (const auto& bj : arcJson["beats"]) {
            StoryBeat beat;
            beat.id = bj.at("id").get<std::string>();
            beat.description = bj.value("description", "");

            // Beat type (string or default to "soft")
            if (bj.contains("type")) {
                beat.type = beatTypeFromString(bj["type"].get<std::string>());
            }

            // Conditions
            if (bj.contains("triggerCondition"))
                beat.triggerCondition = bj["triggerCondition"].get<std::string>();
            if (bj.contains("completionCondition"))
                beat.completionCondition = bj["completionCondition"].get<std::string>();
            if (bj.contains("failureCondition"))
                beat.failureCondition = bj["failureCondition"].get<std::string>();

            // Prerequisites
            if (bj.contains("prerequisites")) {
                for (const auto& p : bj["prerequisites"]) {
                    beat.prerequisites.push_back(p.get<std::string>());
                }
            }

            // Required characters
            if (bj.contains("requiredCharacters")) {
                for (const auto& c : bj["requiredCharacters"]) {
                    beat.requiredCharacters.push_back(c.get<std::string>());
                }
            }

            // Director actions (stored as strings in StoryBeat)
            if (bj.contains("directorActions")) {
                for (const auto& aj : bj["directorActions"]) {
                    if (aj.is_string()) {
                        beat.directorActions.push_back(aj.get<std::string>());
                    } else if (aj.is_object() && aj.contains("type")) {
                        // Serialize structured action as "type:params_json"
                        std::string actionStr = aj.at("type").get<std::string>();
                        if (aj.contains("params")) {
                            actionStr += ":" + aj["params"].dump();
                        }
                        beat.directorActions.push_back(std::move(actionStr));
                    }
                }
            }

            arc.beats.push_back(std::move(beat));
        }
    }

    // Tension curve
    if (arcJson.contains("tensionCurve")) {
        for (const auto& v : arcJson["tensionCurve"]) {
            arc.tensionCurve.push_back(v.get<float>());
        }
    }

    return true;
}

} // namespace Story
} // namespace Phyxel
