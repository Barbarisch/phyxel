#include <gtest/gtest.h>

#include "core/RoomProgram.h"
#include "core/SettlementProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// SettlementProgram — the era/tier data spine (resources/settlement_program.json).
// L1/data gates: the shipped file loads, (era, tier) resolves, an UNKNOWN era or
// tier is REJECTED (nullptr — the era hook must stay honest, no silent default),
// and every tier's typology palette resolves in room_program.json (referential
// integrity — a tier must never name a typology the building generator lacks).
// ============================================================================

namespace {
bool loadShipped(SettlementProgramRegistry& reg) {
    for (const char* p : {"resources/settlement_program.json", "../resources/settlement_program.json",
                          "../../resources/settlement_program.json",
                          "../../../resources/settlement_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}
bool loadRooms(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json", "../../../resources/room_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}
} // namespace

TEST(SettlementProgramTest, ShippedFileLoadsAllMedievalTiers) {
    SettlementProgramRegistry reg;
    if (!loadShipped(reg)) GTEST_SKIP() << "settlement_program.json not reachable from CWD";
    for (const char* tier : {"hamlet", "village", "town", "city"})
        EXPECT_NE(reg.get("medieval", tier), nullptr) << "missing medieval tier: " << tier;
    const SettlementTierPreset* v = reg.get("medieval", "village");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->morphology, "main_street");
    EXPECT_GT(v->street.mainWidth, 0);
    EXPECT_FALSE(v->typologyWeights.empty());
    EXPECT_GE(v->buildingsMax, v->buildingsMin);
}

// The era hook must be HONEST: an unknown era or tier resolves to nullptr (the caller surfaces an
// error) — never a silent fallback to medieval.
TEST(SettlementProgramTest, UnknownEraOrTierRejected) {
    SettlementProgramRegistry reg;
    if (!loadShipped(reg)) GTEST_SKIP() << "settlement_program.json not reachable from CWD";
    EXPECT_EQ(reg.get("renaissance", "village"), nullptr);
    EXPECT_EQ(reg.get("medieval", "metropolis"), nullptr);
    EXPECT_EQ(reg.get("", ""), nullptr);
}

// Referential integrity: every typology a tier can draw must exist in room_program.json — the
// settlement layer must never queue a building the structure generator can't build.
TEST(SettlementProgramTest, EveryTierTypologyExistsInRoomProgram) {
    SettlementProgramRegistry sreg;
    RoomProgramRegistry rreg;
    if (!loadShipped(sreg) || !loadRooms(rreg)) GTEST_SKIP() << "canon files not reachable from CWD";
    for (const std::string& era : sreg.eras())
        for (const std::string& tier : sreg.tiers(era)) {
            const SettlementTierPreset* t = sreg.get(era, tier);
            ASSERT_NE(t, nullptr);
            for (const auto& [typ, w] : t->typologyWeights)
                EXPECT_TRUE(rreg.contains(typ))
                    << era << "/" << tier << " weights unknown typology '" << typ << "'";
            for (const auto& [typ, w] : t->coreTypologyWeights)
                EXPECT_TRUE(rreg.contains(typ))
                    << era << "/" << tier << " CORE weights unknown typology '" << typ << "'";
        }
}

// Grounding rule: every shipped tier carries per-value provenance (sources may FLAG a value as
// NEEDS-RESEARCH — that is honest — but a tier with NO sources at all is unsourced data).
TEST(SettlementProgramTest, EveryTierIsSourced) {
    SettlementProgramRegistry reg;
    if (!loadShipped(reg)) GTEST_SKIP() << "settlement_program.json not reachable from CWD";
    for (const std::string& era : reg.eras())
        for (const std::string& tier : reg.tiers(era)) {
            const SettlementTierPreset* t = reg.get(era, tier);
            ASSERT_NE(t, nullptr);
            EXPECT_FALSE(t->sources.empty()) << era << "/" << tier << " has no sources";
        }
}

TEST(SettlementProgramTest, MalformedJsonRejected) {
    SettlementProgramRegistry reg;
    EXPECT_FALSE(reg.loadFromJson(nlohmann::json::array()));
    EXPECT_FALSE(reg.loadFromJson(nlohmann::json{{"tiers", 1}}));   // no "eras" map
    EXPECT_EQ(reg.size(), 0u);
}

// CityForgePlan M3b (RED on the identity stub): `density` is the caller's settlement->city
// lever. 1.0 = identity (legacy byte-compatible); >1 = tighter blocks, shallower plots,
// smaller setbacks, more buildings, fewer fences; <1 = the reverse. All bounded.
TEST(SettlementProgramTest, DensityScalesThePreset) {
    SettlementProgramRegistry reg;
    if (!loadShipped(reg)) GTEST_SKIP() << "settlement_program.json not reachable from CWD";
    const SettlementTierPreset* city = reg.get("medieval", "city");
    ASSERT_NE(city, nullptr);

    // Identity: density 1.0 changes NOTHING (legacy compatibility).
    const SettlementTierPreset same = applyDensity(*city, 1.0);
    EXPECT_EQ(same.blocksMin, city->blocksMin);
    EXPECT_EQ(same.blocksMax, city->blocksMax);
    EXPECT_EQ(same.plot.depthMin, city->plot.depthMin);
    EXPECT_EQ(same.plot.depthMax, city->plot.depthMax);
    EXPECT_EQ(same.setback.max, city->setback.max);
    EXPECT_EQ(same.buildingsMax, city->buildingsMax);
    EXPECT_DOUBLE_EQ(same.fenceFraction, city->fenceFraction);

    // Dense: blocks + plots tighten, buildings rise, fences thin.
    const SettlementTierPreset dense = applyDensity(*city, 2.0);
    EXPECT_LT(dense.blocksMin, city->blocksMin);
    EXPECT_LT(dense.blocksMax, city->blocksMax);
    EXPECT_LT(dense.plot.depthMax, city->plot.depthMax);
    EXPECT_GT(dense.buildingsMax, city->buildingsMax);
    EXPECT_LT(dense.fenceFraction, city->fenceFraction);
    // Bounds hold: nothing scaled below its structural floor.
    EXPECT_GE(dense.blocksMin, 8);
    EXPECT_GE(dense.plot.depthMin, 6);
    EXPECT_GE(dense.plot.sideGap, 0);
    EXPECT_LE(dense.blocksMin, dense.blocksMax);
    EXPECT_LE(dense.plot.depthMin, dense.plot.depthMax);

    // Sparse: the reverse direction.
    const SettlementTierPreset sparse = applyDensity(*city, 0.5);
    EXPECT_GT(sparse.blocksMax, city->blocksMax);
    EXPECT_LT(sparse.buildingsMax, city->buildingsMax);

    // Out-of-range densities clamp instead of exploding.
    const SettlementTierPreset wild = applyDensity(*city, 100.0);
    EXPECT_EQ(wild.blocksMin, applyDensity(*city, 2.0).blocksMin);
}
