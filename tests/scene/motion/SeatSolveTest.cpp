#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"
#include "scene/motion/MotionOracle.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "physics/VoxelDynamicsWorld.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::AnimatedCharacterState;
using namespace Phyxel::Scene::Motion;

// ============================================================================
// A4 — the seated POSE (docs/AnimationSystemV3Plan.md §4 A4). The six detectors of
// tools/interaction_pipeline/detectors.py (detect_seated_posture + POSITION_SNAP), ported with
// their calibrated thresholds, run on a real sit cycle in the FloorWorld: approach → stand_to_sit
// → sitting_idle → sit_to_stand. RED 2026-09-30 at the §1.4 numbers: knees 0.44 u below the hips
// (sitting_idle is not a seated pose), a ~0.5 u world snap at each clip boundary, feet not on the
// floor. The seat is a synthetic plane (no voxel seat collision) — stated rig delta.
// ============================================================================

namespace {

constexpr const char* kHumanoid = "resources/animated_characters/humanoid.anim";
constexpr float kDt = 1.0f / 60.0f;
constexpr float kFloorY = 16.0f;              // FloorWorld slab top

// detectors.py detect_seated_posture defaults
constexpr float kSeatHalfExtent   = 0.30f;
constexpr float kHipsYBelowTol    = 0.10f;
constexpr float kHipsYAboveTol    = 0.15f;
constexpr float kKneesForwardMin  = 0.10f;
constexpr float kKneesYTol        = 0.35f;
constexpr float kFeetBelowHipsMin = 0.30f;
constexpr float kFeetFloorTol     = 0.05f;    // FEET_ON_FLOOR: sole within 5 cm of the floor
constexpr float kPositionSnapTol  = 0.10f;    // POSITION_SNAP_AT_CLIP_BOUNDARY

struct FloorWorld {
    std::unique_ptr<Phyxel::Physics::PhysicsWorld> physics;
    ChunkManager cm;
    std::vector<std::unique_ptr<Phyxel::Physics::VoxelOccupancyGrid>> grids;
    FloorWorld() {
        physics = std::make_unique<Phyxel::Physics::PhysicsWorld>();
        physics->initialize();
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        auto owned = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
        owned->initializeForLoading();
        cm.chunkMap[glm::ivec3(0, 0, 0)] = owned.get();
        cm.chunks.push_back(std::move(owned));
        auto g = std::make_unique<Phyxel::Physics::VoxelOccupancyGrid>();
        g->setChunkOrigin(glm::ivec3(0, 0, 0));
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z) g->setCube(glm::ivec3(x, 15, z), true);
        physics->getVoxelWorld()->registerGrid(g.get());
        grids.push_back(std::move(g));
    }
    // an extra solid cube (a chair leg / stretcher / the seat block itself) next to the sitter
    void addSolidCube(const glm::ivec3& cell) {
        grids.front()->setCube(cell, true);   // same chunk grid as the floor (one grid per chunk)
    }
    std::unique_ptr<AnimatedVoxelCharacter> character(glm::vec3 pos = {16, 16.05f, 16}) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), pos);
        EXPECT_TRUE(ch->loadModel(kHumanoid));
        ch->setChunkManager(&cm);
        ch->setPhaseJitterSeed(0.0f);
        return ch;
    }
};

struct SitRun {
    std::vector<OracleFrame> frames;
    std::vector<AnimatedCharacterState> states;   // state after each recorded update
    std::vector<glm::vec3> positions;             // worldPosition after each recorded update
    int hips = -1, lKnee = -1, rKnee = -1, lFoot = -1, rFoot = -1;
    float standFootY = kFloorY;                   // foot JOINT height standing on this floor (the "sole on the floor" reference)
    float facingYaw = 0.0f;
    glm::vec3 anchor{0.0f};
};

