#include <gtest/gtest.h>

#include "core/StructureForge.h"

// ============================================================================
// M1 StructureForge stage protocol. RED against the pre-restage monolith:
// buildV2 never emitted a "gates" field, and there was no stage-order contract
// to assert at all. Green = the forge drives the named stages in order and
// every response (including refusals) carries the per-stage verdict trail.
//
// Deps-less on purpose (like SettlementBuildServiceTest): with no ChunkManager
// the pipeline must refuse AT INTAKE — first gate refused, nothing after it
// executed. The full Proceeded sequence on a live world is L4-verified via
// POST /api/structure/build (response.gates).
// ============================================================================

using namespace Phyxel::Core;

TEST(ForgeStageOrder, CanonicalStageSequence) {
    const std::vector<std::string> expected = {
        "intake", "floorplan", "validate_program", "footprint", "realize",
        "validate_realized", "place", "furnish", "emit"};
    EXPECT_EQ(StructureForge::stageNames(), expected)
        << "the forge stage order is a public contract (response[\"gates\"] order; "
           "docs/structure-generation stage mapping) - update BOTH if this is intentional";
}

TEST(ForgeStageOrder, DepslessBuildRefusesAtIntakeWithGateTrail) {
    // No ChunkManager -> the monolith returned {"error": "ChunkManager not available"}
    // with NO trail. The forge must keep that exact error (wire compat) AND attach
    // the gates showing intake refused as the first and only executed stage.
    nlohmann::json res = StructureForge::run(nlohmann::json::object(), {});

    ASSERT_TRUE(res.contains("error"));
    EXPECT_EQ(res["error"], "ChunkManager not available");

    ASSERT_TRUE(res.contains("gates")) << "refusals must carry the gate trail";
    ASSERT_TRUE(res["gates"].is_array());
    ASSERT_EQ(res["gates"].size(), 1u) << "nothing may run after a refused stage";
    EXPECT_EQ(res["gates"][0]["stage"], "intake");
    EXPECT_EQ(res["gates"][0]["outcome"], "refused");
}

TEST(ForgeStageOrder, WrapperDelegates) {
    // buildV2 is a thin wrapper over the forge — identical refusal shape.
    nlohmann::json viaWrapper =
        StructureBuildService::buildV2(nlohmann::json::object(), {});
    nlohmann::json viaForge = StructureForge::run(nlohmann::json::object(), {});
    // Strip stage timings (the only nondeterministic field) before comparing.
    for (auto* r : {&viaWrapper, &viaForge})
        if (r->contains("gates"))
            for (auto& g : (*r)["gates"]) g.erase("ms");
    EXPECT_EQ(viaWrapper, viaForge);
}
