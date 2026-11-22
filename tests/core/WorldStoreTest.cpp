// WorldStoreTest -- docs/VoxelFoundationPlan.md F7.2 (prediction: docs/evidence/voxfound_f7_prediction.txt G1-G5): the
// generators write the new store -- bulk writes exact across chunks, a realized building painted cell for cell, NPCs
// pathing inside it on the new store, generated terrain voxel for voxel.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/BuildingProgram.h"
#include "core/ChunkManager.h"
#include "core/MicroCanvas.h"
#include "core/NPCManager.h"
#include "core/NavGraph.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureGrid.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/WorldGenerator.h"
#include "core/voxfound/WorldStore.h"

using namespace Phyxel;
using namespace Phyxel::Core;
using Phyxel::Core::VoxFound::WorldReads;
using Phyxel::Core::VoxFound::WorldStore;

namespace {

struct Less {
    bool operator()(glm::ivec3 a, glm::ivec3 b) const {
        return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
    }
};

// the hall house of NavGraphMicroTest (section C), realized on the 1/8 m grid
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

}  // namespace

// G1: bulk writes exact across chunk borders (negative coordinates included); whole blocks stay uniform
TEST(WorldStore, BulkWritesEqualADenseReferenceAcrossChunks) {
    WorldStore ws;
    std::map<glm::ivec3, std::string, Less> ref;
    auto refBox = [&](glm::ivec3 lo, glm::ivec3 hi, const std::string& m) {
        for (int x = lo.x; x <= hi.x; ++x)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int z = lo.z; z <= hi.z; ++z) {
                    if (m.empty()) ref.erase({x, y, z});
                    else ref[{x, y, z}] = m;
                }
    };
    std::string err;
    ASSERT_TRUE(ws.fillBox({-20, -3, -9}, {37, 12, 5}, "Stone", &err)) << err;   // crosses x = 0, y = 0, z = 0 (chunk borders)
    refBox({-20, -3, -9}, {37, 12, 5}, "Stone");
    ASSERT_TRUE(ws.fillBox({-4, 0, -4}, {3, 7, 3}, "", &err)) << err;           // carve
    refBox({-4, 0, -4}, {3, 7, 3}, "");
    ASSERT_TRUE(ws.fillBox({250, 250, 250}, {261, 261, 261}, "Bricks", &err)) << err;   // a corner of 8 chunks
    refBox({250, 250, 250}, {261, 261, 261}, "Bricks");
    std::vector<std::pair<glm::ivec3, std::string>> cells;
    for (int i = 0; i < 2000; ++i) {
        const glm::ivec3 v(int((i * 37) % 59) - 48, int((i * 53) % 61) - 30, int((i * 71) % 89) - 44);   // x <= 10
        cells.push_back({v, i % 3 ? "Wood" : ""});
    }
    ASSERT_TRUE(ws.paint(cells, &err)) << err;
    for (const auto& [v, m] : cells) {
        if (m.empty()) ref.erase(v);
        else ref[v] = m;
    }
    size_t bad = 0, checked = 0;
    for (int x = -60; x <= 60; ++x)
        for (int y = -40; y <= 40; ++y)
            for (int z = -50; z <= 50; ++z) {
                auto it = ref.find({x, y, z});
                const std::string want = it == ref.end() ? "" : it->second;
                bad += ws.materialAt({x, y, z}) != want;
                ++checked;
            }
    for (int x = 246; x <= 265; ++x)
        for (int y = 246; y <= 265; ++y)
            for (int z = 246; z <= 265; ++z) {
                auto it = ref.find({x, y, z});
                bad += ws.materialAt({x, y, z}) != (it == ref.end() ? "" : it->second);
                ++checked;
            }
    // a block the first box covers entirely (and nothing later touches) stays one uniform entry: chunk (0, 0, -1),
    // block (2, 0, 31) = voxels x 16-23, y 0-7, z -8..-1
    uint8_t m = 0;
    const auto* s = ws.chunkStore({0, 0, -1});
    ASSERT_NE(s, nullptr);
    const bool uniform = s->uniformBlock({2, 0, 31}, m);
    std::printf("[WorldStore] %zu voxels checked against the dense reference: %zu differ; a covered block uniform: %d\n",
                checked, bad, int(uniform));
    EXPECT_EQ(bad, 0u);
    EXPECT_TRUE(uniform) << "fillBox wrote a covered block voxel by voxel";
    EXPECT_EQ(ws.palette()[m], "Stone");
}

