#include <gtest/gtest.h>

#include "core/NavGraph.h"
#include "core/ChunkManager.h"
#include "core/NPCManager.h"
#include "core/BuildingProgram.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"
#include "physics/PhysicsWorld.h"

#include <nlohmann/json.hpp>
#include <unordered_set>
#include <cstdint>
#include <cmath>

using namespace Phyxel;
using namespace Phyxel::Core;

// ============================================================================
// SUB-CUBE-AWARE NavGraph (Ravenmere G-54, 2026-09-08).
//
// RED (measured live, shipped town): `navgraph_path` street -> tavern interior
// `found:false` while the player walks in. The door column holds a Stone frame —
// two 1-micro jambs + a 2-micro lintel — around a 7-micro (0.78 m) x 16-micro
// (1.78 m) clear reveal, and the graph classified any non-EMPTY cube as solid,
// so EVERY framed door in every generated building was a wall to NPC navigation.
//
// These tests pin the fix at three depths:
//   A. a synthetic micro world (the door geometry the realizer produces),
//      with the legacy cube-granular oracle shown FAILING on the same world;
//   B. real chunk subcubes/microcubes through NPCManager::buildNavGrid() —
//      the exact runtime composite;
//   C. a realized typology (hall_house) where the runtime oracle must agree
//      with the settlement gate's TraversalProbe.
// ============================================================================

