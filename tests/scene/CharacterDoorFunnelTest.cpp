#include <gtest/gtest.h>

#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "scene/AnimatedVoxelCharacter.h"

using Phyxel::Chunk;
using Phyxel::ChunkManager;
using Phyxel::Scene::AnimatedVoxelCharacter;

// ============================================================================
// DOORWAY FUNNEL (Ravenmere run 31, 2026-09-09). A generated door is a 0.78 m clear
// reveal (7 micro: jambs at offsets 0 and 8 of the door cube); the controller is 0.52 m
// wide. With the axis-separated resolve, a walk-in a few centimetres off the reveal's
// centre clipped a jamb and the whole move was reverted: the player stood pinned in
// front of an open door, aligned to 12 cm (`steer_stuck (-24.62, 11.33)` at the
// tavern's street door). The controller now shifts up to a quarter body width sideways
// to free a blocked move - the slide a real body makes off a door frame.
// RED on the old code: the off-centre walk stalls before the wall line.
// ============================================================================

namespace {

struct DoorWorld {
    std::unique_ptr<Phyxel::Physics::PhysicsWorld> physics;
    ChunkManager cm;
    std::vector<std::unique_ptr<Phyxel::Physics::VoxelOccupancyGrid>> grids;

    // Floor cubes at y=15 (stand at y=16) across chunk (0,0,0); a wall band 6 micro thick
    // along x at cube z=20 (micro z offsets 0..5), 2 cubes tall (3 with `realizerLintel`),
    // with an optional 7-micro reveal in cube x=16 (offsets 1..7) - the realizer's framed door.
    explicit DoorWorld(bool withDoor, bool raisedFloorInside = false, bool realizerLintel = false) {
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
        // The grid is hierarchical: queryAABB visits a cube only if its cube bit is set,
        // then its subcube bits only if it is marked subdivided, then a subcube's micro
        // bits only if that subcube is marked subdivided.
        const int wallTop = realizerLintel ? 18 : 17;   // 3 cubes when a lintel must sit above a 2-cube door
        for (int x = 8; x < 24; ++x)
            for (int y = 16; y <= wallTop; ++y) {
                const glm::ivec3 cube(x, y, 20);
                g->setCube(cube, true);            // the cube bit gates every finer level in queryAABB
                g->markSubdivided(cube, true);
                for (int sx = 0; sx < 3; ++sx)
                    for (int sy = 0; sy < 3; ++sy)
                        for (int sz = 0; sz < 2; ++sz) {
                            g->setSubcube(cube, glm::ivec3(sx, sy, sz), true);   // filled bit gates the micro mask
                            g->markSubcubeSubdivided(cube, glm::ivec3(sx, sy, sz), true);
                        }
                for (int mx = 0; mx < 9; ++mx)
                    for (int my = 0; my < 9; ++my)
                        for (int mz = 0; mz < 6; ++mz) {
                            bool inReveal = withDoor && x == 16 && mx >= 1 && mx <= 7;
                            // The realizer's door (2026-09-09): a 2-cube opening, 18 micro
                            // (2.0 m) clear above the slab top, the 2-micro lintel painted in
                            // the wall ABOVE it (canon: door_interior clear_h 2.03 m). It used to
                            // sit inside the opening (1.78 m clear) - too low for the humanoid.
                            if (inReveal && realizerLintel) {
                                const int my_abs = y * 9 + my;
                                const int floorTop = raisedFloorInside ? 16 * 9 + 3 : 16 * 9;
                                if (my_abs >= floorTop + 18) inReveal = false;   // lintel + wall above
                            }
                            if (inReveal) continue;
                            g->setMicrocube(cube, glm::ivec3(mx / 3, my / 3, mz / 3),
                                            glm::ivec3(mx % 3, my % 3, mz % 3), true);
                        }
            }
        // A 1/3 floor slab from the door cell inward (the realizer's ground-floor slab sits
        // flush with the outer wall face): the walk-in must STEP UP 3 micro as it enters.
        if (raisedFloorInside)
            for (int x = 8; x < 24; ++x)
                for (int z = 20; z < 28; ++z) {
                    const glm::ivec3 cube(x, 16, z);
                    g->setCube(cube, true);
                    g->markSubdivided(cube, true);
                    for (int sx = 0; sx < 3; ++sx)
                        for (int sz = 0; sz < 3; ++sz)
                            g->setSubcube(cube, glm::ivec3(sx, 0, sz), true);   // solid 1/3 slab
                }
        physics->getVoxelWorld()->registerGrid(g.get());
        grids.push_back(std::move(g));
    }

    std::unique_ptr<AnimatedVoxelCharacter> makeCharacter(const glm::vec3& pos) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), pos);
        EXPECT_TRUE(ch->loadModel("resources/animated_characters/humanoid.anim"));
        ch->setChunkManager(&cm);
        return ch;
    }
};

// Walk +z at 1.5 m/s for `seconds`; returns the final z.
float walkNorth(AnimatedVoxelCharacter& ch, float seconds) {
    ch.setMoveVelocity(glm::vec3(0.0f));
    for (int i = 0; i < 30; ++i) ch.update(1.0f / 60.0f);      // settle on the floor
    const int frames = static_cast<int>(seconds * 60.0f);
    for (int i = 0; i < frames; ++i) {
        ch.setMoveVelocity(glm::vec3(0.0f, 0.0f, 1.5f));
        ch.update(1.0f / 60.0f);
    }
    return ch.getPosition().z;
}

}  // namespace

