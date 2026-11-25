// WorldReadsTest -- docs/VoxelFoundationPlan.md F7.1 (prediction: docs/evidence/voxfound_f7_prediction.txt R1-R6, N1):
// the engine's voxel read interfaces answered from the new store (1/8 m voxels in BlockStores, one per engine chunk),
// checked against brute-force rules on dense content, and navigation running unchanged on it through ChunkManager.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "core/ChunkManager.h"
#include "core/NPCManager.h"
#include "core/NavGraph.h"
#include "core/voxfound/StorageLab.h"
#include "core/voxfound/VoxelGrid.h"
#include "core/voxfound/WorldReads.h"

using namespace Phyxel;
using namespace Phyxel::Core;
using Phyxel::Core::VoxFound::BlockStore;
using Phyxel::Core::VoxFound::WorldReads;

namespace {

constexpr int kCV = WorldReads::kChunkVoxels;

struct Ivec3Less {
    bool operator()(glm::ivec3 a, glm::ivec3 b) const {
        return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
    }
};

// A world of new-store chunks + its dense copy (world voxel -> material) for the brute-force rules
struct World {
    std::map<glm::ivec3, BlockStore, Ivec3Less> stores;
    std::map<glm::ivec3, uint8_t, Ivec3Less> dense;   // solid voxels only
    static int fd(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
    void set(glm::ivec3 v, uint8_t m) {
        const glm::ivec3 c(fd(v.x, kCV), fd(v.y, kCV), fd(v.z, kCV));
        auto it = stores.find(c);
        if (it == stores.end()) it = stores.emplace(c, BlockStore(glm::ivec3(kCV))).first;
        it->second.set(v - c * kCV, m);
        if (m) dense[v] = m;
        else dense.erase(v);
    }
    void fillBlock(glm::ivec3 cube, uint8_t m) {   // uniform entry (the store's own path)
        const glm::ivec3 c(fd(cube.x, 32), fd(cube.y, 32), fd(cube.z, 32));
        auto it = stores.find(c);
        if (it == stores.end()) it = stores.emplace(c, BlockStore(glm::ivec3(kCV))).first;
        it->second.fillBlock(cube - c * 32, m);
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y)
                for (int z = 0; z < 8; ++z) {
                    const glm::ivec3 v = cube * 8 + glm::ivec3(x, y, z);
                    if (m) dense[v] = m;
                    else dense.erase(v);
                }
    }
    uint8_t at(glm::ivec3 v) const {
        auto it = dense.find(v);
        return it == dense.end() ? 0 : it->second;
    }
    WorldReads reads() const {
        std::vector<glm::ivec3> cs;
        for (const auto& kv : stores) cs.push_back(kv.first);
        return WorldReads([this](glm::ivec3 c) -> const BlockStore* {
            auto it = stores.find(c);
            return it == stores.end() ? nullptr : &it->second;
        }, cs);
    }
    // the brute-force rules (WorldReads.h), from the dense copy
    int solidIn(glm::ivec3 cube) const {
        int n = 0;
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y)
                for (int z = 0; z < 8; ++z) n += at(cube * 8 + glm::ivec3(x, y, z)) != 0;
        return n;
    }
    VoxelLocation::Type typeRule(glm::ivec3 cube) const {
        const int n = solidIn(cube);
        return n == 0 ? VoxelLocation::EMPTY : n == 512 ? VoxelLocation::CUBE : VoxelLocation::SUBDIVIDED;
    }
    float floorRule(glm::ivec3 cube) const {
        const int n = solidIn(cube);
        if (n == 0) return 0.0f;
        if (n == 512) return -1.0f;
        int layers = 0;
        for (int y = 0; y < 8; ++y) {
            int k = 0;
            for (int x = 0; x < 8; ++x)
                for (int z = 0; z < 8; ++z) k += at(cube * 8 + glm::ivec3(x, y, z)) != 0;
            if (k == 64) ++layers;
            else break;
        }
        if (layers == 0 || n != layers * 64) return -1.0f;
        return layers / 8.0f;
    }
};

