#include "core/AssetLibrary.h"

#include <algorithm>
#include <fstream>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

const char* assetStatusName(AssetStatus s) {
    switch (s) {
        case AssetStatus::Approved:    return "approved";
        case AssetStatus::Deprecated:  return "deprecated";
        case AssetStatus::Provisional: default: return "provisional";
    }
}

AssetStatus assetStatusFromString(const std::string& s) {
    if (s == "approved")   return AssetStatus::Approved;
    if (s == "deprecated") return AssetStatus::Deprecated;
    return AssetStatus::Provisional;
}

AssetRecord AssetRecord::fromJson(const nlohmann::json& j) {
    AssetRecord r;
    r.id = j.value("id", "");
    r.archetype = j.value("archetype", "");
    r.templateName = j.value("template", "");
    r.version = j.value("version", 1);
    r.status = assetStatusFromString(j.value("status", "provisional"));
    r.qualityScore = j.value("quality_score", 0.0);
    r.gatesPassed = j.value("gates_passed", false);
    r.provenance = j.value("provenance", "");
    if (j.contains("realized_dims") && j["realized_dims"].is_object())
        for (auto it = j["realized_dims"].begin(); it != j["realized_dims"].end(); ++it)
            if (it.value().is_number()) r.realizedDims[it.key()] = it.value().get<double>();
    return r;
}

nlohmann::json AssetRecord::toJson() const {
    nlohmann::json dims = nlohmann::json::object();
    for (const auto& [k, v] : realizedDims) dims[k] = v;
    return {{"id", id}, {"archetype", archetype}, {"template", templateName},
            {"version", version}, {"status", assetStatusName(status)},
            {"quality_score", qualityScore}, {"gates_passed", gatesPassed},
            {"provenance", provenance}, {"realized_dims", dims}};
}

bool AssetLibrary::setStatus(const std::string& id, AssetStatus status) {
    auto it = m_records.find(id);
    if (it == m_records.end()) return false;
    it->second.status = status;
    return true;
}

bool AssetLibrary::approve(const std::string& id) {
    return setStatus(id, AssetStatus::Approved);
}

std::vector<const AssetRecord*> AssetLibrary::approvedForArchetype(
    const std::string& archetype) const {
    std::vector<const AssetRecord*> out;
    for (const auto& [id, rec] : m_records)
        if (rec.status == AssetStatus::Approved && rec.archetype == archetype)
            out.push_back(&rec);
    return out;
}

const AssetRecord* AssetLibrary::bestApprovedForArchetype(const std::string& archetype) const {
    const AssetRecord* best = nullptr;
    for (const auto& [id, rec] : m_records) {
        if (rec.status != AssetStatus::Approved || rec.archetype != archetype) continue;
        if (!best || rec.version > best->version ||
            (rec.version == best->version && rec.qualityScore > best->qualityScore))
            best = &rec;
    }
    return best;
}

bool AssetLibrary::loadFromJson(const nlohmann::json& j) {
    const nlohmann::json* arr = nullptr;
    if (j.is_array()) arr = &j;
    else if (j.contains("assets") && j["assets"].is_array()) arr = &j["assets"];
    if (!arr) return false;
    for (const auto& e : *arr) {
        AssetRecord rec = AssetRecord::fromJson(e);
        if (!rec.id.empty()) m_records[rec.id] = rec;
    }
    return true;
}

nlohmann::json AssetLibrary::toJson() const {
    nlohmann::json assets = nlohmann::json::array();
    for (const auto& [id, rec] : m_records) assets.push_back(rec.toJson());
    return {{"version", 1}, {"assets", assets}};
}

bool AssetLibrary::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN_FMT("AssetLibrary", "asset_library not found at " << path
                     << " (keeping " << m_records.size() << " records)");
        return false;
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        LOG_WARN_FMT("AssetLibrary", "asset_library parse error in " << path
                     << ": " << e.what());
        return false;
    }
    bool ok = loadFromJson(j);
    LOG_INFO_FMT("AssetLibrary", "Loaded " << m_records.size() << " asset records from " << path);
    return ok;
}

bool AssetLibrary::saveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR_FMT("AssetLibrary", "cannot write asset_library to " << path);
        return false;
    }
    file << toJson().dump(2) << "\n";
    return true;
}

} // namespace Core
} // namespace Phyxel
