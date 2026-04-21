#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

using json = nlohmann::json;

/// Visual transition style when switching between scenes.
enum class SceneTransitionStyle {
    Cut,            ///< Instant switch (no animation)
    Fade,           ///< Fade to black, load, fade in
    LoadingScreen   ///< Show loading screen during transition
};

/// A single scene definition — one "level" or "world state" of a game.
///
/// Each scene has its own world database (chunks), entities, NPCs, lighting,
/// camera defaults, and optional script hooks. The game definition JSON for
/// a scene uses the same schema as the existing single-scene game.json format.
struct SceneDefinition {
    std::string id;             ///< Unique identifier (e.g., "overworld", "dungeon_1")
    std::string name;           ///< Human-readable name
    std::string description;    ///< Optional description

    /// SQLite database filename for this scene's world chunks.
    /// Relative to the project's worlds/ directory (e.g., "overworld.db").
    /// If empty, defaults to "<id>.db".
    std::string worldDatabase;

    /// The scene's game definition — same schema as existing game.json.
    /// Contains: world, structures, player, camera, npcs, locations, story, etc.
    json definition;

    /// Optional Python script to run when entering this scene.
    std::string onEnterScript;

    /// Optional Python script to run when leaving this scene.
    std::string onExitScript;

    /// Transition style when entering this scene.
    SceneTransitionStyle transitionStyle = SceneTransitionStyle::LoadingScreen;

    /// Resolved database path (set at runtime by SceneManager).
    std::string resolvedWorldPath;

    std::string getWorldDatabaseFilename() const {
        return worldDatabase.empty() ? (id + ".db") : worldDatabase;
    }

    static SceneDefinition fromJson(const json& j);
    json toJson() const;
};

/// Top-level scene manifest — defines all scenes in a game.
///
/// When a game.json contains a "scenes" key, it is parsed as a SceneManifest.
/// Games without "scenes" behave exactly as before (single implicit scene).
struct SceneManifest {
    /// All scenes in the game.
    std::vector<SceneDefinition> scenes;

    /// ID of the scene to load on startup.
    std::string startScene;

    /// Player defaults (type, animFile, appearance) applied when spawning
    /// the player in any scene that doesn't override them.
    json playerDefaults;

    /// Global story configuration that persists across all scenes.
    /// Arcs, characters, and world variables defined here survive scene transitions.
    json globalStory;

    /// Find a scene by ID. Returns nullptr if not found.
    const SceneDefinition* findScene(const std::string& sceneId) const;

    /// True if this manifest has valid scenes.
    bool isValid() const { return !scenes.empty() && !startScene.empty(); }

    static SceneManifest fromJson(const json& j);
    json toJson() const;

    /// Check if a game definition JSON uses the multi-scene format.
    static bool isMultiScene(const json& gameDef);
};

// JSON conversion helpers
inline SceneTransitionStyle transitionStyleFromString(const std::string& s) {
    if (s == "cut") return SceneTransitionStyle::Cut;
    if (s == "fade") return SceneTransitionStyle::Fade;
    return SceneTransitionStyle::LoadingScreen;
}

inline std::string transitionStyleToString(SceneTransitionStyle style) {
    switch (style) {
        case SceneTransitionStyle::Cut: return "cut";
        case SceneTransitionStyle::Fade: return "fade";
        case SceneTransitionStyle::LoadingScreen: return "loading_screen";
    }
    return "loading_screen";
}

} // namespace Core
} // namespace Phyxel
