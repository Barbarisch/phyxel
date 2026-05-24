#include "core/InteractionProfileManager.h"
#include "utils/Logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace Phyxel {
namespace Core {

bool InteractionProfileManager::loadArchetype(const std::string& archetype) {
    std::string filePath = m_basePath + archetype + ".json";
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_DEBUG("InteractionProfileManager", "No profile file for archetype '{}': {}", archetype, filePath);
        return false;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(file);
        auto& archetypeProfiles = m_profiles[archetype];
        archetypeProfiles.clear();

        // Helper: read offset/blend/height fields + generic params out of a
        // JSON object into an InteractionProfile. Used both for the base
        // profile and for each entry in the `per_character` block, hence the
        // local lambda.
        //
        // The `kind` discriminator is read from the top of the point object
        // (defaults to "sit" when absent, for pre-Phase-A files). The flat
        // sit fields (sitDownOffset etc.) remain at the top level for
        // ergonomic editing; non-sit kinds park their params under `params`.
        auto readProfileFields = [](const nlohmann::json& src, InteractionProfile& out) {
            if (src.contains("kind") && src["kind"].is_string())
                out.kind = src["kind"].get<std::string>();

            if (src.contains("sitDownOffset") && src["sitDownOffset"].is_array()) {
                auto& a = src["sitDownOffset"];
                out.sitDownOffset = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
            }
            if (src.contains("sittingIdleOffset") && src["sittingIdleOffset"].is_array()) {
                auto& a = src["sittingIdleOffset"];
                out.sittingIdleOffset = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
            }
            if (src.contains("sitStandUpOffset") && src["sitStandUpOffset"].is_array()) {
                auto& a = src["sitStandUpOffset"];
                out.sitStandUpOffset = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
            }
            if (src.contains("blendDuration"))
                out.sitBlendDuration = src["blendDuration"].get<float>();
            if (src.contains("heightOffset"))
                out.seatHeightOffset = src["heightOffset"].get<float>();

            // Generic per-kind params live under `params`. The shape is
            // intentionally untyped here (the kind contract documents which
            // keys belong in which sub-object); each sub-object is optional.
            if (src.contains("params") && src["params"].is_object()) {
                const auto& p = src["params"];
                if (p.contains("vec3") && p["vec3"].is_object()) {
                    for (auto& [k, v] : p["vec3"].items()) {
                        if (v.is_array() && v.size() == 3)
                            out.vec3Params[k] = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
                    }
                }
                if (p.contains("scalar") && p["scalar"].is_object()) {
                    for (auto& [k, v] : p["scalar"].items())
                        if (v.is_number()) out.scalarParams[k] = v.get<float>();
                }
                if (p.contains("string") && p["string"].is_object()) {
                    for (auto& [k, v] : p["string"].items())
                        if (v.is_string()) out.stringParams[k] = v.get<std::string>();
                }
            }
        };

        if (root.contains("profiles") && root["profiles"].is_object()) {
            for (auto& [templateName, templateObj] : root["profiles"].items()) {
                if (!templateObj.is_object()) continue;
                for (auto& [pointId, pointObj] : templateObj.items()) {
                    if (!pointObj.is_object()) continue;
                    InteractionProfile profile;
                    readProfileFields(pointObj, profile);

                    // Optional per-character overrides. Each entry is a full
                    // InteractionProfile authored independently — unset axes
                    // fall through to the override's struct defaults (zeros),
                    // NOT to the base. This keeps semantics predictable.
                    if (pointObj.contains("per_character") && pointObj["per_character"].is_object()) {
                        for (auto& [characterId, overrideObj] : pointObj["per_character"].items()) {
                            if (!overrideObj.is_object()) continue;
                            InteractionProfile ov;
                            readProfileFields(overrideObj, ov);
                            profile.perCharacter[characterId] = ov;
                        }
                    }

                    archetypeProfiles[templateName][pointId] = profile;
                }
            }
        }

        int totalPoints = 0;
        int totalOverrides = 0;
        for (auto& [t, pts] : archetypeProfiles) {
            totalPoints += static_cast<int>(pts.size());
            for (auto& [pid, prof] : pts) totalOverrides += static_cast<int>(prof.perCharacter.size());
        }
        LOG_INFO("InteractionProfileManager", "Loaded archetype '{}': {} templates, {} points, {} overrides",
                 archetype, archetypeProfiles.size(), totalPoints, totalOverrides);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("InteractionProfileManager", "Failed to parse {}: {}", filePath, e.what());
        return false;
    }
}

