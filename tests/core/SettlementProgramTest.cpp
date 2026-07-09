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
