#include "core/DimensionCanon.h"

#include <fstream>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

ArchetypeDims DimensionCanonRegistry::parseRecord(const std::string& name,
                                                  const nlohmann::json& rec) {
    ArchetypeDims a;
    a.name = name;
    if (!rec.is_object()) return a;

    for (auto it = rec.begin(); it != rec.end(); ++it) {
        const std::string& key = it.key();
        const nlohmann::json& v = it.value();

        if (key == "tol" || key == "tolerance") {
            if (v.is_number()) a.tolerance = v.get<double>();
        } else if (key == "category") {
            if (v.is_string()) a.category = v.get<std::string>();
        } else if (key == "source") {
            if (v.is_string()) a.source = v.get<std::string>();
        } else if (key == "description" || key == "_comment") {
            if (v.is_string()) a.description = v.get<std::string>();
        } else if (key == "anchors") {
            if (v.is_array())
                for (const auto& e : v)
                    if (e.is_string()) a.anchors.push_back(e.get<std::string>());
        } else if (key == "sources") {
            if (v.is_object())
                for (auto sit = v.begin(); sit != v.end(); ++sit)
                    if (sit.value().is_string()) a.valueSources[sit.key()] = sit.value().get<std::string>();
        } else if (v.is_boolean()) {
            a.flags[key] = v.get<bool>();
        } else if (v.is_number()) {
            a.values[key] = v.get<double>();
        }
        // strings other than the recognized ones are ignored (free-form notes)
    }
    return a;
}

bool DimensionCanonRegistry::loadFromJson(const nlohmann::json& j) {
    if (!j.is_object()) return false;

    // Accept {"archetypes": {...}} or a flat archetype map.
    const nlohmann::json* table = &j;
    if (j.contains("archetypes") && j["archetypes"].is_object()) {
        table = &j["archetypes"];
    }

    size_t loaded = 0;
    for (auto it = table->begin(); it != table->end(); ++it) {
        const std::string& key = it.key();
        if (!key.empty() && key[0] == '_') continue;   // _comment / metadata
        if (!it.value().is_object()) continue;
        m_archetypes[key] = parseRecord(key, it.value());
        ++loaded;
    }
    return loaded > 0;
}

bool DimensionCanonRegistry::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN_FMT("DimensionCanon", "object_dimensions not found at " << path
                     << " (keeping " << m_archetypes.size() << " existing archetypes)");
        return false;
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        LOG_WARN_FMT("DimensionCanon", "object_dimensions parse error in " << path
                     << ": " << e.what() << " (keeping defaults)");
        return false;
    }
    bool ok = loadFromJson(j);
    // Grounding rule (warn-but-allow): every archetype must cite a source. Flag the unsourced
    // ones loudly so the standing debt is visible — but still load them.
    size_t unsourced = 0;
    for (const auto& [name, a] : m_archetypes)
        if (a.source.empty()) ++unsourced;
    if (unsourced > 0)
        LOG_WARN_FMT("DimensionCanon", unsourced << " of " << m_archetypes.size()
                     << " archetypes are UNSOURCED (no `source`) — must be grounded (E2)");
    LOG_INFO_FMT("DimensionCanon", "Loaded " << m_archetypes.size()
                 << " object archetypes from " << path);
    return ok;
}

} // namespace Core
} // namespace Phyxel