namespace {

// --- A. synthetic micro world --------------------------------------------------
struct MicroWorld {
    std::unordered_set<int64_t> solid;
    static int64_t key(int x, int y, int z) {
        return (static_cast<int64_t>(x + 100000) << 42) |
               (static_cast<int64_t>(y + 100000) << 21) |
                static_cast<int64_t>(z + 100000);
    }
    void fillMicroBox(int x0, int y0, int z0, int w, int h, int d) {
        for (int x = x0; x < x0 + w; ++x)
            for (int y = y0; y < y0 + h; ++y)
                for (int z = z0; z < z0 + d; ++z) solid.insert(key(x, y, z));
    }
    void carveMicroBox(int x0, int y0, int z0, int w, int h, int d) {
        for (int x = x0; x < x0 + w; ++x)
            for (int y = y0; y < y0 + h; ++y)
                for (int z = z0; z < z0 + d; ++z) solid.erase(key(x, y, z));
    }
    void fillCubeBox(int cx, int cy, int cz, int w, int h, int d) {
        fillMicroBox(cx * 9, cy * 9, cz * 9, w * 9, h * 9, d * 9);
    }
    bool micro(int x, int y, int z) const { return solid.count(key(x, y, z)) > 0; }
    CellFill fill(const glm::ivec3& c) const {
        int n = 0;
        for (int x = 0; x < 9; ++x)
            for (int y = 0; y < 9; ++y)
                for (int z = 0; z < 9; ++z) n += micro(c.x * 9 + x, c.y * 9 + y, c.z * 9 + z);
        if (n == 0)   return CellFill::Empty;
        if (n == 729) return CellFill::Solid;
        return CellFill::Partial;
    }
    /// The graph under test: sub-cube aware.
    std::unique_ptr<NavGraph> microGraph() const {
        return std::make_unique<NavGraph>(
            CellFillFunc([this](const glm::ivec3& c) { return fill(c); }),
            MicroQueryFunc([this](const glm::ivec3& m) { return micro(m.x, m.y, m.z); }));
    }
    /// The LEGACY oracle on the same world: any content in a cube = solid.
    std::unique_ptr<NavGraph> cubeGraph() const {
        return std::make_unique<NavGraph>(
            VoxelQueryFunc([this](const glm::ivec3& c) { return fill(c) != CellFill::Empty; }));
    }
};

// Ground cubes y=0 over x 0..6, z 0..4. A 6-micro wall band in cube column x=3
// (micro x 30..35), 3 cubes tall, spanning the full depth. Interior (x >= 4) has a
// 1/3 floor slab (micro y 9..11) — feet at 12 inside vs 9 outside (a 3-micro step).
// The door, when present, is a clear reveal in cell z=2: `clearW` micro wide
// (jambs fill the rest of the 9), `clearH` micro tall from the slab top, lintel above.
MicroWorld framedDoorScene(bool withDoor = true, int clearW = 7, int clearH = 18, int sillMicro = 0) {
    MicroWorld w;
    w.fillCubeBox(0, 0, 0, 7, 1, 5);          // ground
    w.fillMicroBox(36, 9, 0, 27, 3, 45);       // interior floor slab, cubes x 4..6
    w.fillMicroBox(30, 9, 0, 6, 27, 45);       // wall band, x micro 30..35, y 9..35
    if (withDoor) {
        const int jamb = (9 - clearW) / 2;     // 7 clear -> 1-micro jambs at offsets 0 and 8
        w.carveMicroBox(30, 12 + sillMicro, 18 + jamb, 6, clearH, clearW);
    }
    return w;
}

const glm::vec3 kOutside(1.5f, 1.0f, 2.5f);           // feet at micro 9 (ground top 8)
const glm::vec3 kInside(5.5f, 12.0f / 9.0f, 2.5f);     // feet at micro 12 (slab top 11)

bool passesDoorCell(const NavGraph::PathResult& r) {
    for (const auto& n : r.nodes) if (n.x == 3 && n.z == 2) return true;
    return false;
}

// --- B. real chunk voxels ------------------------------------------------------
// A 32x32 chunk: ground cubes at y=15 (stand at 16). A wall of subcubes at cube
// x=10 (subcube x 1..2 -> 6 micro thick, offsets 3..8), y 16..18, full depth.
// `door` carves cubes y 16..17 at z=15 down to two microcube jambs (z offsets 0
// and 8) — the realizer's frame; the y=18 subcubes stay as the lintel. Clear
// reveal: 7 micro wide x 18 micro tall.
struct ChunkDoorWorld {
    ChunkManager cm;
    explicit ChunkDoorWorld(bool door) {
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(0, 0, 0));
        owned->initializeForLoading();
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z) owned->addCube(glm::ivec3(x, 15, z));
        for (int z = 0; z < 32; ++z) {
            for (int y = 16; y <= 18; ++y) {
                const bool reveal = door && z == 15 && y <= 17;
                for (int sx = 1; sx <= 2; ++sx)
                    for (int sy = 0; sy < 3; ++sy)
                        for (int sz = 0; sz < 3; ++sz) {
                            if (!reveal) {
                                owned->addSubcube(glm::ivec3(10, y, z), glm::ivec3(sx, sy, sz), "StoneBricks");
                                continue;
                            }
                            // Jambs: a single micro column at z offset 0 (sz=0,mz=0) and 8 (sz=2,mz=2).
                            if (sz == 0)
                                for (int mx = 0; mx < 3; ++mx)
                                    for (int my = 0; my < 3; ++my)
                                        owned->addMicrocube(glm::ivec3(10, y, z), glm::ivec3(sx, sy, 0),
                                                            glm::ivec3(mx, my, 0), "Stone");
                            if (sz == 2)
                                for (int mx = 0; mx < 3; ++mx)
                                    for (int my = 0; my < 3; ++my)
                                        owned->addMicrocube(glm::ivec3(10, y, z), glm::ivec3(sx, sy, 2),
                                                            glm::ivec3(mx, my, 2), "Stone");
                        }
            }
        }
        cm.chunkMap[glm::ivec3(0, 0, 0)] = owned.get();
        cm.chunks.push_back(std::move(owned));
    }
};

bool crossesWall(const NavGraph::PathResult& r) {
    for (const auto& n : r.nodes) if (n.x == 10) return true;
    return false;
}

