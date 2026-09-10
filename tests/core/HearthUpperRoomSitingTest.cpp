#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <climits>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/FurniturePlacer.h"
#include "core/HearthForge.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel::Core;

// ============================================================================
// Ravenmere G-69 (2026-09-09): the Raven's Rest lot was left EMPTY twice because the
// kitchen fireplace, sited per story by the furnish pass, put its stack in the middle
// of an upstairs chamber, and the realizer's routing gate (rightly) refused. Moving the
// entrance to the street wall (G-64) moved the stair, which moved the chambers under the
// flue. The gate is the last line; the FORGE must site hearths that clear the rooms
// above. RED half: per-story siting (the old path) produces violating stacks across a
// sweep of two-story taverns. GREEN: siteAllStories yields none, every program realizes,
// and no hearth is silently dropped.
// ============================================================================

namespace {

RoomProgramRegistry& registry() {
    static RoomProgramRegistry reg;
    static bool loaded = reg.loadFromFile("resources/room_program.json");
    (void)loaded;
    return reg;
}

// Two wall thicknesses: the timber tavern (0.333 m) and the STONE one (0.667 m) - the
// thick wall pushes an exterior hearth's flue one cube inward, which is how the real
// Ravenmere lot was refused on the upstairs landing after the first fix.
StyleProfile tavernStyle(double extWall) {
    StyleProfileRegistry reg;
    nlohmann::json j = nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone",
                           "hearth":"Bricks" },
            "roof": { "pitch":0.8 } } })");
    j["timber_cottage"]["thickness"]["exterior_wall"] = extWall;
    reg.loadFromJson(j);
    return *reg.get("timber_cottage");
}

BuildingProgram twoStoryTavern(int W, int D, const std::string& front, unsigned seed) {
    BuildingProgram p;
    p.name = "gen"; p.style = "timber_cottage"; p.typology = "tavern";
    p.footprintW = W; p.footprintD = D; p.substructure = "slab"; p.front = front;
    ProgStory g; g.height = 3; p.stories.push_back(g);
    ProgStory u; u.height = 3; p.stories.push_back(u);
    autofillRoomLayout(p, seed, registry().get("tavern"));
    return p;
}

int ventedFixtures(const ProgStory& st) {
    int n = 0;
    for (const auto& f : st.fixtures) if (HearthForge::isVented(f.type)) ++n;
    return n;
}

// The OLD path: each story sited on its own (stairs + stacks below reserved), no look
// upward. Returns how many stacks would rise through the middle of an upper room.
int violationsWithPerStorySiting(BuildingProgram p, int extT, int intT, const std::string& tier) {
    std::vector<Rect> stacksBelow;
    int violations = 0;
    for (size_t si = 0; si < p.stories.size(); ++si) {
        auto reserved = HearthForge::stairRectsForStory(p, static_cast<int>(si));
        reserved.insert(reserved.end(), stacksBelow.begin(), stacksBelow.end());
        ProgStory& st = p.stories[si];
        HearthForge::siteIntoProgram(st, {}, extT, intT, reserved, tier);
        int sx0 = INT_MAX, sz0 = INT_MAX, sx1 = INT_MIN, sz1 = INT_MIN;
        std::map<std::string, Rect> rooms;
        for (const auto& rm : st.rooms) {
            rooms[rm.id] = rm.rect;
            sx0 = std::min(sx0, rm.rect.x);   sz0 = std::min(sz0, rm.rect.z);
            sx1 = std::max(sx1, rm.rect.x1()); sz1 = std::max(sz1, rm.rect.z1());
        }
        const Rect fp{sx0, sz0, sx1 - sx0, sz1 - sz0};
        for (const auto& fx : st.fixtures) {
            if (!HearthForge::isVented(fx.type)) continue;
            const Rect stack = HearthForge::poseOf(fx, rooms[fx.room], fp, extT, intT).stackCubes;
            if (HearthForge::stackCrossesUpperRoomMiddle(stack, p, static_cast<int>(si))) ++violations;
            stacksBelow.push_back(stack);
        }
    }
    return violations;
}

} // namespace