// Centred in the reveal (x = 16.5): the door has always been passable - the control.
TEST(CharacterDoorFunnelTest, CentredWalkPassesTheDoor) {
    DoorWorld w(/*withDoor=*/true);
    auto ch = w.makeCharacter(glm::vec3(16.5f, 16.0f, 18.0f));
    const float z = walkNorth(*ch, 4.0f);
    EXPECT_GT(z, 21.5f) << "a centred walk did not pass the 0.78 m reveal (z=" << z << ")";
}

// 12 cm off the reveal's centre - the Ravenmere alignment - must still get through.
TEST(CharacterDoorFunnelTest, OffCentreWalkIsFunnelledThroughTheDoor) {
    DoorWorld w(/*withDoor=*/true);
    auto ch = w.makeCharacter(glm::vec3(16.5f - 0.12f, 16.0f, 18.0f));
    const float z = walkNorth(*ch, 4.0f);
    EXPECT_GT(z, 21.5f) << "12 cm off-centre: pinned in front of the open door (z=" << z << ")";
    auto ch2 = w.makeCharacter(glm::vec3(16.5f + 0.12f, 16.0f, 18.0f));
    const float z2 = walkNorth(*ch2, 4.0f);
    EXPECT_GT(z2, 21.5f) << "12 cm off-centre the other way (z=" << z2 << ")";
}

// TEETH: the same wall with no reveal is still a wall - the funnel must not tunnel.
TEST(CharacterDoorFunnelTest, SolidWallStillBlocks) {
    DoorWorld w(/*withDoor=*/false);
    auto ch = w.makeCharacter(glm::vec3(16.5f, 16.0f, 18.0f));
    const float z = walkNorth(*ch, 4.0f);
    EXPECT_LT(z, 20.0f) << "the funnel pushed the character through a solid wall (z=" << z << ")";
}


// The Ravenmere tavern: the ground-floor slab (3 micro) starts at the door plane, so the
// walk-in is a step-up taken while crossing the reveal. Run 32 stopped 6 cm short of the
// door plane at (-24.56, 11.32) with the funnel already in place - the step, not the jamb.
TEST(CharacterDoorFunnelTest, RaisedSlabBehindTheDoorIsSteppedInto) {
    DoorWorld w(/*withDoor=*/true, /*raisedFloorInside=*/true);
    auto ch = w.makeCharacter(glm::vec3(16.5f, 16.0f, 18.0f));
    const float z = walkNorth(*ch, 4.0f);
    EXPECT_GT(z, 21.5f) << "the 3-micro slab at the door plane stopped the walk-in (z=" << z << ")";
    EXPECT_NEAR(ch->getPosition().y, 16.0f + 3.0f / 9.0f, 0.1f) << "did not end up standing on the slab";
    // Off-centre as well (funnel + step together).
    auto ch2 = w.makeCharacter(glm::vec3(16.5f - 0.10f, 16.0f, 18.0f));
    const float z2 = walkNorth(*ch2, 4.0f);
    EXPECT_GT(z2, 21.5f) << "off-centre + step-up (z=" << z2 << ")";
}


// The realizer's REAL door: 18 micro (2.0 m) clear over a 3-micro slab, lintel above.
// RED before 2026-09-09 on both counts: the realizer cut 16 micro (1.78 m) and the
// controller box was 2.12 m (model 1.82 m + 0.3 m margin) - the character stopped at
// z=20.4 under the lintel. Canon: door_interior clear_h = 2.03 m (DimensionCanonTest).
TEST(CharacterDoorFunnelTest, RealizerHeightDoorAdmitsTheCharacter) {
    DoorWorld w(/*withDoor=*/true, /*raisedFloorInside=*/true, /*realizerLintel=*/true);
    auto ch = w.makeCharacter(glm::vec3(16.5f, 16.0f, 18.0f));
    const float z = walkNorth(*ch, 4.0f);
    EXPECT_GT(z, 21.5f) << "the character does not fit a 1.78 m door (z=" << z
                        << "); controller half-height " << ch->getControllerHalfHeight();
}


// Ravenmere run 33 (G-81): the town re-entry after the cellar stalled the frame for ~7 s
// (NavGraph build behind the loading screen) while the harness's walk key was still
// held. The shipped runtime hands the character the RAW wall-clock delta, so one update
// integrated seconds of motion in a single axis step, carried the body clean through
// the town into unstreamed terrain, and it fell for ever (y = -1.98 million). A single
// update must never tunnel, whatever dt it is given: RED on the old code (the end point
// of a 7 s step lies beyond the wall in free air, so the overlap test never fires).
TEST(CharacterDoorFunnelTest, ALongFrameNeverTunnelsThroughAWall) {
    DoorWorld w(/*withDoor=*/false);
    auto ch = w.makeCharacter(glm::vec3(16.5f, 16.0f, 18.0f));
    ch->setMoveVelocity(glm::vec3(0.0f));
    for (int i = 0; i < 30; ++i) ch->update(1.0f / 60.0f);
    ch->setMoveVelocity(glm::vec3(0.0f, 0.0f, 1.5f));
    ch->update(7.0f);
    const float z = ch->getPosition().z;
    EXPECT_LT(z, 20.0f) << "a 7 s frame carried the character through the wall (z=" << z << ")";
    EXPECT_GT(z, 18.0f) << "the long frame moved nothing at all (z=" << z << ")";
}
