#include "core/AssetManager.h"
#include "core/EngineConfig.h"

namespace Phyxel {
namespace Core {

AssetManager& AssetManager::instance() {
    static AssetManager s_instance;
    return s_instance;
}

std::string AssetManager::joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (file.empty()) return dir;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + file;
    return dir + "/" + file;
}

void AssetManager::initialize(const EngineConfig& config) {
    m_shadersDir       = config.shadersDir;
    m_templatesDir     = config.templatesPath();
    m_texturesDir      = config.texturesPath();
    m_dialoguesDir     = config.dialoguesPath();
    m_soundsDir        = config.soundsPath();
    m_animatedCharsDir = config.animatedCharsPath();
    m_recipesDir       = config.recipesPath();
    m_worldsDir        = config.worldsDir;
    m_scriptsDir       = config.scriptsDir;

    m_textureAtlasPath = config.textureAtlasPath();
    m_worldDatabasePath = config.worldDatabasePath();
    m_loggingConfigPath = config.loggingConfigFile;
    m_defaultAnimFile   = config.defaultAnimFile;

    // Register all as named search paths too
    m_searchPaths["shaders"]          = m_shadersDir;
    m_searchPaths["templates"]        = m_templatesDir;
    m_searchPaths["textures"]         = m_texturesDir;
    m_searchPaths["dialogues"]        = m_dialoguesDir;
    m_searchPaths["sounds"]           = m_soundsDir;
    m_searchPaths["animated_chars"]   = m_animatedCharsDir;
    m_searchPaths["recipes"]          = m_recipesDir;
    m_searchPaths["worlds"]           = m_worldsDir;
    m_searchPaths["scripts"]          = m_scriptsDir;

    m_initialized = true;
}

// =========================================================================
// Typed resolvers
// =========================================================================

std::string AssetManager::resolveShader(const std::string& filename) const {
    return joinPath(m_shadersDir, filename);
}

std::string AssetManager::resolveTemplate(const std::string& filename) const {
    return joinPath(m_templatesDir, filename);
}

std::string AssetManager::resolveTexture(const std::string& filename) const {
    return joinPath(m_texturesDir, filename);
}

std::string AssetManager::resolveDialogue(const std::string& filename) const {
    return joinPath(m_dialoguesDir, filename);
}

std::string AssetManager::resolveSound(const std::string& filename) const {
    return joinPath(m_soundsDir, filename);
}

std::string AssetManager::resolveAnimatedChar(const std::string& filename) const {
    return joinPath(m_animatedCharsDir, filename);
}

std::string AssetManager::resolveRecipe(const std::string& subpath) const {
    return joinPath(m_recipesDir, subpath);
}

std::string AssetManager::resolveWorld(const std::string& filename) const {
    return joinPath(m_worldsDir, filename);
}

std::string AssetManager::resolveScript(const std::string& filename) const {
    return joinPath(m_scriptsDir, filename);
}

// =========================================================================
// Generic resolver
// =========================================================================

void AssetManager::registerSearchPath(const std::string& name, const std::string& dir) {
    m_searchPaths[name] = dir;
}

std::string AssetManager::resolve(const std::string& name, const std::string& filename) const {
    auto it = m_searchPaths.find(name);
    if (it != m_searchPaths.end()) {
        return joinPath(it->second, filename);
    }
    return filename; // fallback: return filename as-is
}

} // namespace Core
} // namespace Phyxel
