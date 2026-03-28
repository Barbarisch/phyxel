#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/**
 * @brief Engine configuration — all tunable engine parameters in one place.
 *
 * Loaded from a JSON config file (engine.json) or populated with defaults.
 * Passed to subsystem constructors instead of hardcoding paths/values.
 */
struct EngineConfig {

    // -- Window -----------------------------------------------------------
    std::string windowTitle   = "Phyxel";
    int         windowWidth   = 1600;
    int         windowHeight  = 900;

    // -- Asset directories (relative to project root) ---------------------
    std::string assetsDir     = "resources";
    std::string shadersDir    = "shaders";
    std::string worldsDir     = "worlds";
    std::string scriptsDir    = "scripts";

    // -- Specific asset sub-paths (relative to assetsDir) -----------------
    std::string templatesSubdir       = "templates";
    std::string texturesSubdir        = "textures";
    std::string dialoguesSubdir       = "dialogues";
    std::string soundsSubdir          = "sounds";
    std::string animatedCharsSubdir   = "animated_characters";
    std::string recipesSubdir         = "recipes";

    // -- Key asset filenames -------------------------------------------
    std::string textureAtlasFile      = "cube_atlas.png";
    std::string defaultWorldFile      = "default.db";
    std::string loggingConfigFile     = "logging.ini";
    std::string defaultAnimFile       = "resources/animated_characters/humanoid.anim";

    // -- Network ----------------------------------------------------------
    int         apiPort       = 8090;
    bool        enableHTTPAPI = true;

    // -- Optional subsystems ----------------------------------------------
    bool        enablePython  = true;
    bool        enableAudio   = true;

    // -- AI / LLM ---------------------------------------------------------
    std::string aiProvider    = "anthropic";  // "anthropic", "openai", "ollama"
    std::string aiModel       = "";           // e.g. "claude-sonnet-4-20250514", empty = provider default
    std::string aiApiKey      = "";           // API key (prefer env var over config file)
    bool        aiAutoStart   = false;         // Start Goose server automatically on engine init

    // -- Rendering --------------------------------------------------------
    float       maxChunkRenderDistance = 256.0f;
    float       chunkInclusionDistance = 320.0f;

    // -- Camera defaults --------------------------------------------------
    float       cameraStartX  = 50.0f;
    float       cameraStartY  = 50.0f;
    float       cameraStartZ  = 50.0f;
    float       cameraYaw     = -135.0f;
    float       cameraPitch   = -30.0f;

    // -- Game Definition (for packaged games) ---------------------------------
    std::string gameDefinitionFile;   // Path to game.json for auto-load on startup

    // -- Project Mode (engine-as-editor) --------------------------------------
    std::string projectDir;           // Absolute path to game project directory (set via --project)

    // =====================================================================
    // Convenience path helpers
    // =====================================================================

    /// Full path to templates directory (assetsDir / templatesSubdir)
    std::string templatesPath() const;
    /// Full path to textures directory
    std::string texturesPath() const;
    /// Full path to dialogues directory
    std::string dialoguesPath() const;
    /// Full path to sounds directory
    std::string soundsPath() const;
    /// Full path to animated characters directory
    std::string animatedCharsPath() const;
    /// Full path to recipes directory
    std::string recipesPath() const;
    /// Full path to the texture atlas PNG
    std::string textureAtlasPath() const;
    /// Full path to the default world database
    std::string worldDatabasePath() const;

    // =====================================================================
    // Load / Save
    // =====================================================================

    /// Load config from a JSON file. Missing keys keep their defaults.
    /// Returns true even if file doesn't exist (defaults used).
    /// Returns false only on parse errors.
    static bool loadFromFile(const std::string& path, EngineConfig& out);

    /// Parse config from a JSON object. Missing keys keep defaults.
    static void fromJson(const nlohmann::json& j, EngineConfig& cfg);

    /// Serialize current config to JSON.
    nlohmann::json toJson() const;

    /// Save config to a JSON file.
    bool saveToFile(const std::string& path) const;
};

} // namespace Core
} // namespace Phyxel
