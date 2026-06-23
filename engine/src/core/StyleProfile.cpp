#include "core/StyleProfile.h"

#include <fstream>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

void StyleProfileRegistry::parseNumberMap(const nlohmann::json& obj,
                                          std::map<std::string, double>& out) {
    if (!obj.is_object()) return;
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (it.value().is_number()) out[it.key()] = it.value().get<double>();
}

StyleProfile StyleProfileRegistry::parseRecord(const std::string& name,
                                               const nlohmann::json& rec) {
    StyleProfile s;
    s.name = name;
    if (!rec.is_object()) return s;

    if (rec.contains("description") && rec["description"].is_string())
        s.description = rec["description"].get<std::string>();
    if (rec.contains("source") && rec["source"].is_string())
        s.source = rec["source"].get<std::string>();
    if (rec.contains("roof_style") && rec["roof_style"].is_string())
        s.roofStyle = rec["roof_style"].get<std::string>();
    if (rec.contains("foundation") && rec["foundation"].is_string())
        s.foundation = rec["foundation"].get<std::string>();

    if (rec.contains("thickness")) parseNumberMap(rec["thickness"], s.thickness);
    if (rec.contains("trim"))      parseNumberMap(rec["trim"], s.trim);
    if (rec.contains("roof"))      parseNumberMap(rec["roof"], s.roof);
    if (rec.contains("ceiling"))   parseNumberMap(rec["ceiling"], s.ceiling);

    if (rec.contains("materials") && rec["materials"].is_object())
        for (auto it = rec["materials"].begin(); it != rec["materials"].end(); ++it)
            if (it.value().is_string()) s.materials[it.key()] = it.value().get<std::string>();

    if (rec.contains("flags") && rec["flags"].is_object())
        for (auto it = rec["flags"].begin(); it != rec["flags"].end(); ++it)
            if (it.value().is_boolean()) s.flags[it.key()] = it.value().get<bool>();

    if (rec.contains("sources") && rec["sources"].is_object())
        for (auto it = rec["sources"].begin(); it != rec["sources"].end(); ++it)
            if (it.value().is_string()) s.sources[it.key()] = it.value().get<std::string>();

    return s;
}

bool StyleProfileRegistry::loadFromJson(const nlohmann::json& j) {
    if (!j.is_object()) return false;

    const nlohmann::json* table = &j;
    if (j.contains("styles") && j["styles"].is_object()) table = &j["styles"];

    size_t loaded = 0;
    for (auto it = table->begin(); it != table->end(); ++it) {
        const std::string& key = it.key();
        if (!key.empty() && key[0] == '_') continue;
        if (!it.value().is_object()) continue;
        m_styles[key] = parseRecord(key, it.value());
        ++loaded;
    }
    return loaded > 0;
}

bool StyleProfileRegistry::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN_FMT("StyleProfile", "structure_styles not found at " << path
                     << " (keeping " << m_styles.size() << " existing styles)");
        return false;
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        LOG_WARN_FMT("StyleProfile", "structure_styles parse error in " << path
                     << ": " << e.what() << " (keeping defaults)");
        return false;
    }
    bool ok = loadFromJson(j);
    // Grounding rule (warn-but-allow): flag styles whose thicknesses/materials aren't cited.
    size_t unsourced = 0;
    for (const auto& [name, s] : m_styles)
        if (s.source.empty()) ++unsourced;
    if (unsourced > 0)
        LOG_WARN_FMT("StyleProfile", unsourced << " of " << m_styles.size()
                     << " styles are UNSOURCED (no `source`) — wall thicknesses/materials must be grounded (E2)");
    LOG_INFO_FMT("StyleProfile", "Loaded " << m_styles.size() << " styles from " << path);
    return ok;
}

} // namespace Core
} // namespace Phyxel
