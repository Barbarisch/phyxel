// LightWallMatrixTest.cpp — LAYER 1 of the light-leak rig: the DATA, no engine, no rendering.
//
// Sealed boxes (floor, four walls, roof, NO openings of any kind) that differ ONLY in wall
// thickness, with a light source sealed inside. Any light outside is a defect by definition.
//
// GROUNDING — this is the part that matters, and the first version of this file got it wrong.
// The walls are NOT hand-assembled out of voxel types I picked. They are painted into a real
// `MicroCanvas` at a thickness taken from `StructureRealizer::thicknessMicro()` applied to REAL
// style values out of structure_styles.json, and then `MicroCanvas::exportVoxels()` decides what
// voxel types come out. That is the same function the structure realizer calls, and it coalesces:
//
//     729 micro cells of a cube present and uniform -> one Cube
//     else per subcube, all 27 present and uniform  -> one Subcube
//     else                                           -> individual Microcubes
//
// So the resolution mix in the report below is the engine's own decision, not a test fixture's.
// (A 3-micro wall really does become Subcubes; a 2-micro wall really does stay Microcubes,
// because 18 of a subcube's 27 cells is not uniform — MicroCanvas.h says exactly this.)
//
// Two questions are asked per probe cell, deliberately kept apart:
//     OPAQUE  — does the engine's opacity data mark this shell cell as blocking? (lightOpaqueAt)
//     ESC     — did baked light from the sealed source actually reach the air cell outside it?
// OPAQUE set but ESC > 0 means the fault is downstream of the data. OPAQUE clear explains itself.
//
// This file states no verdict. It prints a table.

// ===========================================================================================
// ⚑ PARKED BY THE LIGHTING REBUILD (M0, 2026-08-29).
//
// The tests below marked DISABLED_ assert behaviour of the per-cell "flood" light field, which
// M0 DELETED (see ChunkRenderManager::rebuildCubeFaces). They are kept, not removed, because
// each states a REQUIREMENT the replacement has to meet:
//
//   * a sealed room admits no daylight            -> gate for M3 (sky as a traced emitter)
//   * a wall holds an interior light in           -> gate for M2 (traced point-light visibility)
//   * a sub-voxel roof/wall occludes at all       -> gate for M2 and M3
//
// Re-enable them as each milestone lands; they must pass against the new system unchanged, on
// the same geometry. If a replacement cannot satisfy one of these, that is a finding about the
// replacement, not a reason to weaken the test.
// ===========================================================================================

#include <gtest/gtest.h>

#include <array>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "graphics/ChunkRenderManager.h"
#include "graphics/VoxelLightOccupancy.h"   // the traced model these gates now assert against
#include "physics/VoxelOccupancyGrid.h"
#include "core/MicroCanvas.h"
#include "core/StructureRealizer.h"
#include "core/MaterialRegistry.h"
#include "core/Cube.h"

#include <filesystem>

using namespace Phyxel;
using namespace Phyxel::Graphics;
using Phyxel::Core::MaterialRegistry;
using Phyxel::Core::MicroCanvas;
using Phyxel::Core::CanvasRes;
using Phyxel::Core::StructureRealizer;

