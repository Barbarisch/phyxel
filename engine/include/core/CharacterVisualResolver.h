#pragma once
#include "scene/CharacterAppearance.h"

#include <map>
#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Resolves an NPC/character definition JSON into a concrete visual:
/// which .anim file to load and the CharacterAppearance to apply.
///
/// This is THE single resolution path for character visuals — the spawn_npc
/// API handler, GameDefinitionLoader, and any future spawn route must all go
/// through it so race/preset behavior can never fork.
///
/// Resolution order (later wins):
///   1. Seeded appearance from name+role (colors + morphology), as before.
///   2. Race visual block (RaceRegistry): animFile, appearancePreset
///      proportions (SET, not multiplied — role/seed proportion tweaks are
///      discarded so racial silhouettes stay inside the rig-validated band),
///      palette skin tone (deterministic pick by name), appearanceOverrides.
///   3. Explicit "appearance" JSON in the definition. If it names a
///      "preset"/"presetId" from AppearancePresetRegistry, that preset's
///      proportions are applied first, then the remaining fields on top.
///   4. Explicit "animFile" in the definition (overrides the race's).
struct CharacterVisualResolver {
    struct Resolved {
        std::string animFile;
        Scene::CharacterAppearance appearance;
        std::string raceId;    ///< resolved race id, empty if none
        bool raceFound = false; ///< true if a requested race id existed
        /// FSM state-name -> clip-name overrides (race gait flavor: a halfling
        /// walks with scamper_walk, an ogre with ogre_walk). Sourced from the
        /// race visual "animationMapping" object, then the definition's own
        /// "animationMapping" merged on top. Callers apply via
        /// AnimatedVoxelCharacter::setAnimationMapping.
        std::map<std::string, std::string> animationMapping;
    };

    /// def: NPC/character definition JSON. Recognized keys: "race" (or
    /// "raceId"), "animFile", "appearance", "role". name seeds deterministic
    /// per-NPC variation.
    static Resolved resolve(const nlohmann::json& def, const std::string& name);
};

} // namespace Core
} // namespace Phyxel
