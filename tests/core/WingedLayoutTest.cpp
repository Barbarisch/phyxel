#include <gtest/gtest.h>

#include <queue>
#include <set>

#include "core/RoomLayout.h"
#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// Winged (non-rectangular) layouts — generateWingedLayout("L") must produce an L-PLAN: rooms whose
// UNION leaves a notch the bounding box doesn't fill (so the realizer builds a non-rect shell), with
// the rooms tiling their wings, connected by doors, and one exterior entrance. L2 = the layout shape;
// L3 = the realized L-house is internally walkable wing-to-wing AND the notch is genuinely outside.
// ============================================================================

namespace {
StyleProfile cottageStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"slab",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}

// All cube cells covered by some room (the footprint union).
std::set<std::pair<int,int>> covered(const RoomLayout& rl) {
    std::set<std::pair<int,int>> s;
    for (const auto& r : rl.rooms)
        for (int x = r.rect.x; x < r.rect.x1(); ++x)
            for (int z = r.rect.z; z < r.rect.z1(); ++z) s.insert({x, z});
    return s;
}
}  // namespace

// L2 — the layout is a real L: rooms tile (no overlap), the union is non-rectangular (a notch is
// uncovered while the wings ARE covered), all rooms are connected, and there is one exterior entrance.
TEST(WingedLayoutTest, LShapeUnionIsNonRectangularAndConnected) {
    const int W = 12, D = 10;
    const RoomLayout rl = generateWingedLayout(W, D, "L", 1u);
    ASSERT_GE(rl.rooms.size(), 3u) << "an L-plan should have a main range (split) + a wing";

    // rooms don't overlap
    auto overlap = [](const Rect& a, const Rect& b) {
        return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
    };
    for (size_t i = 0; i < rl.rooms.size(); ++i)
        for (size_t j = i + 1; j < rl.rooms.size(); ++j)
            EXPECT_FALSE(overlap(rl.rooms[i].rect, rl.rooms[j].rect)) << "rooms " << i << "," << j << " overlap";

    const auto cov = covered(rl);
    EXPECT_LT(cov.size(), static_cast<size_t>(W * D)) << "footprint fills the whole box — not an L";
    // a NOTCH cell (far corner opposite the wing) must be UNCOVERED; a wing cell must be covered.
    EXPECT_EQ(cov.count({W - 1, D - 1}), 0u) << "the notch corner should be outside the L";
    EXPECT_EQ(cov.count({1, D - 1}), 1u)     << "the wing should extend to the deep edge";

    // connectivity: every room reachable from the exterior-entered room via interior doors
    int exteriorPortals = 0;
    std::map<std::string, std::vector<std::string>> g;
    std::string entry;
    for (const auto& p : rl.portals) {
        if (p.a == "exterior" || p.b == "exterior") { ++exteriorPortals; entry = (p.a == "exterior") ? p.b : p.a; continue; }
        g[p.a].push_back(p.b); g[p.b].push_back(p.a);
    }
    EXPECT_EQ(exteriorPortals, 1) << "exactly one exterior entrance";
    std::set<std::string> seen; std::queue<std::string> q; q.push(entry); seen.insert(entry);
    while (!q.empty()) { auto r = q.front(); q.pop(); for (auto& n : g[r]) if (seen.insert(n).second) q.push(n); }
    EXPECT_EQ(seen.size(), rl.rooms.size()) << "not every room is reachable through interior doors";
}

// L3 — realize the L-house and prove: (a) the NOTCH is genuinely outside (no finish floor there), and
// (b) a character walks from the far main room to the wing room (the L is internally navigable).
TEST(WingedLayoutTest, RealizedLHouseIsWalkableWingToWingAndNotchIsOutside) {
    const int W = 12, D = 10;
    const RoomLayout rl = generateWingedLayout(W, D, "L", 1u);
    ASSERT_GE(rl.rooms.size(), 3u);

    BuildingProgram p;
    p.name = "lhouse"; p.style = "timber_cottage"; p.footprintW = W; p.footprintD = D;
    p.substructure = "slab";
    ProgStory s; s.height = 3; s.rooms = rl.rooms; s.portals = rl.portals;
    p.stories.push_back(s);
    const auto shell = StructureRealizer::realizeShell(p, cottageStyle());
    ASSERT_TRUE(shell.ok) << shell.error;
    const int floorY = shell.floorTopByStory[0];
    const auto& cv = shell.canvas;

    // (a) the notch corner cube has NO finish floor just below the walk surface -> it is outside the L.
    EXPECT_FALSE(cv.occupiedMicro((W - 1) * 9 + 4, floorY - 1, (D - 1) * 9 + 4))
        << "the notch has a floor — the building was realized as a filled box, not an L";
    // sanity: a wing cube DOES have floor.
    EXPECT_TRUE(cv.occupiedMicro(1 * 9 + 4, floorY - 1, (D - 1) * 9 + 4)) << "the wing should have a floor";

    // (b) walk far-main-room -> wing-room through the interior doors.
    TraversalProbe probe([&](int x, int y, int z) { return cv.occupiedMicro(x, y, z); }, AgentBox{2, 16, 4});
    auto centerMicro = [](const Rect& r, int axis) { return (axis == 0 ? r.x + r.w / 2 : r.z + r.d / 2) * 9 + 4; };
    // main range rooms are at z<D/2; the wing room is the one reaching z=D-1.
    const ProgRoom* wing = nullptr; const ProgRoom* mainFar = nullptr;
    for (const auto& r : rl.rooms) {
        if (r.rect.z1() >= D) wing = &r;
        else if (!mainFar || r.rect.x > mainFar->rect.x) mainFar = &r;  // the main room farthest from the wing
    }
    ASSERT_TRUE(wing && mainFar);
    const glm::ivec3 start(centerMicro(mainFar->rect, 0), floorY, centerMicro(mainFar->rect, 1));
    const int gx = centerMicro(wing->rect, 0), gz = centerMicro(wing->rect, 1);
    const glm::ivec3 lo(0, floorY - 2, 0), hi(W * 9, floorY + 28, D * 9);
    EXPECT_TRUE(probe.reachable(start, glm::ivec3(gx - 2, floorY - 1, gz - 2),
                                glm::ivec3(gx + 2, floorY + 1, gz + 2), lo, hi))
        << "can't walk from the main range to the wing — the L-house isn't internally navigable";
}
