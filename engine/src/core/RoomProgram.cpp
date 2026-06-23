#include "core/RoomProgram.h"

#include <fstream>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

RoomProgram RoomProgramRegistry::parse(const std::string& name, const nlohmann::json& rec) {
    RoomProgram p;
    p.name = name;
    if (!rec.is_object()) return p;

    p.description = rec.value("description", "");
    p.source = rec.value("source", "");
    p.widthMin = rec.value("width_min", 0.0);
    p.widthMax = rec.value("width_max", 0.0);
    p.bayLength = rec.value("bay_length", 0.0);
    p.bays = rec.value("bays", 0);
    p.proportionMin = rec.value("proportion_min", 0.0);
    p.proportionMax = rec.value("proportion_max", 0.0);

    if (rec.contains("rooms") && rec["rooms"].is_array())
        for (const auto& r : rec["rooms"]) {
            if (!r.is_object()) continue;
            RoomSpec rs;
            rs.id = r.value("id", "");
            rs.purpose = r.value("purpose", "");
            rs.bays = r.value("bays", 1.0);
            p.rooms.push_back(rs);
        }

    if (rec.contains("sources") && rec["sources"].is_object())
        for (auto it = rec["sources"].begin(); it != rec["sources"].end(); ++it)
            if (it.value().is_string()) p.sources[it.key()] = it.value().get<std::string>();

    return p;
}

bool RoomProgramRegistry::loadFromJson(const nlohmann::json& j) {
    if (!j.is_object()) return false;
    const nlohmann::json* table = &j;
    if (j.contains("programs") && j["programs"].is_object()) table = &j["programs"];

    size_t loaded = 0;
    for (auto it = table->begin(); it != table->end(); ++it) {
        const std::string& key = it.key();
        if (!key.empty() && key[0] == '_') continue;
        if (!it.value().is_object()) continue;
        m_programs[key] = parse(key, it.value());
        ++loaded;
    }
    return loaded > 0;
}

std::string RoomProgramRegistry::defaultTypologyForFunction(const std::string& function) {
    // Coarse default ONLY (overridden by an explicit BuildingProgram.typology). The general
    // medieval dwelling is the hall house; small/rural and grand cases should declare their
    // typology so the brief-driven, status-aware path can pick croft/longhouse/manor_hall.
    if (function == "house" || function == "tavern" || function == "shop") return "hall_house";
    if (function == "cottage")                                            return "croft";
    if (function == "farmhouse" || function == "longhouse")               return "longhouse";
    if (function == "manor" || function == "hall")                        return "manor_hall";
    return "";  // church / tower / etc — no dwelling typology; skip the room gate
}

bool RoomProgramRegistry::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN_FMT("RoomProgram", "room_program not found at " << path);
        return false;
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        LOG_WARN_FMT("RoomProgram", "parse error in " << path << ": " << e.what());
        return false;
    }
    bool ok = loadFromJson(j);
    // Grounding rule (warn-but-allow): flag any typology without a citation.
    size_t unsourced = 0;
    for (const auto& [name, p] : m_programs)
        if (p.source.empty() && p.sources.empty()) ++unsourced;
    if (unsourced > 0)
        LOG_WARN_FMT("RoomProgram", unsourced << " of " << m_programs.size()
                     << " room programs are UNSOURCED — must be grounded");
    LOG_INFO_FMT("RoomProgram", "Loaded " << m_programs.size() << " room programs from " << path);
    return ok;
}

} // namespace Core
} // namespace Phyxel
