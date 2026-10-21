#include <gtest/gtest.h>

#include "core/WaterManager.h"

#include <functional>
#include <memory>

#include <glm/glm.hpp>

using Phyxel::Core::WaterManager;

// ============================================================================
// M2b-2 (docs/WorldForgeV2Plan.md, M2 engineering design): the water runtime's river-vs-creek
// split is the generator's CLASS, not the Strahler order. Under the grounded width/depth law an
// order-3 stream can be 0.3 u deep: with the historical "order >= 3 → full pins only where the
// carve >= 0.5" rule it got a channel tag and NO water (a dry tagged bed). With the class bound
// (carved = false for such a stream) it gets the creek ribbon.
//
// Rig: a null ChunkManager (no terrain sync), a solid floor at y = 0 so the bed is y = 1, one
// channel line along z = 16 reporting carve depth 0.3 and order 3. The only variable is the class
// query. Red on the committed code: the class query is stored and ignored, so the creek case
// yields no water.
// ============================================================================
namespace {

float ribbonMassWith(bool bindClass, bool carved) {
    auto wm = std::make_unique<WaterManager>(nullptr, glm::ivec3(0, 0, 0), glm::ivec3(32, 8, 32));
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z) wm->setSolidWorld(x, 0, z, true);
    wm->setRiverQuery([](float, float z) { return (z >= 16.0f && z < 17.0f) ? 0.3f : 0.0f; });
    wm->setRiverOrderQuery([](float, float) { return 3; });
    if (bindClass) wm->setRiverClassQuery([carved](float, float) { return carved; });
    for (int i = 0; i < 3; ++i) wm->update(0.1f);
    return wm->massAtWorld(glm::vec3(8.5f, 1.5f, 16.5f));
}

}  // namespace

TEST(RiverPinClassTest, TheClassNotTheOrderDecidesTheCreekRibbon) {
    // Control 1 (legacy, no class bound): order 3 with a 0.3 carve pins nothing — today's rule.
    EXPECT_FLOAT_EQ(ribbonMassWith(false, false), 0.0f) << "legacy order>=3 rule changed";
    // Control 2 (class = carved river): the full pin needs carve >= 0.5, so still nothing.
    EXPECT_FLOAT_EQ(ribbonMassWith(true, true), 0.0f) << "a carved river's shallow edge must stay unpinned";
    // The case: class = creek → the fractional ribbon (min(carve, MIN_HOLD) >= 0.15).
    EXPECT_GE(ribbonMassWith(true, false), 0.15f) << "an order-3 creek got no ribbon: the class is ignored";
}
