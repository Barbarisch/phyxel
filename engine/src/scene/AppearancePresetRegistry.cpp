#include "scene/AppearancePresetRegistry.h"
#include "utils/Logger.h"

#include <fstream>

namespace Phyxel {
namespace Scene {

AppearancePresetRegistry& AppearancePresetRegistry::instance() {
    static AppearancePresetRegistry s_instance;
    return s_instance;
}

bool AppearancePresetRegistry::registerPreset(const std::string& id, const CharacterAppearance& preset) {
    if (id.empty()) {
        LOG_WARN("AppearancePresetRegistry", "Cannot register preset with empty ID");
        return false;
    }
    auto [it, inserted] = m_presets.emplace(id, preset);
    if (!inserted) {
        LOG_WARN("AppearancePresetRegistry", "Preset '{}' already registered — skipping", id);
    }
    return inserted;
}

const CharacterAppearance* AppearancePresetRegistry::getPreset(const std::string& id) const {
    auto it = m_presets.find(id);
    return (it != m_presets.end()) ? &it->second : nullptr;
}

bool AppearancePresetRegistry::hasPreset(const std::string& id) const {
    return m_presets.find(id) != m_presets.end();
}

std::vector<std::string> AppearancePresetRegistry::getAllPresetIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_presets.size());
    for (const auto& [id, _] : m_presets) ids.push_back(id);
    return ids;
}

int AppearancePresetRegistry::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_WARN("AppearancePresetRegistry", "Could not open preset file: {}", filepath);
        return 0;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        return loadFromJson(j);
    } catch (const std::exception& e) {
        LOG_WARN("AppearancePresetRegistry", "Failed to parse preset file '{}': {}", filepath, e.what());
        return 0;
    }
}

int AppearancePresetRegistry::loadFromJson(const nlohmann::json& j) {
    const nlohmann::json* arr = nullptr;
    if (j.is_array()) {
        arr = &j;
    } else if (j.is_object() && j.contains("presets") && j["presets"].is_array()) {
        arr = &j["presets"];
    } else {
        LOG_WARN("AppearancePresetRegistry", "Preset JSON must be an array or {{\"presets\": [...]}}");
        return 0;
    }

    int count = 0;
    for (const auto& entry : *arr) {
        if (!entry.is_object()) continue;
        std::string id = entry.value("presetId", entry.value("preset_id", ""));
        if (id.empty()) {
            LOG_WARN("AppearancePresetRegistry", "Skipping preset entry without presetId");
            continue;
        }
        CharacterAppearance app = CharacterAppearance::fromJson(entry);
        app.presetId = id;
        if (registerPreset(id, app)) ++count;
    }
    return count;
}

void AppearancePresetRegistry::ensureLoaded() {
    if (m_loadAttempted || !m_presets.empty()) return;
    m_loadAttempted = true;
    int n = loadFromFile("resources/appearance_presets.json");
    LOG_INFO("AppearancePresetRegistry", "Loaded {} appearance presets", n);
}

void AppearancePresetRegistry::clear() {
    m_presets.clear();
    m_loadAttempted = false;
}

} // namespace Scene
} // namespace Phyxel
