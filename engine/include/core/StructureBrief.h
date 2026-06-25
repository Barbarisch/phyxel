#pragma once

// ============================================================================
// StructureBrief — a filled-in intake (docs/structure-generation/StructureBrief.md), engine-resident.
//
// Schema-driven: rather than rigid typed sections, the brief is a flat map of
// fieldId -> {value, source, confirmed}. This stays in lock-step with the
// data-driven StructureBriefSchema (any driver can fill any schema field without
// a code change), and carries the PROVENANCE the grounding rule requires: every
// value is "user", a citation, or unsourced (which the validator flags).
//
// Wire format: { "schema": "...", "fields": { "<id>": { "value": …, "source": "…",
// "confirmed": bool } } }.
// ============================================================================

#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

struct BriefValue {
    nlohmann::json value;       ///< the field's value (string/bool/int/array/dims)
    std::string    source;      ///< "user" | a citation | "" (unsourced)
    bool           confirmed = false;

    bool sourced() const { return !source.empty(); }
};

class StructureBrief {
public:
    void set(const std::string& fieldId, const nlohmann::json& value,
             const std::string& source, bool confirmed) {
        m_fields[fieldId] = BriefValue{value, source, confirmed};
    }
    bool has(const std::string& fieldId) const { return m_fields.count(fieldId) > 0; }
    const BriefValue* get(const std::string& fieldId) const {
        auto it = m_fields.find(fieldId);
        return it == m_fields.end() ? nullptr : &it->second;
    }
    const std::map<std::string, BriefValue>& fields() const { return m_fields; }

    std::string schema = "structure_brief/v1";

    static StructureBrief fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;

private:
    std::map<std::string, BriefValue> m_fields;
};

} // namespace Core
} // namespace Phyxel