TEST(HearthUpperRoomSitingTest, TwoStoryTavernsSiteTheirStacksClearOfTheChambersAbove) {
    ASSERT_NE(registry().get("tavern"), nullptr);
    FurniturePlacer::clearRecipes();
    const int intT = StructureRealizer::thicknessMicro(0.222);
    const std::string tier = registry().get("tavern")->wealthTier;

    int programs = 0, oldViolations = 0, newViolations = 0, realized = 0, hearthsBefore = 0, hearthsAfter = 0, unresolvedPrograms = 0, windowsOnHearths = 0;
    std::vector<std::string> failures, diagnostics;
    for (double extWall : {0.333, 0.667})
      for (int W : {7, 8, 9, 10})
        for (int D : {14, 16, 18, 20})
            for (const char* front : {"z1", "z0", "x0", "x1"})
                for (unsigned seed : {1u, 7u, 13u}) {
                    const int extT = StructureRealizer::thicknessMicro(extWall);
                    const StyleProfile style = tavernStyle(extWall);
                    BuildingProgram p = twoStoryTavern(W, D, front, seed);
                    if (p.stories.size() < 2 || p.stories[0].rooms.empty() || p.stories[1].rooms.empty()) continue;
                    ++programs;
                    oldViolations += violationsWithPerStorySiting(p, extT, intT, tier);

                    BuildingProgram q = p;
                    std::vector<std::string> notes;
                    hearthsAfter += HearthForge::siteAllStories(q, {}, extT, intT, tier, &notes);
                    bool unresolved = false;
                    for (const auto& n : notes)
                        if (n.find("LOSE") != std::string::npos || n.find("EXHAUSTED") != std::string::npos) unresolved = true;
                    if (unresolved) ++unresolvedPrograms;
                    // Every ground hearth must clear the rooms above...
                    for (size_t si = 0; si + 1 < q.stories.size(); ++si) {
                        const ProgStory& st = q.stories[si];
                        std::map<std::string, Rect> rooms;
                        int sx0 = INT_MAX, sz0 = INT_MAX, sx1 = INT_MIN, sz1 = INT_MIN;
                        for (const auto& rm : st.rooms) {
                            rooms[rm.id] = rm.rect;
                            sx0 = std::min(sx0, rm.rect.x);   sz0 = std::min(sz0, rm.rect.z);
                            sx1 = std::max(sx1, rm.rect.x1()); sz1 = std::max(sz1, rm.rect.z1());
                        }
                        const Rect fp{sx0, sz0, sx1 - sx0, sz1 - sz0};
                        for (const auto& fx : st.fixtures) {
                            if (!HearthForge::isVented(fx.type)) continue;
                            const Rect stack = HearthForge::poseOf(fx, rooms[fx.room], fp, extT, intT).stackCubes;
                            std::string which;
                            if (HearthForge::stackCrossesUpperRoomMiddle(stack, q, static_cast<int>(si), &which)) {
                                ++newViolations;
                                if (diagnostics.size() < 3) {
                                    std::string d = std::string(front) + " " + std::to_string(W) + "x" + std::to_string(D) +
                                        " seed " + std::to_string(seed) + ": " + fx.type + " in " + fx.room + " rect(" +
                                        std::to_string(rooms[fx.room].x) + "," + std::to_string(rooms[fx.room].z) + "," +
                                        std::to_string(rooms[fx.room].w) + "," + std::to_string(rooms[fx.room].d) + ") piece(" +
                                        std::to_string(fx.rect.x) + "," + std::to_string(fx.rect.z) + "," + std::to_string(fx.rect.w) + "," +
                                        std::to_string(fx.rect.d) + ") rot " + std::to_string(fx.rotation) + " stack(" +
                                        std::to_string(stack.x) + "," + std::to_string(stack.z) + "," + std::to_string(stack.w) + "," +
                                        std::to_string(stack.d) + ") -> " + which + "; upper rooms:";
                                    for (const auto& ur : q.stories[1].rooms)
                                        d += " " + ur.id + "(" + std::to_string(ur.rect.x) + "," + std::to_string(ur.rect.z) + "," +
                                             std::to_string(ur.rect.w) + "," + std::to_string(ur.rect.d) + ")";
                                    d += "; portals:";
                                    for (const auto& po : q.stories[0].portals)
                                        if (po.kind == "door") d += " " + po.a + "-" + po.b + "@(" + std::to_string(po.px) + "," + std::to_string(po.pz) + ")";
                                    for (const auto& n : notes) d += " | " + n;
                                    diagnostics.push_back(d);
                                }
                            }
                        }
                    }
                    // ...and no exterior window may remain on the wall span a hearth backs onto
                    // (the breast displaced it) - the forge's own rule: backDirFor + overlap.
                    {
                        std::map<std::string, Rect> rooms0;
                        for (const auto& rm : q.stories[0].rooms) rooms0[rm.id] = rm.rect;
                        for (const auto& fx : q.stories[0].fixtures) {
                            if (!HearthForge::isVented(fx.type) || !rooms0.count(fx.room)) continue;
                            const Rect& rm = rooms0[fx.room];
                            const glm::ivec3 bd = FurniturePlacer::backDirFor(rm, fx.rect);
                            for (const auto& po : q.stories[0].portals) {
                                if (po.kind != "window") continue;
                                const int w = std::max(1, po.width);
                                bool hit = false;
                                if (bd.x != 0 && po.px == (bd.x < 0 ? rm.x : rm.x1()))
                                    hit = po.pz < fx.rect.z + fx.rect.d && fx.rect.z < po.pz + w;
                                if (bd.z != 0 && po.pz == (bd.z < 0 ? rm.z : rm.z1()))
                                    hit = hit || (po.px < fx.rect.x + fx.rect.w && fx.rect.x < po.px + w);
                                if (hit) ++windowsOnHearths;
                            }
                        }
                    }
                    // ...and the hearth count must not drop (no silent "solved by deleting it").
                    BuildingProgram r = p;
                    std::vector<Rect> none;
                    for (auto& st : r.stories) hearthsBefore += HearthForge::siteIntoProgram(st, {}, extT, intT, none, tier);
                    // ...and the realizer must accept it (its gate shares the same rule).
                    auto sh = StructureRealizer::realizeShell(q, style);
                    if (sh.ok) ++realized;
                    else failures.push_back(std::string(front) + " " + std::to_string(W) + "x" + std::to_string(D) +
                                            " seed " + std::to_string(seed) + ": " + sh.error);
                }
    ASSERT_GT(programs, 0);
    // RED (recorded): before the furnish-pass fix this sweep measured 6 violations / 192 programs with
    // per-story siting - the Ravenmere refusal - and siteAllStories could not re-site any of them
    // (docs/structure-generation/WalkabilityGateAndPlaytestLoop.md, 2026-09-09). The fix moved
    // upstream (vented pieces prefer exterior walls and may displace a window), so the per-story
    // path is now clean too; the number is kept as a report, the invariants below are the gate.
    std::cout << "[hearth sweep] programs " << programs << ", per-story violations " << oldViolations
              << ", siteAllStories violations " << newViolations << ", unresolved " << unresolvedPrograms
              << ", windows on hearth walls " << windowsOnHearths << std::endl;
    // A stack the forge could not move off an upper room is REPORTED (unresolved), never
    // solved by dropping the hearth; the residue must be small and the improvement real.
    EXPECT_EQ(hearthsAfter, hearthsBefore) << "re-siting must not lose hearths";
    EXPECT_LE(newViolations, unresolvedPrograms) << "a violation may only remain where the forge said it was unresolved";
    EXPECT_EQ(newViolations, 0) << "a stack still rises through an upper room; e.g. " << (diagnostics.empty() ? "" : diagnostics.front());
    for (const auto& d : diagnostics) std::cout << "[hearth diag] " << d << std::endl;
    EXPECT_EQ(windowsOnHearths, 0) << "a window portal still sits on a hearth back wall";
    EXPECT_LE(unresolvedPrograms * 20, programs) << "more than 5% of programs unresolved: " << unresolvedPrograms << "/" << programs;
    EXPECT_GE(realized + unresolvedPrograms, programs) << "realizer refused " << failures.size() << " program(s); first: "
                                  << (failures.empty() ? "" : failures.front());
}
