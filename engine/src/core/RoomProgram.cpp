#include "core/RoomProgram.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

// ---- M6 access classes -----------------------------------------------------
// GROUNDED in the vernacular plan types the room programs already cite: the
// medieval SCREENS PASSAGE is the canonical circulation element (a cross-passage
// separating hall from service), and the galleried inn (The New Inn, Gloucester)
// is the canonical upper-floor gallery serving chambers off it. Private = the
// rooms a person sleeps in; service = the working rooms.
const char* accessClassName(AccessClass a) {
    switch (a) {
        case AccessClass::Circulation: return "circulation";
        case AccessClass::Private:     return "private";
        case AccessClass::Service:     return "service";
        default:                       return "public";
    }
}

AccessClass accessClassFor(const std::string& purpose) {
    std::string p = purpose;
    std::transform(p.begin(), p.end(), p.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto has = [&](const char* k) { return p.find(k) != std::string::npos; };

    // Circulation FIRST: "screens passage" contains neither a room noun nor a
    // private one, and a "landing" must not fall through to the default.
    if (has("passage") || has("landing") || has("gallery") || has("screens") ||
        has("corridor") || has("hallway") || has("stair") || has("lobby") || has("vestibule"))
        return AccessClass::Circulation;
    // Private: sleeping rooms. NB "solar" is the medieval private upper chamber.
    if (has("bedchamber") || has("bedroom") || has("chamber") || has("solar") ||
        has("garderobe") || has("privy") || has("closet"))
        return AccessClass::Private;
    // Service / working rooms.
    if (has("kitchen") || has("byre") || has("service") || has("store") || has("pantry") ||
        has("buttery") || has("bakehouse") || has("forge") || has("shambles") ||
        has("dispensary") || has("cellar") || has("larder") || has("scullery") ||
        has("stable") || has("workshop"))
        return AccessClass::Service;
    return AccessClass::Public;   // hall, taproom, salesroom, living, great chamber...
}

RoomProgram RoomProgramRegistry::parse(const std::string& name, const nlohmann::json& rec) {
    RoomProgram p;
    p.name = name;
    if (!rec.is_object()) return p;

    p.description = rec.value("description", "");
    p.source = rec.value("source", "");
    p.period = rec.value("period", std::string("medieval"));   // M8 period axis
    p.widthMin = rec.value("width_min", 0.0);
    p.widthMax = rec.value("width_max", 0.0);
    p.bayLength = rec.value("bay_length", 0.0);
    p.bays = rec.value("bays", 0);
    p.proportionMin = rec.value("proportion_min", 0.0);
    p.proportionMax = rec.value("proportion_max", 0.0);
    p.stories = std::max(1, rec.value("stories", 1));
    p.upperPurpose = rec.value("upper_purpose", "");
    p.wealthTier = rec.value("wealth_tier", std::string("humble"));
    p.entrance = rec.value("entrance", "");
    p.entranceOpposed = rec.value("entrance_opposed", false);
    p.signItem = rec.value("sign_item", "");   // trade sign asset (item id), "" = none authored
    if (rec.contains("entrance_between") && rec["entrance_between"].is_array())
        for (const auto& r : rec["entrance_between"])
            if (r.is_string()) p.entranceBetween.push_back(r.get<std::string>());

    if (rec.contains("windows") && rec["windows"].is_object()) {
        const auto& w = rec["windows"];
        p.windows.width  = w.value("width", 0);
        p.windows.height = w.value("height", 0);
        p.windows.perBay = w.value("per_bay", 0.0);
        p.windows.walls  = w.value("walls", std::string("long"));
        p.windows.infill = w.value("infill", std::string("shuttered"));
    }

    if (rec.contains("rooms") && rec["rooms"].is_array())
        for (const auto& r : rec["rooms"]) {
            if (!r.is_object()) continue;
            RoomSpec rs;
            rs.id = r.value("id", "");
            rs.purpose = r.value("purpose", "");
            rs.bays = r.value("bays", 1.0);
            // M8: absent => REQUIRED. Every room a typology declares today is there
            // on purpose, so silence must not quietly demote existing content.
            rs.required = r.value("required", true);
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
    if (function == "tavern" || function == "inn")                        return "tavern";
    if (function == "shop" || function == "store" || function == "general_store") return "general_store";
    if (function == "smithy" || function == "forge" || function == "blacksmith")  return "blacksmith";
    if (function == "bakery" || function == "bakehouse" || function == "baker")    return "bakery";
    if (function == "apothecary" || function == "herbalist")                       return "apothecary";
    if (function == "butcher" || function == "shambles")                           return "butcher";
    if (function == "house")                                              return "hall_house";
    if (function == "cottage")                                            return "croft";
    if (function == "farmhouse" || function == "longhouse")               return "longhouse";
    if (function == "manor" || function == "hall")                        return "manor_hall";
    return "";  // church / tower / etc — no dwelling typology; skip the room gate
}

bool RoomProgramRegistry::loadPeriodPack(const std::string& path, const std::string& period) {
    std::ifstream file(path);
    if (!file.is_open()) return false;          // an absent era pack is not an error
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        LOG_WARN_FMT("RoomProgram", "period pack parse error in " << path << ": " << e.what());
        return false;
    }
    const nlohmann::json* table = &j;
    if (j.is_object() && j.contains("programs") && j["programs"].is_object()) table = &j["programs"];
    if (!table->is_object()) return false;
    // The pack's own "period" wins if it declares one; otherwise the caller's label.
    const std::string packPeriod =
        (j.is_object() ? j.value("period", period) : period);
    size_t loaded = 0;
    for (auto it = table->begin(); it != table->end(); ++it) {
        const std::string& key = it.key();
        if (!key.empty() && key[0] == '_') continue;
        if (!it.value().is_object()) continue;
        RoomProgram p = parse(key, it.value());
        if (!it.value().contains("period")) p.period = packPeriod;
        // Programs are keyed by name; two eras sharing a typology name would collide,
        // so the era-qualified key keeps them distinct while `get(name)` still finds
        // the medieval one by its plain name (the default era).
        m_programs[p.period == "medieval" ? key : (p.period + ":" + key)] = p;
        ++loaded;
    }
    LOG_INFO_FMT("RoomProgram", "Loaded " << loaded << " '" << packPeriod
                 << "' room programs from " << path);
    return loaded > 0;
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