namespace {

// Sealed box over cube cells [kLo,kHi]^3 — small, one chunk, well clear of the chunk edge so
// nothing here is about chunk seams.
constexpr int kLo = 8;
constexpr int kHi = 13;
constexpr int kMid = 10;
constexpr int kSpanCubes = kHi - kLo + 1;

/// A wall thickness, expressed the way the generator expresses it: a style's thickness in CUBES,
/// put through the engine's own thicknessMicro().
struct WallKind {
    const char* label;      ///< what it is, in the engine's terms
    double styleCubes;      ///< the value a structure_styles.json entry would carry
};
const WallKind kWalls[] = {
    {"stone_keep   exterior_wall 3.000", 3.000},   // -> 9 micro
    {"stone_manor  exterior_wall 0.667", 0.667},   // -> 6 micro
    {"default      exterior_wall 0.333", 0.333},   // -> 3 micro
    {"timber_cott. exterior_wall 0.222", 0.222},   // -> 2 micro
    {"thinnest possible          0.111", 0.111},   // -> 1 micro
};

// Plain voxel records (the Subcube / Microcube classes were deleted in D3c-5; the traced tests
// below only need the exported geometry, which they turn into an occupancy grid).
struct SubV   { glm::ivec3 cube, sub; std::string material; };
struct MicroV { glm::ivec3 cube, sub, micro; std::string material; };
struct Room {
    std::vector<std::unique_ptr<Cube>>      cubes;
    std::vector<SubV>                       subs;
    std::vector<MicroV>                     micros;
    int nCube = 0, nSub = 0, nMicro = 0;   ///< what exportVoxels actually chose
};

/// Paint a sealed shell of thickness `t` micro into a MicroCanvas, then let the ENGINE decide
/// the voxel resolutions. Overlapping face fills give real corners, where two (or three) walls
/// meet inside one cube cell.
Room buildSealedBox(int t) {
    MicroCanvas canvas;
    const int g0 = kLo * 9;
    const int S  = kSpanCubes * 9;
    const int g1 = g0 + S;

    canvas.fillMicroBox(g0,     g0, g0, t, S, S, "Stone");   // -X
    canvas.fillMicroBox(g1 - t, g0, g0, t, S, S, "Stone");   // +X
    canvas.fillMicroBox(g0, g0,     g0, S, t, S, "Stone");   // -Y  floor
    canvas.fillMicroBox(g0, g1 - t, g0, S, t, S, "Stone");   // +Y  roof
    canvas.fillMicroBox(g0, g0, g0,     S, S, t, "Stone");   // -Z
    canvas.fillMicroBox(g0, g0, g1 - t, S, S, t, "Stone");   // +Z

    Room r;
    for (const auto& v : canvas.exportVoxels()) {
        switch (v.res) {
            case CanvasRes::Cube:
                r.cubes.push_back(std::make_unique<Cube>(v.cube, v.material));
                ++r.nCube;
                break;
            case CanvasRes::Subcube:
                r.subs.push_back({v.cube, v.sub, v.material});
                ++r.nSub;
                break;
            case CanvasRes::Microcube:
                r.micros.push_back({v.cube, v.sub, v.micro, v.material});
                ++r.nMicro;
                break;
        }
    }
    return r;
}

/// Same sealed shell, with ONE doorway carved through the -X wall at mid-height and mid-depth.
/// A sealed box cannot expose the defect below, because nothing ever gets light into the wall
/// line in the first place — which is exactly why the sealed-box tests all passed while the real
/// building leaked. Every real building has a door.
Room buildBoxWithDoorway(int t) {
    MicroCanvas canvas;
    const int g0 = kLo * 9;
    const int S  = kSpanCubes * 9;
    const int g1 = g0 + S;

    canvas.fillMicroBox(g0,     g0, g0, t, S, S, "Stone");
    canvas.fillMicroBox(g1 - t, g0, g0, t, S, S, "Stone");
    canvas.fillMicroBox(g0, g0,     g0, S, t, S, "Stone");
    canvas.fillMicroBox(g0, g1 - t, g0, S, t, S, "Stone");
    canvas.fillMicroBox(g0, g0, g0,     S, S, t, "Stone");
    canvas.fillMicroBox(g0, g0, g1 - t, S, S, t, "Stone");

    // Carve the doorway: painting AIR ("") erases cells (MicroCanvas.h). One cube wide, two
    // tall, through the -X wall at the box's mid depth.
    const int dz = kMid * 9;
    const int dy = (kLo + 1) * 9;
    canvas.fillMicroBox(g0, dy, dz, t, 18, 9, "");

    Room r;
    for (const auto& v : canvas.exportVoxels()) {
        switch (v.res) {
            case CanvasRes::Cube:
                r.cubes.push_back(std::make_unique<Cube>(v.cube, v.material)); ++r.nCube; break;
            case CanvasRes::Subcube:
                r.subs.push_back({v.cube, v.sub, v.material}); ++r.nSub; break;
            case CanvasRes::Microcube:
                r.micros.push_back({v.cube, v.sub, v.micro, v.material});
                ++r.nMicro; break;
        }
    }
    return r;
}

/// The three shell geometries a leak could differ between, plus the roof. `out` is the air cell
/// immediately outside the shell cell.
struct Probe {
    const char* name;
    glm::ivec3 shell;
    glm::ivec3 out;
};
const Probe kProbes[] = {
    {"face  ", {kMid, kMid, kLo}, {kMid,    kMid,    kLo - 1}},
    {"edge  ", {kLo,  kMid, kLo}, {kLo - 1, kMid,    kLo - 1}},   // wall/wall vertical edge
    {"corner", {kLo,  kLo,  kLo}, {kLo - 1, kLo - 1, kLo - 1}},   // wall/wall/floor corner
    {"roof  ", {kMid, kHi,  kMid}, {kMid,   kHi + 1, kMid}},
};

std::string findMaterialsJson() {
    for (const auto& p : {"resources/materials.json", "../resources/materials.json",
                          "../../resources/materials.json", "../../../resources/materials.json"})
        if (std::filesystem::exists(p)) return p;
    return "resources/materials.json";
}

int maxBlock(const ChunkRenderManager::BakedLight& b) {
    return std::max({static_cast<int>(b.r), static_cast<int>(b.g), static_cast<int>(b.b)});
}

std::string axesOf(bool have, uint8_t mask) {
    if (!have) return "?";
    if (mask == 0) return "-";
    std::string s;
    if (mask & 1u) s += "x";
    if (mask & 2u) s += "y";
    if (mask & 4u) s += "z";
    return s;
}

class LightWallMatrix : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(MaterialRegistry::instance().loadFromJson(findMaterialsJson()))
            << "materials.json did not load — the bake reads emissive/alpha off it";
    }
};

}  // namespace

