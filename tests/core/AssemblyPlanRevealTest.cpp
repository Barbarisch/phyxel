#include <gtest/gtest.h>

#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// Claims Ledger increment 2 (docs/structure-generation/ClaimsLedger.md):
// opening reveals + quoin corner zones become plan data, recorded by the passes
// that paint them. RED state: the realizer carves openings and paints
// jambs/lintels/sills/leaves/quoins but records NONE of it — OpeningCut.reveal
// stays empty, plan.corners stays empty, and featureAt classifies a carved
// doorway cube as "wall" (a real misclassification: the destruction system and
// editor overlay consume featureAt). Every record assertion here is
// cross-checked against the REAL MicroCanvas so a record that lies about
// geometry fails against ground truth, not just against itself.
// ============================================================================

namespace {

StyleProfile styleFromJson(const char* id, const std::string& json) {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(json));
    return *reg.get(id);
}

StyleProfile plainStyle() {
    return styleFromJson("timber_cottage", R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "WoodPlanks", "floor": "Wood", "roof": "Wood",
                           "foundation": "Stone", "trim": "Log" },
            "roof": { "pitch": 0.8 }
        }
    })");
}

StyleProfile quoinStyle() {
    return styleFromJson("stone_quoins", R"({
        "stone_quoins": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "StoneBricks", "floor": "Wood", "roof": "Wood",
                           "foundation": "Stone", "trim": "Sandstone" },
            "flags": { "quoins": true },
            "roof": { "pitch": 0.8 }
        }
    })");
}

ProgPortal portal(const std::string& a, const std::string& b, int px, int pz,
                  int w, int h, const std::string& kind, const std::string& infill = "open") {
    ProgPortal p; p.a = a; p.b = b; p.px = px; p.pz = pz;
    p.width = w; p.height = h; p.kind = kind; p.infill = infill;
    return p;
}

// Door + glazed window (exterior), interior door + arch across both partition axes.
BuildingProgram openingsFixture() {
    BuildingProgram p;
    p.name = "reveal_house"; p.style = "timber_cottage";
    p.footprintW = 9; p.footprintD = 9; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom a; a.id = "a"; a.rect = {0, 0, 4, 9}; a.purpose = "living";
    ProgRoom b; b.id = "b"; b.rect = {4, 0, 5, 5}; b.purpose = "kitchen";
    ProgRoom c; c.id = "c"; c.rect = {4, 5, 5, 4}; c.purpose = "storage";
    st.rooms.push_back(a); st.rooms.push_back(b); st.rooms.push_back(c);
    st.portals.push_back(portal("exterior", "a", 0, 4, 1, 2, "door"));
    st.portals.push_back(portal("exterior", "a", 0, 6, 2, 1, "window", "glass"));
    st.portals.push_back(portal("a", "b", 4, 2, 1, 2, "door"));   // partition at x=4
    st.portals.push_back(portal("b", "c", 6, 5, 1, 2, "arch"));   // partition at z=5
    p.stories.push_back(st);
    return p;
}

BuildingProgram quoinFixture() {
    BuildingProgram p;
    p.name = "reveal_quoins"; p.style = "stone_quoins";
    p.footprintW = 8; p.footprintD = 6; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom r; r.id = "hall"; r.rect = {0, 0, 8, 6}; r.purpose = "living";
    st.rooms.push_back(r);
    st.portals.push_back(portal("exterior", "hall", 3, 0, 1, 2, "door"));
    p.stories.push_back(st);
    return p;
}

const OpeningCut* findOpening(const AssemblyPlan& plan, const std::string& kind,
                              int px, int pz) {
    for (const auto& o : plan.openings)
        if (o.kind == kind && o.x == px && o.z == pz) return &o;
    return nullptr;
}

const TrimBox* findRole(const OpeningCut& o, const std::string& role) {
    for (const auto& t : o.reveal)
        if (t.role == role) return &t;
    return nullptr;
}

} // namespace