// Sit on a synthetic seat of height `seatTop` above the floor, record the whole cycle.
SitRun runSitCycle(FloorWorld& w, float seatTopAboveFloor, int idleFrames = 90) {
    auto ch = w.character();
    for (int i = 0; i < 30; ++i) ch->update(kDt);             // ground + settle
    SitRun r;
    r.facingYaw = 0.0f;
    r.anchor = glm::vec3(16.0f, kFloorY + seatTopAboveFloor, 16.0f);
    const auto legs = ch->oracleLegChains();                   // (hip, knee, foot) per leg
    r.hips = ch->bodyPlanResolved().rootBoneId;
    if (legs.size() >= 2) { r.lKnee = (int)legs[0][1]; r.lFoot = (int)legs[0][2]; r.rKnee = (int)legs[1][1]; r.rFoot = (int)legs[1][2]; }
    // "Feet on the floor" is judged at the foot JOINT, so the reference is where that joint sits
    // while STANDING on this floor (the ankle is above the sole by the rig's foot height).
    ch->startOracleRecording(4);
    ch->update(kDt); ch->update(kDt);
    {
        const auto pre = ch->takeOracleFrames();
        if (!pre.empty() && r.lFoot >= 0 && r.rFoot >= 0)
            r.standFootY = 0.5f * (pre.back().worldJointPositions[r.lFoot].y + pre.back().worldJointPositions[r.rFoot].y);
    }
    ch->startOracleRecording(600);
    ch->sitAt(r.anchor, r.facingYaw, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), 0.2f, 0.0f);
    auto step = [&](int n) { for (int i = 0; i < n; ++i) { ch->update(kDt); r.states.push_back(ch->getAnimationState()); r.positions.push_back(ch->getPosition()); } };
    step(140);                                                 // stand_to_sit is 2.21 s
    step(idleFrames);
    ch->standUp();
    step(140);                                                 // sit_to_stand is 2.25 s
    step(20);
    r.frames = ch->takeOracleFrames();
    return r;
}

float forwardOf(const glm::vec3& d, float yaw) { return d.x * std::sin(yaw) + d.z * std::cos(yaw); }

} // namespace

TEST(SeatSolve, SeatedIdleSatisfiesTheSixDetectorsOnAStandardChair) {
    FloorWorld w;
    const auto r = runSitCycle(w, 0.45f);
    ASSERT_EQ(r.frames.size(), r.states.size());
    ASSERT_GE(r.hips, 0); ASSERT_GE(r.lKnee, 0); ASSERT_GE(r.lFoot, 0);

    float worstHipsY = 0.0f, worstHipsXZ = 0.0f, worstKneeY = 0.0f, worstKneeFwd = 1e9f, worstFeetFloor = 0.0f, worstFeetBelow = 1e9f;
    int seatedFrames = 0;
    for (size_t f = 0; f < r.frames.size(); ++f) {
        if (r.states[f] != AnimatedCharacterState::SittingIdle) continue;
        ++seatedFrames;
        const auto& J = r.frames[f].worldJointPositions;
        const glm::vec3 hips = J[r.hips];
        worstHipsY  = std::max(worstHipsY, std::fabs(hips.y - r.anchor.y) - (hips.y < r.anchor.y ? kHipsYBelowTol : kHipsYAboveTol));
        worstHipsXZ = std::max(worstHipsXZ, glm::length(glm::vec2(hips.x - r.anchor.x, hips.z - r.anchor.z)));
        for (int knee : {r.lKnee, r.rKnee}) {
            worstKneeY   = std::max(worstKneeY, std::fabs(J[knee].y - hips.y));
            worstKneeFwd = std::min(worstKneeFwd, forwardOf(J[knee] - hips, r.facingYaw));
        }
        for (int foot : {r.lFoot, r.rFoot}) {
            worstFeetFloor = std::max(worstFeetFloor, std::fabs(J[foot].y - r.standFootY));
            worstFeetBelow = std::min(worstFeetBelow, hips.y - J[foot].y);
        }
    }
    ASSERT_GT(seatedFrames, 30) << "the cycle must reach SittingIdle";
    std::printf("[SeatSolve] chair 0.45: hipsY excess %.3f | hips XZ %.3f | knee dY %.3f | knee fwd %.3f | feet-floor %.3f | feet below hips %.3f\n",
                worstHipsY, worstHipsXZ, worstKneeY, worstKneeFwd, worstFeetFloor, worstFeetBelow);
    EXPECT_LE(worstHipsY, 0.0f)               << "SEATED_HIPS_OFF_SEAT_Y (hips center within -0.10/+0.15 of the seat)";
    EXPECT_LE(worstHipsXZ, kSeatHalfExtent)   << "SEATED_HIPS_OFF_SEAT_XZ";
    EXPECT_LE(worstKneeY, kKneesYTol)         << "SEATED_THIGHS_NOT_HORIZONTAL (knees within 0.35 of hips height)";
    EXPECT_GE(worstKneeFwd, kKneesForwardMin) << "knees forward of the hips";
    EXPECT_LE(worstFeetFloor, kFeetFloorTol)  << "FEET_ON_FLOOR";
    EXPECT_GE(worstFeetBelow, kFeetBelowHipsMin) << "shins vertical: feet well below the hips";
}