bool InteractionProfileManager::saveArchetype(const std::string& archetype) const {
    auto it = m_profiles.find(archetype);
    if (it == m_profiles.end()) return false;

    std::string filePath = m_basePath + archetype + ".json";

    // Ensure directory exists
    std::filesystem::path dir(m_basePath);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    nlohmann::json root;
    root["archetype"] = archetype;
    root["profiles"] = nlohmann::json::object();

    for (auto& [templateName, pointMap] : it->second) {
        nlohmann::json templateObj = nlohmann::json::object();
        for (auto& [pointId, profile] : pointMap) {
            // Write base profile fields. The `kind` discriminator is always
            // emitted so disk files round-trip cleanly (older files that
            // lacked the field will gain it on first save). Generic params
            // only emit when populated, to keep sit-only files compact.
            auto writeProfileFields = [](const InteractionProfile& src, nlohmann::json& dst) {
                dst["kind"]              = src.kind;
                dst["sitDownOffset"]     = {src.sitDownOffset.x,     src.sitDownOffset.y,     src.sitDownOffset.z};
                dst["sittingIdleOffset"] = {src.sittingIdleOffset.x, src.sittingIdleOffset.y, src.sittingIdleOffset.z};
                dst["sitStandUpOffset"]  = {src.sitStandUpOffset.x,  src.sitStandUpOffset.y,  src.sitStandUpOffset.z};
                dst["blendDuration"]     = src.sitBlendDuration;
                dst["heightOffset"]      = src.seatHeightOffset;

                if (!src.vec3Params.empty() || !src.scalarParams.empty() || !src.stringParams.empty()) {
                    nlohmann::json params = nlohmann::json::object();
                    if (!src.vec3Params.empty()) {
                        nlohmann::json v = nlohmann::json::object();
                        for (auto& [k, val] : src.vec3Params) v[k] = {val.x, val.y, val.z};
                        params["vec3"] = v;
                    }
                    if (!src.scalarParams.empty()) {
                        nlohmann::json v = nlohmann::json::object();
                        for (auto& [k, val] : src.scalarParams) v[k] = val;
                        params["scalar"] = v;
                    }
                    if (!src.stringParams.empty()) {
                        nlohmann::json v = nlohmann::json::object();
                        for (auto& [k, val] : src.stringParams) v[k] = val;
                        params["string"] = v;
                    }
                    dst["params"] = params;
                }
            };
            nlohmann::json pointObj;
            writeProfileFields(profile, pointObj);

            // Optional per-character overrides — only emit the block if any.
            if (!profile.perCharacter.empty()) {
                nlohmann::json overridesObj = nlohmann::json::object();
                for (auto& [characterId, ov] : profile.perCharacter) {
                    nlohmann::json ovObj;
                    writeProfileFields(ov, ovObj);
                    overridesObj[characterId] = ovObj;
                }
                pointObj["per_character"] = overridesObj;
            }

            templateObj[pointId] = pointObj;
        }
        root["profiles"][templateName] = templateObj;
    }

    std::ofstream out(filePath);
    if (!out.is_open()) {
        LOG_ERROR("InteractionProfileManager", "Cannot write to {}", filePath);
        return false;
    }
    out << root.dump(2) << "\n";
    out.close();

    LOG_INFO("InteractionProfileManager", "Saved archetype '{}' to {}", archetype, filePath);
    return true;
}

const InteractionProfile* InteractionProfileManager::getProfile(
    const std::string& archetype,
    const std::string& templateName,
    const std::string& pointId) const
{
    auto archIt = m_profiles.find(archetype);
    if (archIt == m_profiles.end()) return nullptr;
    auto tmplIt = archIt->second.find(templateName);
    if (tmplIt == archIt->second.end()) return nullptr;
    auto ptIt = tmplIt->second.find(pointId);
    if (ptIt == tmplIt->second.end()) return nullptr;
    return &ptIt->second;
}

void InteractionProfileManager::setProfile(
    const std::string& archetype,
    const std::string& templateName,
    const std::string& pointId,
    const InteractionProfile& profile)
{
    // Preserve any existing per-character overrides when the caller is just
    // updating the base profile; setProfile callers don't pass overrides in.
    // (To intentionally clear overrides, pass an InteractionProfile whose
    //  perCharacter map is empty AND first delete the base — we deliberately
    //  treat unset perCharacter on the incoming profile as "leave existing".)
    auto& slot = m_profiles[archetype][templateName][pointId];
    auto savedOverrides = std::move(slot.perCharacter);
    slot = profile;
    if (slot.perCharacter.empty()) slot.perCharacter = std::move(savedOverrides);
}

bool InteractionProfileManager::setCharacterOverride(
    const std::string& archetype,
    const std::string& templateName,
    const std::string& pointId,
    const std::string& characterId,
    const InteractionProfile& override)
{
    if (characterId.empty()) {
        LOG_WARN("InteractionProfileManager",
                 "setCharacterOverride called with empty characterId for {}/{}/{}",
                 archetype, templateName, pointId);
        return false;
    }
    auto archIt = m_profiles.find(archetype);
    if (archIt == m_profiles.end()) return false;
    auto tmplIt = archIt->second.find(templateName);
    if (tmplIt == archIt->second.end()) return false;
    auto ptIt = tmplIt->second.find(pointId);
    if (ptIt == tmplIt->second.end()) return false;
    // Strip nested overrides from the incoming struct so we never accidentally
    // store a tree of overrides-within-overrides.
    InteractionProfile clean = override;
    clean.perCharacter.clear();
    ptIt->second.perCharacter[characterId] = clean;
    return true;
}

const InteractionProfile* InteractionProfileManager::resolveProfile(
    const std::string& archetype,
    const std::string& templateName,
    const std::string& pointId,
    const std::string& characterId) const
{
    const InteractionProfile* base = getProfile(archetype, templateName, pointId);
    if (!base) return nullptr;
    if (characterId.empty()) return base;
    auto ovIt = base->perCharacter.find(characterId);
    if (ovIt == base->perCharacter.end()) return base;
    return &ovIt->second;
}

bool InteractionProfileManager::hasProfile(
    const std::string& archetype,
    const std::string& templateName,
    const std::string& pointId) const
{
    return getProfile(archetype, templateName, pointId) != nullptr;
}

InteractionProfileManager::TemplateMap& InteractionProfileManager::getMutableProfiles(
    const std::string& archetype)
{
    return m_profiles[archetype];
}

std::vector<std::string> InteractionProfileManager::getLoadedArchetypes() const {
    std::vector<std::string> result;
    result.reserve(m_profiles.size());
    for (auto& [name, _] : m_profiles) {
        result.push_back(name);
    }
    return result;
}

} // namespace Core
} // namespace Phyxel
