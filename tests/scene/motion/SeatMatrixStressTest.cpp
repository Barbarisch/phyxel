#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"
#include "scene/AppearancePresetRegistry.h"
#include "scene/SeatFit.h"
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
using Phyxel::Scene::AppearancePresetRegistry;
using namespace Phyxel::Scene::Motion;
namespace Fit = Phyxel::Scene::SeatFit;

// ============================================================================
// A4 STRESS — "100 seats, 100 poses" (docs/AnimationSystemV3Plan.md §4 A4). The dimension that
// scales is SEAT HEIGHT against BODY SIZE: 34 synthetic seat planes from 0.20 to 1.30 u × three
// presets (halfling / standard / goliath) = 102 cells. Every cell is judged individually:
//   - the seat-fit policy (engine-side SeatFit, the same rules sit_character enforces) says
//     FIT or REFUSED for this body on this seat;
//   - FIT cells run the whole sit cycle and must satisfy the seated detectors — hips exactly on
//     the seat, feet on the floor (ankle inside the 0..0.15 u band above it, or dangling by no
//     more than the geometric shortfall — never sunk), knees level with the hips wherever the
//     shin leaves room for level thighs (seat <= shin + ankle + 0.30; above that the legs hang
//     and the knees must simply be BELOW the hips), no world snap at any clip boundary;
//   - REFUSED cells are never sat on (accuracy over coverage); one refused-TALL cell per preset
//     is sat on anyway as the CONTROL, and must show the feet hanging well off the floor — the
//     refusal is protecting against a real failure, not a number.
// Rig deltas: 60 Hz, flat floor, seat planes (no voxel seat collision), one seat width/depth.
// ============================================================================

namespace {

constexpr const char* kHumanoid = "resources/animated_characters/humanoid.anim";
constexpr float kDt = 1.0f / 60.0f;
constexpr float kFloorY = 16.0f;
constexpr float kHipsYBelowTol = 0.10f, kHipsYAboveTol = 0.15f, kSeatHalfExtent = 0.30f;
constexpr float kKneesYTol = 0.35f, kFeetFloorTol = 0.05f, kPositionSnapTol = 0.10f;

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
    std::unique_ptr<AnimatedVoxelCharacter> character(const char* presetId) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), glm::vec3(16.0f, 16.05f, 16.0f));
        if (presetId) {
            auto& presets = AppearancePresetRegistry::instance();
            presets.ensureLoaded();
            const auto* preset = presets.getPreset(presetId);
            EXPECT_NE(preset, nullptr) << presetId;
            if (preset) ch->setAppearance(*preset);
        }
        EXPECT_TRUE(ch->loadModel(kHumanoid));
        ch->setChunkManager(&cm);
        ch->setPhaseJitterSeed(0.0f);
        return ch;
    }
};

struct CellResult {
    int seatedFrames = 0;
    float hipsYExcess = 0.0f, hipsXZ = 0.0f, kneeDY = 0.0f, feetFloor = 0.0f, worstStep = 0.0f;
    // one seated sample for diagnosis
    float hipsY = 0.0f, kneeY = 0.0f, footY = 0.0f, standFootY = 0.0f, shin = 0.0f, readbackFeet = 0.0f;
};