// G2 + G3: a realized building painted into the store, cell for cell; NPCs path inside it on the new store
TEST(WorldStore, RealizedBuildingPaintedAndPathableOnTheNewStore) {
    BuildingProgram p = hallHouseProgram();
    const ProgRoom* service = roomByPurpose(p.stories[0], "service");
    const ProgRoom* solar = roomByPurpose(p.stories[0], "solar");
    ASSERT_NE(service, nullptr);
    ASSERT_NE(solar, nullptr);
    const StructureGrid g8 = StructureGrid::foundation();
    auto paintHouse = [&](const BuildingProgram& prog, WorldStore& ws, StructureRealizer::ShellResult& sh) {
        sh = StructureRealizer::realizeShell(prog, cottageStyle(), g8);
        ASSERT_TRUE(sh.ok) << sh.error;
        ASSERT_EQ(sh.canvas.cellsPerM(), 8);
        std::string err;
        // ground: cubes y = 15 under and around the footprint; the house's canvas (0,0,0) at cube (-6, 16, 3):
        // it straddles the chunk border at x = 0
        ASSERT_TRUE(ws.fillBox({-10 * 8, 15 * 8, -2 * 8}, {(-6 + prog.footprintW + 4) * 8 - 1, 16 * 8 - 1,
                                                          (3 + prog.footprintD + 5) * 8 - 1}, "Stone", &err)) << err;
        ASSERT_TRUE(ws.paintCanvas(sh.canvas, {-6 * 8, 16 * 8, 3 * 8}, &err)) << err;
    };
    {
        WorldStore ws;
        StructureRealizer::ShellResult sh;
        paintHouse(p, ws, sh);
        if (HasFatalFailure()) return;
        // G2: cell for cell
        size_t bad = 0;
        const std::vector<glm::ivec3> cells = sh.canvas.occupiedCells();
        for (const glm::ivec3& c : cells)
            bad += ws.materialAt(glm::ivec3(-6 * 8, 16 * 8, 3 * 8) + c) != sh.canvas.materialAt(c.x, c.y, c.z);
        glm::ivec3 lo, hi;
        ASSERT_TRUE(sh.canvas.microBounds(lo, hi));
        size_t extra = 0;   // nothing written past the canvas's cells inside its bounds (the ground is below y = 0)
        for (int x = lo.x; x <= hi.x; ++x)
            for (int y = std::max(lo.y, 0); y <= hi.y; ++y)
                for (int z = lo.z; z <= hi.z; ++z)
                    if (!sh.canvas.occupiedMicro(x, y, z) && ws.voxel(glm::ivec3(-6 * 8, 16 * 8, 3 * 8) + glm::ivec3(x, y, z)))
                        ++extra;
        std::printf("[WorldStore] hall house (grid 8): %zu canvas cells, %zu differ, %zu extra voxels; chunks %zu\n",
                    cells.size(), bad, extra, ws.chunks().size());
        EXPECT_EQ(bad, 0u);
        EXPECT_EQ(extra, 0u);
        EXPECT_GE(ws.chunks().size(), 2u) << "the house must straddle a chunk border";
        // G3: navigation on the new store
        const WorldReads r = ws.reads();
        ChunkManager cm;
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        cm.setVoxFoundReads(&r);
        NPCManager npc;
        npc.setChunkManager(&cm);
        npc.buildNavGrid();
        ASSERT_NE(npc.getNavGraph(), nullptr);
        const int floorTop = sh.floorTopByStory.empty() ? sh.floorTopMicro : sh.floorTopByStory[0];
        auto feet = [&](const ProgRoom& room) {
            return glm::vec3(-6 + room.rect.x + room.rect.w / 2 + 0.5f, 16.0f + floorTop / 8.0f,
                             3 + room.rect.z + room.rect.d / 2 + 0.5f);
        };
        NavAgentProfile agent;
        const auto path = npc.getNavGraph()->findPath(feet(*service), feet(*solar), agent);
        std::printf("[WorldStore] service -> solar on the new store: %s (%zu nodes); floor top %d cells\n",
                    path.found ? "found" : "NOT found", path.nodes.size(), floorTop);
        EXPECT_TRUE(path.found) << "NPCs could not walk service -> solar in a building on the new store";
    }
    {   // TEETH: the interior doors dropped
        BuildingProgram sealed = p;
        std::vector<ProgPortal> exteriorOnly;
        for (const auto& po : sealed.stories[0].portals)
            if (po.a == "exterior" || po.b == "exterior") exteriorOnly.push_back(po);
        sealed.stories[0].portals = exteriorOnly;
        WorldStore ws;
        StructureRealizer::ShellResult sh;
        paintHouse(sealed, ws, sh);
        if (HasFatalFailure()) return;
        const WorldReads r = ws.reads();
        ChunkManager cm;
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        cm.setVoxFoundReads(&r);
        NPCManager npc;
        npc.setChunkManager(&cm);
        npc.buildNavGrid();
        ASSERT_NE(npc.getNavGraph(), nullptr);
        const int floorTop = sh.floorTopByStory.empty() ? sh.floorTopMicro : sh.floorTopByStory[0];
        auto feet = [&](const ProgRoom& room) {
            return glm::vec3(-6 + room.rect.x + room.rect.w / 2 + 0.5f, 16.0f + floorTop / 8.0f,
                             3 + room.rect.z + room.rect.d / 2 + 0.5f);
        };
        NavAgentProfile agent;
        EXPECT_FALSE(npc.getNavGraph()->findPath(feet(*service), feet(*solar), agent).found)
            << "with no interior doors service -> solar must be unpathable";
    }
}

