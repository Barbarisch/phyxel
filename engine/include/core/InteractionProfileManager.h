#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

/// Per-archetype tuning offsets for a specific interaction point on a specific asset.
///
/// The base struct holds the default offsets for this archetype. Optional
/// per-character overrides (keyed by `presetId` such as "giant" or "dwarf")
/// live alongside in `perCharacter`; `resolveProfile()` picks the right one
/// at lookup time so the runtime never has to merge.
///
/// `kind` tags which interaction kind these params describe. The flat sit
/// fields (sitDownOffset, etc.) are the canonical home for kind="sit"; new
/// kinds (pivot, slide, climb, control, carry, container, mount) store
/// their params in the typed dictionaries (vec3Params/scalarParams/
/// stringParams). The dictionaries are also available to sit overrides if
/// kind-specific extension data is ever needed.
struct InteractionProfile {
    /// Which interaction kind this profile parameterises. Must match the
    /// `kind` (or `type`) of the InteractionPointDef this profile is bound
    /// to. Defaults to "sit" for backward compatibility with profiles
    /// loaded from pre-Phase-A JSON files (no `kind` field on disk).
    std::string kind = "sit";

    // --- kind="sit" params (kept as typed fields for ergonomics) ---
    glm::vec3 sitDownOffset{0.0f};
    glm::vec3 sittingIdleOffset{0.0f};
    glm::vec3 sitStandUpOffset{0.0f};
    float sitBlendDuration = 0.0f;
    float seatHeightOffset = 0.0f;

    // --- generic params for non-sit kinds ---
    // Keys are kind-defined (e.g. pivot uses "hinge_origin", "hinge_axis").
    // See docs/InteractionKinds.md for the param schema per kind.
    std::unordered_map<std::string, glm::vec3>   vec3Params;
    std::unordered_map<std::string, float>       scalarParams;
    std::unordered_map<std::string, std::string> stringParams;

    // Per-character overrides. Each override is a *complete* InteractionProfile
    // (NOT a delta) — at lookup time we return either the base or one of these,
    // never a merge. This keeps the runtime path branch-free and predictable.
    // The map's own `perCharacter` field is always empty inside overrides.
    std::unordered_map<std::string, InteractionProfile> perCharacter;
};

/// Manages interaction profiles: per-(archetype, templateName, pointId) tuning data.
/// Profiles live in JSON files under resources/interactions/<archetype>.json.
class InteractionProfileManager {
public:
    /// Set the base directory for profile JSON files (e.g. "resources/interactions/").
    void setBasePath(const std::string& path) { m_basePath = path; }
    const std::string& getBasePath() const { return m_basePath; }

    /// Load all profiles for an archetype from its JSON file. Returns false if file missing.
    bool loadArchetype(const std::string& archetype);

    /// Save all profiles for an archetype to its JSON file.
    bool saveArchetype(const std::string& archetype) const;

    /// Get the base profile (no character override applied), or nullptr.
    /// Prefer `resolveProfile()` from runtime sit code so per-character
    /// overrides are honoured automatically.
    const InteractionProfile* getProfile(const std::string& archetype,
                                          const std::string& templateName,
                                          const std::string& pointId) const;

    /// Resolve to the override for `characterId` if one exists, else the base.
    /// Empty `characterId` always returns the base. Returns nullptr if no
    /// base profile exists (regardless of overrides — we never serve a
    /// per-character override without a base, by construction of the schema).
    const InteractionProfile* resolveProfile(const std::string& archetype,
                                              const std::string& templateName,
                                              const std::string& pointId,
                                              const std::string& characterId) const;

    /// Set (create or overwrite) the base profile for a key.
    void setProfile(const std::string& archetype,
                    const std::string& templateName,
                    const std::string& pointId,
                    const InteractionProfile& profile);

    /// Set (create or overwrite) a per-character override on an existing base
    /// profile. No-op + returns false if the base profile doesn't exist yet —
    /// callers must seed the base first so resolveProfile() has a fallback.
    bool setCharacterOverride(const std::string& archetype,
                              const std::string& templateName,
                              const std::string& pointId,
                              const std::string& characterId,
                              const InteractionProfile& override);

    /// Check if a profile exists.
    bool hasProfile(const std::string& archetype,
                    const std::string& templateName,
                    const std::string& pointId) const;

    /// Get all template→pointId→profile mappings for an archetype (for UI editing).
    using PointMap = std::unordered_map<std::string, InteractionProfile>;
    using TemplateMap = std::unordered_map<std::string, PointMap>;
    TemplateMap& getMutableProfiles(const std::string& archetype);

    /// Get list of all archetypes that have been loaded or modified.
    std::vector<std::string> getLoadedArchetypes() const;

private:
    std::string m_basePath = "resources/interactions/";
    // archetype → templateName → pointId → profile
    std::unordered_map<std::string, TemplateMap> m_profiles;
};

} // namespace Core
} // namespace Phyxel