uint32_t hash3(int x, int y, int z) {
    uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

// Mixed content in the cubes [lo, lo + n): every kind of block the rules distinguish
void mixedContent(World& w, glm::ivec3 lo, int n) {
    for (int cx = 0; cx < n; ++cx)
        for (int cy = 0; cy < n; ++cy)
            for (int cz = 0; cz < n; ++cz) {
                const glm::ivec3 cube = lo + glm::ivec3(cx, cy, cz);
                const uint32_t h = hash3(cube.x, cube.y, cube.z);
                const int kind = int(h % 9), k = 1 + int((h >> 8) % 7);
                auto vox = [&](int x, int y, int z, uint8_t m) { w.set(cube * 8 + glm::ivec3(x, y, z), m); };
                switch (kind) {
                    case 0: break;                                   // air
                    case 1: w.fillBlock(cube, 1); break;             // uniform solid
                    case 2:                                          // full, written voxel by voxel (detailed)
                        for (int x = 0; x < 8; ++x) for (int y = 0; y < 8; ++y) for (int z = 0; z < 8; ++z) vox(x, y, z, 2);
                        break;
                    case 3: vox(int(h >> 4) & 7, int(h >> 7) & 7, int(h >> 10) & 7, 1); break;   // one voxel
                    case 4:                                          // a slab of k layers: a platform
                        for (int x = 0; x < 8; ++x) for (int y = 0; y < k; ++y) for (int z = 0; z < 8; ++z) vox(x, y, z, 1);
                        break;
                    case 5:                                          // a slab + one voxel above: a wall, solid
                        for (int x = 0; x < 8; ++x) for (int y = 0; y < k; ++y) for (int z = 0; z < 8; ++z) vox(x, y, z, 1);
                        vox(3, std::min(7, k + 1), 4, 1);
                        break;
                    case 6:                                          // layers floating above air: no floor
                        for (int x = 0; x < 8; ++x) for (int y = k; y < 8; ++y) for (int z = 0; z < 8; ++z) vox(x, y, z, 1);
                        break;
                    case 7:                                          // random half
                        for (int x = 0; x < 8; ++x) for (int y = 0; y < 8; ++y) for (int z = 0; z < 8; ++z)
                            if (hash3(cube.x * 8 + x, cube.y * 8 + y, cube.z * 8 + z) & 1) vox(x, y, z, 3);
                        break;
                    case 8:                                          // full then one voxel cleared
                        for (int x = 0; x < 8; ++x) for (int y = 0; y < 8; ++y) for (int z = 0; z < 8; ++z) vox(x, y, z, 1);
                        vox(int(h >> 4) & 7, int(h >> 7) & 7, int(h >> 10) & 7, 0);
                        break;
                }
            }
}

}  // namespace

// R1 + R2: every cube of two chunks (one at negative coordinates) and a chunk the store lacks
TEST(WorldReads, TypeAndFloorEqualTheRuleOnEveryCube) {
    World w;
    mixedContent(w, {-24, 2, 4}, 8);    // chunk (-1, 0, 0)
    mixedContent(w, {2, 10, 2}, 8);     // chunk (0, 0, 0)
    // not collapsed: a full block written voxel by voxel stays DETAILED (popcount 512) next to fillBlock's uniform ones
    const WorldReads r = w.reads();
    size_t checked = 0, bad = 0, kinds[3] = {0, 0, 0}, floors = 0;
    for (const glm::ivec3 chunk : {glm::ivec3(-1, 0, 0), glm::ivec3(0, 0, 0), glm::ivec3(5, 5, 5)})
        for (int x = 0; x < 32; ++x)
            for (int y = 0; y < 32; ++y)
                for (int z = 0; z < 32; ++z) {
                    const glm::ivec3 cube = chunk * 32 + glm::ivec3(x, y, z);
                    const VoxelLocation::Type t = r.typeAt(cube), tw = w.typeRule(cube);
                    const float f = r.subVoxelFloor(cube), fw = w.floorRule(cube);
                    ++checked;
                    ++kinds[tw];
                    floors += fw > 0.0f;
                    if (t != tw || f != fw || r.hasVoxelAt(cube) != (tw != VoxelLocation::EMPTY)) {
                        if (bad++ < 5)
                            ADD_FAILURE() << "cube (" << cube.x << "," << cube.y << "," << cube.z << "): type " << t
                                          << " vs " << tw << ", floor " << f << " vs " << fw;
                    }
                }
    std::printf("[WorldReads] %zu cubes (empty %zu, cube %zu, subdivided %zu; %zu platforms): %zu differ from the rule\n",
                checked, kinds[0], kinds[1], kinds[2], floors, bad);
    EXPECT_EQ(bad, 0u);
    EXPECT_GT(kinds[1], 0u);
    EXPECT_GT(kinds[2], 0u);
    EXPECT_GT(floors, 0u);
}

