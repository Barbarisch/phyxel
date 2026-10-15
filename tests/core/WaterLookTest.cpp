#include <gtest/gtest.h>

#include "core/water/WaterBodyTable.h"
#include "core/water/WaterLook.h"
#include "core/water/WaterSurfaceMesh.h"

#include <nlohmann/json.hpp>

// WaterCore Phase G3 (docs/WaterCore.md 18.7): the per-body look profile - what a body stores, how
// it persists, how a column finds its body's look, and that every simulated field draws with its own.

namespace Phyxel::Core::Water {
namespace {

TEST(WaterLookTest, UnsetIsNeutralAndPacksToTheShaderSentinels) {
    const WaterLook none;
    EXPECT_FALSE(none.any());
    const WaterLookPacked p = packLook(none);
    EXPECT_EQ(p.look0, glm::vec4(-1.0f, -1.0f, -1.0f, 0.0f));   // tint unset, clarity derived
    EXPECT_EQ(p.look1, glm::vec4(-1.0f, -1.0f, 0.0f, 0.0f));
    EXPECT_EQ(lookToJson(none), "{}");
    WaterLook l; l.clarity = 2.0f; l.tint = glm::vec3(0.2f, 0.15f, 0.05f); l.turbidity = 0.4f;
    const WaterLookPacked q = packLook(l);
    EXPECT_EQ(q.look0, glm::vec4(0.2f, 0.15f, 0.05f, 2.0f));
    EXPECT_EQ(q.look1, glm::vec4(0.4f, -1.0f, 0.0f, 0.0f));   // roughness stays derived
}

TEST(WaterLookTest, ClampsAndSaysWhat) {
    WaterLook l; l.clarity = 500.0f; l.tint = glm::vec3(2.0f, 0.5f, 0.0f); l.turbidity = 0.3f; l.roughness = 9.0f;
    std::vector<std::string> notes;
    const WaterLook c = clampLook(l, &notes);
    EXPECT_FLOAT_EQ(c.clarity, 100.0f);
    EXPECT_FLOAT_EQ(c.tint.x, 1.0f);
    EXPECT_FLOAT_EQ(c.turbidity, 0.3f);
    EXPECT_FLOAT_EQ(c.roughness, 2.0f);
    EXPECT_EQ(notes.size(), 3u) << "clarity, tint.r and roughness were clamped; turbidity was not";
    WaterLook low; low.clarity = 0.0f;   // 0 is SET (a knob value), clamped to the floor
    EXPECT_FLOAT_EQ(clampLook(low).clarity, 0.1f);
}

TEST(WaterLookTest, RoundTripsOnTheBodyRecordAndOldWorldsStayIdentical) {
    WaterBodyTable t;
    WaterBodyRecord ocean; ocean.id = 0; ocean.cls = "ocean"; ocean.level = 16.0f; ocean.bboxMin = {0, 0}; ocean.bboxMax = {999, 999};
    WaterBodyRecord lake; lake.id = 1; lake.cls = "lake"; lake.level = 40.0f;
    t.importGeneration({ocean, lake});
    const std::string before = t.serialize();
    EXPECT_EQ(before.find("\"look\""), std::string::npos) << "an unset look serialises to nothing";
    t.findMutable(0)->look.clarity = 3.5f;
    t.findMutable(0)->look.tint = glm::vec3(0.1f, 0.2f, 0.05f);
    WaterBodyTable u;
    ASSERT_TRUE(u.load(t.serialize()));
    ASSERT_NE(u.find(0), nullptr);
    EXPECT_FLOAT_EQ(u.find(0)->look.clarity, 3.5f);
    EXPECT_EQ(u.find(0)->look.tint, glm::vec3(0.1f, 0.2f, 0.05f));
    EXPECT_FALSE(u.find(0)->look.hasTurbidity());
    EXPECT_FALSE(u.find(1)->look.any());
    // a load clamps (a hand-edited DB cannot smuggle an out-of-range value to the shader)
    nlohmann::json j = nlohmann::json::parse(t.serialize());
    j["bodies"][0]["look"]["clarity"] = 1000.0;
    WaterBodyTable v; ASSERT_TRUE(v.load(j.dump()));
    EXPECT_FLOAT_EQ(v.find(0)->look.clarity, 100.0f);
    // a re-import of the bake keeps the looks the world set
    u.importGeneration({ocean, lake});
    EXPECT_FLOAT_EQ(u.find(0)->look.clarity, 3.5f);
}

TEST(WaterLookTest, ResolverIsAFunctionOfTheColumn) {
    // the generation body under a column (bake query), else the av pond whose box holds it, else none;
    // the same answer from a copy of the table and in any query order (no hidden state)
    WaterBodyTable t;
    WaterBodyRecord ocean; ocean.id = 7; ocean.cls = "ocean"; ocean.look.clarity = 4.0f;
    t.importGeneration({ocean});
    WaterBodyRecord pond; pond.id = -3; pond.origin = "av"; pond.bboxMin = {100, 100}; pond.bboxMax = {104, 104}; pond.look.tint = glm::vec3(0.3f, 0.2f, 0.1f);
    t.recordsMutable().push_back(pond);
    auto bake = [](int x, int) { return x < 50 ? 7 : -1; };
    EXPECT_EQ(t.bodyAt(10, 10, bake), 7);
    EXPECT_FLOAT_EQ(t.lookAt(10, 10, bake).clarity, 4.0f);
    EXPECT_EQ(t.bodyAt(102, 101, bake), -3);
    EXPECT_EQ(t.lookAt(102, 101, bake).tint, glm::vec3(0.3f, 0.2f, 0.1f));
    EXPECT_EQ(t.bodyAt(200, 200, bake), WaterBodyTable::kNoBody);
    EXPECT_FALSE(t.lookAt(200, 200, bake).any());
    EXPECT_FALSE(t.lookAt(10, 10, nullptr).any()) << "without a bake query only av ponds resolve";
    const WaterBodyTable copy = t;
    for (int z = 95; z <= 110; z += 3) for (int x = 0; x <= 210; x += 7)
        EXPECT_EQ(copy.lookAt(x, z, bake), t.lookAt(x, z, bake)) << x << "," << z;
}

TEST(WaterLookTest, RegionsMatchByBoxAndLevelAndStayOutOfTheMassLedger) {
    // stored water no body owned, named for its look: the sea at 16 over a big box; a pond at 18
    // inside the same box is NOT the sea; a region carries no mass and is never credited
    WaterBodyTable t;
    WaterBodyRecord sea; sea.cls = "sea"; sea.level = 16.0f; sea.bboxMin = {0, 0}; sea.bboxMax = {500, 500}; sea.mass = 99.0; sea.look.clarity = 2.0f;
    const int id = t.addRegion(sea);
    EXPECT_LT(id, 0);
    ASSERT_NE(t.find(id), nullptr);
    EXPECT_EQ(t.find(id)->origin, "region");
    EXPECT_EQ(t.find(id)->mass, 0.0) << "a region is a name for a look, not a mass record";
    EXPECT_EQ(t.avMass(), 0.0);
    EXPECT_EQ(t.bodyAt(100, 100, nullptr, 16.0f), id);
    EXPECT_EQ(t.bodyAt(100, 100, nullptr, 18.0f), WaterBodyTable::kNoBody) << "a pond at another level inside the box";
    EXPECT_EQ(t.bodyAt(100, 100, nullptr), id) << "no top known (a volume, the eye): the box decides";
    EXPECT_FLOAT_EQ(t.lookAt(100, 100, nullptr, 16.02f).clarity, 2.0f);
    // a write-back credit at a column inside the region makes an av pond, never touches the region
    t.credit({WaterBodyTable::ColumnMass{100, 100, 1.5, 16.0f}}, {}, [](int, int) { return -1; });
    EXPECT_EQ(t.find(id)->mass, 0.0);
    EXPECT_NEAR(t.avMass(), 1.5, 1e-12);
    WaterBodyTable u; ASSERT_TRUE(u.load(t.serialize()));
    EXPECT_EQ(u.find(id)->origin, "region");
    EXPECT_FLOAT_EQ(u.find(id)->look.clarity, 2.0f);
}

TEST(WaterLookTest, EveryFieldDrawsWithItsOwnLook) {
    // two fields (a volume and the band), different looks: the mesh carries one draw range each,
    // covering every index exactly once, in order; an empty field adds no range
    auto field = [](int ox, float top) {
        WaterSurfaceField f; f.origin = glm::ivec3(ox, 0, 0); f.nx = 2; f.nz = 2; f.h = 1.0f;
        f.cols.assign(4, SurfaceColumn{});
        for (auto& c : f.cols) { c.runs = 1.0f; c.bottom[0] = 0.0f; c.top[0] = top; }
        return f;
    };
    WaterSurfaceField a = field(0, 1.0f), b = field(10, 1.5f), empty = field(20, 1.0f);
    for (auto& c : empty.cols) c.runs = 0.0f;
    WaterLook la; la.clarity = 2.0f; a.look = packLook(la);
    WaterLook lb; lb.tint = glm::vec3(0.1f, 0.1f, 0.05f); b.look = packLook(lb);
    WaterSurfaceMesh m;
    appendFieldToMesh(a, m); appendFieldToMesh(empty, m); appendFieldToMesh(b, m);
    ASSERT_EQ(m.ranges.size(), 2u);
    EXPECT_EQ(m.ranges[0].firstIndex, 0u);
    EXPECT_EQ(m.ranges[1].firstIndex, m.ranges[0].indexCount);
    EXPECT_EQ(m.ranges[0].indexCount + m.ranges[1].indexCount, m.indices.size());
    EXPECT_EQ(m.ranges[0].look.look0.w, 2.0f);
    EXPECT_EQ(m.ranges[1].look.look0, glm::vec4(0.1f, 0.1f, 0.05f, 0.0f));
    m.clear();
    EXPECT_TRUE(m.ranges.empty());
}

}  // namespace
}  // namespace Phyxel::Core::Water
