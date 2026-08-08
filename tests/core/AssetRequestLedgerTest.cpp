#include <gtest/gtest.h>

#include <fstream>

#include "core/AssetRequestLedger.h"
#include "core/FurnitureCatalog.h"
#include "core/FurniturePlacer.h"

// ============================================================================
// M3.5 asset-request ledger — the pipeline's demand side.
//
// Standing rule: the generator NEVER invents or substitutes an asset. A build
// that needs a fixture type the engine cannot supply records a structured
// request and REFUSES. RED before this milestone: a recipe naming a nonexistent
// type produced nothing durable — a transient response field at best — and the
// build shipped a room quietly missing that piece.
//
// The determinism clause matters as much as the gate: the ledger is a COMMITTED
// file, so a re-run that reorders or re-stamps it would churn the repo and make
// review meaningless.
// ============================================================================

using namespace Phyxel::Core;

namespace {

bool loadShippedRecipes() {
    for (const char* p : {"resources/furnishing_recipes.json",
                          "../resources/furnishing_recipes.json",
                          "../../resources/furnishing_recipes.json",
                          "../../../resources/furnishing_recipes.json"})
        if (FurniturePlacer::loadRecipesFromFile(p)) return true;
    return false;
}

AssetRequest req(const std::string& type, const std::string& purpose,
                 const std::string& typology = "bakery") {
    return {type, "furniture", purpose, typology, "unmapped",
            purpose + " requires a '" + type + "' but no template is mapped for it"};
}

}  // namespace

TEST(AssetRequestLedger, MergeIsDeterministicAndDoesNotChurn) {
    const nlohmann::json empty = {{"requests", nlohmann::json::array()}};
    const std::vector<AssetRequest> reqs = {req("stove", "kitchen"), req("icebox", "kitchen"),
                                            req("stove", "bakehouse")};

    const nlohmann::json once = AssetRequestLedger::merge(empty, reqs, "2026-08-08");
    // Same input, DIFFERENT order -> byte-identical document (sorted by type,
    // requesters sorted, no insertion-order or timestamp leakage).
    const std::vector<AssetRequest> shuffled = {reqs[2], reqs[0], reqs[1]};
    const nlohmann::json again = AssetRequestLedger::merge(empty, shuffled, "2026-08-08");
    EXPECT_EQ(once.dump(2), again.dump(2)) << "ledger merge is order-sensitive — it would churn";

    // Re-merging the SAME requests on a LATER day must not change the file:
    // first_seen is preserved and requesters are a set, not an append log.
    const nlohmann::json reRun = AssetRequestLedger::merge(once, reqs, "2026-09-01");
    EXPECT_EQ(once.dump(2), reRun.dump(2)) << "a re-run churned the committed ledger";

    ASSERT_EQ(once["requests"].size(), 2u);              // stove + icebox
    EXPECT_EQ(once["requests"][0]["type"], "icebox");    // sorted by type
    EXPECT_EQ(once["requests"][1]["type"], "stove");
    EXPECT_EQ(once["requests"][1]["requested_by"].size(), 2u);   // kitchen + bakehouse
    EXPECT_EQ(once["requests"][1]["first_seen"], "2026-08-08");
}

TEST(AssetRequestLedger, AuthoredEntriesAreNotReopenedByANewRequest) {
    nlohmann::json ledger = AssetRequestLedger::merge(
        {{"requests", nlohmann::json::array()}}, {req("stove", "kitchen")}, "2026-08-08");
    ledger["requests"][0]["status"] = "conformant";   // the asset got authored

    const nlohmann::json after =
        AssetRequestLedger::merge(ledger, {req("stove", "kitchen")}, "2026-09-01");
    EXPECT_EQ(after["requests"][0]["status"], "conformant")
        << "a stale request re-opened an already-authored asset";
    EXPECT_TRUE(AssetRequestLedger::openTypes(after).empty());
}

// The SCOPED coverage check the forge's asset gate refuses on: a building is
// blocked by ITS OWN missing assets, never by an unrelated typology's.
TEST(AssetGate, ScopedCoverageOnlyReportsThisBuildingsPurposes) {
    ASSERT_TRUE(loadShippedRecipes());
    // Nothing is loadable => every mapped template counts as missing, so the
    // gaps reported are exactly the demand of the purposes we ask about.
    auto nothingLoaded = [](const std::string&) { return false; };

    auto tap = validateFurnitureCoverageFor({"taproom"}, nothingLoaded);
    auto forge = validateFurnitureCoverageFor({"forge"}, nothingLoaded);
    ASSERT_FALSE(tap.ok());
    ASSERT_FALSE(forge.ok());
    for (const auto& g : tap.gaps)
        EXPECT_EQ(g.purpose, "taproom") << "taproom scope leaked a foreign purpose: " << g.message;
    for (const auto& g : forge.gaps)
        EXPECT_EQ(g.purpose, "forge") << "forge scope leaked a foreign purpose: " << g.message;
    // A smithy's anvil must NOT appear in a tavern's gap list (the absurd-refusal case).
    for (const auto& g : tap.gaps) EXPECT_NE(g.type, "anvil");
}

// The standing invariant that keeps main out of a mass-refusing state: every
// purpose the shipped recipes can emit resolves to a MAPPED template. (Asset
// existence on disk is the conformance audit's job; this pins the mapping.)
TEST(AssetGate, ShippedRecipesHaveZeroUnmappedTypes) {
    ASSERT_TRUE(loadShippedRecipes());
    auto rep = validateFurnitureCoverage();   // no templateExists => mapping only
    if (!rep.ok()) {
        std::string msg;
        for (const auto& g : rep.gaps) msg += "\n  " + g.message;
        FAIL() << "shipped recipes reference " << rep.gaps.size()
               << " unmapped type(s) — every build using them would REFUSE:" << msg;
    }
}
