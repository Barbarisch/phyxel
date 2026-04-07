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

        if (root.contains("profiles") && root["profiles"].is_object()) {
            for (auto& [templateName, templateObj] : root["profiles"].items()) {
                if (!templateObj.is_object()) continue;
                for (auto& [pointId, pointObj] : templateObj.items()) {
                    if (!pointObj.is_object()) continue;
                    InteractionProfile profile;
                    if (pointObj.contains("sitDownOffset") && pointObj["sitDownOffset"].is_array()) {
                        auto& a = pointObj["sitDownOffset"];
                        profile.sitDownOffset = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
                    }
                    if (pointObj.contains("sittingIdleOffset") && pointObj["sittingIdleOffset"].is_array()) {
                        auto& a = pointObj["sittingIdleOffset"];
                        profile.sittingIdleOffset = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
                    }
                    if (pointObj.contains("sitStandUpOffset") && pointObj["sitStandUpOffset"].is_array()) {
                        auto& a = pointObj["sitStandUpOffset"];
                        profile.sitStandUpOffset = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
                    }
                    if (pointObj.contains("blendDuration"))
                        profile.sitBlendDuration = pointObj["blendDuration"].get<float>();
                    if (pointObj.contains("heightOffset"))
                        profile.seatHeightOffset = pointObj["heightOffset"].get<float>();
                    archetypeProfiles[templateName][pointId] = profile;
                }
            }
        }

        int totalPoints = 0;
        for (auto& [t, pts] : archetypeProfiles) totalPoints += static_cast<int>(pts.size());
        LOG_INFO("InteractionProfileManager", "Loaded archetype '{}': {} templates, {} points",
                 archetype, archetypeProfiles.size(), totalPoints);
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
            nlohmann::json pointObj;
            pointObj["sitDownOffset"] = {profile.sitDownOffset.x, profile.sitDownOffset.y, profile.sitDownOffset.z};
            pointObj["sittingIdleOffset"] = {profile.sittingIdleOffset.x, profile.sittingIdleOffset.y, profile.sittingIdleOffset.z};
            pointObj["sitStandUpOffset"] = {profile.sitStandUpOffset.x, profile.sitStandUpOffset.y, profile.sitStandUpOffset.z};
            pointObj["blendDuration"] = profile.sitBlendDuration;
            pointObj["heightOffset"] = profile.seatHeightOffset;
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
    m_profiles[archetype][templateName][pointId] = profile;
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