// --- C. realized typology --------------------------------------------------------
RoomProgram hallHouse() {
    RoomProgram rp;
    rp.name = "hall_house"; rp.bays = 4; rp.bayLength = 4; rp.widthMin = 6; rp.widthMax = 8;
    rp.rooms = {{"service", "service", 1.0}, {"hall", "hall", 2.0}, {"solar", "solar", 1.0}};
    return rp;
}
StyleProfile cottageStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}
BuildingProgram hallHouseProgram() {
    BuildingProgram p;
    p.name = "hh"; p.style = "timber_cottage"; p.footprintW = 16; p.footprintD = 7;
    p.substructure = "slab"; p.typology = "hall_house";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    const RoomProgram rp = hallHouse();
    autofillRoomLayout(p, 1u, &rp);
    return p;
}
const ProgRoom* roomByPurpose(const ProgStory& s, const std::string& purpose) {
    for (const auto& r : s.rooms) if (r.purpose == purpose) return &r;
    return nullptr;
}
struct CanvasWorld {
    const MicroCanvas& canvas;
    explicit CanvasWorld(const MicroCanvas& c) : canvas(c) {}
    CellFill fill(const glm::ivec3& c) const {
        int n = 0;
        for (int x = 0; x < 9; ++x)
            for (int y = 0; y < 9; ++y)
                for (int z = 0; z < 9; ++z) n += canvas.occupiedMicro(c.x * 9 + x, c.y * 9 + y, c.z * 9 + z);
        if (n == 0)   return CellFill::Empty;
        if (n == 729) return CellFill::Solid;
        return CellFill::Partial;
    }
    std::unique_ptr<NavGraph> graph() const {
        return std::make_unique<NavGraph>(
            CellFillFunc([this](const glm::ivec3& c) { return fill(c); }),
            MicroQueryFunc([this](const glm::ivec3& m) { return canvas.occupiedMicro(m.x, m.y, m.z); }));
    }
};
glm::vec3 roomCentreFeet(const ProgRoom& r, int floorTopMicro) {
    return glm::vec3(r.rect.x + r.rect.w / 2 + 0.5f, floorTopMicro / 9.0f, r.rect.z + r.rect.d / 2 + 0.5f);
}
bool probeWalks(const StructureRealizer::ShellResult& sh, const ProgRoom& from, const ProgRoom& to, int W, int D) {
    const int floorY = sh.floorTopByStory.empty() ? 12 : sh.floorTopByStory[0];
    TraversalProbe probe([&](int x, int y, int z) { return sh.canvas.occupiedMicro(x, y, z); }, AgentBox{2, 16, 4});
    const glm::ivec3 start((from.rect.x + from.rect.w / 2) * 9 + 4, floorY, (from.rect.z + from.rect.d / 2) * 9 + 4);
    const int gx = (to.rect.x + to.rect.w / 2) * 9 + 4, gz = (to.rect.z + to.rect.d / 2) * 9 + 4;
    return probe.reachable(start, glm::ivec3(gx - 2, floorY - 1, gz - 2), glm::ivec3(gx + 2, floorY + 1, gz + 2),
                           glm::ivec3(0, floorY - 2, 0), glm::ivec3(W * 9, floorY + 28, D * 9));
}

} // namespace

// ============================================================================
// A. synthetic framed door
// ============================================================================

// RED on the old model: the legacy cube oracle sees the door cell as solid and finds no
// way in. GREEN: the sub-cube-aware graph walks through the 0.78 m x 1.78 m reveal.
TEST(NavGraphMicroTest, FramedDoorIsPassableAtMicroResolution) {
    const MicroWorld w = framedDoorScene();
    NavAgentProfile agent;

    auto legacy = w.cubeGraph();
    legacy->buildRegion({0, 0}, {6, 4}, agent);
    EXPECT_FALSE(legacy->findPath(kOutside, kInside, agent).found)
        << "the RED half: the cube-granular oracle must treat the framed door as a wall";

    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    auto r = g->findPath(kOutside, kInside, agent);
    ASSERT_TRUE(r.found) << "sub-cube-aware graph could not path through a 7x16-micro door reveal";
    EXPECT_TRUE(passesDoorCell(r)) << "the path must go THROUGH the door cell, not around";
}

// Feet heights are exact micro: the slab top is 12/9 m, not a full cube up.
TEST(NavGraphMicroTest, StandingHeightsAreExactMicro) {
    const MicroWorld w = framedDoorScene();
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    const auto& outside = g->columnSurfaces(1, 2);
    const auto& inside  = g->columnSurfaces(5, 2);
    ASSERT_EQ(outside.size(), 1u); ASSERT_EQ(inside.size(), 1u);
    EXPECT_EQ(outside[0].floorTopMicro, 9);
    EXPECT_EQ(inside[0].floorTopMicro, 12);
    auto r = g->findPath(kOutside, kInside, agent);
    ASSERT_TRUE(r.found);
    EXPECT_NEAR(r.waypoints.back().y, 12.0f / 9.0f, 1e-5f);
    EXPECT_NEAR(r.waypoints.front().y, 1.0f, 1e-5f);
}

