#include "core/CharacterVisualResolver.h"
#include "core/RaceDefinition.h"
#include "scene/AppearancePresetRegistry.h"
#include "utils/Logger.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

namespace {

constexpr const char* kDefaultAnimFile = "resources/animated_characters/humanoid.anim";

Scene::MorphologyType morphologyFromAnimFile(const std::string& animFile) {
    std::string lower = animFile;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("wolf") != std::string::npos)   return Scene::MorphologyType::Quadruped;
    if (lower.find("spider") != std::string::npos) return Scene::MorphologyType::Arachnid;
    if (lower.find("dragon") != std::string::npos) return Scene::MorphologyType::Dragon;
    return Scene::MorphologyType::Humanoid;
}

/// Deterministically pick a skin tone from a race palette by NPC name.
void applyPaletteSkinTone(const nlohmann::json& palette, const std::string& name,
                          Scene::CharacterAppearance& app) {
    if (!palette.is_object() || !palette.contains("skinTones")) return;
    const auto& tones = palette["skinTones"];
    if (!tones.is_array() || tones.empty()) return;

    size_t idx = std::hash<std::string>{}(name + "_skin") % tones.size();
    const auto& t = tones[idx];
    app.skinColor = glm::vec4(
        t.value("r", app.skinColor.r),
        t.value("g", app.skinColor.g),
        t.value("b", app.skinColor.b),
        1.0f);
}

} // namespace

CharacterVisualResolver::Resolved CharacterVisualResolver::resolve(
        const nlohmann::json& def, const std::string& name) {
    Resolved out;
    out.animFile = kDefaultAnimFile;

    const std::string role = def.value("role", "");

    // ---- Race lookup ----
    out.raceId = def.value("race", def.value("raceId", ""));
    const RaceDefinition* race = nullptr;
    if (!out.raceId.empty()) {
        auto& races = RaceRegistry::instance();
        races.ensureLoaded();
        race = races.getRace(out.raceId);
        out.raceFound = (race != nullptr);
        if (!race) {
            LOG_WARN("CharacterVisualResolver", "Unknown race '{}' for '{}' — using defaults",
                     out.raceId, name);
        }
    }

    // ---- animFile: race visual, then explicit override ----
    if (race && race->hasVisual()) {
        out.animFile = race->visual.value("animFile", out.animFile);
    }
    if (def.contains("animFile") && def["animFile"].is_string()) {
        out.animFile = def["animFile"].get<std::string>();
    }

    const Scene::MorphologyType morph = morphologyFromAnimFile(out.animFile);
    const bool hasExplicitAppearance = def.contains("appearance") && def["appearance"].is_object();

    // ---- 1. Base appearance ----
    // Legacy semantics preserved for raceless NPCs: an explicit "appearance"
    // REPLACES the seeded one (defaults + given fields), it does not merge onto
    // seeded colors. With a race, the seed provides clothing colors and the
    // race/appearance layers merge on top.
    if (race || !hasExplicitAppearance) {
        out.appearance = Scene::CharacterAppearance::generateFromSeed(name, role, morph);
    }

    // ---- 2. Race visual block ----
    if (race && race->hasVisual()) {
        auto& presets = Scene::AppearancePresetRegistry::instance();
        presets.ensureLoaded();

        const std::string presetId = race->visual.value("appearancePreset", "");
        if (!presetId.empty()) {
            if (const auto* preset = presets.getPreset(presetId)) {
                out.appearance.applyProportionsFrom(*preset);
            } else {
                LOG_WARN("CharacterVisualResolver", "Race '{}' names unknown preset '{}'",
                         out.raceId, presetId);
            }
        }
        if (race->visual.contains("palette")) {
            applyPaletteSkinTone(race->visual["palette"], name, out.appearance);
        }
        if (race->visual.contains("appearanceOverrides") &&
            race->visual["appearanceOverrides"].is_object()) {
            out.appearance = Scene::CharacterAppearance::fromJson(
                race->visual["appearanceOverrides"], out.appearance);
        }
    }

    // ---- 3. Explicit appearance JSON (may itself name a preset) ----
    if (def.contains("appearance") && def["appearance"].is_object()) {
        const auto& aj = def["appearance"];
        const std::string presetId = aj.value("preset", aj.value("presetId", ""));
        if (!presetId.empty()) {
            auto& presets = Scene::AppearancePresetRegistry::instance();
            presets.ensureLoaded();
            if (const auto* preset = presets.getPreset(presetId)) {
                out.appearance.applyProportionsFrom(*preset);
            } else {
                LOG_WARN("CharacterVisualResolver", "Appearance for '{}' names unknown preset '{}'",
                         name, presetId);
            }
        }
        out.appearance = Scene::CharacterAppearance::fromJson(aj, out.appearance);
    }

    return out;
}

} // namespace Core
} // namespace Phyxel
