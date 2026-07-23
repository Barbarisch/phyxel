#pragma once
#include "scene/CharacterAppearance.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Scene {

/// Registry of named appearance presets (dwarf, halfling, giant, ...) loaded
/// from resources/appearance_presets.json. A preset is a CharacterAppearance
/// proportion set applied on top of a base rig — no per-race .anim needed.
///
/// Shared source of truth with tools/interaction_pipeline/morphology_presets.py,
/// which loads the same JSON file.
class AppearancePresetRegistry {
public:
    static AppearancePresetRegistry& instance();

    bool registerPreset(const std::string& id, const CharacterAppearance& preset);
    const CharacterAppearance* getPreset(const std::string& id) const;
    bool hasPreset(const std::string& id) const;
    std::vector<std::string> getAllPresetIds() const;
    size_t count() const { return m_presets.size(); }

    /// Load presets from a JSON file ({"presets": [...]}). Returns count loaded.
    int loadFromFile(const std::string& filepath);

    /// Load from a parsed JSON document ({"presets": [...]} or a bare array).
    int loadFromJson(const nlohmann::json& j);

    /// Load the default resources/appearance_presets.json once if empty.
    /// Safe to call on every lookup path.
    void ensureLoaded();

    void clear();

private:
    AppearancePresetRegistry() = default;
    AppearancePresetRegistry(const AppearancePresetRegistry&) = delete;
    AppearancePresetRegistry& operator=(const AppearancePresetRegistry&) = delete;

    std::unordered_map<std::string, CharacterAppearance> m_presets;
    bool m_loadAttempted = false;
};

} // namespace Scene
} // namespace Phyxel
