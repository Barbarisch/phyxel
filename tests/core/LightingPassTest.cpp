#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/FurniturePlacer.h"

// ============================================================================
// M5 place_lights (#18) — the lighting pass.
//
// RED before M5: candle_stand / wall_lantern / chandelier were ordinary furniture
// built from the `glow` material. `glow` SELF-lights its own voxels and
// illuminates nothing, and structure generation registered ZERO engine point
// lights (ledger row 18 was the only L0 row in the furnishing tier). A "lit"
// tavern was pitch black at night with three glowing props in it.
//
// The engine-side registration needs a live LightManager, so these tests pin the
// PURE half — which fixtures emit, and with what grounded photometry — plus the
// honest-reporting contract. The registration itself is L4 (a live build reports
// lights_registered > 0).
// ============================================================================

using namespace Phyxel::Core;

TEST(LightingPass, LampsAndHearthsEmitAndFurnitureDoesNot) {
    for (const char* t : {"candle_stand", "wall_lantern", "chandelier"})
        EXPECT_TRUE(FurniturePlacer::emitterFor(t).emits) << t << " must light the room";
    // The hearth FIRE is a light source too — the biggest one in a medieval room.
    for (const char* t : {"fireplace", "forge_hearth", "oven_bread"})
        EXPECT_TRUE(FurniturePlacer::emitterFor(t).emits) << t << " burns — it must emit light";
    // Ordinary furniture must not secretly glow.
    for (const char* t : {"bed", "table", "chest", "bench", "tavern_bar", "barrel", "wardrobe"})
        EXPECT_FALSE(FurniturePlacer::emitterFor(t).emits) << t << " is not a light source";
}

// Colour is the GROUNDED part of the photometry: flame is warm (~1850 K candle,
// ~1700 K fire), so red > green > blue by a clear margin, and a hearth fire is
// unmistakably warmer (more orange) than a candle. If someone "tunes" these to
// white the rooms stop reading as firelit.
TEST(LightingPass, FlameColoursAreWarmAndTheHearthIsWarmerThanACandle) {
    const auto candle = FurniturePlacer::emitterFor("candle_stand");
    const auto fire   = FurniturePlacer::emitterFor("fireplace");
    for (const auto& e : {candle, fire}) {
        EXPECT_GT(e.r, e.g) << "flame light must be warm (r > g)";
        EXPECT_GT(e.g, e.b) << "flame light must be warm (g > b)";
        EXPECT_FLOAT_EQ(e.r, 1.0f) << "red channel is the reference for a flame";
    }
    EXPECT_LT(fire.b, candle.b) << "a wood fire (~1700 K) must be warmer than a candle (~1850 K)";
}

// Relative brightness IS grounded (1 candle ~= 1 cd by definition; a chandelier
// carries ~8-12 candles; an open hearth out-lights them all), even though the
// absolute engine units are disclosed tuning values.
TEST(LightingPass, RelativeBrightnessFollowsCandleCount) {
    const float candle = FurniturePlacer::emitterFor("candle_stand").intensity;
    const float lantern = FurniturePlacer::emitterFor("wall_lantern").intensity;
    const float chandelier = FurniturePlacer::emitterFor("chandelier").intensity;
    const float hearth = FurniturePlacer::emitterFor("fireplace").intensity;
    EXPECT_LT(candle, lantern)     << "a lantern carries more than one candle";
    EXPECT_LT(lantern, chandelier) << "a chandelier carries more than a lantern";
    EXPECT_GT(hearth, lantern)     << "an open hearth out-lights a lantern";
    // Radius must follow brightness — a brighter source reaches further.
    EXPECT_LT(FurniturePlacer::emitterFor("candle_stand").radius,
              FurniturePlacer::emitterFor("chandelier").radius);
}

// The flame sits ABOVE the fixture base — a candle burns at the top of its stand,
// a hearth fire just above the floor. A zero offset would bury every light inside
// its own prop and light the floor instead of the room.
TEST(LightingPass, FlameSitsAboveTheFixtureBase) {
    EXPECT_GT(FurniturePlacer::emitterFor("candle_stand").emitMicroY, 0.0f)
        << "the candle flame must sit at the TOP of the stand, not in its foot";
    EXPECT_GT(FurniturePlacer::emitterFor("fireplace").emitMicroY, 0.0f)
        << "the fire must sit above the hearth base";
    // The candle stand is tall; its flame offset must exceed the hearth's.
    EXPECT_GT(FurniturePlacer::emitterFor("candle_stand").emitMicroY,
              FurniturePlacer::emitterFor("fireplace").emitMicroY);
}

// Lighting fixtures belong to the LIGHTING pass (M4), so they place AFTER the
// furniture they illuminate — a sconce should not take the wall a bed needs.
TEST(LightingPass, EmittersPlaceInTheLightingPass) {
    for (const char* t : {"candle_stand", "wall_lantern", "chandelier"})
        EXPECT_EQ(FurniturePlacer::passRank(t), 2) << t << " must be in the lighting pass";
    // ...except the hearth, which is HEAVY: it is a structural, vented fixture that
    // happens to emit. It must still claim its wall first.
    EXPECT_EQ(FurniturePlacer::passRank("fireplace"), 0)
        << "the hearth is a heavy fixture that emits, not a lamp";
}