// First, show that the rig is building what the generator would build. If this table does not
// match the coalescing rule, nothing below it means anything.
TEST_F(LightWallMatrix, ReportWhatTheEngineActuallyEmitsForEachWallThickness) {
    std::cout << "\n  Walls painted into a real MicroCanvas; resolutions chosen by "
                 "MicroCanvas::exportVoxels().\n\n";
    for (const auto& w : kWalls) {
        const int t = StructureRealizer::thicknessMicro(w.styleCubes);
        const Room r = buildSealedBox(t);
        std::cout << "    " << std::setw(34) << std::left << w.label << std::right
                  << " -> " << t << " micro   emitted: "
                  << std::setw(5) << r.nCube  << " Cube  "
                  << std::setw(5) << r.nSub   << " Subcube  "
                  << std::setw(7) << r.nMicro << " Microcube\n";
        EXPECT_GT(r.nCube + r.nSub + r.nMicro, 0) << "the canvas produced no geometry at all";
    }
    std::cout << "\n";
}

// ---------------------------------------------------------------------------------------------
// THE DARK BAND ALONG THE BOTTOM OF INTERIOR WALLS.
//
// User report (2026-08-28): "the bottom row of voxels on the wall in the interior wall surfaces
// are unusually dark even though the lighting should be consistent." Measured live with debug
// view 3 (baked skylight, NO albedo): upper interior wall 239.6/255, wall/floor junction 3.6/255.
// So it is in the light field, not the texture.
//
// The per-cell values do NOT explain it: the interior AIR cells beside the floor and one cube
// higher carry the same skylight (measured live: y18 and y19 both ~12 at the same column). So the
// darkness must come from the PER-CORNER averaging that turns cell values into vertex light --
// "the light of the four cells touching that corner in the air cell's plane is averaged; SOLID
// cells read 0" (LightingPipeline.md sec.2). A wall face's bottom corners touch the floor, which
// is solid, so they average in zeros.
//
// This reads the actual per-corner nibbles out of the emitted faces, which is the only way to see
// it: it is invisible in the per-cell field and only appears once the corners are averaged.
// ---------------------------------------------------------------------------------------------
// U7 (docs/UnifiedLightingPlan.md): DISABLED_InteriorWallFaceCornersAtTheFloorAverageInSolidZeros
// was REMOVED here 2026-09-01. It read InstanceData::light2 / light3 -- the per-corner BLOCK-light
// words -- to measure the wall-base black band. Those fields no longer exist: block light was
// pinned to 0 from M0 onward, U3.2 made emissive voxels real point lights instead, and U7 deleted
// the two attributes (InstanceData 24 -> 16 bytes). The band it investigated was a property of the
// per-cell flood, which is gone. Kept as a note rather than a silently deleted test.

