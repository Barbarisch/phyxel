#pragma once

// ============================================================================
// AssetRequestLedger — the pipeline's DEMAND SIDE, made explicit and tracked.
//
// Standing rule (user decision, 2026-08-07): the generator NEVER invents or
// substitutes an asset. When a build needs a fixture type the engine cannot
// supply (no catalog mapping, or the mapped template isn't loadable), it does
// NOT quietly place nothing and it does NOT hand-assemble a lookalike — it
// records a structured ASSET REQUEST and REFUSES the build. The request is then
// burned down through the normal authoring discipline (archetype sheet ->
// generator -> conformance audit), after which the same build succeeds.
//
// The ledger file (resources/asset_requests.json) is committed and merged
// DETERMINISTICALLY: entries are keyed by type, requesters are a sorted unique
// set, and no timestamps enter the dedup key — so re-running a build never
// churns the file. `first_seen` is carried over from the existing entry.
//
// Consequence: recipe/vocabulary growth is ASSETS-FIRST. A new recipe entry
// ships in the same change as its authored asset, never before. Demand is
// discovered ahead of time by tools/asset_requests.py --scan, not by shipping
// refusals to players.
// ============================================================================

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// One unsatisfied demand for an engine asset.
struct AssetRequest {
    std::string type;       ///< fixture/item type the generator wanted, e.g. "stove"
    std::string category;   ///< "furniture" | "item" | ... (authoring pipeline hint)
    std::string purpose;    ///< room purpose that needs it, e.g. "kitchen"
    std::string typology;   ///< requesting typology when known, e.g. "bakery"
    std::string reason;     ///< "unmapped" (no catalog entry) | "template_missing"
    std::string message;    ///< human-readable one-liner

    /// Dedup key: one ledger entry per TYPE (requesters accumulate under it).
    const std::string& key() const { return type; }
};

class AssetRequestLedger {
public:
    /// Requests as the API returns them (array, sorted by type — deterministic).
    static nlohmann::json toJson(const std::vector<AssetRequest>& requests);

    /// Merge `requests` into the ledger json (creating entries, unioning
    /// requesters, preserving `status` and `first_seen` of existing entries).
    /// Pure: takes and returns the document, so it is unit-testable without IO.
    /// `today` is the date stamped on NEW entries only (never on existing ones,
    /// so a re-run cannot churn the file).
    static nlohmann::json merge(const nlohmann::json& ledger,
                                const std::vector<AssetRequest>& requests,
                                const std::string& today);

    /// Read/write the ledger file. Missing/unparseable file reads as an empty
    /// ledger (the ledger is a record, never a build dependency).
    static nlohmann::json load(const std::string& path = "resources/asset_requests.json");
    static bool save(const nlohmann::json& ledger,
                     const std::string& path = "resources/asset_requests.json");

    /// Entries whose status is not "conformant" — the burn-down list.
    static std::vector<std::string> openTypes(const nlohmann::json& ledger);
};

} // namespace Core
} // namespace Phyxel