// TEETH — the same scene with the reveal sealed: the wall must block.
TEST(NavGraphMicroTest, SealedWallBlocks) {
    const MicroWorld w = framedDoorScene(/*withDoor=*/false);
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    EXPECT_FALSE(g->findPath(kOutside, kInside, agent).found) << "a 6-micro wall band let the agent through";
}

// TEETH — a reveal one micro too low for the 16-micro agent is not a door.
TEST(NavGraphMicroTest, DoorTooLowBlocks) {
    const MicroWorld w = framedDoorScene(true, 7, /*clearH=*/15);
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    EXPECT_FALSE(g->findPath(kOutside, kInside, agent).found) << "15-micro headroom must reject a 16-micro agent";
}

// TEETH — a reveal narrower than the footprint (5 micro) is not a door.
TEST(NavGraphMicroTest, DoorTooNarrowBlocks) {
    const MicroWorld w = framedDoorScene(true, /*clearW=*/3, 16);
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    EXPECT_FALSE(g->findPath(kOutside, kInside, agent).found) << "a 3-micro slit must not pass a 5-micro footprint";
}

// TEETH — a window (sill at 1 m) is not a door even though the opening is door-sized.
TEST(NavGraphMicroTest, WindowIsNotADoor) {
    const MicroWorld w = framedDoorScene(true, 7, 16, /*sillMicro=*/9);
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    EXPECT_FALSE(g->findPath(kOutside, kInside, agent).found) << "a 1 m sill exceeds the 4-micro step-up";
}

// THE REAL DOOR (Ravenmere tavern, column dump 2026-09-09): the interior 1/3 slab runs
// flush to the OUTER wall face, so the 3-micro riser sits exactly on the cell boundary and
// the reveal starts at the slab top. Outside feet 9, door cell feet 12, interior feet 12.
// A sweep that keeps the box at the low level until the boundary clips the slab edge with
// its trailing corners; the controller auto-steps there, so the edge must exist BOTH ways.
TEST(NavGraphMicroTest, StepAtTheOuterWallFaceWithASlabFlushToIt) {
    MicroWorld w;
    w.fillCubeBox(0, 0, 0, 7, 1, 5);          // ground
    w.fillMicroBox(27, 9, 0, 36, 3, 45);       // slab from the wall's outer face (x=27) inward
    w.fillMicroBox(27, 12, 0, 6, 24, 45);      // wall band x 27..32 ABOVE the slab
    w.carveMicroBox(27, 12, 19, 6, 18, 7);     // reveal: 7 wide, 18 tall (the realizer's door since G-77), from the slab top
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    // (the wall band's open top is a legitimate second, unreachable surface in the door column)
    ASSERT_GE(g->columnSurfaces(2, 2).size(), 1u);
    ASSERT_GE(g->columnSurfaces(3, 2).size(), 1u);
    EXPECT_EQ(g->columnSurfaces(2, 2)[0].floorTopMicro, 9);
    EXPECT_EQ(g->columnSurfaces(3, 2)[0].floorTopMicro, 12);
    EXPECT_EQ(g->columnSurfaces(2, 2)[0].edge[0], 0) << "doorstep -> door cell (+x) edge missing: the riser straddle";
    EXPECT_EQ(g->columnSurfaces(3, 2)[0].edge[1], 0) << "door cell -> doorstep (-x) edge missing";
    const glm::vec3 inside(5.5f, 12.0f / 9.0f, 2.5f);
    EXPECT_TRUE(g->findPath(kOutside, inside, agent).found) << "in";
    EXPECT_TRUE(g->findPath(inside, kOutside, agent).found) << "out";
    // TEETH: a riser taller than the auto step (5 micro) is still a wall.
    MicroWorld tall;
    tall.fillCubeBox(0, 0, 0, 7, 1, 5);
    tall.fillMicroBox(27, 9, 0, 36, 5, 45);
    tall.fillMicroBox(27, 14, 0, 6, 22, 45);
    tall.carveMicroBox(27, 14, 19, 6, 18, 7);
    auto g2 = tall.microGraph();
    g2->buildRegion({0, 0}, {6, 4}, agent);
    EXPECT_FALSE(g2->findPath(kOutside, glm::vec3(5.5f, 14.0f / 9.0f, 2.5f), agent).found)
        << "a 5-micro riser exceeds the 4-micro auto step";
}