TEST(SeatSolve, NoWorldSnapAtAnyClipBoundaryAcrossTheWholeCycle) {
    FloorWorld w;
    const auto r = runSitCycle(w, 0.45f);
    ASSERT_GE(r.hips, 0);
    float worstStep = 0.0f; size_t worstAt = 0;
    for (size_t f = 1; f < r.frames.size(); ++f) {
        const float d = glm::length(r.frames[f].worldJointPositions[r.hips] - r.frames[f - 1].worldJointPositions[r.hips]);
        if (d > worstStep) { worstStep = d; worstAt = f; }
    }
    std::printf("[SeatSolve] worst hips world step %.3f u at frame %zu (state %d)\n", worstStep, worstAt,
                worstAt < r.states.size() ? (int)r.states[worstAt] : -1);
    for (size_t f = (worstAt > 2 ? worstAt - 2 : 0); f < std::min(r.frames.size(), worstAt + 2); ++f) {
        const glm::vec3 h = r.frames[f].worldJointPositions[r.hips];
        const glm::vec3 p = f < r.positions.size() ? r.positions[f] : glm::vec3(0.0f);
        std::printf("[SeatSolve]   f%zu state %d hips (%.3f %.3f %.3f) worldPos (%.3f %.3f %.3f)\n", f,
                    f < r.states.size() ? (int)r.states[f] : -1, h.x, h.y, h.z, p.x, p.y, p.z);
    }
    EXPECT_LT(worstStep, kPositionSnapTol) << "POSITION_SNAP_AT_CLIP_BOUNDARY: the pelvis must not jump between clips";
}

TEST(SeatSolve, ATallAndALowSeatStillPlaceThePelvisOnTheSeatAndFeetTowardTheFloor) {
    // The fit band from SeatFit: leg ~0.9 → seats from 0.55 (knee rise 0.35) to 1.10 (foot drop 0.20).
    for (float top : {0.60f, 0.90f}) {
        FloorWorld w;
        const auto r = runSitCycle(w, top);
        float worstHipsY = 0.0f, worstFeetFloor = 0.0f; int seated = 0;
        for (size_t f = 0; f < r.frames.size(); ++f) {
            if (r.states[f] != AnimatedCharacterState::SittingIdle) continue;
            ++seated;
            const auto& J = r.frames[f].worldJointPositions;
            const glm::vec3 hips = J[r.hips];
            worstHipsY = std::max(worstHipsY, std::fabs(hips.y - r.anchor.y) - (hips.y < r.anchor.y ? kHipsYBelowTol : kHipsYAboveTol));
            for (int foot : {r.lFoot, r.rFoot}) worstFeetFloor = std::max(worstFeetFloor, std::fabs(J[foot].y - r.standFootY));
        }
        ASSERT_GT(seated, 30);
        std::printf("[SeatSolve] seat %.2f: hipsY excess %.3f | feet-floor %.3f\n", top, worstHipsY, worstFeetFloor);
        EXPECT_LE(worstHipsY, 0.0f) << "seat " << top;
        // Feet reach the floor when the leg can; on a seat near the leg length they dangle by
        // exactly the shortfall (SeatFit allows 0.20 of drop) — never a hover beyond it.
        const float allowed = (top <= 0.60f) ? kFeetFloorTol : 0.15f;
        EXPECT_LE(worstFeetFloor, allowed) << "seat " << top;
    }
}

// Lean + armrests: the same seat with and without affordances. The control (no affordances) pins
// that the metric measures the intended thing — no lean, no contacts, no joint near the armrest
// points; the treatment leans the spine by the backrest angle (head moves back) and puts a hand on
// each side's armrest.
namespace {
struct AffordanceOut { float lean = 0.0f; int contacts = 0; float farthestArmrest = 0.0f; float headBack = 0.0f; };

AffordanceOut sitWithAffordances(bool with, const glm::vec3& anchor, const std::vector<glm::vec3>& armrests) {
    FloorWorld w;
    auto ch = w.character();
    for (int i = 0; i < 30; ++i) ch->update(kDt);
    ch->sitAt(anchor, 0.0f, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), 0.2f, 0.0f);
    if (with) ch->setSeatAffordances(20.0f, armrests);
    for (int i = 0; i < 200; ++i) ch->update(kDt);            // deep into sitting_idle (w = 1)
    ch->startOracleRecording(4);
    ch->update(kDt); ch->update(kDt);
    const auto frames = ch->takeOracleFrames();
    AffordanceOut o;
    if (frames.empty()) return o;
    const auto& J = frames.back().worldJointPositions;
    o.lean = ch->seatSolve().leanDeg;
    o.contacts = ch->seatSolve().armrestContacts;
    for (const auto& p : armrests) {                            // nearest ANY joint to each point
        float best = 1e9f;
        for (const auto& j : J) best = std::min(best, glm::distance(j, p));
        o.farthestArmrest = std::max(o.farthestArmrest, best);
    }
    const int head = ch->headBoneId(), hips = ch->bodyPlanResolved().rootBoneId;
    if (head >= 0 && hips >= 0) o.headBack = -(J[head].z - J[hips].z);   // facing 0 = +Z; back = -Z
    return o;
}
} // namespace

