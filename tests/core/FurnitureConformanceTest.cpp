#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <string>

#include "core/FurnitureConformance.h"
#include "core/DimensionCanon.h"

using namespace Phyxel::Core;

// ============================================================================
// Dimension-conformance tracker (Part 2): flag furniture templates whose ACTUAL
// dims don't match grounded canon, have no .metrics, or have no canon archetype —
// so we know which to regenerate.
// ============================================================================

namespace {
DimensionCanonRegistry hermeticCanon() {
    DimensionCanonRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "bed_single":      {"category":"furniture","length":1.9,"width":0.9,"tol":0.15},
        "chest":           {"category":"furniture","height":0.7,"width":1.2,"depth":0.55,"tol":0.15},
        "hearth":          {"category":"fixture","height":1.2,"width":1.5,"depth":0.6,"tol":0.15},
        "table_dining":    {"category":"furniture","depth":0.84,"tol":0.15},
        "counter_kitchen": {"category":"fixture","depth":0.6,"tol":0.15},
        "bench":           {"category":"furniture","depth":0.4,"tol":0.15}
    })"));
    return reg;
}
std::string statusOf(const FurnitureConformanceReport& r, const std::string& type) {
    for (const auto& f : r.findings) if (f.type == type) return f.status;
    return "absent";
}
} // namespace

// TEETH: a bed whose real width is wildly off canon is flagged out_of_tolerance; a canon-matching bed
// is ok. The same detector, two inputs, opposite verdicts -> the dimensional check actually measures.
TEST(FurnitureConformanceTest, FlagsOutOfToleranceNotConforming) {
    const auto canon = hermeticCanon();
    auto conforming = [](const std::string& t) -> AssetExtents {
        if (t == "bed_single") return {0.9, 1.6, 1.9, true};   // matches canon width/length
        return {1.0, 1.0, 1.0, true};
    };
    auto drifted = [](const std::string& t) -> AssetExtents {
        if (t == "bed_single") return {5.0, 1.6, 1.9, true};   // width 5.0 — way off canon 0.9
        return {1.0, 1.0, 1.0, true};
    };
    EXPECT_EQ(statusOf(checkFurnitureConformance(canon, conforming), "bed"), "ok");
    EXPECT_EQ(statusOf(checkFurnitureConformance(canon, drifted), "bed"), "out_of_tolerance");
}

// A template with no .metrics.json sidecar is flagged (the signal it needs metrics / regeneration).
TEST(FurnitureConformanceTest, FlagsMissingMetrics) {
    const auto canon = hermeticCanon();
    auto noCounterMetrics = [](const std::string& t) -> AssetExtents {
        if (t == "counter") return {0, 0, 0, false};           // counter's template has no metrics
        return {1.0, 1.0, 1.0, true};
    };
    EXPECT_EQ(statusOf(checkFurnitureConformance(canon, noCounterMetrics), "counter"), "no_metrics");
}

// no_canon has two causes, both flagged: (a) an archetype the LOADED canon lacks, (b) a type with no
// archetype mapping at all. (barrel now HAS a real archetype + canon; this hermetic canon omits it.)
TEST(FurnitureConformanceTest, FlagsNoCanonArchetype) {
    const auto canon = hermeticCanon();   // a SUBSET — has bed/chest/hearth/table/counter/bench only
    auto present = [](const std::string&) -> AssetExtents { return {1.0, 1.0, 1.0, true}; };
    // (a) barrel maps to an archetype, but it's absent from THIS canon -> no_canon.
    EXPECT_EQ(statusOf(checkFurnitureConformance(canon, present), "barrel"), "no_canon");
    // (b) a type with no archetype mapping at all -> "". (Use a sentinel that will never be a real
    // fixture type, so adding new fixtures — e.g. anvil/forge_hearth — can't silently break this.)
    EXPECT_EQ(archetypeForType("__no_such_fixture__"), "");
    EXPECT_EQ(archetypeForType("bed"), "bed_single");
}

// "ok" must mean MEASURED-and-conforms, never "nothing to check". An archetype with only FEATURE dims
// (seat_top/length_min, no height/width/depth/length) gives no overall-size key, so the verdict is
// no_checkable_dims — not a vacuous "ok". (The real `bench` canon is exactly this shape.)
TEST(FurnitureConformanceTest, FeatureOnlyArchetypeIsNotVacuouslyOk) {
    DimensionCanonRegistry canon;
    canon.loadFromJson(nlohmann::json::parse(R"({
        "bench": {"category":"furniture","seat_top":0.45,"seat_depth":0.4,"length_min":1.2,"tol":0.15}
    })"));
    auto present = [](const std::string&) -> AssetExtents { return {2.0, 1.0, 1.0, true}; };
    const auto st = statusOf(checkFurnitureConformance(canon, present), "bench");
    EXPECT_EQ(st, "no_checkable_dims") << "a feature-only archetype must not read as 'ok'";
    EXPECT_NE(st, "ok");
}