// =============================================================================================
// THE GATES, RE-ENABLED AGAINST THE TRACED MODEL (2026-08-30).
//
// The DISABLED_ tests above were parked at M0 with a note to myself: "Re-enable them as each
// milestone lands; they must pass against the new system unchanged, on the same geometry."
// M2 and M3 then landed and I gated them on ad-hoc rigs built in a populated test project
// instead — which produced a dead control, wrong probe heights and refused rig voxels. These are
// the gates the plan actually specified, on the geometry it specified.
//
// The assertions are new because the old ones measured the deleted flood; the GEOMETRY is
// unchanged, and it is better grounded than anything hand-placed: thicknesses come from
// StructureRealizer::thicknessMicro() applied to real structure_styles.json values, and
// MicroCanvas::exportVoxels() picks the voxel resolutions.
// =============================================================================================

namespace {

/// Convert a built Room into the occupancy the lighting actually consumes, mirroring what
/// ChunkPhysicsManager derives from chunk content: cubes are solid, subcubes and microcubes mark
/// their parent subdivided. Going through the exported voxels (rather than re-painting the canvas)
/// keeps the engine's own resolution choices in the loop.
Phyxel::Graphics::PackedOccupancyPool poolFromRoom(const Room& r) {
    Phyxel::Physics::VoxelOccupancyGrid g;
    g.setChunkOrigin({0, 0, 0});
    for (const auto& c : r.cubes) g.setCube(c->getPosition(), true);
    for (const auto& s : r.subs) {
        g.setCube(s.cube, true);
        g.markSubdivided(s.cube, true);
        g.setSubcube(s.cube, s.sub, true);
    }
    for (const auto& m : r.micros) {
        g.setCube(m.cube, true);
        g.markSubdivided(m.cube, true);
        g.setSubcube(m.cube, m.sub, true);
        g.markSubcubeSubdivided(m.cube, m.sub, true);
        g.setMicrocube(m.cube, m.sub, m.micro, true);
    }
    return Phyxel::Graphics::packOccupancyPool(
        {{glm::ivec3{0, 0, 0}, Phyxel::Graphics::buildLightOccupancy(g)}},
        Phyxel::Graphics::PackedOccupancyPool::boxMinChunkFor(glm::vec3(0.0f)));
}

/// Centre of the sealed room's interior, in world units.
glm::vec3 roomCentre() {
    return glm::vec3(kMid + 0.5f, kMid + 0.5f, kMid + 0.5f);
}

}  // namespace

// M2 GATE — "the sealed box shows zero exterior contribution at every wall thickness INCLUDING
// corners, with positive controls firing." A light sealed inside must not reach any outside face.
TEST(LightWallMatrixTraced, M2_SealedBoxLeaksNoLightAtAnyWallThicknessIncludingCorners) {
    const glm::vec3 inside = roomCentre();

    for (const auto& w : kWalls) {
        const int t = StructureRealizer::thicknessMicro(w.styleCubes);
        const Room room = buildSealedBox(t);
        const auto pool = poolFromRoom(room);

        // Probe OUTSIDE faces and OUTSIDE CORNERS. Corners are called out because that is where
        // the user actually saw the original defect, and where two or three walls share one cell.
        struct Probe { const char* name; glm::vec3 pos; glm::vec3 n; };
        const float lo = static_cast<float>(kLo), hi = static_cast<float>(kHi + 1);
        const Probe probes[] = {
            {"-X face",   {lo - 0.5f, kMid + 0.5f, kMid + 0.5f}, { 1, 0, 0}},
            {"+X face",   {hi + 0.5f, kMid + 0.5f, kMid + 0.5f}, {-1, 0, 0}},
            {"-Y floor",  {kMid + 0.5f, lo - 0.5f, kMid + 0.5f}, { 0, 1, 0}},
            {"+Y roof",   {kMid + 0.5f, hi + 0.5f, kMid + 0.5f}, { 0,-1, 0}},
            {"-Z face",   {kMid + 0.5f, kMid + 0.5f, lo - 0.5f}, { 0, 0, 1}},
            {"+Z face",   {kMid + 0.5f, kMid + 0.5f, hi + 0.5f}, { 0, 0,-1}},
            {"-X-Z corner", {lo - 0.5f, kMid + 0.5f, lo - 0.5f}, { 1, 0, 1}},
            {"+X+Z corner", {hi + 0.5f, kMid + 0.5f, hi + 0.5f}, {-1, 0,-1}},
            {"-X-Y-Z corner", {lo - 0.5f, lo - 0.5f, lo - 0.5f}, { 1, 1, 1}},
            {"+X+Y+Z corner", {hi + 0.5f, hi + 0.5f, hi + 0.5f}, {-1,-1,-1}},
        };

        for (const auto& p : probes) {
            const auto v = Phyxel::Graphics::packedPoolLightVisibility(
                pool, p.pos, glm::normalize(p.n), inside);
            EXPECT_FALSE(v.visible)
                << w.label << " (" << t << " micro): light sealed inside REACHED the outside "
                << p.name << " — this is the reported defect";
        }

        // POSITIVE CONTROL, at the same thickness: move the light OUTSIDE, just off the -X face.
        // A test that only ever asserts "blocked" passes just as well when the march is broken.
        const glm::vec3 outsideLight{lo - 2.0f, kMid + 0.5f, kMid + 0.5f};
        const auto ctrl = Phyxel::Graphics::packedPoolLightVisibility(
            pool, {lo - 0.5f, kMid + 0.5f, kMid + 0.5f}, {1, 0, 0}, outsideLight);
        EXPECT_TRUE(ctrl.visible)
            << w.label << " (" << t << " micro): the CONTROL failed — an outside light could not "
            << "reach the outside face, so the zeros above prove nothing";
    }
}

