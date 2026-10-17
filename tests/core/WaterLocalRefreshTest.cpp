// WaterCore.md 26.19 GATE: a world change re-read LOCALLY - only the cells of the cubes the occupancy pool says changed - gives
// the same occupancy as a re-read of the whole box, cell for cell. Two managers over one fake world: one is handed the change
// list, the other never is (so every change re-reads its whole box). RED control: a change left out of the list must show.

#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <tuple>
#include <vector>

#include "core/water/WaterCoreManager.h"

using namespace Phyxel::Core::Water;

namespace {

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

struct FakeWorld {
    // ground: whole voxels with y < 4 are solid; then edits on top
    std::set<std::tuple<int, int, int>> dug, added, microSolid;
    std::set<std::tuple<int, int, int>> unknownChunks;   // chunk coords (32^3 voxels) the pool does not hold
    uint64_t revision = 1;
    std::vector<std::pair<uint64_t, std::vector<SolidsChangeBox>>> packs;   // what each revision changed

    int state(const glm::ivec3& m) const {
        const glm::ivec3 c(floorDiv(m.x, 9), floorDiv(m.y, 9), floorDiv(m.z, 9));
        if (unknownChunks.count({floorDiv(c.x, 32), floorDiv(c.y, 32), floorDiv(c.z, 32)})) return 2;
        if (microSolid.count({m.x, m.y, m.z})) return 1;
        const auto k = std::make_tuple(c.x, c.y, c.z);
        if (dug.count(k)) return 0;
        if (added.count(k)) return 1;
        return c.y < 4 ? 1 : 0;
    }
    void pack(std::vector<SolidsChangeBox> boxes) { packs.push_back({++revision, std::move(boxes)}); }
    bool since(uint64_t rev, std::vector<SolidsChangeBox>& out) const {
        for (const auto& [r, b] : packs) if (r > rev) out.insert(out.end(), b.begin(), b.end());
        return true;
    }
};

struct Pair {
    FakeWorld& w;
    WaterCoreManager local, full;
    int idLocal = 0, idFull = 0;
    Pair(FakeWorld& world, const glm::ivec3& lo, const glm::ivec3& hi, float h)
        : w(world),
          local([&world](const glm::ivec3& m) { return world.state(m); }, [&world]() { return world.revision; }),
          full([&world](const glm::ivec3& m) { return world.state(m); }, [&world]() { return world.revision; }) {
        local.setSolidsChangesQuery([&world](uint64_t since, std::vector<SolidsChangeBox>& out) { return world.since(since, out); });
        std::string err;
        idLocal = local.create(lo, hi, h, "eulerian", &err, "cpu"); EXPECT_NE(idLocal, 0) << err;
        idFull = full.create(lo, hi, h, "eulerian", &err, "cpu");   EXPECT_NE(idFull, 0) << err;
        refresh();
    }
    void refresh() { local.probe(glm::vec3(0.0f)); full.probe(glm::vec3(0.0f)); }
    // cells whose occupancy differs between the two
    long differing() const {
        const WaterGrid* a = local.gridForTest(idLocal); const WaterGrid* b = full.gridForTest(idFull);
        long d = 0;
        for (int z = 0; z < a->nz(); ++z) for (int y = 0; y < a->ny(); ++y) for (int x = 0; x < a->nx(); ++x)
            if (a->occ(x, y, z) != b->occ(x, y, z)) ++d;
        return d;
    }
    AvRecord rec() const { return local.list().front(); }
};

void runScenes(float h, const char* tag) {
    FakeWorld w;
    w.unknownChunks.insert({1, 0, 0});   // x >= 32 is not resident yet
    Pair p(w, glm::ivec3(26, 0, 1), glm::ivec3(36, 6, 6), h);   // straddles the chunk seam at x 32
    const long cells = static_cast<long>(p.local.gridForTest(p.idLocal)->cellCount());
    auto check = [&](const char* what, long maxResampled) {
        p.refresh();
        const AvRecord r = p.rec();
        std::printf("  [%s] %-34s differing %ld, resampled %ld of %ld, changed %ld, local %ld, full %ld\n", tag, what, p.differing(),
                    r.occCellsResampled, cells, r.occCellsChanged, r.occLocalRefreshes, r.occFullRefreshes);
        EXPECT_EQ(p.differing(), 0) << tag << " " << what << ": the local re-read must equal the whole-box re-read";
        EXPECT_EQ(r.occFullRefreshes, 1) << tag << " " << what << ": only the creation re-read the whole box";
        if (maxResampled > 0) EXPECT_LE(r.occCellsResampled, maxResampled) << tag << " " << what << ": the local re-read stays local";
    };
    const int per = static_cast<int>(std::lround(1.0f / h));
    const long oneCube = static_cast<long>(per + 2) * (per + 2) * (per + 2);
    w.dug.insert({28, 3, 2}); w.pack({{{28, 3, 2}, {28, 3, 2}}});                  check("dig one cube", oneCube);
    w.added.insert({29, 4, 3}); w.pack({{{29, 4, 3}, {29, 4, 3}}});                check("add one cube", oneCube);
    w.microSolid.insert({30 * 9 + 4, 4 * 9, 4 * 9 + 4}); w.pack({{{30, 4, 4}, {30, 4, 4}}});   check("one micro", oneCube);
    w.pack({});                                                                     check("a pack with nothing (a remesh)", 0);
    w.dug.insert({100, 3, 3}); w.pack({{{100, 3, 3}, {100, 3, 3}}});               check("a dig far outside the box", 0);
    w.unknownChunks.clear(); w.pack({{{32, 0, 0}, {63, 31, 31}}});                  check("a chunk arriving (unknown -> known)", 0);
    for (int x = 30; x <= 33; ++x) w.dug.insert({x, 3, 4});
    w.pack({{{30, 3, 4}, {31, 3, 4}}, {{32, 3, 4}, {33, 3, 4}}});                   check("a trench across the chunk seam", 0);
    w.unknownChunks.insert({1, 0, 0}); w.pack({{{32, 0, 0}, {63, 31, 31}}});        check("a chunk leaving (known -> unknown)", 0);
    // RED control: an edit the list does not report - the local volume must miss it (else the comparison sees nothing)
    w.dug.insert({27, 2, 2}); w.pack({});
    p.refresh();
    std::printf("  [%s] control (an unreported dig): differing %ld\n", tag, p.differing());
    EXPECT_GT(p.differing(), 0) << tag << " control: an unreported change must differ";
}

}  // namespace

TEST(WaterLocalRefreshTest, LocalRefreshEqualsFull) {
    runScenes(1.0f / 9.0f, "1/9 m");
    runScenes(1.0f / 3.0f, "1/3 m");   // cells coarser than a micro: sampled on a lattice
}
