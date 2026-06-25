#pragma once

// ============================================================================
// AssetLibrary — the TRUSTED store for generated objects (Structure Generation
// v2; docs/structure-generation/StructureGenerationV2.md, "the trust mechanism — library status, not
// self-claim").
//
// Every asset is a record with provenance + a status. The realizer may select
// ONLY 'approved' assets, so a mediocre fence physically cannot propagate: it is
// quarantined as 'provisional' until it passes the deterministic gates + the
// comparative visual judge AND a one-time user approval (setStatus -> Approved).
// Versioning + the approved-only rule give the golden-regression guarantee: a
// re-generated asset registers as a new provisional version and does NOT replace
// the blessed one until it too is approved.
//
// (A0 builds the trusted store + gate plumbing; the A1 generation loop —
// variants -> rank -> repair -> approve — consumes this.)
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

enum class AssetStatus { Provisional, Approved, Deprecated };

const char* assetStatusName(AssetStatus s);
AssetStatus assetStatusFromString(const std::string& s);

struct AssetRecord {
    std::string id;            ///< unique record id (e.g. "chair_dining_v2")
    std::string archetype;     ///< DimensionCanon archetype this realizes
    std::string templateName;  ///< the .voxel template the realizer spawns
    int         version = 1;
    AssetStatus status = AssetStatus::Provisional;
    double      qualityScore = 0.0;   ///< comparative visual judge score (0..1)
    bool        gatesPassed = false;  ///< deterministic AssetValidator verdict
    std::string provenance;           ///< how it was generated (model/prompt/etc.)
    std::map<std::string, double> realizedDims;  ///< measured dims (cubes)

    static AssetRecord fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

class AssetLibrary {
public:
    // ----- persistence (CWD-relative, like the other registries) -----
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;
    bool loadFromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;

    // ----- registration + the approval hook -----
    /// Add or replace a record by id. New assets should be Provisional.
    void registerAsset(const AssetRecord& rec) { m_records[rec.id] = rec; }
    /// The one-time user-approval hook (and demote/retire). Returns false if id unknown.
    bool setStatus(const std::string& id, AssetStatus status);
    /// Convenience: promote a record to Approved (gates must already have passed).
    bool approve(const std::string& id);

    const AssetRecord* get(const std::string& id) const {
        auto it = m_records.find(id);
        return it == m_records.end() ? nullptr : &it->second;
    }

    // ----- the realizer's selection: APPROVED ONLY -----
    std::vector<const AssetRecord*> approvedForArchetype(const std::string& archetype) const;
    /// The asset the realizer should use for an archetype: highest approved
    /// version (tie-break by quality score), or nullptr if none is approved.
    const AssetRecord* bestApprovedForArchetype(const std::string& archetype) const;

    size_t count() const { return m_records.size(); }
    void clear() { m_records.clear(); }

private:
    std::map<std::string, AssetRecord> m_records;
};

} // namespace Core
} // namespace Phyxel