// A 1-micro-thin wall sheet at a cell's far edge leaves the cell itself standable but
// must block the CROSSING into the next cell — the build-time edge sweep, not the
// per-cell footprint test, is what catches it.
TEST(NavGraphMicroTest, ThinWallAtCellEdgeBlocksTheCrossing) {
    MicroWorld w;
    w.fillCubeBox(0, 0, 0, 7, 1, 5);
    w.fillMicroBox(35, 9, 0, 1, 27, 45);       // 1-micro sheet at x offset 8 of cell x=3
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    ASSERT_EQ(g->columnSurfaces(3, 2).size(), 1u) << "the cell beside the sheet is standable";
    EXPECT_FALSE(g->findPath(kOutside, glm::vec3(5.5f, 1.0f, 2.5f), agent).found)
        << "the sweep from cell 3 to cell 4 must hit the 1-micro sheet";
    EXPECT_TRUE(g->findPath(kOutside, glm::vec3(3.5f, 1.0f, 2.5f), agent).found)
        << "walking UP TO the sheet is still fine";
}

// Path smoothing must not cut a sub-cube corner: with the door the only crossing, a
// straight line from outside to inside is never a "clear walk".
TEST(NavGraphMicroTest, SmoothingNeverShortcutsThroughAWall) {
    const MicroWorld w = framedDoorScene();
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    EXPECT_FALSE(g->hasClearWalk(glm::vec3(1.5f, 1.0f, 0.5f), glm::vec3(5.5f, 12.0f / 9.0f, 4.5f), agent));
    // The mechanism: the door crossing is a TIGHT edge (7-micro reveal, 5-micro footprint ->
    // 1 micro of lateral slack), open ground is fully slack.
    const auto& doorstep = g->columnSurfaces(2, 2);
    const auto& ground   = g->columnSurfaces(1, 2);
    ASSERT_EQ(doorstep.size(), 1u); ASSERT_EQ(ground.size(), 1u);
    EXPECT_EQ(doorstep[0].slack[0], 1) << "+x crossing into the door cell must be tight";
    EXPECT_EQ(ground[0].slack[0], 4)   << "open ground crossing must have full slack";
    EXPECT_TRUE(ground[0].openCell());
    EXPECT_FALSE(doorstep[0].openCell());
    auto r = g->findPath(glm::vec3(1.5f, 1.0f, 0.5f), glm::vec3(5.5f, 12.0f / 9.0f, 4.5f), agent);
    ASSERT_TRUE(r.found);
    auto smooth = g->smoothWaypoints(r.waypoints, agent);
    // Every consecutive smoothed span must itself be a clear walk (the invariant the
    // mover relies on), and wherever the polyline crosses the wall line (x=3) it must do so
    // inside the door's lateral slack: within 1 micro of the reveal's centre z=2.5.
    for (size_t i = 1; i < smooth.size(); ++i)
        EXPECT_TRUE(g->hasClearWalk(smooth[i - 1], smooth[i], agent)) << "span " << i;
    bool crossed = false;
    for (size_t i = 1; i < smooth.size(); ++i) {
        const glm::vec3& p = smooth[i - 1];
        const glm::vec3& q = smooth[i];
        const float lo = std::min(p.x, q.x), hi = std::max(p.x, q.x);
        for (float wallX : {3.0f, 4.0f}) {              // both faces of the door cell
            if (lo < wallX && hi > wallX) {
                const float t  = (wallX - p.x) / (q.x - p.x);
                const float zc = p.z + (q.z - p.z) * t;
                EXPECT_LE(std::fabs(zc - 2.5f) * 9.0f, 1.0f + 1e-3f) << "span " << i << " crosses x=" << wallX << " at z=" << zc;
                crossed = true;
            }
        }
    }
    EXPECT_TRUE(crossed) << "the smoothed path never crossed the wall line";
}

