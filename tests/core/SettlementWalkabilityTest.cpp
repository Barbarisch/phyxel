#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/FenceBuilder.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/SettlementLayout.h"
#include "core/SettlementProgram.h"
#include "core/SettlementWalkability.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// Settlement walkability — the L3 gate for "a generated town is walkable BY
// CONSTRUCTION" (standing user directive).
//
// SettlementTraversalTest already proves street -> interior on a TOY 2-plot grid
// with NO fences. This test raises it to the thing we actually ship:
//   * the REAL main-street planner (planMainStreetLayout) at the REAL village
//     tier preset from resources/settlement_program.json,
//   * REAL typologies drawn per plot, realized at their natural size with the
//     street-facing `front` the build handler assigns,
//   * and the REAL parcel FENCES — planParcelFenceRuns + planFenceProfile with
//     the same cube-aligned gate window the build_settlement stamper cuts.
//
// The fences are the point. Every plot is enclosed; the ONLY way in is the gate.
// If the gate doesn't line up with the door, or the yard between the fence and
// the front wall is too tight for the character box, the resident cannot get
// home -- and today that shows up only as NPCs jamming at runtime, which is a
// symptom, not a measurement.
//
// DISCLOSED SIMPLIFICATIONS (so this is not read as more than it proves):
//   * FLAT ground, and the ground top is aligned to the building floor top (the
//     same composition SettlementTraversalTest uses). This ISOLATES the geometric
//     question -- layout + fences + doors -- from terrain grading and from the
//     threshold step. A failure here is therefore unambiguously a GEOMETRY defect;
//     a pass here does NOT clear terrain-mode walkability, which needs the graded
//     surface and is a separate slice.
//   * Ground floor only; upstairs reach is TavernUpstairsTest's job.
//   * Occupancy is baked into a dense bitset over the settlement volume, so the
//     probe's flood is a bit test rather than a walk over 14 building canvases.
// ============================================================================

