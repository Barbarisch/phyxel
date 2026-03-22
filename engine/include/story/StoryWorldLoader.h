#pragma once

#include "story/StoryEngine.h"
#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Story {

// ============================================================================
// StoryWorldLoader — loads a JSON world definition into a StoryEngine.
//
// Parses the developer-facing JSON format:
// {
//   "world": { "factions": [...], "factionRelations": {...}, "locations": [...], "variables": {...} },
//   "characters": [ { "id", "name", "faction", "agencyLevel", "traits", "goals",
//                      "relationships", "roles", "startingKnowledge", ... } ],
//   "storyArcs": [ { "id", "name", "constraintMode", "beats": [...], "tensionCurve": [...] } ]
// }
// ============================================================================

class StoryWorldLoader {
public:
    /// Load a world definition from a JSON object into a StoryEngine.
    /// Returns true on success, false on error (with error message in outError).
    static bool loadFromJson(const nlohmann::json& definition,
                              StoryEngine& engine,
                              std::string* outError = nullptr);

    /// Load a world definition from a JSON file.
    static bool loadFromFile(const std::string& filePath,
                              StoryEngine& engine,
                              std::string* outError = nullptr);

    /// Validate a world definition JSON without loading it.
    /// Returns true if valid, false with error message if not.
    static bool validate(const nlohmann::json& definition,
                          std::string* outError = nullptr);

private:
    static bool parseWorld(const nlohmann::json& worldJson,
                            WorldState& state,
                            std::string* outError);

    static bool parseCharacter(const nlohmann::json& charJson,
                                CharacterProfile& profile,
                                std::vector<std::pair<std::string, std::string>>& startingKnowledge,
                                std::string* outError);

    static bool parseStoryArc(const nlohmann::json& arcJson,
                               StoryArc& arc,
                               std::string* outError);

    static void setError(std::string* outError, const std::string& msg);
};

} // namespace Story
} // namespace Phyxel