// Ravenmere G-79: movers advanced every waypoint at 0.5 m, but a waypoint kept at a tight
// crossing was proven by the sweep only within ~1 micro of the cell centre. The graph now
// reports an arrival radius per waypoint: tight (0.15 m) on a cell with a tight EXISTING crossing,
// loose (0.5 m) elsewhere - a missing edge (wall, region boundary) is not tightness;
// PathService fills it after smoothing.
TEST(NavGraphMicroTest, ArrivalRadiusIsTightAtTheDoorAndLooseOnOpenGround) {
    const MicroWorld w = framedDoorScene();
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    EXPECT_FLOAT_EQ(g->arrivalRadiusAt(glm::vec3(1.5f, 1.0f, 2.5f)), NavGraph::kArriveLoose) << "open ground";
    EXPECT_FLOAT_EQ(g->arrivalRadiusAt(glm::vec3(2.5f, 1.0f, 2.5f)), NavGraph::kArriveTight) << "doorstep";
    auto r = g->findPath(glm::vec3(1.5f, 1.0f, 0.5f), glm::vec3(5.5f, 12.0f / 9.0f, 4.5f), agent);
    ASSERT_TRUE(r.found);
    auto smooth = g->smoothWaypoints(r.waypoints, agent);
    auto radii  = g->arrivalRadii(smooth);
    ASSERT_EQ(radii.size(), smooth.size());
    bool anyTight = false;
    for (size_t i = 0; i < smooth.size(); ++i) {
        const bool inDoorBand = smooth[i].x >= 2.0f && smooth[i].x < 5.0f;   // doorstep / door / inside-step cells
        if (radii[i] == NavGraph::kArriveTight) anyTight = true;
        if (!inDoorBand) EXPECT_FLOAT_EQ(radii[i], NavGraph::kArriveLoose) << "waypoint " << i << " at x=" << smooth[i].x;
    }
    EXPECT_TRUE(anyTight) << "the smoothed route through the door kept no tight waypoint";
}

// Incremental rebuild: sealing the door after the build and rebuilding that column
// must remove the crossing (edges of the neighbours are re-swept too).
TEST(NavGraphMicroTest, RebuildColumnReSweepsNeighbourEdges) {
    MicroWorld w = framedDoorScene();
    NavAgentProfile agent;
    auto g = w.microGraph();
    g->buildRegion({0, 0}, {6, 4}, agent);
    ASSERT_TRUE(g->findPath(kOutside, kInside, agent).found);
    w.fillMicroBox(30, 12, 19, 6, 18, 7);      // brick the reveal up
    g->rebuildColumn(3, 2, agent);
    EXPECT_FALSE(g->findPath(kOutside, kInside, agent).found);
}

// ============================================================================
// B. real chunk voxels through the runtime composite (NPCManager::buildNavGrid)
// ============================================================================

TEST(NavGraphMicroTest, ChunkManagerMicroSamplerSeesJambsAndReveal) {
    ChunkDoorWorld w(/*door=*/true);
    // Jamb micro column at cube (10,16,15), subcube x=1, z offset 0 -> micro (90+3, 144, 135+0)
    EXPECT_TRUE(w.cm.occupiedMicro(glm::ivec3(93, 144, 135)));
    // Reveal centre: micro x 93, z offset 4 -> clear
    EXPECT_FALSE(w.cm.occupiedMicro(glm::ivec3(93, 144, 139)));
    // A full ground cube is solid at every micro; the air above is not.
    EXPECT_TRUE(w.cm.occupiedMicro(glm::ivec3(45, 15 * 9 + 8, 45)));
    EXPECT_FALSE(w.cm.occupiedMicro(glm::ivec3(45, 16 * 9, 45)));
    // A wall subcube (no door) covers its 27 micro cells.
    EXPECT_TRUE(w.cm.occupiedMicro(glm::ivec3(10 * 9 + 5, 16 * 9 + 4, 3 * 9 + 4)));
    EXPECT_EQ(w.cm.getVoxelTypeAt(glm::ivec3(10, 16, 3)), VoxelLocation::SUBDIVIDED);
}

