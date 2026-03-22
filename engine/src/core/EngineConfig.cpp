#include "core/EngineConfig.h"
#include <fstream>
#include <filesystem>

namespace Phyxel {
namespace Core {

// =========================================================================
// Path helpers — join with '/' separator
// =========================================================================

static std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

std::string EngineConfig::templatesPath()     const { return joinPath(assetsDir, templatesSubdir); }
std::string EngineConfig::texturesPath()      const { return joinPath(assetsDir, texturesSubdir); }
std::string EngineConfig::dialoguesPath()     const { return joinPath(assetsDir, dialoguesSubdir); }
std::string EngineConfig::soundsPath()        const { return joinPath(assetsDir, soundsSubdir); }
std::string EngineConfig::animatedCharsPath() const { return joinPath(assetsDir, animatedCharsSubdir); }
std::string EngineConfig::recipesPath()       const { return joinPath(assetsDir, recipesSubdir); }

std::string EngineConfig::textureAtlasPath()  const {
    return joinPath(texturesPath(), textureAtlasFile);
}

std::string EngineConfig::worldDatabasePath() const {
    return joinPath(worldsDir, defaultWorldFile);
}

// =========================================================================
// JSON serialization
// =========================================================================

void EngineConfig::fromJson(const nlohmann::json& j, EngineConfig& cfg) {
    // -- Window
    if (j.contains("window")) {
        auto& w = j["window"];
        if (w.contains("title"))  cfg.windowTitle  = w["title"].get<std::string>();
        if (w.contains("width"))  cfg.windowWidth   = w["width"].get<int>();
        if (w.contains("height")) cfg.windowHeight  = w["height"].get<int>();
    }

    // -- Directories
    if (j.contains("directories")) {
        auto& d = j["directories"];
        if (d.contains("assets"))  cfg.assetsDir  = d["assets"].get<std::string>();
        if (d.contains("shaders")) cfg.shadersDir = d["shaders"].get<std::string>();
        if (d.contains("worlds"))  cfg.worldsDir  = d["worlds"].get<std::string>();
        if (d.contains("scripts")) cfg.scriptsDir = d["scripts"].get<std::string>();
    }

    // -- Asset sub-paths
    if (j.contains("assets")) {
        auto& a = j["assets"];
        if (a.contains("templates"))       cfg.templatesSubdir     = a["templates"].get<std::string>();
        if (a.contains("textures"))        cfg.texturesSubdir      = a["textures"].get<std::string>();
        if (a.contains("dialogues"))       cfg.dialoguesSubdir     = a["dialogues"].get<std::string>();
        if (a.contains("sounds"))          cfg.soundsSubdir        = a["sounds"].get<std::string>();
        if (a.contains("animated_chars"))  cfg.animatedCharsSubdir = a["animated_chars"].get<std::string>();
        if (a.contains("recipes"))         cfg.recipesSubdir       = a["recipes"].get<std::string>();
        if (a.contains("texture_atlas"))   cfg.textureAtlasFile    = a["texture_atlas"].get<std::string>();
        if (a.contains("default_world"))   cfg.defaultWorldFile    = a["default_world"].get<std::string>();
        if (a.contains("logging_config"))  cfg.loggingConfigFile   = a["logging_config"].get<std::string>();
        if (a.contains("default_anim"))    cfg.defaultAnimFile     = a["default_anim"].get<std::string>();
    }

    // -- Network
    if (j.contains("network")) {
        auto& n = j["network"];
        if (n.contains("api_port"))       cfg.apiPort       = n["api_port"].get<int>();
        if (n.contains("enable_http_api")) cfg.enableHTTPAPI = n["enable_http_api"].get<bool>();
    }

    // -- Features
    if (j.contains("features")) {
        auto& f = j["features"];
        if (f.contains("python")) cfg.enablePython = f["python"].get<bool>();
        if (f.contains("audio"))  cfg.enableAudio  = f["audio"].get<bool>();
    }

    // -- Rendering
    if (j.contains("rendering")) {
        auto& r = j["rendering"];
        if (r.contains("max_chunk_render_distance")) cfg.maxChunkRenderDistance = r["max_chunk_render_distance"].get<float>();
        if (r.contains("chunk_inclusion_distance"))  cfg.chunkInclusionDistance = r["chunk_inclusion_distance"].get<float>();
    }

    // -- Camera
    if (j.contains("camera")) {
        auto& c = j["camera"];
        if (c.contains("start_x")) cfg.cameraStartX = c["start_x"].get<float>();
        if (c.contains("start_y")) cfg.cameraStartY = c["start_y"].get<float>();
        if (c.contains("start_z")) cfg.cameraStartZ = c["start_z"].get<float>();
        if (c.contains("yaw"))     cfg.cameraYaw    = c["yaw"].get<float>();
        if (c.contains("pitch"))   cfg.cameraPitch   = c["pitch"].get<float>();
    }
}

nlohmann::json EngineConfig::toJson() const {
    return nlohmann::json{
        {"window", {
            {"title", windowTitle},
            {"width", windowWidth},
            {"height", windowHeight}
        }},
        {"directories", {
            {"assets",  assetsDir},
            {"shaders", shadersDir},
            {"worlds",  worldsDir},
            {"scripts", scriptsDir}
        }},
        {"assets", {
            {"templates",       templatesSubdir},
            {"textures",        texturesSubdir},
            {"dialogues",       dialoguesSubdir},
            {"sounds",          soundsSubdir},
            {"animated_chars",  animatedCharsSubdir},
            {"recipes",         recipesSubdir},
            {"texture_atlas",   textureAtlasFile},
            {"default_world",   defaultWorldFile},
            {"logging_config",  loggingConfigFile},
            {"default_anim",    defaultAnimFile}
        }},
        {"network", {
            {"api_port",        apiPort},
            {"enable_http_api", enableHTTPAPI}
        }},
        {"features", {
            {"python", enablePython},
            {"audio",  enableAudio}
        }},
        {"rendering", {
            {"max_chunk_render_distance", maxChunkRenderDistance},
            {"chunk_inclusion_distance",  chunkInclusionDistance}
        }},
        {"camera", {
            {"start_x", cameraStartX},
            {"start_y", cameraStartY},
            {"start_z", cameraStartZ},
            {"yaw",     cameraYaw},
            {"pitch",   cameraPitch}
        }}
    };
}

// =========================================================================
// File I/O
// =========================================================================

bool EngineConfig::loadFromFile(const std::string& path, EngineConfig& out) {
    out = EngineConfig{}; // start from defaults

    if (!std::filesystem::exists(path)) {
        // No config file — that's fine, use defaults
        return true;
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return true; // can't open → defaults
    }

    try {
        nlohmann::json j = nlohmann::json::parse(ifs);
        fromJson(j, out);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false; // parse error
    }
}

bool EngineConfig::saveToFile(const std::string& path) const {
    // Ensure parent directory exists
    auto parentPath = std::filesystem::path(path).parent_path();
    if (!parentPath.empty() && !std::filesystem::exists(parentPath)) {
        std::filesystem::create_directories(parentPath);
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;

    ofs << toJson().dump(2);
    return ofs.good();
}

} // namespace Core
} // namespace Phyxel
