#include "core/AssetRequestLedger.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace Phyxel {
namespace Core {

namespace {

// A requester tuple, rendered as one object; the SET of them is what makes an
// entry's provenance auditable ("who wanted a stove?").
nlohmann::json requesterOf(const AssetRequest& r) {
    return {{"typology", r.typology}, {"purpose", r.purpose}, {"reason", r.reason}};
}

// Deterministic ordering for requester objects (no timestamps, no insertion order).
std::string requesterKey(const nlohmann::json& j) {
    return j.value("typology", std::string()) + "\x1f" + j.value("purpose", std::string()) +
           "\x1f" + j.value("reason", std::string());
}

} // namespace

nlohmann::json AssetRequestLedger::toJson(const std::vector<AssetRequest>& requests) {
    std::vector<AssetRequest> sorted = requests;
    std::sort(sorted.begin(), sorted.end(), [](const AssetRequest& a, const AssetRequest& b) {
        if (a.type != b.type) return a.type < b.type;
        if (a.typology != b.typology) return a.typology < b.typology;
        return a.purpose < b.purpose;
    });
    nlohmann::json out = nlohmann::json::array();
    for (const auto& r : sorted)
        out.push_back({{"type", r.type}, {"category", r.category}, {"purpose", r.purpose},
                       {"typology", r.typology}, {"reason", r.reason}, {"message", r.message}});
    return out;
}

nlohmann::json AssetRequestLedger::merge(const nlohmann::json& ledger,
                                         const std::vector<AssetRequest>& requests,
                                         const std::string& today) {
    // type -> entry, in a std::map so the written file is sorted by type.
    std::map<std::string, nlohmann::json> byType;
    if (ledger.is_object() && ledger.contains("requests") && ledger["requests"].is_array())
        for (const auto& e : ledger["requests"]) {
            const std::string t = e.value("type", std::string());
            if (!t.empty()) byType[t] = e;
        }

    for (const auto& r : requests) {
        if (r.type.empty()) continue;
        auto it = byType.find(r.type);
        if (it == byType.end()) {
            byType[r.type] = {{"type", r.type}, {"category", r.category},
                              {"dims", "NEEDS-RESEARCH"}, {"status", "open"},
                              {"first_seen", today},
                              {"requested_by", nlohmann::json::array({requesterOf(r)})}};
            continue;
        }
        // Existing entry: union the requester, never touch status/first_seen (a
        // re-run must not churn the file or reopen an authored asset).
        nlohmann::json& e = it->second;
        if (!e.contains("requested_by") || !e["requested_by"].is_array())
            e["requested_by"] = nlohmann::json::array();
        std::set<std::string> seen;
        for (const auto& q : e["requested_by"]) seen.insert(requesterKey(q));
        const nlohmann::json add = requesterOf(r);
        if (seen.insert(requesterKey(add)).second) e["requested_by"].push_back(add);
    }

    // Sort each entry's requesters so the document is byte-stable.
    nlohmann::json out = nlohmann::json::array();
    for (auto& [type, entry] : byType) {
        if (entry.contains("requested_by") && entry["requested_by"].is_array()) {
            std::vector<nlohmann::json> rs(entry["requested_by"].begin(),
                                           entry["requested_by"].end());
            std::sort(rs.begin(), rs.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
                return requesterKey(a) < requesterKey(b);
            });
            entry["requested_by"] = rs;
        }
        out.push_back(entry);
    }
    return nlohmann::json{{"requests", out}};
}

nlohmann::json AssetRequestLedger::load(const std::string& path) {
    try {
        std::ifstream in(path);
        if (!in.good()) return nlohmann::json{{"requests", nlohmann::json::array()}};
        nlohmann::json j;
        in >> j;
        if (!j.is_object() || !j.contains("requests"))
            return nlohmann::json{{"requests", nlohmann::json::array()}};
        return j;
    } catch (...) {
        return nlohmann::json{{"requests", nlohmann::json::array()}};
    }
}

bool AssetRequestLedger::save(const nlohmann::json& ledger, const std::string& path) {
    try {
        std::ofstream out(path);
        if (!out.good()) return false;
        out << ledger.dump(2) << "\n";
        return out.good();
    } catch (...) {
        return false;
    }
}

std::vector<std::string> AssetRequestLedger::openTypes(const nlohmann::json& ledger) {
    std::vector<std::string> out;
    if (!ledger.is_object() || !ledger.contains("requests") || !ledger["requests"].is_array())
        return out;
    for (const auto& e : ledger["requests"])
        if (e.value("status", std::string("open")) != "conformant")
            out.push_back(e.value("type", std::string()));
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace Core
} // namespace Phyxel
