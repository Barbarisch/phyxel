// docs/VoxelFoundationPlan.md F7.6b -- EditHistory: undo / redo cell lists exact, depth cap, redo cleared, limit.
#include <gtest/gtest.h>

#include <map>
#include <random>
#include <tuple>

#include "core/voxfound/EditHistory.h"

using Phyxel::Core::VoxFound::EditHistory;

namespace {
struct Grid {   // a sparse synthetic world: air (0) where unset
    std::map<std::tuple<int, int, int>, uint8_t> v;
    uint8_t at(glm::ivec3 p) const {
        auto it = v.find({p.x, p.y, p.z});
        return it == v.end() ? 0 : it->second;
    }
    void set(glm::ivec3 p, uint8_t m) {
        if (m) v[{p.x, p.y, p.z}] = m; else v.erase({p.x, p.y, p.z});
    }
    void apply(const EditHistory::Cells& c) { for (auto& [p, m] : c) set(p, m); }
    EditHistory::Read reader() const { return [this](glm::ivec3 p) { return at(p); }; }
};
void edit(Grid& g, EditHistory& h, glm::ivec3 lo, glm::ivec3 hi, uint8_t m) {
    h.begin("edit", lo, hi, g.reader());
    EditHistory::forEach(lo, hi, [&](glm::ivec3 p) { g.set(p, m); });
    h.commit(g.reader());
}
}  // namespace

TEST(VoxFoundEditHistory, UndoRedoExactOverOverlappingEdits) {
    Grid g;
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 8; ++y) g.set({x, y, z}, uint8_t(1 + (x + z) % 3));
    EditHistory h;
    std::mt19937 rng(7);
    std::vector<decltype(g.v)> states{g.v};
    for (int i = 0; i < 12; ++i) {
        const glm::ivec3 lo(int(rng() % 12), int(rng() % 8), int(rng() % 12));
        edit(g, h, lo, lo + glm::ivec3(3), uint8_t(i % 2 ? 0 : 5));
        states.push_back(g.v);
    }
    EXPECT_EQ(h.undoDepth(), 12u);
    for (int i = 12; i > 0; --i) {
        g.apply(h.undo(g.reader()));
        EXPECT_EQ(g.v, states[size_t(i - 1)]) << "after undo to step " << i - 1;
    }
    EXPECT_FALSE(h.canUndo());
    EXPECT_TRUE(h.undo(g.reader()).empty());
    for (int i = 1; i <= 12; ++i) {
        g.apply(h.redo(g.reader()));
        EXPECT_EQ(g.v, states[size_t(i)]) << "after redo to step " << i;
    }
}

TEST(VoxFoundEditHistory, OnlyDifferingCellsAreReturned) {
    Grid g;
    EditHistory h;
    edit(g, h, {0, 0, 0}, {3, 3, 3}, 2);          // 64 voxels air -> 2
    const auto c = h.undo(g.reader());
    EXPECT_EQ(c.size(), 64u);
    g.apply(c);
    edit(g, h, {0, 0, 0}, {3, 3, 3}, 0);          // air -> air: nothing changed, not recorded
    EXPECT_EQ(h.undoDepth(), 0u);
    EXPECT_EQ(h.redoDepth(), 1u);                  // a no-op edit leaves redo alone (commit records nothing)
}

TEST(VoxFoundEditHistory, DepthCapAndRedoClearedByANewEdit) {
    Grid g;
    EditHistory h;
    for (int i = 0; i < 25; ++i) edit(g, h, {i, 0, 0}, {i, 0, 0}, 3);
    EXPECT_EQ(h.undoDepth(), EditHistory::kMaxDepth);
    for (int i = 0; i < 3; ++i) g.apply(h.undo(g.reader()));
    EXPECT_EQ(h.redoDepth(), 3u);
    edit(g, h, {40, 0, 0}, {40, 0, 0}, 4);
    EXPECT_EQ(h.redoDepth(), 0u);
    EXPECT_EQ(h.undoDepth(), EditHistory::kMaxDepth - 2);
}

TEST(VoxFoundEditHistory, OverTheLimitIsNotRecorded) {
    Grid g;
    EditHistory h;
    EXPECT_FALSE(h.begin("big", {0, 0, 0}, {256, 256, 256}, g.reader()));   // 16.97 M > 16 M
    EXPECT_FALSE(h.commit(g.reader()));
    EXPECT_TRUE(h.begin("ok", {0, 0, 0}, {63, 63, 63}, g.reader()));
}
