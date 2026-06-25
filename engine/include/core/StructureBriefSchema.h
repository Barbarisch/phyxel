#pragma once

// ============================================================================
// StructureBriefSchema — engine-resident, machine-readable field list for the
// mandatory structure-generation intake (docs/structure-generation/StructureBrief.md).
//
// This is the SINGLE SOURCE OF TRUTH for every driver of the intake — the
// in-engine wizard/CLI and the Claude `/structure` skill all read THIS schema
// (served over the API) instead of duplicating the field list. It holds field
// METADATA only (stage, blocking flag, type, branching, where a grounded default
// comes from) — NO dimension values (those live in the grounded canons).
//
// Loaded best-effort, CWD-relative (resources/structure_brief_schema.json).
// ============================================================================

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// One intake field.
struct BriefField {
    std::string id;
    std::string label;
    std::string kind = "default";   ///< "blocking" (ask directly) | "default" (propose grounded+cited)
    std::string type;               ///< string | enum | bool | int | dims | list
    std::string branch;             ///< optional condition under which the field applies
    std::string defaultSource;      ///< grounding frame/authority for a proposed default (never a number)
    std::vector<std::string> options;   ///< for enum fields

    bool isBlocking() const { return kind == "blocking"; }
};

/// A stage groups fields and may branch on earlier answers.
struct BriefStage {
    std::string id;
    std::string title;
    std::string note;
    std::string branch;
    int order = 0;
    std::vector<BriefField> fields;
};

class StructureBriefSchema {
public:
    /// Best-effort load (CWD-relative). Returns false + keeps existing on failure.
    bool loadFromFile(const std::string& path);
    bool loadFromJson(const nlohmann::json& j);

    const std::vector<BriefStage>& stages() const { return m_stages; }
    size_t stageCount() const { return m_stages.size(); }
    size_t fieldCount() const;

    /// Every blocking field across all stages (the must-ask set).
    std::vector<const BriefField*> blockingFields() const;
    /// Find a field by id anywhere in the schema (nullptr if absent).
    const BriefField* field(const std::string& id) const;

    void clear() { m_stages.clear(); }

private:
    static BriefField parseField(const nlohmann::json& j);

    std::vector<BriefStage> m_stages;
};

} // namespace Core
} // namespace Phyxel