// G4: generated terrain through forEachRegionChunk, voxel for voxel
TEST(WorldStore, GeneratedTerrainEqualsTheRegionChunks) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    WorldStore ws;
    std::string err;
    const int cy = int(std::floor(gen.sampleSurface(32, 32).bedHeight / 32.0f));   // the chunk row holding the surface
    const glm::ivec3 lo(0, cy, 0), hi(1, cy, 1);
    ASSERT_TRUE(ws.fromRegion(gen, lo, hi, nullptr, &err)) << err;
    size_t bad = 0, solid = 0, chunks = 0;
    WorldGenerator gen2(WorldGenerator::GenerationType::Perlin, 7u);
    Core::VoxFound::forEachRegionChunk(gen2, lo, hi, nullptr,
                                       [&](const Core::VoxFound::RegionChunk& rc, const std::vector<std::string>& pal) {
        ++chunks;
        const int n = WorldStore::kChunkVoxels;
        for (int x = 0; x < n; ++x)
            for (int y = 0; y < n; ++y)
                for (int z = 0; z < n; ++z) {
                    const uint8_t gm = rc.grid.mat[rc.grid.idx({x, y, z})];
                    const std::string want = gm ? pal[gm] : std::string();
                    const uint8_t sm = ws.voxel(rc.originVoxel + glm::ivec3(x, y, z));
                    const std::string got = sm ? ws.palette()[sm] : std::string();
                    solid += gm != 0;
                    bad += want != got;
                }
    });
    std::printf("[WorldStore] generated terrain: %zu chunks, %zu solid voxels, %zu differ; palette %zu\n", chunks,
                solid, bad, ws.palette().size());
    EXPECT_EQ(chunks, 4u);
    EXPECT_GT(solid, 0u);
    EXPECT_LT(solid, size_t(chunks) * 256u * 256u * 256u) << "the chunks must hold the surface (air and ground)";
    EXPECT_EQ(bad, 0u);
}

// G5: refusals
TEST(WorldStore, Refusals) {
    WorldStore ws;
    std::string err;
    MicroCanvas c9(9);
    c9.setMicroCell(0, 0, 0, "Stone");
    EXPECT_FALSE(ws.paintCanvas(c9, {0, 0, 0}, &err)) << "a 1/9 m canvas";
    EXPECT_EQ(ws.voxel({0, 0, 0}), 0u);
    for (int i = 1; i < 256; ++i) ASSERT_GE(ws.materialId("m" + std::to_string(i)), 1);
    EXPECT_EQ(ws.materialId("one too many"), -1);
    EXPECT_FALSE(ws.fillBox({0, 0, 0}, {1, 1, 1}, "one too many", &err));
    EXPECT_FALSE(ws.paint({{{0, 0, 0}, "m1"}, {{1, 0, 0}, "one too many"}}, &err)) << "all or nothing";
    EXPECT_EQ(ws.voxel({0, 0, 0}), 0u) << "a refused batch wrote something";
}