TEST(SeatSolve, BackrestLeanAndArmrestsAreAppliedOnlyWhenTheSeatHasThem) {
    const glm::vec3 anchor(16.0f, kFloorY + 0.45f, 16.0f);
    const std::vector<glm::vec3> armrests = { anchor + glm::vec3( 0.25f, 0.25f, 0.15f),
                                              anchor + glm::vec3(-0.25f, 0.25f, 0.15f) };
    const auto ctrl  = sitWithAffordances(false, anchor, armrests);
    const auto treat = sitWithAffordances(true,  anchor, armrests);
    std::printf("[SeatSolve] control: lean %.1f contacts %d farthest-armrest-to-nearest-joint %.3f head back %.3f | "
                "treatment: lean %.1f contacts %d farthest %.3f head back %.3f\n",
                ctrl.lean, ctrl.contacts, ctrl.farthestArmrest, ctrl.headBack,
                treat.lean, treat.contacts, treat.farthestArmrest, treat.headBack);
    EXPECT_FLOAT_EQ(ctrl.lean, 0.0f);
    EXPECT_EQ(ctrl.contacts, 0);
    EXPECT_GT(ctrl.farthestArmrest, 0.08f) << "control: no joint rests on the armrest points";
    EXPECT_NEAR(treat.lean, 20.0f, 0.01f);
    EXPECT_EQ(treat.contacts, 2);
    EXPECT_LT(treat.farthestArmrest, 0.05f) << "each armrest point has a hand on it";
    EXPECT_GT(treat.headBack, ctrl.headBack + 0.05f) << "20 deg of lean moves the head back";
}

// Live 2026-09-30 (elf on chair_wood, CharacterTestbed): the ankles hung 0.38 u above the floor
// while the readback said feet error 0.000 — the per-ankle ground query (a 0.25 u column from a
// unit above the capsule) returned the CHAIR's own geometry (a rail / leg / the seat block) as
// "the floor", so the feet landed on the furniture and the knees rose above the hips. The seated
// feet go to the floor the character STOOD on; furniture near the ankles must not move that.
TEST(SeatSolve, FeetLandOnTheApproachFloorNotOnFurnitureNextToTheAnkles) {
    FloorWorld w;
    // solid block in the cell ahead of the seat where the seated ankles are (z 17..18), top at 17
    w.addSolidCube(glm::ivec3(16, 16, 17));
    auto ch = w.character();
    for (int i = 0; i < 30; ++i) ch->update(kDt);
    const auto legs = ch->oracleLegChains();
    ASSERT_GE(legs.size(), 2u);
    const int lFoot = (int)legs[0][2], rFoot = (int)legs[1][2], hips = ch->bodyPlanResolved().rootBoneId;
    ch->startOracleRecording(4); ch->update(kDt); ch->update(kDt);
    const auto pre = ch->takeOracleFrames();
    ASSERT_FALSE(pre.empty());
    const float standFootY = 0.5f * (pre.back().worldJointPositions[lFoot].y + pre.back().worldJointPositions[rFoot].y);

    // seat plane 0.45 above the floor, anchored so the feet fall in the blocked cell
    const glm::vec3 anchor(16.0f, kFloorY + 0.45f, 16.7f);
    ch->sitAt(anchor, 0.0f, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), 0.2f, 0.0f);
    for (int i = 0; i < 200; ++i) ch->update(kDt);
    ch->startOracleRecording(4); ch->update(kDt); ch->update(kDt);
    const auto fr = ch->takeOracleFrames();
    ASSERT_FALSE(fr.empty());
    const auto& J = fr.back().worldJointPositions;
    const float lErr = J[lFoot].y - standFootY, rErr = J[rFoot].y - standFootY;
    std::printf("[SeatSolve] furniture under the ankles: feet above the approach-floor reference L %.3f R %.3f (ankle z %.2f / %.2f, block z 17..18) | knee-hips dY %.3f\n",
                lErr, rErr, J[lFoot].z, J[rFoot].z, J[(int)legs[0][1]].y - J[hips].y);
    ASSERT_GT(J[lFoot].z, 17.0f) << "rig check: the ankles must actually be over the blocked cell";
    EXPECT_LT(std::fabs(lErr), kFeetFloorTol) << "left foot rests on the approach floor, not on the block";
    EXPECT_LT(std::fabs(rErr), kFeetFloorTol) << "right foot rests on the approach floor, not on the block";
    EXPECT_LT(ch->seatSolve().feetFloorError[0] + ch->seatSolve().feetFloorError[1], 0.02f) << "and the readback agrees with the measurement";
}
