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

// A type with no grounded archetype (barrel isn't in object_dimensions) can't be measured -> no_canon.
TEST(FurnitureConformanceTest, FlagsNoCanonArchetype) {
    const auto canon = hermeticCanon();
    auto present = [](const std::string&) -> AssetExtents { return {1.0, 1.0, 1.0, true}; };
    EXPECT_EQ(statusOf(checkFurnitureConformance(canon, present), "barrel"), "no_canon");
    EXPECT_EQ(archetypeForType("barrel"), "");
    EXPECT_EQ(archetypeForType("bed"), "bed_single");
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
        const char* roots[] = {"resources/templates/", "../resources/templates/",
                               "../../resources/templates/", "../../../resources/templates/"};
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

    // Teeth: the known gaps must be surfaced (proves the real audit measures, not passes blindly).
    EXPECT_EQ(statusOf(rep, "barrel"), "no_canon")   << "barrel has no object_dimensions archetype";
    EXPECT_EQ(statusOf(rep, "counter"), "no_metrics") << "counter template has no .metrics.json";
}
