#include <gtest/gtest.h>

#include "core/FurniturePlacer.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// planEdit (step 2 of conversational fine-tuning) — the geometry brain behind
// "move the bed to the opposite wall" / "...to the north wall". The hard invariant
// the user worried about: a re-placed piece must back onto the named wall AND face
// INTO the room (not into the wall), same guarantee furnish() gives.
// A 6(x) by 8(z) room at the world origin's structure: rx=10, rz=20.
// ============================================================================

namespace {
Rect room() { Rect r; r.x = 10; r.z = 20; r.w = 6; r.d = 8; return r; }  // x:[10,16) z:[20,28)
using Edit = FurniturePlacer::FurnitureEdit;
}

// THE case: a bed on the WEST wall (min-x) moved to the opposite wall must land on the EAST wall
// (max-x) and face -x (rot 90) INTO the room. (Red on the stub that seats it back on the SAME wall.)
TEST(FurnitureEditTest, OppositeWallFlipsSideAndFacesIntoRoom) {
    const Rect r = room();
    // seat a bed on the west wall first (so "current" is unambiguous)
    const Edit west = FurniturePlacer::planEdit(r, 0, 0, "wall:west");
    ASSERT_TRUE(west.ok);
    EXPECT_EQ(west.x, 10);                 // min-x column
    EXPECT_EQ(west.rotation, 270);         // faces +x into the room

    const Edit opp = FurniturePlacer::planEdit(r, west.x, west.z, "opposite_wall");
    ASSERT_TRUE(opp.ok);
    EXPECT_EQ(opp.x, 15) << "opposite of the west wall must be the EAST wall (max-x), not the same wall";
    EXPECT_EQ(opp.z, west.z) << "opposite wall keeps the same centered cross-position";
    EXPECT_EQ(opp.rotation, 90) << "on the east wall the piece must face -x INTO the room";
}

// Named walls land on the correct side with inward facing. north=+z (max-z), south=-z (min-z).
TEST(FurnitureEditTest, NamedWallsSeatWithInwardFacing) {
    const Rect r = room();
    const Edit n = FurniturePlacer::planEdit(r, 0, 0, "wall:north");
    EXPECT_EQ(n.z, 27); EXPECT_EQ(n.rotation, 180);   // max-z, faces -z in
    const Edit s = FurniturePlacer::planEdit(r, 0, 0, "wall:south");
    EXPECT_EQ(s.z, 20); EXPECT_EQ(s.rotation, 0);     // min-z, faces +z in
    const Edit e = FurniturePlacer::planEdit(r, 0, 0, "wall:east");
    EXPECT_EQ(e.x, 15); EXPECT_EQ(e.rotation, 90);    // max-x, faces -x in
    const Edit w = FurniturePlacer::planEdit(r, 0, 0, "wall:west");
    EXPECT_EQ(w.x, 10); EXPECT_EQ(w.rotation, 270);   // min-x, faces +x in
}

// rotate keeps the cell and normalizes the angle; center goes to the middle; unknown op fails.
TEST(FurnitureEditTest, RotateCenterAndUnknownOp) {
    const Rect r = room();
    const Edit rot = FurniturePlacer::planEdit(r, 12, 24, "rotate", 450);
    EXPECT_TRUE(rot.ok); EXPECT_EQ(rot.x, 12); EXPECT_EQ(rot.z, 24); EXPECT_EQ(rot.rotation, 90);
    const Edit c = FurniturePlacer::planEdit(r, 12, 24, "center");
    EXPECT_TRUE(c.ok); EXPECT_EQ(c.x, 13); EXPECT_EQ(c.z, 24);   // rx+rw/2, rz+rd/2
    const Edit bad = FurniturePlacer::planEdit(r, 12, 24, "teleport");
    EXPECT_FALSE(bad.ok); EXPECT_FALSE(bad.error.empty());
}

// north<->south flips too (covers the z-axis opposite, not just x).
TEST(FurnitureEditTest, OppositeWallNorthToSouth) {
    const Rect r = room();
    const Edit n = FurniturePlacer::planEdit(r, 0, 0, "wall:north");
    const Edit opp = FurniturePlacer::planEdit(r, n.x, n.z, "opposite_wall");
    ASSERT_TRUE(opp.ok);
    EXPECT_EQ(opp.z, 20) << "opposite of north (max-z) is south (min-z)";
    EXPECT_EQ(opp.rotation, 0) << "on the south wall the piece faces +z into the room";
}