// M3-REDESIGN GATE — the same sealed-room requirement, but at the settings the BAKE ACTUALLY
// SHIPS WITH (5 rays, 16 u reach), not at full probe quality.
//
// Per-fragment sky tracing was correct and unshippable (24.6 ms/frame, 275 -> 35 fps), so it moved
// to a chunk-bake. The bake trades ray count and reach for speed — 39.89 ms -> 14.38 ms per chunk —
// and a cheaper ray set is exactly the kind of change that quietly stops a room reading as sealed.
// So the shipped configuration gets its own gate rather than inheriting the probe's.
TEST(LightWallMatrixTraced, M3REDESIGN_BakeSettingsStillSealARoomAtEveryWallThickness) {
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    constexpr float kBakeReach = 16.0f;   // must match RenderCoordinator's injected query
    constexpr int   kBakeRays  = 5;

    for (const auto& w : kWalls) {
        const int t = StructureRealizer::thicknessMicro(w.styleCubes);
        const auto sealedPool = poolFromRoom(buildSealedBox(t));
        const float floorTop = static_cast<float>(kLo) + 1.0f;

        const float vSealed = Phyxel::Graphics::packedPoolSkyVisibility(
            sealedPool, {kMid + 0.5f, floorTop, kMid + 0.5f}, up, kBakeReach, kBakeRays);
        EXPECT_EQ(vSealed, 0.0f)
            << w.label << " (" << t << " micro): a SEALED room admitted sky AT THE BAKE'S SETTINGS "
            << "(" << kBakeRays << " rays, " << kBakeReach << " u) even though it seals at full "
            << "quality — the cost reduction broke the thing the feature exists to do";

        // CONTROL — an opening must still admit sky at these settings. Without it a "0" above
        // would only prove the cheaper ray set stopped finding the sky at all.
        // ⚠️ This is the sharp end of the cost reduction: at full quality the single ray that
        // threaded this doorway was a 60-degree DIAGONAL, and the diagonals are exactly what the
        // 5-ray set drops. If this fails, the bake seals rooms by being blind, not by being right.
        const auto doorPool = poolFromRoom(buildBoxWithDoorway(t));
        const float nearDoor = Phyxel::Graphics::packedPoolSkyVisibility(
            doorPool, {static_cast<float>(kLo) + 0.5f, floorTop, kMid + 0.5f}, up,
            kBakeReach, kBakeRays);
        EXPECT_GT(nearDoor, 0.0f)
            << w.label << ": at the bake's " << kBakeRays << "-ray set a DOORWAY admitted no sky. "
            << "The room is sealed because the tracer cannot see out, not because it is enclosed.";
    }
}