namespace {

// ---- shipped-data loaders (same multi-path probe the other settlement tests use) ----
bool loadSettlementProgram(SettlementProgramRegistry& reg) {
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
bool loadStyles(StyleProfileRegistry& reg) {
    for (const char* p : {"resources/structure_styles.json", "../resources/structure_styles.json",
                          "../../resources/structure_styles.json",
                          "../../../resources/structure_styles.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}

// The build handler's street-side -> `front` hint mapping (Application.cpp build_settlement).
std::string frontForSide(char side) {
    switch (side) {
        case 'S': return "z0";
        case 'N': return "z1";
        case 'W': return "x0";
        case 'E': return "x1";
        default: return "";
    }
}

struct RealizedBuilding {
    Rect footprint;                        // settlement-local cubes
    std::string typology, style;
    char streetSide = 0;
    StructureRealizer::ShellResult shell;  // realized at LOCAL origin
    BuildingProgram program;               // autofilled (room rects for goal centres)
    bool ok = false;
};

// Realize one assigned plot's building the way StructureBuildService::buildV2 does:
// declared typology drives autofill, `front` faces the street, natural footprint,
// slab substructure, one 3-cube story. `sealDoor` strips exterior portals (teeth).
RealizedBuilding realizeAssigned(const AssignedPlot& ap, const RoomProgramRegistry& rooms,
                                 const StyleProfile& style, const std::string& styleName,
                                 bool sealDoor) {
    RealizedBuilding rb;
    rb.footprint = ap.footprint;
    rb.typology = ap.typology;
    rb.style = styleName;
    rb.streetSide = ap.streetSide;

    BuildingProgram p;
    p.name = ap.typology;
    p.style = styleName;
    p.typology = ap.typology;
    p.footprintW = ap.footprint.w;
    p.footprintD = ap.footprint.d;
    p.substructure = "slab";
    p.front = frontForSide(ap.streetSide);
    ProgStory s;
    s.height = 3;
    p.stories.push_back(s);

    const RoomProgram* rp = rooms.get(ap.typology);
    // buildV2 derives the autofill seed from the WORLD position; the settlement origin is
    // (0,0) here, so this is the same derivation the real build performs.
    unsigned seed = (static_cast<unsigned>(ap.footprint.x) * 73856093u) ^
                    (static_cast<unsigned>(ap.footprint.z) * 19349663u) ^ 0x9e3779b9u;
    autofillRoomLayout(p, seed ? seed : 1u, rp);

    if (sealDoor) {
        std::vector<ProgPortal> interiorOnly;
        for (const auto& po : p.stories[0].portals)
            if (po.a != "exterior" && po.b != "exterior") interiorOnly.push_back(po);
        p.stories[0].portals = interiorOnly;
    }

    rb.program = p;
    rb.shell = StructureRealizer::realizeShell(p, style);
    rb.ok = rb.shell.ok;
    return rb;
}

// ---- composed settlement occupancy, baked dense --------------------------------
// A micro bitset over the whole settlement volume. Built by ITERATING the sources
// (ground fill, each building's occupied cells, each fence cell) rather than by
// probing every cell, so baking costs O(solid voxels).
class BakedOccupancy {
public:
    BakedOccupancy(int wCubes, int dCubes, int yLo, int yHi)
        : m_x0(0), m_z0(0), m_y0(yLo),
          m_w(wCubes * 9), m_h(yHi - yLo), m_d(dCubes * 9),
          m_bits(static_cast<size_t>(m_w) * m_h * m_d, false) {}

    void set(int x, int y, int z) {
        if (!inside(x, y, z)) return;
        m_bits[idx(x, y, z)] = true;
    }
    bool get(int x, int y, int z) const {
        // Outside the baked volume: solid BELOW the ground plane, air above. This keeps
        // the world closed (the agent can't walk off into undefined space and "reach" a
        // goal through the void) without pretending we know anything out there.
        if (!inside(x, y, z)) return y < m_groundTop;
        return m_bits[idx(x, y, z)];
    }
    /// Flat ground: solid everywhere below `topMicro`.
    void fillGround(int topMicro) {
        m_groundTop = topMicro;
        for (int y = m_y0; y < topMicro && y < m_y0 + m_h; ++y)
            for (int x = 0; x < m_w; ++x)
                for (int z = 0; z < m_d; ++z) m_bits[idx(x, y, z)] = true;
    }
    std::function<bool(int, int, int)> sampler() const {
        return [this](int x, int y, int z) { return get(x, y, z); };
    }

private:
    bool inside(int x, int y, int z) const {
        return x >= m_x0 && x < m_x0 + m_w && y >= m_y0 && y < m_y0 + m_h &&
               z >= m_z0 && z < m_z0 + m_d;
    }
    size_t idx(int x, int y, int z) const {
        return (static_cast<size_t>(y - m_y0) * m_w + (x - m_x0)) * m_d + (z - m_z0);
    }
    int m_x0, m_z0, m_y0, m_w, m_h, m_d;
    int m_groundTop = 0;
    std::vector<bool> m_bits;
};

// Stamp the parcel fences EXACTLY as build_settlement does (Application.cpp "fencing
// parcels"): micro-precise runs from planParcelFenceRuns, the canon picket profile, and
// a CUBE-ALIGNED gate window on the plot's street side. `withGate=false` omits the gate
// -- the negative control that proves this test measures the gate.
void stampFences(BakedOccupancy& occ, const MainStreetLayout& msl, int baseMicro,
                 bool withGate, long* cellsOut = nullptr) {
    // Canon picket dims. The shipped stamper reads object_dimensions.json and falls back
    // to these when the canon is absent; we use the fallbacks directly so the test does
    // not silently change meaning if that file moves.
    const int fH = 8, fSp = 16, fRails = 2;
    const int gateW = 2;  // cubes -- matches the stamper
    long cells = 0;

    for (size_t pi = 0; pi < msl.base.plots.size(); ++pi) {
        const Rect& pr = msl.base.plots[pi].rect;
        if (pr.w < 2 || pr.d < 2) continue;
        const char gate = (pi < msl.assigned.size()) ? msl.assigned[pi].streetSide : 0;

        for (const auto& run : planParcelFenceRuns(pr.x, pr.z, pr.w, pr.d)) {
            const int runLenMicro = run.toMicro - run.fromMicro;
            if (runLenMicro <= 0) continue;
            const FenceProfile prof =
                planFenceProfile(runLenMicro, fH, fSp, fRails, FenceType::Picket, 1, run.cornerPosts);
            if (!prof.ok) continue;

            int gLo = -1, gHi = -1;
            if (withGate && run.side == gate) {
                // Shared with the real stamper (Core::fenceGateWindow) rather than
                // reimplemented -- a validator holding its own copy of the rule it
                // validates stops checking anything the moment either copy drifts.
                fenceGateWindow(runLenMicro, gateW, gLo, gHi);
            }
            for (const auto& c : prof.cells) {
                if (c.u >= gLo && c.u < gHi) continue;  // the gate opening
                const int wx = run.alongX ? run.fromMicro + c.u : run.fixedMicro + c.w;
                const int wz = run.alongX ? run.fixedMicro + c.w : run.fromMicro + c.u;
                occ.set(wx, baseMicro + c.y, wz);
                ++cells;
            }
        }
    }
    if (cellsOut) *cellsOut = cells;
}

// Stamp the outdoor PLACED PROPS -- per-plot yard props (planYardProps) and the tier
// well on the main street's verge -- as the build handler sites them. These are real
// solid geometry in the world and they sit in the YARD and ON THE STREET, i.e. exactly
// where residents walk, so a walkability gate that omits them is measuring a village
// that was never built.
//
// Footprint source: the placer's OWN declared cube footprint (YardProp::w/d, which
// already accounts for its rotation). Height: the shipped asset's measured
// `.metrics.json` overall_max Y -- grounded in what actually gets stamped, not invented.
//   woodpile    1.78 x 1.00 x 0.56 m -> 9 micro tall, BLOCKS
//   garden_bed  2.00 x 0.33 x 1.00 m -> 3 micro tall, STEPPABLE (below the 4-micro step-up)
//   well        1.22 x 0.89 x 1.22 m -> 8 micro tall, BLOCKS
// Modelled as SOLID BOXES: a deliberate over-approximation. If a route survives props
// modelled as solid, it survives the real (hollower) props -- so a PASS is safe. A
// FAILURE here would need the real template voxels before being called a defect.
int propHeightMicro(const std::string& type) {
    if (type == "garden_bed") return 3;   // 0.333 m
    if (type == "woodpile") return 9;     // 1.0 m
    if (type == "well") return 8;         // 0.889 m
    return 9;                             // unknown prop: assume a blocker (conservative)
}

void stampYardProps(BakedOccupancy& occ, const MainStreetLayout& msl, int baseMicro, unsigned seed,
                    bool tierWell, long* propsOut = nullptr) {
    long n = 0;
    auto box = [&](int cx, int cz, int wCubes, int dCubes, int hMicro) {
        for (int x = cx * 9; x < (cx + wCubes) * 9; ++x)
            for (int z = cz * 9; z < (cz + dCubes) * 9; ++z)
                for (int y = baseMicro; y < baseMicro + hMicro; ++y) occ.set(x, y, z);
        ++n;
    };

    for (const auto& ap : msl.assigned)
        for (const auto& yp : planYardProps(ap, seed))
            box(yp.cx, yp.cz, yp.w, yp.d, propHeightMicro(yp.type));

    if (tierWell) {
        // Village: the main street's verge at mid-length (Application.cpp "yard props + well").
        int wcx, wcz;
        if (msl.hasSquare) {
            wcx = msl.marketSquare.x + msl.marketSquare.w / 2;
            wcz = msl.marketSquare.z + msl.marketSquare.d / 2;
        } else {
            const Rect& ms = msl.mainStreet;
            const bool msAlongX = ms.w >= ms.d;
            wcx = msAlongX ? ms.x + ms.w / 2 : ms.x + 1;
            wcz = msAlongX ? ms.z + 1 : ms.z + ms.d / 2;
        }
        box(wcx, wcz, 2, 2, propHeightMicro("well"));   // 1.22 m spans 2 cubes unaligned
    }
    if (propsOut) *propsOut = n;
}

// A whole composed settlement, ready to probe.
struct Village {
    bool ok = false;
    std::string why;
    MainStreetLayout msl;
    std::vector<RealizedBuilding> buildings;
    int floorY = 0;        // walk surface micro (== ground top == building floor top)
    int W = 0, D = 0;      // cubes
    std::unique_ptr<BakedOccupancy> occ;
    long fenceCells = 0;
    long props = 0;
};

// Compose the real village: plan -> realize every building -> bake ground + buildings +
// fences into one occupancy. `sealDoorIndex` >= 0 strips that building's exterior door.
std::unique_ptr<Village> composeVillage(int W, int D, unsigned seed, bool withGate,
                                        int sealDoorIndex = -1, bool withProps = false) {
    auto v = std::make_unique<Village>();
    v->W = W;
    v->D = D;

    SettlementProgramRegistry sreg;
    RoomProgramRegistry rreg;
    StyleProfileRegistry streg;
    if (!loadSettlementProgram(sreg) || !loadRooms(rreg) || !loadStyles(streg)) {
        v->why = "shipped generation data not found (settlement_program/room_program/structure_styles)";
        return v;
    }
    const SettlementTierPreset* village = sreg.get("medieval", "village");
    if (!village) {
        v->why = "medieval/village tier missing";
        return v;
    }
    const StyleProfile* sp = streg.get("timber_cottage");
    if (!sp) {
        v->why = "timber_cottage style missing";
        return v;
    }

    v->msl = planMainStreetLayout(*village, W, D, rreg, seed);
    if (!v->msl.ok || v->msl.assigned.empty()) {
        v->why = "planMainStreetLayout produced no assigned plots";
        return v;
    }

    for (size_t i = 0; i < v->msl.assigned.size(); ++i) {
        auto rb = realizeAssigned(v->msl.assigned[i], rreg, *sp, "timber_cottage",
                                  static_cast<int>(i) == sealDoorIndex);
        if (!rb.ok) {
            v->why = "realizeShell failed for plot " + std::to_string(i) + ": " + rb.shell.error;
            return v;
        }
        v->buildings.push_back(std::move(rb));
    }

    // The walk surface: every building is seated on the same flat ground, so its floor
    // top IS the ground top (the disclosed flat-world alignment).
    v->floorY = v->buildings[0].shell.floorTopByStory.empty()
                    ? 27
                    : v->buildings[0].shell.floorTopByStory[0];

    const int yLo = v->floorY - 9, yHi = v->floorY + 40;
    v->occ = std::make_unique<BakedOccupancy>(W, D, yLo, yHi);
    v->occ->fillGround(v->floorY);

    for (const auto& b : v->buildings) {
        const int ox = b.footprint.x * 9, oz = b.footprint.z * 9;
        for (const auto& c : b.shell.canvas.occupiedCells())
            v->occ->set(ox + c.x, c.y, oz + c.z);
    }
    stampFences(*v->occ, v->msl, v->floorY, withGate, &v->fenceCells);
    if (withProps)
        stampYardProps(*v->occ, v->msl, v->floorY, seed, village->pub.well, &v->props);

    v->ok = true;
    return v;
}

// The route every resident depends on: from the street DIRECTLY IN FRONT of the plot,
// into the building's first room. Starting in front of the plot (rather than at the far
// end of the street) keeps the question sharp -- "can you get in from your own frontage"
// -- and keeps the BFS bounded.
WalkRoute routeIntoBuilding(const Village& v, size_t i, glm::ivec3& boundLo, glm::ivec3& boundHi) {
    const RealizedBuilding& b = v.buildings[i];
    const Rect& plot = v.msl.base.plots[i].rect;
    const Rect& room0 = b.program.stories[0].rooms[0].rect;

    const int gx = (b.footprint.x + room0.x + room0.w / 2) * 9 + 4;
    const int gz = (b.footprint.z + room0.z + room0.d / 2) * 9 + 4;

    // Street-side start: one cube outside the plot's street edge, centred on its frontage.
    int sx = 0, sz = 0;
    switch (b.streetSide) {
        case 'S': sx = (plot.x + plot.w / 2) * 9 + 4; sz = (plot.z - 2) * 9 + 4; break;
        case 'N': sx = (plot.x + plot.w / 2) * 9 + 4; sz = (plot.z + plot.d + 1) * 9 + 4; break;
        case 'W': sx = (plot.x - 2) * 9 + 4;          sz = (plot.z + plot.d / 2) * 9 + 4; break;
        default:  sx = (plot.x + plot.w + 1) * 9 + 4; sz = (plot.z + plot.d / 2) * 9 + 4; break;
    }

    // Bound the search to this plot plus its frontage strip. A legitimate route
    // (street -> gate -> yard -> door -> room) never leaves this band, so the bound
    // cannot manufacture a false failure; it just keeps the flood tractable.
    const int pad = 6;
    boundLo = glm::ivec3(std::max(0, (plot.x - pad)) * 9, v.floorY - 2, std::max(0, (plot.z - pad)) * 9);
    boundHi = glm::ivec3(std::min(v.W, plot.x + plot.w + pad) * 9, v.floorY + 30,
                         std::min(v.D, plot.z + plot.d + pad) * 9);

    WalkRoute r;
    r.label = "plot " + std::to_string(i) + " (" + b.typology + ", side " +
              std::string(1, b.streetSide) + "): street -> interior";
    r.from = glm::ivec3(sx, v.floorY + 2, sz);
    r.to = glm::ivec3(gx, v.floorY + 2, gz);
    return r;
}

// Probe every building, each within its own bounded band. Returns a merged report.
WalkabilityReport probeAllPlots(const Village& v, bool diagnose = true) {
    WalkabilityReport merged;
    const AgentBox box;  // defaults == the engine character
    for (size_t i = 0; i < v.buildings.size(); ++i) {
        glm::ivec3 lo, hi;
        const WalkRoute r = routeIntoBuilding(v, i, lo, hi);
        const auto rep = checkRoutes(v.occ->sampler(), box, {r}, lo, hi, diagnose);
        for (const auto& rr : rep.routes) merged.routes.push_back(rr);
        merged.walkable += rep.walkable;
        merged.blocked += rep.blocked;
    }
    return merged;
}

// The interior centre of a building's first room, in world micro (agent feet).
glm::ivec3 interiorOf(const Village& v, size_t i) {
    const RealizedBuilding& b = v.buildings[i];
    const Rect& room0 = b.program.stories[0].rooms[0].rect;
    return glm::ivec3((b.footprint.x + room0.x + room0.w / 2) * 9 + 4, v.floorY + 2,
                      (b.footprint.z + room0.z + room0.d / 2) * 9 + 4);
}

}  // namespace

// ---------------------------------------------------------------------------
// The gate: every building in a real generated village is enterable from its own
// street frontage, THROUGH its parcel fence's gate.
// ---------------------------------------------------------------------------
TEST(SettlementWalkabilityTest, EveryPlotIsEnterableFromItsStreetFrontage) {
    auto v = composeVillage(60, 36, /*seed=*/3, /*withGate=*/true);
    ASSERT_TRUE(v->ok) << v->why;
    ASSERT_GE(v->buildings.size(), 3u) << "a village should plan at least a few plots";
    ASSERT_GT(v->fenceCells, 0) << "no fence was stamped -- this test would have no teeth";

    const auto rep = probeAllPlots(*v);
    EXPECT_TRUE(rep.ok()) << "a resident cannot walk from the street into their own building.\n"
                          << rep.summary();
}

// ---------------------------------------------------------------------------
// TEETH 1 — the gate is load-bearing. Stamp the same fences with NO gate opening
// and every plot must become unreachable. If plots stay reachable without a gate,
// the test above was passing through the fence and proves nothing.
// ---------------------------------------------------------------------------
TEST(SettlementWalkabilityTest, WithoutAGateEveryPlotIsSealed) {
    auto v = composeVillage(60, 36, /*seed=*/3, /*withGate=*/false);
    ASSERT_TRUE(v->ok) << v->why;
    ASSERT_GT(v->fenceCells, 0);

    // diagnose=false: every route is EXPECTED to fail here, and locating a pinch we
    // deliberately created costs two exhaustive floods per plot for no information.
    const auto rep = probeAllPlots(*v, /*diagnose=*/false);
    EXPECT_EQ(rep.walkable, 0)
        << "reached " << rep.walkable
        << " building(s) through a GATELESS fence -- the walkability probe is passing through "
           "the fence, so the positive test has no teeth";
}

// ---------------------------------------------------------------------------
// TEETH 2 — the DOOR is load-bearing too. Seal one building's exterior portals;
// exactly that one must become unreachable while its neighbours stay fine (so the
// probe is measuring per-building entry, not a global property).
// ---------------------------------------------------------------------------
TEST(SettlementWalkabilityTest, SealingOneDoorBlocksOnlyThatBuilding) {
    auto base = composeVillage(60, 36, /*seed=*/3, /*withGate=*/true);
    ASSERT_TRUE(base->ok) << base->why;
    const auto baseRep = probeAllPlots(*base);
    ASSERT_GT(baseRep.walkable, 1) << "need >1 reachable building for this control to mean anything";

    // Seal the first building that was reachable in the baseline.
    int target = -1;
    for (size_t i = 0; i < baseRep.routes.size(); ++i)
        if (baseRep.routes[i].walkable) { target = static_cast<int>(i); break; }
    ASSERT_GE(target, 0);

    auto sealed = composeVillage(60, 36, /*seed=*/3, /*withGate=*/true, target);
    ASSERT_TRUE(sealed->ok) << sealed->why;
    const auto rep = probeAllPlots(*sealed);

    ASSERT_LT(static_cast<size_t>(target), rep.routes.size());
    EXPECT_FALSE(rep.routes[target].walkable)
        << "building " << target << " has NO exterior door but was still reached";
    EXPECT_EQ(rep.walkable, baseRep.walkable - 1)
        << "sealing one door changed reachability for more than that one building";
}

// ---------------------------------------------------------------------------
// freeWidthMicro is the measurement the min-corridor rule is stated in, so it needs
// its own teeth: on open ground it reads wide, and a deliberate 1-cube slot reads
// narrow. Without this, a corridor-width assertion elsewhere could be measuring
// nothing.
// ---------------------------------------------------------------------------
TEST(SettlementWalkabilityTest, FreeWidthMeasuresAnActualSqueeze) {
    const int top = 27;
    BakedOccupancy occ(20, 20, top - 9, top + 30);
    occ.fillGround(top);
    const AgentBox box;

    const int openWidth = freeWidthMicro(occ.sampler(), box, glm::ivec3(90, top, 90), 'x');
    EXPECT_GT(openWidth, kMinCorridorWidthMicro)
        << "open ground did not measure as a wide corridor";

    // Two walls 9 micro (1 cube) apart, running along z: the classic 1-cube slot.
    for (int y = top; y < top + 20; ++y)
        for (int z = 0; z < 180; ++z) {
            occ.set(90, y, z);
            occ.set(100, y, z);
        }
    const int slot = freeWidthMicro(occ.sampler(), box, glm::ivec3(95, top, 90), 'x');
    EXPECT_GT(slot, 0) << "the box should still stand in the slot";
    EXPECT_LT(slot, kMinCorridorWidthMicro)
        << "a 1-cube slot measured >= the min corridor width (" << slot
        << " micro) -- freeWidthMicro is not measuring the squeeze";
}

// ---------------------------------------------------------------------------
// The RESIDENT route. Getting into your own house from your own frontage is the
// easy half; the schedule that produced the observed jam is "leave home, walk the
// street, enter the tavern". That crosses two gates, two doors and the full street
// spine -- so probe it end to end, over the WHOLE settlement (no per-plot band).
//
// This is the geometric half of the standing walkable-by-construction directive.
// If it holds, a runtime jam is NOT flat-ground layout geometry, and the search
// moves to terrain grading, furniture, or the mover -- which is exactly the kind of
// narrowing the validator exists to provide.
// ---------------------------------------------------------------------------
TEST(SettlementWalkabilityTest, AResidentCanWalkFromHomeAcrossTheVillageAndBackIn) {
    auto v = composeVillage(60, 36, /*seed=*/3, /*withGate=*/true);
    ASSERT_TRUE(v->ok) << v->why;
    ASSERT_GE(v->buildings.size(), 2u);

    // Destination: the tavern if this seed drew one (the evening stop), else the
    // building farthest along the street -- the longest route available.
    size_t dest = v->buildings.size() - 1;
    for (size_t i = 0; i < v->buildings.size(); ++i)
        if (v->buildings[i].typology == "tavern") { dest = i; break; }
    ASSERT_NE(dest, 0u);

    const glm::ivec3 home = interiorOf(*v, 0);
    const glm::ivec3 away = interiorOf(*v, dest);

    const glm::ivec3 lo(0, v->floorY - 2, 0);
    const glm::ivec3 hi(v->W * 9, v->floorY + 30, v->D * 9);

    WalkRoute out;
    out.label = "resident: " + v->buildings[0].typology + " (plot 0) -> " +
                v->buildings[dest].typology + " (plot " + std::to_string(dest) + ")";
    out.from = home;
    out.to = away;

    WalkRoute back;
    back.label = "resident: return trip (plot " + std::to_string(dest) + " -> plot 0)";
    back.from = away;
    back.to = home;

    const AgentBox box;
    const auto rep = checkRoutes(v->occ->sampler(), box, {out, back}, lo, hi);
    EXPECT_TRUE(rep.ok()) << "a resident cannot complete their daily route across the village.\n"
                          << rep.summary();
}

// ---------------------------------------------------------------------------
// The village AS BUILT — with the outdoor placed props in it.
//
// The previous tests compose buildings + fences only, but build_settlement also
// drops a woodpile and a garden bed in every rear toft and a WELL on the main
// street's verge. Those are solid geometry standing exactly where residents walk,
// and the well in particular has a history: it was recorded as nav-INVISIBLE and
// "walls straight paths". A walkability gate that omits them is measuring a
// village that was never built.
//
// Props are modelled as SOLID BOXES from the placer's own declared footprint and
// the shipped assets' measured heights -- a deliberate over-approximation, so a
// PASS here is strictly stronger than the real geometry requires.
// ---------------------------------------------------------------------------
TEST(SettlementWalkabilityTest, TheVillageIsStillWalkableWithYardPropsAndTheWell) {
    auto v = composeVillage(60, 36, /*seed=*/3, /*withGate=*/true, /*sealDoorIndex=*/-1,
                            /*withProps=*/true);
    ASSERT_TRUE(v->ok) << v->why;
    ASSERT_GT(v->props, 0) << "no props were stamped -- this test adds nothing over the base case";

    const auto rep = probeAllPlots(*v);
    EXPECT_TRUE(rep.ok()) << "yard props / the street well block a resident's route home.\n"
                          << rep.summary();

    // And the long route, which is the one that passes the well on the street verge.
    size_t dest = v->buildings.size() - 1;
    for (size_t i = 0; i < v->buildings.size(); ++i)
        if (v->buildings[i].typology == "tavern") { dest = i; break; }

    WalkRoute r;
    r.label = "resident with props present: plot 0 -> plot " + std::to_string(dest);
    r.from = interiorOf(*v, 0);
    r.to = interiorOf(*v, dest);

    const AgentBox box;
    const auto cross = checkRoutes(v->occ->sampler(), box, {r},
                                   glm::ivec3(0, v->floorY - 2, 0),
                                   glm::ivec3(v->W * 9, v->floorY + 30, v->D * 9));
    EXPECT_TRUE(cross.ok()) << "the cross-village route is blocked once props are placed.\n"
                            << cross.summary();
}
