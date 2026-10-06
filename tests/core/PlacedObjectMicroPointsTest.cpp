#include <gtest/gtest.h>

#include "core/ChunkManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace Phyxel;
using namespace Phyxel::Core;

// ============================================================================
// A4 / W1 (docs/AnimationSystemV3Plan.md §4 A4 item 3, FunctionalWiringBacklog W1): a MICRO-placed
// template's interaction points come from its real origin (microAnchor / 9) — at placement AND on
// reload — so a generator-placed chair is a seat the character can actually find, and the two
// paths agree bit-for-bit ("chunking must not change the answer" shape). RED 2026-09-30: the micro
// path wrote no points at all; the reload path anchored at the floored cube (up to 0.89 u off).
// ============================================================================

namespace {

fs::path writeTempSeat() {
    auto path = fs::temp_directory_path() / "test_micro_seat.voxel";
    std::ofstream f(path);
    // a 4x4-micro stool: slab top at micro y=4 (0.444 u); seat point at the slab-top centre
    f << "# name: test_micro_seat\n"
         "# interaction_point: seat_0 seat 0.2222 0.4444 0.2222 0.0 *\n"
         "M 0 0 0 0 0 0 0 0 0 Wood\n"
         "M 0 0 0 1 1 1 0 0 0 Wood\n";
    return path;
}

const InteractionPoint* seatOf(const PlacedObjectManager& pom, const std::string& id) {
    const PlacedObject* obj = pom.get(id);
    if (!obj || obj->interactionPoints.empty()) return nullptr;
    return &obj->interactionPoints.front();
}

} // namespace

TEST(PlacedObjectMicroPoints, MicroPlacementCarriesItsSeatAtTheRealOriginNotTheFlooredCube) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    cm.createChunk(glm::ivec3(0, 0, 0), /*populate=*/false);
    ObjectTemplateManager otm(&cm);
    ASSERT_TRUE(otm.loadTemplate(writeTempSeat().string()));
    const VoxelTemplate* tmpl = otm.getTemplate("test_micro_seat");
    ASSERT_NE(tmpl, nullptr);
    ASSERT_EQ(tmpl->interactionPoints.size(), 1u) << "the header line must parse into a def";

    PlacedObjectManager pom(&cm, &otm, nullptr);
    pom.registerTemplateDefs("test_micro_seat", tmpl->interactionPoints);

    // off-grid micro anchor: cube (1, 0, 2) with remainders (4, 0, 4) — the case the cube floor lost
    const glm::ivec3 micro(13, 0, 22);
    const std::string id = pom.placeTemplateMicro("test_micro_seat", micro, 0, "");
    ASSERT_FALSE(id.empty()) << "micro placement into the pre-created chunk";
    const auto* pt = seatOf(pom, id);
    ASSERT_NE(pt, nullptr) << "W1 fault 1: the micro path must write interaction points";
    const glm::vec3 expected = glm::vec3(micro) / 9.0f + glm::vec3(0.2222f, 0.4444f, 0.2222f);
    EXPECT_NEAR(pt->worldPos.x, expected.x, 1e-4f);
    EXPECT_NEAR(pt->worldPos.y, expected.y, 1e-4f);
    EXPECT_NEAR(pt->worldPos.z, expected.z, 1e-4f);
    // and it is NOT the cube-floored answer (that would be 0.444 u off in x and z here)
    const glm::vec3 floored = glm::vec3(1, 0, 2) + glm::vec3(0.2222f, 0.4444f, 0.2222f);
    EXPECT_GT(glm::distance(pt->worldPos, floored), 0.4f) << "control: the two anchors differ on an off-grid placement";
}

TEST(PlacedObjectMicroPoints, PlacementAndReloadAgreeBitForBit) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    cm.createChunk(glm::ivec3(0, 0, 0), false);
    ObjectTemplateManager otm(&cm);
    ASSERT_TRUE(otm.loadTemplate(writeTempSeat().string()));
    const VoxelTemplate* tmpl = otm.getTemplate("test_micro_seat");
    ASSERT_NE(tmpl, nullptr);

    for (int rot : {0, 90, 180, 270}) {
        PlacedObjectManager pom(&cm, &otm, nullptr);
        pom.registerTemplateDefs("test_micro_seat", tmpl->interactionPoints);
        const std::string id = pom.placeTemplateMicro("test_micro_seat", glm::ivec3(13, 0, 22), rot, "");
        ASSERT_FALSE(id.empty()) << "rot " << rot;
        const auto* placed = seatOf(pom, id);
        ASSERT_NE(placed, nullptr) << "rot " << rot;

        // save → load into a fresh manager → recompute (the world-load path)
        const nlohmann::json saved = pom.toJson();
        PlacedObjectManager reloaded(&cm, &otm, nullptr);
        reloaded.fromJson(saved);
        reloaded.registerTemplateDefs("test_micro_seat", tmpl->interactionPoints);
        reloaded.recomputeAllInteractionPoints();
        const auto* again = seatOf(reloaded, id);
        ASSERT_NE(again, nullptr) << "rot " << rot;
        EXPECT_EQ(again->worldPos.x, placed->worldPos.x) << "rot " << rot;
        EXPECT_EQ(again->worldPos.y, placed->worldPos.y) << "rot " << rot;
        EXPECT_EQ(again->worldPos.z, placed->worldPos.z) << "rot " << rot;
        EXPECT_EQ(again->facingYaw, placed->facingYaw) << "rot " << rot;
        pom.clear(); reloaded.clear();
    }
}