// Every realized exterior opening must carry its reveal: the carved clear box
// plus the jamb/lintel trim (windows: sill + ledge + leaf too). Each non-clear
// box's center micro must hold ITS RECORDED MATERIAL in the real canvas; a
// door's clear-box center must be air.
TEST(AssemblyPlanRevealTest, ExteriorOpeningsRecordReveal) {
    auto r = StructureRealizer::realizeShell(openingsFixture(), plainStyle());
    ASSERT_TRUE(r.ok) << r.error;

    const OpeningCut* door = findOpening(r.plan, "door", 0, 4);
    ASSERT_NE(door, nullptr) << "exterior door missing from plan.openings";
    ASSERT_FALSE(door->reveal.empty())
        << "realizer painted the door reveal but recorded nothing";
    for (const char* role : {"clear", "jamb", "lintel"})
        EXPECT_NE(findRole(*door, role), nullptr) << "door reveal missing role " << role;

    const OpeningCut* win = findOpening(r.plan, "window", 0, 6);
    ASSERT_NE(win, nullptr) << "exterior window missing from plan.openings";
    ASSERT_FALSE(win->reveal.empty());
    for (const char* role : {"clear", "jamb", "lintel", "sill", "ledge", "leaf"})
        EXPECT_NE(findRole(*win, role), nullptr) << "window reveal missing role " << role;
    const TrimBox* leaf = findRole(*win, "leaf");
    if (leaf) EXPECT_EQ(leaf->material, "Glass") << "glazed window leaf must be Glass";

    // Ground truth: every recorded solid box holds its material in the canvas.
    int checked = 0;
    for (const OpeningCut* o : {door, win})
        for (const auto& t : o->reveal) {
            const int mx = t.x + t.w / 2, my = t.y + t.h / 2, mz = t.z + t.d / 2;
            if (t.role == "clear") continue;   // leaf/trim may repaint inside the carve
            EXPECT_EQ(r.canvas.materialAt(mx, my, mz), t.material)
                << o->kind << " " << t.role << " record disagrees with canvas at ("
                << mx << "," << my << "," << mz << ")";
            ++checked;
        }
    EXPECT_GT(checked, 0);
    // The door's carved passage is genuinely air at its center.
    const TrimBox* dc = findRole(*door, "clear");
    if (dc)
        EXPECT_FALSE(r.canvas.occupiedMicro(dc->x + dc->w / 2, dc->y + dc->h / 2,
                                            dc->z + dc->d / 2))
            << "door clear box center is not air";
}

// Interior doors/arches carve an 18-micro band straddling the partition plane -
// the clear box must be recorded, and both straddling cubes must be air at the
// passage center (the doorway is passable; BuildingHarness caught the sliver).
TEST(AssemblyPlanRevealTest, InteriorOpeningsRecordClearBand) {
    auto r = StructureRealizer::realizeShell(openingsFixture(), plainStyle());
    ASSERT_TRUE(r.ok) << r.error;

    for (const auto& want : {std::pair<const char*, std::pair<int,int>>{"door", {4, 2}},
                             std::pair<const char*, std::pair<int,int>>{"arch", {6, 5}}}) {
        const OpeningCut* o = findOpening(r.plan, want.first, want.second.first,
                                          want.second.second);
        ASSERT_NE(o, nullptr) << "interior " << want.first << " missing from plan";
        const TrimBox* clear = findRole(*o, "clear");
        ASSERT_NE(clear, nullptr) << "interior " << want.first << " has no clear record";
        EXPECT_EQ(std::max(clear->w, clear->d), 18)
            << "interior clear band must straddle the partition (18 micro)";
        EXPECT_FALSE(r.canvas.occupiedMicro(clear->x + clear->w / 2,
                                            clear->y + clear->h / 2,
                                            clear->z + clear->d / 2))
            << "interior " << want.first << " clear center is not air";
    }
}

// featureAt answers "opening" for carved passage cubes - exterior AND both
// straddling cubes of an interior doorway - while the flanking wall stays "wall".
TEST(AssemblyPlanRevealTest, FeatureAtClassifiesOpenings) {
    auto r = StructureRealizer::realizeShell(openingsFixture(), plainStyle());
    ASSERT_TRUE(r.ok) << r.error;

    const OpeningCut* door = findOpening(r.plan, "door", 0, 4);
    ASSERT_NE(door, nullptr);
    const TrimBox* dc = findRole(*door, "clear");
    ASSERT_NE(dc, nullptr) << "no clear record - see ExteriorOpeningsRecordReveal";
    const glm::ivec3 doorCube{dc->x / 9, (dc->y + dc->h / 2) / 9, dc->z / 9};
    EXPECT_EQ(r.plan.featureAt(doorCube), "opening")
        << "carved doorway cube still classifies as wall";
    EXPECT_EQ(r.plan.featureAt({doorCube.x, doorCube.y, doorCube.z - 2}), "wall")
        << "wall beside the door must stay wall";

    const OpeningCut* idoor = findOpening(r.plan, "door", 4, 2);
    ASSERT_NE(idoor, nullptr);
    const TrimBox* ic = findRole(*idoor, "clear");
    ASSERT_NE(ic, nullptr);
    const int midY = (ic->y + ic->h / 2) / 9;
    EXPECT_EQ(r.plan.featureAt({3, midY, 2}), "opening")
        << "interior doorway cube (low side of the plane) still wall";
    EXPECT_EQ(r.plan.featureAt({4, midY, 2}), "opening")
        << "interior doorway cube (high side of the plane) still wall";
    EXPECT_EQ(r.plan.featureAt({3, midY, 0}), "wall")
        << "partition away from the doorway must stay wall";
}

