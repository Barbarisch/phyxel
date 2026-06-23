#include "core/StructureBrief.h"

namespace Phyxel {
namespace Core {

StructureBrief StructureBrief::fromJson(const nlohmann::json& j) {
    StructureBrief b;
    if (!j.is_object()) return b;
    b.schema = j.value("schema", std::string("structure_brief/v1"));
    if (j.contains("fields") && j["fields"].is_object()) {
        for (auto it = j["fields"].begin(); it != j["fields"].end(); ++it) {
            const nlohmann::json& f = it.value();
            if (!f.is_object()) continue;
            BriefValue bv;
            bv.value = f.value("value", nlohmann::json());
            bv.source = f.value("source", std::string());
            bv.confirmed = f.value("confirmed", false);
            b.m_fields[it.key()] = std::move(bv);
        }
    }
    return b;
}

nlohmann::json StructureBrief::toJson() const {
    nlohmann::json fields = nlohmann::json::object();
    for (const auto& [id, bv] : m_fields)
        fields[id] = {{"value", bv.value}, {"source", bv.source}, {"confirmed", bv.confirmed}};
    return {{"schema", schema}, {"fields", fields}};
}

} // namespace Core
} // namespace Phyxel
