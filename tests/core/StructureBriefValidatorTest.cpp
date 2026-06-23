#include <gtest/gtest.h>

#include "core/StructureBrief.h"
#include "core/StructureBriefSchema.h"
#include "core/StructureBriefValidator.h"

using namespace Phyxel::Core;

namespace {
StructureBriefSchema miniSchema() {
    StructureBriefSchema s;
    s.loadFromJson(nlohmann::json::parse(R"({
        "stages": [ { "id": "s", "fields": [
            { "id": "period", "kind": "blocking", "type": "string" },
            { "id": "function", "kind": "blocking", "type": "string" },
            { "id": "owner_status", "kind": "default", "type": "enum",
              "options": ["peasant", "noble"] }
        ] } ]
    })"));
    return s;
}
bool has(const ValidationReport& r, const std::string& code) {
    for (const auto& i : r.issues()) if (i.code == code) return true;
    return false;
}
} // namespace

TEST(StructureBriefValidatorTest, BlockingMissingFails) {
    StructureBrief b;
    b.set("period", "medieval", "user", true);   // function (blocking) omitted
    auto r = StructureBriefValidator::validate(b, miniSchema());
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has(r, "blocking_missing"));
}

TEST(StructureBriefValidatorTest, FullySourcedConfirmedPasses) {
    StructureBrief b;
    b.set("period", "medieval", "user", true);
    b.set("function", "house", "user", true);
    auto r = StructureBriefValidator::validate(b, miniSchema());
    EXPECT_TRUE(r.ok()) << r.summary();
}

TEST(StructureBriefValidatorTest, UnsourcedValueFails) {
    StructureBrief b;
    b.set("period", "medieval", "", true);        // no source
    b.set("function", "house", "user", true);
    auto r = StructureBriefValidator::validate(b, miniSchema());
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has(r, "unsourced"));
}

TEST(StructureBriefValidatorTest, UnconfirmedValueFails) {
    StructureBrief b;
    b.set("period", "medieval", "user", true);
    b.set("function", "house", "Neufert p.123", false);   // sourced but not confirmed
    auto r = StructureBriefValidator::validate(b, miniSchema());
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has(r, "unconfirmed"));
}

TEST(StructureBriefValidatorTest, OutOfRangeEnumIsWarningNotError) {
    StructureBrief b;
    b.set("period", "medieval", "user", true);
    b.set("function", "house", "user", true);
    b.set("owner_status", "wizard", "user", true);   // not in [peasant, noble]
    auto r = StructureBriefValidator::validate(b, miniSchema());
    EXPECT_TRUE(has(r, "enum_unknown"));
    EXPECT_TRUE(r.ok());     // advisory only
    EXPECT_TRUE(r.hasWarnings());
}

TEST(StructureBriefTest, RoundTripsThroughJson) {
    StructureBrief b;
    b.set("period", "medieval", "user", true);
    b.set("footprint", nlohmann::json::array({7, 9}), "user", true);
    StructureBrief p = StructureBrief::fromJson(b.toJson());
    ASSERT_TRUE(p.has("period"));
    EXPECT_EQ(p.get("period")->value.get<std::string>(), "medieval");
    EXPECT_EQ(p.get("period")->source, "user");
    EXPECT_TRUE(p.get("period")->confirmed);
    ASSERT_TRUE(p.has("footprint"));
    EXPECT_EQ(p.get("footprint")->value[1].get<int>(), 9);
}