// Run a whole sit cycle on a seat plane `top` above the floor; judge the seated frames.
CellResult sitCycle(FloorWorld& w, const char* preset, float top) {
    auto ch = w.character(preset);
    for (int i = 0; i < 30; ++i) ch->update(kDt);
    const auto legs = ch->oracleLegChains();
    const int hips = ch->bodyPlanResolved().rootBoneId;
    std::vector<int> knees, feet;
    for (const auto& l : legs) { knees.push_back((int)l[1]); feet.push_back((int)l[2]); }
    // standing foot-joint height on this floor = the "sole on the floor" reference
    ch->startOracleRecording(4); ch->update(kDt); ch->update(kDt);
    float standFootY = kFloorY, shin = 0.0f;
    { const auto pre = ch->takeOracleFrames();
      if (!pre.empty() && !feet.empty()) { standFootY = pre.back().worldJointPositions[feet[0]].y;
                                          shin = glm::distance(pre.back().worldJointPositions[knees[0]], pre.back().worldJointPositions[feet[0]]); } }

    const glm::vec3 anchor(16.0f, kFloorY + top, 16.0f);
    std::vector<AnimatedCharacterState> states;
    ch->startOracleRecording(400);
    ch->sitAt(anchor, 0.0f, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), 0.2f, 0.0f);
    auto step = [&](int n) { for (int i = 0; i < n; ++i) { ch->update(kDt); states.push_back(ch->getAnimationState()); } };
    step(140); step(40);
    CellResult r;
    r.readbackFeet = ch->seatSolve().feetFloorError[0];
    ch->standUp(); step(150);
    const auto frames = ch->takeOracleFrames();
    r.standFootY = standFootY; r.shin = shin;
    if (hips < 0 || frames.size() != states.size()) return r;
    { const auto& J = frames[179].worldJointPositions; r.hipsY = J[hips].y; r.kneeY = J[knees[0]].y; r.footY = J[feet[0]].y; }
    for (size_t f = 0; f < frames.size(); ++f) {
        const auto& J = frames[f].worldJointPositions;
        if (f > 0) r.worstStep = std::max(r.worstStep, glm::length(J[hips] - frames[f - 1].worldJointPositions[hips]));
        if (states[f] != AnimatedCharacterState::SittingIdle) continue;
        ++r.seatedFrames;
        const glm::vec3 h = J[hips];
        r.hipsYExcess = std::max(r.hipsYExcess, std::fabs(h.y - anchor.y) - (h.y < anchor.y ? kHipsYBelowTol : kHipsYAboveTol));
        r.hipsXZ = std::max(r.hipsXZ, glm::length(glm::vec2(h.x - anchor.x, h.z - anchor.z)));
        for (int k : knees) r.kneeDY = std::max(r.kneeDY, std::fabs(J[k].y - h.y));
        for (int ft : feet) r.feetFloor = std::max(r.feetFloor, std::fabs(J[ft].y - standFootY));
    }
    return r;
}

Fit::CharacterFitMetrics measure(FloorWorld& w, const char* preset) {
    auto ch = w.character(preset);
    for (int i = 0; i < 30; ++i) ch->update(kDt);
    return Fit::measureCharacter(*ch);
}

} // namespace