TEST(NavGraphMicroTest, RuntimeCompositePathsThroughAFramedDoorOfRealVoxels) {
    NavAgentProfile agent;
    const glm::vec3 from(5.5f, 16.0f, 15.5f), to(20.5f, 16.0f, 15.5f);
    {
        ChunkDoorWorld w(/*door=*/true);
        NPCManager npc;
        npc.setChunkManager(&w.cm);
        npc.buildNavGrid();
        ASSERT_NE(npc.getNavGraph(), nullptr);
        ASSERT_TRUE(npc.getNavGraph()->microMode()) << "the runtime graph must be sub-cube aware";
        auto r = npc.getNavGraph()->findPath(from, to, agent);
        ASSERT_TRUE(r.found) << "NPCs could not path through a framed door built from real subcubes/microcubes";
        EXPECT_TRUE(crossesWall(r));
        bool viaDoor = false;
        for (const auto& n : r.nodes) if (n.x == 10 && n.z == 15) viaDoor = true;
        EXPECT_TRUE(viaDoor) << "the only crossing is the door at z=15";
    }
    {
        ChunkDoorWorld w(/*door=*/false);   // TEETH: the same wall with no reveal
        NPCManager npc;
        npc.setChunkManager(&w.cm);
        npc.buildNavGrid();
        ASSERT_NE(npc.getNavGraph(), nullptr);
        EXPECT_FALSE(npc.getNavGraph()->findPath(from, to, agent).found)
            << "a continuous subcube wall must still block";
    }
}

// A prop hanging over the doorway (the tavern sign) must not eat the door's headroom.
// The real-voxel door reveal is 18 micro tall (feet 144, y 16.0 .. 18.0); the 17-micro
// agent (AgentProfile.h) needs 144..160. A sign whose body occupies y 17.9..18.0 (micro 161) sits
// ABOVE the agent - the cube-rounded overlay marked cube 17 solid and sealed the door;
// the micro overlay must let it through. Lowered into the agent's band it must block.
TEST(NavGraphMicroTest, OverheadPropBoxDoesNotEatDoorHeadroom) {
    NavAgentProfile agent;
    const glm::vec3 from(5.5f, 16.0f, 15.5f), to(20.5f, 16.0f, 15.5f);
    ChunkDoorWorld w(/*door=*/true);
    NPCManager npc;
    npc.setChunkManager(&w.cm);
    npc.setNavObstacleProvider([]() {
        return std::vector<std::pair<glm::vec3, glm::vec3>>{
            {glm::vec3(9.0f, 17.9f, 14.0f), glm::vec3(12.0f, 18.0f, 17.0f)}};   // sign, 1 micro thick, above the 17-micro agent (feet 144 .. 160)
    });
    npc.buildNavGrid();
    ASSERT_NE(npc.getNavGraph(), nullptr);
    auto r = npc.getNavGraph()->findPath(from, to, agent);
    EXPECT_TRUE(r.found) << "a sign above the agent's head must not block the door";
    // The sign IS solid where it hangs (micro y 160,161 over the reveal), not below it.
    EXPECT_TRUE(npc.navObstacleMicro(glm::ivec3(93, 161, 139)));
    EXPECT_FALSE(npc.navObstacleMicro(glm::ivec3(93, 159, 139)));
    EXPECT_EQ(npc.navObstacleFill(glm::ivec3(10, 17, 15)), 1) << "partial, not a solid cube";

    npc.setNavObstacleProvider([]() {
        return std::vector<std::pair<glm::vec3, glm::vec3>>{
            {glm::vec3(9.0f, 17.4f, 14.0f), glm::vec3(12.0f, 18.0f, 17.0f)}};   // lowered into the band
    });
    npc.buildNavGrid();
    EXPECT_FALSE(npc.getNavGraph()->findPath(from, to, agent).found)
        << "a sign hanging into the 1.75 m band must block";
}

// ============================================================================
// C. realized typology: the runtime oracle must agree with the settlement gate
// ============================================================================

