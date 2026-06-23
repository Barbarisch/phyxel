#include <gtest/gtest.h>

#include <fstream>

#include "core/StructureBriefSchema.h"

using namespace Phyxel::Core;

TEST(StructureBriefSchemaTest, LoadsStagesAndFields) {
    auto j = nlohmann::json::parse(R"({
        "stages": [
            { "id": "setting", "title": "Setting", "order": 0, "fields": [
                { "id": "period", "label": "Era", "kind": "blocking", "type": "string" },
                { "id": "magic_present", "label": "Magic?", "kind": "default", "type": "bool",
                  "default_source": "world lore; default false" }
            ]}
        ]
    })");
    StructureBriefSchema s;
    ASSERT_TRUE(s.loadFromJson(j));
    EXPECT_EQ(s.stageCount(), 1u);
    EXPECT_EQ(s.fieldCount(), 2u);
    ASSERT_NE(s.field("period"), nullptr);
    EXPECT_TRUE(s.field("period")->isBlocking());
    EXPECT_FALSE(s.field("magic_present")->isBlocking());
    EXPECT_EQ(s.field("magic_present")->defaultSource, "world lore; default false");
    EXPECT_EQ(s.field("missing"), nullptr);
}

TEST(StructureBriefSchemaTest, CollectsBlockingFields) {
    auto j = nlohmann::json::parse(R"({
        "stages": [
            { "id": "a", "fields": [ { "id": "x", "kind": "blocking" }, { "id": "y", "kind": "default" } ] },
            { "id": "b", "fields": [ { "id": "z", "kind": "blocking" } ] }
        ]
    })");
    StructureBriefSchema s;
    ASSERT_TRUE(s.loadFromJson(j));
    EXPECT_EQ(s.blockingFields().size(), 2u);
}

TEST(StructureBriefSchemaTest, EnumOptionsParse) {
    auto j = nlohmann::json::parse(R"({
        "stages": [ { "id": "f", "fields": [
            { "id": "owner_status", "kind": "blocking", "type": "enum",
              "options": ["peasant", "noble", "royal"] } ] } ]
    })");
    StructureBriefSchema s;
    ASSERT_TRUE(s.loadFromJson(j));
    ASSERT_NE(s.field("owner_status"), nullptr);
    EXPECT_EQ(s.field("owner_status")->options.size(), 3u);
}

// The shipped engine schema must parse and carry the full intake — all 8 stages,
// the ~43 fields, and the blocking set the wizard/CLI/skill all depend on.
TEST(StructureBriefSchemaTest, ShippedSchemaParses) {
    const char* candidates[] = {
        "resources/structure_brief_schema.json",
        "../resources/structure_brief_schema.json",
        "../../resources/structure_brief_schema.json",
        "../../../resources/structure_brief_schema.json",
    };
    StructureBriefSchema s;
    bool found = false;
    for (const char* p : candidates) {
        std::ifstream f(p);
        if (f.good()) { found = s.loadFromFile(p); break; }
    }
    if (!found) GTEST_SKIP() << "resources/structure_brief_schema.json not reachable from CWD";

    EXPECT_EQ(s.stageCount(), 8u);
    EXPECT_GE(s.fieldCount(), 40u);
    // The blocking set the intake must always ask:
    for (const char* id : {"period", "culture_region", "climate", "function", "owner_status",
                           "site_context", "terrain", "structural_material", "footprint",
                           "stories", "enterable"}) {
        const BriefField* f = s.field(id);
        ASSERT_NE(f, nullptr) << "missing field: " << id;
        EXPECT_TRUE(f->isBlocking()) << id << " should be blocking";
    }
    EXPECT_GE(s.blockingFields().size(), 11u);
}