// Quoined corners are recorded (4 zones on a rectangular quoin-style footprint)
// and classify as "quoin"; the outermost corner micro really is trim material.
TEST(AssemblyPlanRevealTest, QuoinCornersRecordedAndClassified) {
    auto r = StructureRealizer::realizeShell(quoinFixture(), quoinStyle());
    ASSERT_TRUE(r.ok) << r.error;

    ASSERT_EQ(r.plan.corners.size(), 4u)
        << "quoin pass painted corners but recorded nothing";
    for (const auto& q : r.plan.corners) {
        EXPECT_EQ(q.material, "Sandstone");
        EXPECT_LT(q.baseY, q.topY);
        const int midY = (q.baseY + q.topY) / 2;
        EXPECT_EQ(r.plan.featureAt({q.x, midY, q.z}), "quoin")
            << "corner cube (" << q.x << "," << q.z << ") not classified quoin";
        // Ground truth: outermost corner micro at a mid course is the trim.
        const int mx = q.dx > 0 ? q.x * 9 : q.x * 9 + 8;
        const int mz = q.dz > 0 ? q.z * 9 : q.z * 9 + 8;
        EXPECT_EQ(r.canvas.materialAt(mx, midY * 9 + 4, mz), "Sandstone")
            << "corner (" << q.x << "," << q.z << ") canvas micro is not quoin trim";
    }
    // A plain (no-quoins) style records no corner zones.
    auto plain = StructureRealizer::realizeShell(openingsFixture(), plainStyle());
    ASSERT_TRUE(plain.ok);
    EXPECT_TRUE(plain.plan.corners.empty());
}

// Reveal + corner records survive the assembly_plan metadata round-trip.
TEST(AssemblyPlanRevealTest, RevealAndCornersJsonRoundTrip) {
    auto r = StructureRealizer::realizeShell(openingsFixture(), plainStyle());
    ASSERT_TRUE(r.ok) << r.error;
    auto q = StructureRealizer::realizeShell(quoinFixture(), quoinStyle());
    ASSERT_TRUE(q.ok) << q.error;
    ASSERT_FALSE(r.plan.openings.empty());
    ASSERT_FALSE(q.plan.corners.empty());

    AssemblyPlan rBack = AssemblyPlan::fromJson(r.plan.toJson());
    ASSERT_EQ(rBack.openings.size(), r.plan.openings.size());
    for (size_t i = 0; i < r.plan.openings.size(); ++i) {
        const auto& oa = r.plan.openings[i];
        const auto& ob = rBack.openings[i];
        ASSERT_EQ(ob.reveal.size(), oa.reveal.size()) << "opening " << i;
        for (size_t k = 0; k < oa.reveal.size(); ++k) {
            const auto& ta = oa.reveal[k];
            const auto& tb = ob.reveal[k];
            EXPECT_EQ(ta.x, tb.x); EXPECT_EQ(ta.y, tb.y); EXPECT_EQ(ta.z, tb.z);
            EXPECT_EQ(ta.w, tb.w); EXPECT_EQ(ta.h, tb.h); EXPECT_EQ(ta.d, tb.d);
            EXPECT_EQ(ta.role, tb.role); EXPECT_EQ(ta.material, tb.material);
        }
    }
    AssemblyPlan qBack = AssemblyPlan::fromJson(q.plan.toJson());
    ASSERT_EQ(qBack.corners.size(), q.plan.corners.size());
    for (size_t i = 0; i < q.plan.corners.size(); ++i) {
        const auto& ca = q.plan.corners[i];
        const auto& cb = qBack.corners[i];
        EXPECT_EQ(ca.x, cb.x); EXPECT_EQ(ca.z, cb.z);
        EXPECT_EQ(ca.dx, cb.dx); EXPECT_EQ(ca.dz, cb.dz);
        EXPECT_EQ(ca.baseY, cb.baseY); EXPECT_EQ(ca.topY, cb.topY);
        EXPECT_EQ(ca.legLongMicro, cb.legLongMicro);
        EXPECT_EQ(ca.legShortMicro, cb.legShortMicro);
        EXPECT_EQ(ca.material, cb.material);
    }
}
