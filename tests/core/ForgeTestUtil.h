#pragma once

// Shared helpers for the WorldForge realization tests (RoadFieldSeamTest, BridgeSpanTest):
// generated chunks read as a MICRO-cell occupancy field for TraversalProbe, so a test can
// ask whether a character-box can actually WALK the road, deck or ford the generator emits.

#include <map>
#include <memory>
#include <tuple>

#include <glm/glm.hpp>

#include "core/Chunk.h"
#include "core/WorldGenerator.h"

namespace Phyxel::ForgeTest {

inline int floorDiv32(int a) { return a >= 0 ? a / 32 : (a - 31) / 32; }
inline int floorDivN(int a, int n) { return a >= 0 ? a / n : (a - n + 1) / n; }
inline int floorModN(int a, int n) { return a - floorDivN(a, n) * n; }

// Generated chunks as a MICRO-cell occupancy field (1 cube = 9 micros per axis) for
// TraversalProbe — cubes, subcubes and microcubes all count as solid. Chunks generate
// lazily on first touch and are cached.
struct ChunkMicroWorld {
    WorldGenerator& gen;
    std::map<std::tuple<int, int, int>, std::unique_ptr<Chunk>> chunks;
    explicit ChunkMicroWorld(WorldGenerator& g) : gen(g) {}
    Chunk& chunkAt(int cx, int cy, int cz) {
        auto key = std::make_tuple(cx, cy, cz);
        auto it = chunks.find(key);
        if (it == chunks.end()) {
            auto c = std::make_unique<Chunk>(glm::ivec3(cx, cy, cz) * 32);
            c->initializeForLoading();
            gen.generateChunk(*c, glm::ivec3(cx, cy, cz));
            it = chunks.emplace(key, std::move(c)).first;
        }
        return *it->second;
    }
    bool solidMicro(int mx, int my, int mz) {
        const int wx = floorDivN(mx, 9), wy = floorDivN(my, 9), wz = floorDivN(mz, 9);
        Chunk& c = chunkAt(floorDiv32(wx), floorDiv32(wy), floorDiv32(wz));
        const glm::ivec3 lp(floorModN(wx, 32), floorModN(wy, 32), floorModN(wz, 32));
        if (c.getCubeAt(lp)) return true;
        const glm::ivec3 sp(floorModN(mx, 9) / 3, floorModN(my, 9) / 3, floorModN(mz, 9) / 3);
        if (c.hasSubcubeAt(lp, sp)) return true;
        const glm::ivec3 mp(floorModN(mx, 3), floorModN(my, 3), floorModN(mz, 3));
        return c.getMicrocubeAt(lp, sp, mp) != nullptr;
    }
};

// Generate the chunk holding one world cell (initializeForLoading wires the voxel-manager
// callbacks — the FloraMarginTest pattern; a bare Chunk throws bad_function_call).
inline std::unique_ptr<Chunk> genChunkAt(WorldGenerator& gen, const glm::ivec3& chunkCoord) {
    auto chunk = std::make_unique<Chunk>(chunkCoord * 32);
    chunk->initializeForLoading();
    gen.generateChunk(*chunk, chunkCoord);
    return chunk;
}

}  // namespace Phyxel::ForgeTest