// R2: the floor rule's cases, stated
TEST(WorldReads, SubVoxelFloorCases) {
    World w;
    for (int k = 1; k <= 7; ++k)
        for (int x = 0; x < 8; ++x) for (int y = 0; y < k; ++y) for (int z = 0; z < 8; ++z) w.set({k * 8 + x, y, z}, 1);
    for (int x = 0; x < 8; ++x) for (int y = 0; y < 3; ++y) for (int z = 0; z < 8; ++z) w.set({80 + x, y, z}, 1);
    w.set({83, 5, 3}, 1);                                           // cube 10: a slab + a voxel above
    for (int x = 0; x < 8; ++x) for (int y = 2; y < 8; ++y) for (int z = 0; z < 8; ++z) w.set({88 + x, y, z}, 1);   // 11
    w.fillBlock({12, 0, 0}, 1);                                     // cube 12: full
    const WorldReads r = w.reads();
    for (int k = 1; k <= 7; ++k) EXPECT_FLOAT_EQ(r.subVoxelFloor({k, 0, 0}), k / 8.0f) << k << " layers";
    EXPECT_EQ(r.subVoxelFloor({10, 0, 0}), -1.0f) << "a slab with a voxel above is a wall";
    EXPECT_EQ(r.subVoxelFloor({11, 0, 0}), -1.0f) << "no bottom layer";
    EXPECT_EQ(r.subVoxelFloor({12, 0, 0}), -1.0f) << "full";
    EXPECT_EQ(r.subVoxelFloor({13, 0, 0}), 0.0f) << "empty";
    EXPECT_EQ(r.subVoxelFloor({0, 0, 40}), 0.0f) << "a chunk the store lacks";
}

// R3: occupiedMicro = the voxel under each micro cell's centre (computed independently, in double)
TEST(WorldReads, OccupiedMicroSamplesTheVoxelUnderTheCentre) {
    World w;
    mixedContent(w, {-4, 0, -4}, 4);   // 4 x 4 x 4 m across chunk borders at x = 0 and z = 0
    const WorldReads r = w.reads();
    size_t checked = 0, bad = 0, solid = 0;
    for (int mx = -36; mx < 0 + 0; ++mx)
        for (int my = 0; my < 36; ++my)
            for (int mz = -36; mz < 0; ++mz) {
                auto v = [](int m) { return int(std::floor((m + 0.5) / 9.0 * 8.0)); };
                const bool want = w.at({v(mx), v(my), v(mz)}) != 0;
                ++checked;
                solid += want;
                bad += r.occupiedMicro({mx, my, mz}) != want;
            }
    std::printf("[WorldReads] occupiedMicro: %zu micro cells (%zu solid), %zu differ\n", checked, solid, bad);
    EXPECT_EQ(bad, 0u);
    EXPECT_GT(solid, 0u);
    EXPECT_FALSE(r.occupiedMicro({1000, 1000, 1000})) << "a chunk the store lacks";
}

namespace {
// NavGraphMicroTest's door world in 1/8 m voxels: ground cubes at y = 15 (stand at 16); a wall at cube x = 10, 5 voxels
// thick (x 83-87), y cubes 16-18, the full depth; `door` carves cube z = 15, y 16-17 down to two one-voxel jambs
// (z 120 and 127): a reveal 6 voxels (0.75 m) wide x 16 voxels (2 m) tall under the y = 18 lintel.
World doorWorld(bool door) {
    World w;
    for (int cx = 0; cx < 32; ++cx)
        for (int cz = 0; cz < 32; ++cz) w.fillBlock({cx, 15, cz}, 1);
    for (int x = 83; x < 88; ++x)
        for (int y = 128; y < 152; ++y)
            for (int z = 0; z < 256; ++z) {
                const bool reveal = door && y < 144 && z > 120 && z < 127;
                if (!reveal) w.set({x, y, z}, 1);
            }
    return w;
}
}  // namespace