// THE TRACKER on the REAL library: load real object_dimensions.json + real .metrics.json, audit every
// furniture type, PRINT the non-conforming ones (what to regenerate), and assert the detector surfaces
// the two KNOWN gaps (barrel has no canon; counter has no metrics) so the real audit isn't vacuous.
// CWD-tolerant; skips if resources/ isn't reachable.
TEST(FurnitureConformanceTest, RealLibraryAuditReportsKnownGaps) {
    DimensionCanonRegistry canon;
    const char* canonPaths[] = {"resources/object_dimensions.json", "../resources/object_dimensions.json",
                                "../../resources/object_dimensions.json", "../../../resources/object_dimensions.json"};
    bool loaded = false;
    for (const char* p : canonPaths) if (canon.loadFromFile(p)) { loaded = true; break; }
    if (!loaded) GTEST_SKIP() << "resources/object_dimensions.json not reachable from CWD";

    auto extentsOf = [](const std::string& tmpl) -> AssetExtents {
        // Sidecars live under the CATEGORY taxonomy (docs/AssetLibrary.md, 2026-08-07);
        // the flat root is the pre-reorg layout that still exists in some build copies.
        // Search both, or a newly authored asset reads as `no_metrics` purely because
        // the audit looked in the old place.
        const char* roots[] = {"resources/templates/", "resources/templates/furniture/",
                               "resources/templates/items/", "resources/templates/architecture/",
                               "../resources/templates/", "../resources/templates/furniture/",
                               "../../resources/templates/", "../../resources/templates/furniture/",
                               "../../../resources/templates/",
                               "../../../resources/templates/furniture/"};
        for (const char* r : roots) {
            std::ifstream in(std::string(r) + tmpl + ".metrics.json");
            if (!in.good()) continue;
            try {
                auto j = nlohmann::json::parse(in);
                const auto& mn = j.at("overall_min");
                const auto& mx = j.at("overall_max");
                AssetExtents e;
                e.width  = mx[0].get<double>() - mn[0].get<double>();
                e.height = mx[1].get<double>() - mn[1].get<double>();
                e.depth  = mx[2].get<double>() - mn[2].get<double>();
                e.present = true;
                return e;
            } catch (...) { return {}; }
        }
        return {};   // present == false
    };

    const auto rep = checkFurnitureConformance(canon, extentsOf);
    std::cout << "--- furniture conformance audit (" << rep.nonConforming().size()
              << " non-conforming of " << rep.findings.size() << ") ---\n";
    for (const auto& f : rep.findings)
        std::cout << "  " << f.type << " (" << f.templateName << ") [" << f.archetype << "]: "
                  << f.status << (f.detail.empty() ? "" : " — " + f.detail) << "\n";

    // Teeth + REGRESSION GUARD: pin the FULL current conformance verdict, so the regenerate-list can't
    // silently drift — fixing an asset (or a new one drifting) flips a status and fails this test,
    // prompting an update here + in AssetConformance.md.
    EXPECT_EQ(statusOf(rep, "bed"),       "ok");                 // bed_single conforms
    EXPECT_EQ(statusOf(rep, "chest"),     "ok");                 // REGENERATED -> coffer 1.22x0.56x0.67
    EXPECT_EQ(statusOf(rep, "fireplace"), "ok");                 // REGENERATED -> hearth 1.56x1.22x0.56
    // Un-grounded furniture REGENERATED to canon (deterministic micro builds):
    EXPECT_EQ(statusOf(rep, "barrel"),    "ok");                 // cask 0.89h x 0.56dia vs canon 0.88/0.56
    EXPECT_EQ(statusOf(rep, "counter"),   "ok");                 // worktop 0.89h x 0.56d vs canon 0.9/0.6
    EXPECT_EQ(statusOf(rep, "bench"),     "ok");                 // bench 0.44h x 0.44d vs canon 0.45/0.4
    EXPECT_EQ(statusOf(rep, "table"),     "ok");                 // table 0.78h x 0.89d vs canon 0.75/0.84
    EXPECT_EQ(statusOf(rep, "tavern_table"), "ok");              // long table 0.78h x 0.78d vs canon 0.75/0.8
    // Inn asset depth (bar + stools): deterministic microcube builds grounded to object_dimensions.
    EXPECT_EQ(statusOf(rep, "tavern_bar"), "ok");                // counter 1.11h x 0.67d vs canon 1.07/0.6
    EXPECT_EQ(statusOf(rep, "bar_stool"),  "ok");                // 0.78h x 0.44 vs canon 0.78/0.44
    EXPECT_EQ(statusOf(rep, "back_bar"),   "ok");                // shelving 1.89h x 0.33d vs canon 1.85/0.35
    // Lighting fixtures (emissive glow): deterministic micro builds grounded to object_dimensions.
    EXPECT_EQ(statusOf(rep, "candle_stand"), "ok");              // 1.33h x 0.33 vs canon 1.3/0.33
    EXPECT_EQ(statusOf(rep, "wall_lantern"), "ok");              // 0.44h x 0.33 x 0.22 vs canon 0.4/0.33/0.22
    EXPECT_EQ(statusOf(rep, "chandelier"),   "ok");              // 0.67h x 0.56dia vs canon 0.6/0.6
    // Tableware clutter (mugs + bottles): microcube-floor props grounded to object_dimensions.
    EXPECT_EQ(statusOf(rep, "mug"),    "ok");                    // 0.11 cube vs canon 0.13/0.095
    EXPECT_EQ(statusOf(rep, "bottle"), "ok");                    // 0.11x0.33 vs canon 0.08/0.30
    // Smithy fixtures: deterministic microcube builds grounded to object_dimensions.
    EXPECT_EQ(statusOf(rep, "forge_hearth"), "ok");             // 1.0w x 0.78d vs canon 1.0/0.8 (work_top 0.8)
    EXPECT_EQ(statusOf(rep, "anvil"),        "ok");             // 0.22w x 0.56len vs canon 0.25/0.55
    EXPECT_EQ(statusOf(rep, "bellows"),      "ok");             // 0.33w x 0.33h x 1.56len vs canon 0.3/0.35/1.5
    EXPECT_EQ(statusOf(rep, "tool_rack"),    "ok");             // 1.0w x 0.22d vs canon 1.0/0.15
}
