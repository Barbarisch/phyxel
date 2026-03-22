#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace Phyxel {
namespace Core {

struct EngineConfig;

/**
 * @brief Centralized asset path resolution.
 *
 * Initialized from EngineConfig.  All engine/game code resolves file paths
 * through this class instead of using hardcoded strings.
 *
 * Usage:
 *   auto& am = AssetManager::instance();
 *   am.initialize(config);
 *   std::string path = am.resolveShader("static_voxel.vert.spv");
 *   std::string tmpl = am.resolveTemplate("tree.txt");
 */
class AssetManager {
public:
    /// Global singleton.  Must call initialize() before use.
    static AssetManager& instance();

    /// Configure from an EngineConfig.  Can be called again to reconfigure.
    void initialize(const EngineConfig& config);

    /// Has initialize() been called?
    bool isInitialized() const { return m_initialized; }

    // =================================================================
    // Typed resolvers — return full path ready for file I/O
    // =================================================================

    /// Shader: shadersDir / filename
    std::string resolveShader(const std::string& filename) const;

    /// Template: assetsDir/templates / filename
    std::string resolveTemplate(const std::string& filename) const;

    /// Texture: assetsDir/textures / filename
    std::string resolveTexture(const std::string& filename) const;

    /// Dialogue: assetsDir/dialogues / filename
    std::string resolveDialogue(const std::string& filename) const;

    /// Sound: assetsDir/sounds / filename
    std::string resolveSound(const std::string& filename) const;

    /// Animated character: assetsDir/animated_characters / filename
    std::string resolveAnimatedChar(const std::string& filename) const;

    /// Recipe: assetsDir/recipes / filename (or subpath)
    std::string resolveRecipe(const std::string& subpath) const;

    /// World database: worldsDir / filename
    std::string resolveWorld(const std::string& filename) const;

    /// Script: scriptsDir / filename
    std::string resolveScript(const std::string& filename) const;

    // =================================================================
    // Directory accessors
    // =================================================================

    const std::string& shadersDir()     const { return m_shadersDir; }
    const std::string& templatesDir()   const { return m_templatesDir; }
    const std::string& texturesDir()    const { return m_texturesDir; }
    const std::string& dialoguesDir()   const { return m_dialoguesDir; }
    const std::string& soundsDir()      const { return m_soundsDir; }
    const std::string& animatedCharsDir() const { return m_animatedCharsDir; }
    const std::string& recipesDir()     const { return m_recipesDir; }
    const std::string& worldsDir()      const { return m_worldsDir; }
    const std::string& scriptsDir()     const { return m_scriptsDir; }

    // =================================================================
    // Special files from config
    // =================================================================

    /// Full path to the texture atlas PNG
    const std::string& textureAtlasPath() const { return m_textureAtlasPath; }

    /// Full path to the default world database
    const std::string& worldDatabasePath() const { return m_worldDatabasePath; }

    /// Full path to the logging config
    const std::string& loggingConfigPath() const { return m_loggingConfigPath; }

    /// Default animation file name
    const std::string& defaultAnimFile() const { return m_defaultAnimFile; }

    // =================================================================
    // Generic resolver (for custom/dynamic paths)
    // =================================================================

    /// Register a named search directory.
    void registerSearchPath(const std::string& name, const std::string& dir);

    /// Resolve filename against a named search directory.
    std::string resolve(const std::string& name, const std::string& filename) const;

private:
    AssetManager() = default;

    static std::string joinPath(const std::string& dir, const std::string& file);

    bool m_initialized = false;

    // Cached directory paths
    std::string m_shadersDir;
    std::string m_templatesDir;
    std::string m_texturesDir;
    std::string m_dialoguesDir;
    std::string m_soundsDir;
    std::string m_animatedCharsDir;
    std::string m_recipesDir;
    std::string m_worldsDir;
    std::string m_scriptsDir;

    // Cached special files
    std::string m_textureAtlasPath;
    std::string m_worldDatabasePath;
    std::string m_loggingConfigPath;
    std::string m_defaultAnimFile;

    // Custom named search paths
    std::unordered_map<std::string, std::string> m_searchPaths;
};

} // namespace Core
} // namespace Phyxel
