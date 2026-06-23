#include "core/StructureBriefSchema.h"

#include <fstream>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

BriefField StructureBriefSchema::parseField(const nlohmann::json& j) {
    BriefField f;
    f.id = j.value("id", "");
    f.label = j.value("label", "");
    f.kind = j.value("kind", "default");
    f.type = j.value("type", "string");
    f.branch = j.value("branch", "");
    f.defaultSource = j.value("default_source", "");
    if (j.contains("options") && j["options"].is_array())
        for (const auto& o : j["options"])
            if (o.is_string()) f.options.push_back(o.get<std::string>());
    return f;
}

bool StructureBriefSchema::loadFromJson(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("stages") || !j["stages"].is_array()) return false;
    m_stages.clear();
    for (const auto& s : j["stages"]) {
        if (!s.is_object()) continue;
        BriefStage stage;
        stage.id = s.value("id", "");
        stage.title = s.value("title", "");
        stage.note = s.value("note", "");
        stage.branch = s.value("branch", "");
        stage.order = s.value("order", 0);
        if (s.contains("fields") && s["fields"].is_array())
            for (const auto& f : s["fields"]) stage.fields.push_back(parseField(f));
        m_stages.push_back(std::move(stage));
    }
    return !m_stages.empty();
}

bool StructureBriefSchema::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN_FMT("StructureBriefSchema", "schema not found at " << path);
        return false;
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        LOG_WARN_FMT("StructureBriefSchema", "parse error in " << path << ": " << e.what());
        return false;
    }
    bool ok = loadFromJson(j);
    LOG_INFO_FMT("StructureBriefSchema", "Loaded " << m_stages.size() << " stages, "
                 << fieldCount() << " fields from " << path);
    return ok;
}

size_t StructureBriefSchema::fieldCount() const {
    size_t n = 0;
    for (const auto& s : m_stages) n += s.fields.size();
    return n;
}

std::vector<const BriefField*> StructureBriefSchema::blockingFields() const {
    std::vector<const BriefField*> out;
    for (const auto& s : m_stages)
        for (const auto& f : s.fields)
            if (f.isBlocking()) out.push_back(&f);
    return out;
}

const BriefField* StructureBriefSchema::field(const std::string& id) const {
    for (const auto& s : m_stages)
        for (const auto& f : s.fields)
            if (f.id == id) return &f;
    return nullptr;
}

} // namespace Core
} // namespace Phyxel