// M3 GATE — "sealed box reads black; a box with one doorway is lit near the opening and falls off;
// the interior/exterior ordering holds." Same geometry, same thicknesses.
TEST(LightWallMatrixTraced, M3_SealedBoxSeesNoSkyAndADoorwayAdmitsItWithFalloff) {
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const float floorTop = static_cast<float>(kLo) + 1.0f;   // above the floor shell

    for (const auto& w : kWalls) {
        const int t = StructureRealizer::thicknessMicro(w.styleCubes);

        // 1. SEALED — no sky anywhere inside, at every wall thickness down to 1 micro.
        const Room sealedRoom = buildSealedBox(t);
        const auto sealedPool = poolFromRoom(sealedRoom);

        // Before asserting anything about light, prove the SHELL SURVIVED the trip into the
        // occupancy. A zero-or-nonzero light result cannot distinguish "the trace is wrong" from
        // "the geometry was never there", and at 1 micro that distinction is the whole question.
        {
            const int gTop = (kHi + 1) * 9 - 1;          // topmost micro row of the roof
            const int gSide = kLo * 9;                   // outermost micro column of the -X wall
            EXPECT_TRUE(Phyxel::Graphics::packedPoolSolidAt(
                sealedPool, {kMid * 9 + 4, gTop, kMid * 9 + 4}))
                << w.label << " (" << t << " micro): the ROOF is not in the occupancy at all";
            EXPECT_TRUE(Phyxel::Graphics::packedPoolSolidAt(
                sealedPool, {gSide, kMid * 9 + 4, kMid * 9 + 4}))
                << w.label << " (" << t << " micro): the -X WALL is not in the occupancy at all";
            std::cout << "    [" << w.label << "] exported " << sealedRoom.nCube << " cubes, "
                      << sealedRoom.nSub << " subcubes, " << sealedRoom.nMicro << " microcubes\n";
        }
        const float vSealed = Phyxel::Graphics::packedPoolSkyVisibility(
            sealedPool, {kMid + 0.5f, floorTop, kMid + 0.5f}, up);
        EXPECT_EQ(vSealed, 0.0f)
            << w.label << " (" << t << " micro): a SEALED room saw sky — a "
            << t << "-micro roof did not occlude";

        // 2. DOORWAY — the same room with one opening. Near the door must see sky; the far side
        //    must see less. This is the falloff the flood could not produce.
        //    ⚠️ The probe must be ON the threshold, centred in the opening. The doorway is ONE cube
        //    wide and two tall, and the sampling hemisphere tilts at most 60 degrees off vertical,
        //    so a floor probe set even 1.5 cubes back cannot thread it — no ray both clears the
        //    lintel and stays within the opening's width. That is real geometry, not a defect; a
        //    probe placed further in measures a wall, which is what my first version did.
        const auto doorPool = poolFromRoom(buildBoxWithDoorway(t));
        const float nearDoor = Phyxel::Graphics::packedPoolSkyVisibility(
            doorPool, {static_cast<float>(kLo) + 0.5f, floorTop, kMid + 0.5f}, up);
        const float farSide = Phyxel::Graphics::packedPoolSkyVisibility(
            doorPool, {static_cast<float>(kHi) - 0.5f, floorTop, kMid + 0.5f}, up);

        EXPECT_GT(nearDoor, 0.0f)
            << w.label << ": a doorway admitted NO sky — the opening is not being seen at all";
        EXPECT_GE(nearDoor, farSide)
            << w.label << ": sky did not fall off with distance from the doorway (near "
            << nearDoor << " vs far " << farSide << ")";
        EXPECT_LT(farSide, 1.0f)
            << w.label << ": the far side of a roofed room saw the FULL sky";

        std::cout << "  " << w.label << " (" << t << " micro): sealed " << vSealed
                  << "  near-door " << nearDoor << "  far " << farSide << "\n";
    }
}

// ============================================================================
// THE FLUSH-EMITTER LEAK (user report 2026-09-22: "point light sources shining through
// structure walls"). phxLightVisibility excludes the emitter's own body by measuring the
// solid run around the light and stopping the shadow ray there. A torch mounted ON a wall
// shares one solid run with that wall, so the exclusion swallows the wall too and the light
// reaches straight through it.
//
// The three sealed-box tests above cannot see this: every one of them puts the light in open
// interior air. Mounting a light on a wall is how rooms are actually lit.
// ============================================================================