// N1: navigation unchanged on the new store, through ChunkManager's routed reads
TEST(WorldReads, NavigationPathsThroughAFramedDoorOfNewStoreVoxels) {
    NavAgentProfile agent;
    const glm::vec3 from(5.5f, 16.0f, 15.5f), to(20.5f, 16.0f, 15.5f);
    auto crossesAt = [](const NavGraph::PathResult& p, int x, int z) {
        for (const auto& n : p.nodes) if (n.x == x && n.z == z) return true;
        return false;
    };
    {   // the door
        World w = doorWorld(true);
        const WorldReads r = w.reads();
        ChunkManager cm;
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        // (no old chunks: every read comes from the new store -- today's Chunk store went with D4)
        cm.setVoxFoundReads(&r);
        EXPECT_EQ(cm.getVoxelTypeAt({10, 16, 3}), VoxelLocation::SUBDIVIDED) << "the wall cube is partial";
        EXPECT_TRUE(cm.occupiedMicro({10 * 9 + 6, 16 * 9 + 4, 3 * 9 + 4})) << "inside the wall";
        EXPECT_FALSE(cm.occupiedMicro({10 * 9 + 6, 16 * 9 + 4, 15 * 9 + 4})) << "the reveal is clear";
        NPCManager npc;
        npc.setChunkManager(&cm);
        npc.buildNavGrid();
        ASSERT_NE(npc.getNavGraph(), nullptr);
        ASSERT_TRUE(npc.getNavGraph()->microMode()) << "the runtime graph must be sub-cube aware";
        const auto p = npc.getNavGraph()->findPath(from, to, agent);
        ASSERT_TRUE(p.found) << "NPCs could not path through a framed door of new-store voxels";
        bool crosses = false;
        for (const auto& n : p.nodes) crosses |= n.x == 10;
        EXPECT_TRUE(crosses);
        EXPECT_TRUE(crossesAt(p, 10, 15)) << "the only crossing is the door at z = 15";
        std::printf("[WorldReads] door: a path of %zu nodes through (10, 15)\n", p.nodes.size());
    }
    {   // TEETH: no reveal
        World w = doorWorld(false);
        const WorldReads r = w.reads();
        ChunkManager cm;
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        cm.setVoxFoundReads(&r);
        NPCManager npc;
        npc.setChunkManager(&cm);
        npc.buildNavGrid();
        ASSERT_NE(npc.getNavGraph(), nullptr);
        EXPECT_FALSE(npc.getNavGraph()->findPath(from, to, agent).found) << "a continuous wall must block";
    }
    {   // control: not routed -> no chunks, no nav
        ChunkManager cm;
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        NPCManager npc;
        npc.setChunkManager(&cm);
        npc.buildNavGrid();
        EXPECT_EQ(npc.getNavGraph(), nullptr);
    }
}

// R6: cost
TEST(WorldReads, Cost) {
    World w;
    mixedContent(w, {0, 0, 0}, 16);
    for (auto& kv : w.stores) kv.second.collapse();
    const WorldReads r = w.reads();
    std::vector<glm::ivec3> cubes, detailed;
    for (int i = 0; i < 1 << 20; ++i) {
        const uint32_t h = hash3(i, 7, 11);
        cubes.push_back({int(h % 16), int((h >> 4) % 16), int((h >> 8) % 16)});
    }
    for (const glm::ivec3& c : cubes)
        if (r.typeAt(c) == VoxelLocation::SUBDIVIDED && detailed.size() < 100000) detailed.push_back(c);
    using Clock = std::chrono::steady_clock;
    volatile int sink = 0;
    auto t0 = Clock::now();
    for (const glm::ivec3& c : cubes) sink += int(r.typeAt(c));
    const double typeNs = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / cubes.size();
    t0 = Clock::now();
    float fs = 0.0f;
    for (const glm::ivec3& c : detailed) fs += r.subVoxelFloor(c);
    const double floorNs = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / std::max<size_t>(1, detailed.size());
    sink += int(fs);
    // attribution: the test's store lookup alone (the StoreOf std::function over a std::map), the same calls
    const WorldReads::StoreOf storeOf = [&w](glm::ivec3 c) -> const BlockStore* {
        auto it = w.stores.find(c);
        return it == w.stores.end() ? nullptr : &it->second;
    };
    t0 = Clock::now();
    for (const glm::ivec3& c : cubes) sink += storeOf(glm::ivec3(World::fd(c.x, 32), World::fd(c.y, 32), World::fd(c.z, 32))) != nullptr;
    const double lookupNs = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / cubes.size();
    std::printf("[WorldReads] typeAt %.1f ns per call (%zu calls), of which the test's store lookup alone %.1f ns; "
                "subVoxelFloor on detailed blocks %.0f ns (%zu calls)\n", typeNs, cubes.size(), lookupNs, floorNs,
                detailed.size());
    EXPECT_LE(typeNs, 50.0);
    EXPECT_LE(floorNs, 2000.0);
}
