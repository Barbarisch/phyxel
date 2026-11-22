// WorldEditorRoutingTest -- docs/VoxelFoundationPlan.md F7.4 E1 (prediction: docs/evidence/voxfound_f7_prediction.txt):
// with an IWorldEditor set, VoxelInteractionSystem's hover is the editor's pick and break / place are its brushes
// (cube / subcube / microcube = 8 / 2 / 1 voxels; placing in front of the hit face, the provider's material); without
// one, the editor is never called (today's path). Headless: no window, so the hover is the camera's forward ray.

#include <gtest/gtest.h>

#include <vector>

#include "core/ChunkManager.h"
#include "scene/IWorldEditor.h"
#include "scene/VoxelInteractionSystem.h"

using namespace Phyxel;

namespace {

struct Call {
    glm::ivec3 voxel;
    int size;
    std::string material;
};

class FakeEditor : public IWorldEditor {
public:
    WorldEditHit next;
    mutable int picks = 0;
    mutable glm::vec3 lastOrigin{0.0f}, lastDir{0.0f};
    std::vector<Call> calls;
    WorldEditHit pick(const glm::vec3& origin, const glm::vec3& dir, float) const override {
        ++picks;
        lastOrigin = origin;
        lastDir = dir;
        return next;
    }
    size_t brush(const glm::ivec3& voxel, int size, const std::string& material) override {
        calls.push_back({voxel, size, material});
        return size_t(size) * size * size;
    }
};

}  // namespace

TEST(WorldEditorRouting, HoverBreakAndPlaceGoToTheEditor) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    VoxelInteractionSystem vis(&cm, nullptr, nullptr, nullptr);
    FakeEditor ed;
    ed.next.hit = true;
    ed.next.voxel = {-100, 50, 200};
    ed.next.normal = {0, 1, 0};
    ed.next.material = "Dirt";
    std::vector<glm::ivec3> changed;
    vis.setVoxelChangeCallback([&](const glm::ivec3& c) { changed.push_back(c); });
    vis.setMaterialProvider([] { return std::string("Bricks"); });
    vis.setWorldEditor(&ed);
    const glm::vec3 cam(1.0f, 2.0f, 3.0f), front(0.0f, -1.0f, 0.0f), up(0.0f, 0.0f, 1.0f);
    auto hover = [&] { vis.updateMouseHover(cam, front, up, 400.0, 300.0, false); };

    hover();
    EXPECT_EQ(ed.picks, 1);
    EXPECT_EQ(ed.lastOrigin, cam);
    EXPECT_EQ(ed.lastDir, front) << "no window: the camera's forward ray";
    EXPECT_TRUE(vis.hasHoveredCube());

    vis.breakHoveredCube(cam);
    hover(); vis.breakHoveredSubcube();
    hover(); vis.breakHoveredMicrocube();
    hover(); vis.placeVoxelAtHover();
    hover(); vis.placeSubcubeAtHover();
    hover(); vis.placeMicrocubeAtHover();
    ASSERT_EQ(ed.calls.size(), 6u);
    const glm::ivec3 hit(-100, 50, 200), front1(-100, 51, 200);
    const int sizes[6] = {8, 2, 1, 8, 2, 1};
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(ed.calls[i].size, sizes[i]) << i;
        EXPECT_EQ(ed.calls[i].voxel, i < 3 ? hit : front1) << i;
        EXPECT_EQ(ed.calls[i].material, i < 3 ? std::string() : std::string("Bricks")) << i;
    }
    ASSERT_EQ(changed.size(), 6u);
    EXPECT_EQ(changed[0], glm::ivec3(-13, 6, 25)) << "the edit's cube (floor division: x -100 -> -13)";
    EXPECT_EQ(vis.lastEditVoxels(), 1u);

    // nothing hovered: no brush
    ed.next.hit = false;
    hover();
    vis.breakHoveredCube(cam);
    EXPECT_EQ(ed.calls.size(), 6u);
    EXPECT_FALSE(vis.hasHoveredCube());

    // the control: no editor -> today's path, the editor is never called
    vis.setWorldEditor(nullptr);
    ed.next.hit = true;
    const int picks = ed.picks;
    hover();
    vis.breakHoveredCube(cam);
    vis.placeVoxelAtHover();
    EXPECT_EQ(ed.picks, picks);
    EXPECT_EQ(ed.calls.size(), 6u);
}