TEST(SeatMatrixStress, EveryFitCellSeatsCorrectlyAndEveryRefusedCellIsRefusedForAReason) {
    const char* presets[] = {"halfling", nullptr, "goliath"};
    const char* names[]   = {"halfling", "standard", "goliath"};
    int fitCells = 0, refusedCells = 0, cells = 0;
    for (int p = 0; p < 3; ++p) {
        FloorWorld w;
        const auto m = measure(w, presets[p]);
        std::printf("[SeatMatrix] %-8s leg %.3f hip_w %.3f height %.3f\n", names[p], m.leg_length, m.hip_width, m.total_height);
        ASSERT_GT(m.leg_length, 0.3f) << names[p];
        bool controlDone = false;
        for (int i = 0; i < 34; ++i) {
            const float top = 0.20f + i * (1.10f / 33.0f);
            ++cells;
            Fit::SeatFeatures seat;
            seat.seat_top_y = top; seat.seat_width_x = 0.6f; seat.seat_depth_z = 0.6f;
            const auto issues = Fit::evaluate(m, seat);
            const bool refused = Fit::refused(issues);
            if (refused) {
                ++refusedCells;
                bool tall = false, low = false;
                for (const auto& is : issues) { tall |= is.ruleId == "SEAT_TOO_TALL"; low |= is.ruleId == "SEAT_TOO_LOW"; }
                EXPECT_TRUE(tall || low) << names[p] << " seat " << top << ": a refusal on a 0.6 x 0.6 plane can only be height";
                EXPECT_EQ(tall, top > m.leg_length + Fit::kFootDropMax) << names[p] << " seat " << top;
                EXPECT_EQ(low,  top < m.leg_length - Fit::kKneeRiseMax) << names[p] << " seat " << top;
                // CONTROL: the first refused-TALL seat is sat on anyway — the feet must hang well
                // off the floor, proving the refusal guards a real failure.
                if (tall && !controlDone) {
                    controlDone = true;
                    const auto c = sitCycle(w, presets[p], top);
                    std::printf("[SeatMatrix] %-8s CONTROL seat %.3f (refused TALL): foot above floor %.3f hipsY excess %.3f\n", names[p], top, c.footY - kFloorY, c.hipsYExcess);
                    EXPECT_GT(c.footY - kFloorY, 0.15f + kFeetFloorTol) << names[p] << ": the refused-tall seat leaves the feet hanging above the ankle band";
                }
                continue;
            }
            ++fitCells;
            const auto r = sitCycle(w, presets[p], top);
            const float shortfall = std::max(0.0f, top + AnimatedVoxelCharacter::kHipsAboveSeat - m.leg_length);
            // Feet: judged against the FLOOR (ankle band), not the standing foot-joint height — a
            // scaled body (halfling) stands with its foot joint 0.32 u up in this rig (its foot
            // offset does not follow legLengthScale; logged 2026-09-30), which would make a
            // correct seated foot read as 0.24 u "off".
            const float footAboveFloor = r.footY - kFloorY;
            // this body's ankle height (capped: the floating halfling reads 0.32) + the 0.05 detector
            // tolerance; the shortfall comes from the SeatFit leg PROXY (hips box centre to foot box
            // bottom), which under-reads the real hip-to-ankle reach by ~1-2 cm on the goliath.
            const float ankle = std::max(0.0f, std::min(r.standFootY - kFloorY, 0.15f));
            const float ankleBand = ankle + kFeetFloorTol;
            // Knees: level thighs are only possible while the seat leaves room for a vertical shin.
            const bool kneeRoom = (top + AnimatedVoxelCharacter::kHipsAboveSeat) - (r.shin + ankle) <= kKneesYTol;
            std::printf("[SeatMatrix] %-8s seat %.3f: hipsY excess %.3f | hips XZ %.3f | knee dY %.3f | feet-floor %.3f (allowed %.3f) | worst step %.3f"
                        " || sample hips %.3f knee %.3f foot %.3f standFoot %.3f shin %.3f readback %.3f\n",
                        names[p], top, r.hipsYExcess, r.hipsXZ, r.kneeDY, r.feetFloor, kFeetFloorTol + shortfall, r.worstStep,
                        r.hipsY - kFloorY, r.kneeY - kFloorY, r.footY - kFloorY, r.standFootY - kFloorY, r.shin, r.readbackFeet);
            EXPECT_GT(r.seatedFrames, 20) << names[p] << " seat " << top;
            EXPECT_LE(r.hipsYExcess, 0.0f) << names[p] << " seat " << top << " SEATED_HIPS_OFF_SEAT_Y";
            EXPECT_LE(r.hipsXZ, kSeatHalfExtent) << names[p] << " seat " << top << " SEATED_HIPS_OFF_SEAT_XZ";
            if (kneeRoom) EXPECT_LE(r.kneeDY, kKneesYTol) << names[p] << " seat " << top << " SEATED_THIGHS_NOT_HORIZONTAL";
            else          EXPECT_LT(r.kneeY, r.hipsY)     << names[p] << " seat " << top << " tall seat: the legs hang, knees below the hips";
            EXPECT_GE(footAboveFloor, -0.02f) << names[p] << " seat " << top << " feet never sink into the floor";
            EXPECT_LE(footAboveFloor, ankleBand + shortfall) << names[p] << " seat " << top << " FEET_ON_FLOOR (ankle band + geometric shortfall)";
            EXPECT_LT(r.worstStep, kPositionSnapTol) << names[p] << " seat " << top << " POSITION_SNAP_AT_CLIP_BOUNDARY";
        }
    }
    std::printf("[SeatMatrix] %d cells: %d fit (all sat + judged), %d refused (never sat)\n", cells, fitCells, refusedCells);
    EXPECT_EQ(cells, 102);
    EXPECT_GT(fitCells, 30) << "the fit band must cover a real share of the ladder for three bodies";
    EXPECT_GT(refusedCells, 30) << "and refuse the rest — accuracy over coverage";
}