namespace {

// A sealed box whose -X wall carries a light FLUSH against its inner face, the way a wall
// sconce sits. `gapMicro` pushes the emitter that many micro cells off the wall: 0 = flush
// (the defect), >=1 = free-standing (the control).
Room buildSealedBoxWithMountedLight(int t, int gapMicro, glm::vec3& lightWorldOut) {
    MicroCanvas canvas;
    const int g0 = kLo * 9;
    const int S  = kSpanCubes * 9;
    const int g1 = g0 + S;

    canvas.fillMicroBox(g0,     g0, g0, t, S, S, "Stone");
    canvas.fillMicroBox(g1 - t, g0, g0, t, S, S, "Stone");
    canvas.fillMicroBox(g0, g0,     g0, S, t, S, "Stone");
    canvas.fillMicroBox(g0, g1 - t, g0, S, t, S, "Stone");
    canvas.fillMicroBox(g0, g0, g0,     S, S, t, "Stone");
    canvas.fillMicroBox(g0, g0, g1 - t, S, S, t, "Stone");

    // The emitter is SOLID - that is the whole reason the run-length exclusion exists.
    const int ex = g0 + t + gapMicro;              // first micro cell clear of the wall
    const int ey = kMid * 9, ez = kMid * 9;
    canvas.fillMicroBox(ex, ey, ez, 1, 1, 1, "glow");
    lightWorldOut = glm::vec3((ex + 0.5f) / 9.0f, (ey + 0.5f) / 9.0f, (ez + 0.5f) / 9.0f);

    Room r;
    for (const auto& v : canvas.exportVoxels()) {
        switch (v.res) {
            case CanvasRes::Cube:
                r.cubes.push_back(std::make_unique<Cube>(v.cube, v.material)); ++r.nCube; break;
            case CanvasRes::Subcube:
                r.subs.push_back({v.cube, v.sub, v.material}); ++r.nSub; break;
            default:
                r.micros.push_back({v.cube, v.sub, v.micro, v.material});
                ++r.nMicro; break;
        }
    }
    return r;
}

}  // namespace

// DISABLED because it FAILS - it is the reproduction of G-157, not a regression guard yet.
// Enabling it is the definition of done for that fix. Measured 2026-09-22, it leaks at EVERY
// wall thickness in the matrix, from 1 micro up to a 3-cube stone keep wall, while the
// control (the same light one micro cell off the wall) is correctly blocked at all of them.
// The emitter here is ONE microcube, so its own half-extent is the bounding-sphere radius
// of a 1/9 cube: sqrt(3)/18 = 0.0962 u. That is what ChunkRenderManager now reports for
// such a light, and it is the whole fix: the march may exclude that much solid around the
// light and no more, so the wall behind it is still tested however thin it is.
constexpr float kOneMicroExtent = 0.0962f;

TEST(LightWallMatrixTraced, M4_ALightMountedFlushOnAWallDoesNotShineThroughIt) {
    for (const auto& w : kWalls) {
        const int t = StructureRealizer::thicknessMicro(w.styleCubes);

        // CONTROL FIRST, at this same thickness: the same light one micro cell OFF the wall
        // must be blocked. Without it, a "blocked" result below could just mean the march is
        // broken and returns zero for everything.
        glm::vec3 freeLight{};
        const Room freeRoom = buildSealedBoxWithMountedLight(t, 1, freeLight);
        const auto freePool = poolFromRoom(freeRoom);
        const auto ctrl = Phyxel::Graphics::packedPoolLightVisibility(
            freePool, {static_cast<float>(kLo) - 0.5f, kMid + 0.5f, kMid + 0.5f},
            {1, 0, 0}, freeLight, kOneMicroExtent);
        EXPECT_FALSE(ctrl.visible)
            << w.label << " (" << t << " micro): CONTROL failed - a light one micro cell off "
            << "the wall already leaks, so the flush result below proves nothing";

        // THE CASE: flush against the inner face, like a sconce.
        glm::vec3 flushLight{};
        const Room flushRoom = buildSealedBoxWithMountedLight(t, 0, flushLight);
        const auto flushPool = poolFromRoom(flushRoom);
        const auto v = Phyxel::Graphics::packedPoolLightVisibility(
            flushPool, {static_cast<float>(kLo) - 0.5f, kMid + 0.5f, kMid + 0.5f},
            {1, 0, 0}, flushLight, kOneMicroExtent);
        EXPECT_FALSE(v.visible)
            << w.label << " (" << t << " micro): a light mounted FLUSH on the inside of the "
            << "wall lit the OUTSIDE face - the emitter's solid run merged with the wall's "
            << "and the exclusion swallowed the wall (user report: lights shine through walls)";
    }
}