TEST(NavGraphMicroTest, RuntimeOracleAgreesWithTraversalProbeOnARealizedHouse) {
    BuildingProgram p = hallHouseProgram();
    const ProgRoom* service = roomByPurpose(p.stories[0], "service");
    const ProgRoom* solar   = roomByPurpose(p.stories[0], "solar");
    ASSERT_NE(service, nullptr); ASSERT_NE(solar, nullptr);
    auto sh = StructureRealizer::realizeShell(p, cottageStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    ASSERT_TRUE(probeWalks(sh, *service, *solar, p.footprintW, p.footprintD))
        << "precondition: the gate's probe walks service -> solar";

    CanvasWorld cw(sh.canvas);
    auto g = cw.graph();
    NavAgentProfile agent;
    g->buildRegion({-1, -1}, {p.footprintW, p.footprintD}, agent);
    const int floorTop = sh.floorTopByStory.empty() ? sh.floorTopMicro : sh.floorTopByStory[0];
    auto r = g->findPath(roomCentreFeet(*service, floorTop), roomCentreFeet(*solar, floorTop), agent);
    EXPECT_TRUE(r.found) << "the runtime NavGraph disagrees with the gate: service -> solar unpathable";

    // TEETH: drop the interior doors — both oracles must now say NO.
    auto& portals = p.stories[0].portals;
    std::vector<ProgPortal> exteriorOnly;
    for (const auto& po : portals)
        if (po.a == "exterior" || po.b == "exterior") exteriorOnly.push_back(po);
    portals = exteriorOnly;
    auto sealed = StructureRealizer::realizeShell(p, cottageStyle());
    ASSERT_TRUE(sealed.ok) << sealed.error;
    ASSERT_FALSE(probeWalks(sealed, *service, *solar, p.footprintW, p.footprintD));
    CanvasWorld cw2(sealed.canvas);
    auto g2 = cw2.graph();
    g2->buildRegion({-1, -1}, {p.footprintW, p.footprintD}, agent);
    const int floorTop2 = sealed.floorTopByStory.empty() ? sealed.floorTopMicro : sealed.floorTopByStory[0];
    EXPECT_FALSE(g2->findPath(roomCentreFeet(*service, floorTop2), roomCentreFeet(*solar, floorTop2), agent).found)
        << "sealed partitions must block the runtime oracle too";
}

// Ravenmere G-86 (run 43, found by WorldHealth): NPC 'Gerrit' spawned on the street at
// (40,18,16) beside a tree; buildNavGrid's relocation read the legacy NavGrid - the cell
// was "nearWall" (anything tall next to it) and the "safe" cell's surface was the TOPMOST
// voxel - and lifted him to (39.5,22,16.5), the canopy. Relocation must decide on the
// sub-cube NavGraph and keep the NPC at his own storey. RED on the old code: y rises to 22.
TEST(NavGraphMicroTest, AnNpcBesideATallTrunkStaysOnTheGround) {
    ChunkDoorWorld w(/*door=*/false);
    // A 1-cube trunk with a 3x3 canopy, 5 cubes tall, at (20,16..21,8): a street tree.
    for (int y = 16; y <= 20; ++y) w.cm.chunkMap.at(glm::ivec3(0, 0, 0))->addCube(glm::ivec3(20, y, 8));
    for (int x = 19; x <= 21; ++x) for (int z = 7; z <= 9; ++z) w.cm.chunkMap.at(glm::ivec3(0, 0, 0))->addCube(glm::ivec3(x, 21, z));
    Phyxel::Physics::PhysicsWorld physics;
    physics.initialize();
    NPCManager npc;
    npc.setChunkManager(&w.cm);
    npc.setPhysicsWorld(&physics);
    auto* gerrit = npc.spawnNPC("Gerrit", "resources/animated_characters/humanoid.anim",
                                glm::vec3(20.5f, 16.0f, 9.5f), NPCBehaviorType::Idle);
    ASSERT_NE(gerrit, nullptr);
    npc.buildNavGrid();
    const glm::vec3 p = gerrit->getPosition();
    EXPECT_LT(p.y, 17.5f) << "the NPC was lifted off the street: y=" << p.y;
    EXPECT_TRUE(npc.getNavGraph()->surfaceAt(p).valid()) << "the NPC must stand on a walker surface";
    EXPECT_NEAR(p.x, 20.5f, 1.6f); EXPECT_NEAR(p.z, 9.5f, 1.6f);
}
