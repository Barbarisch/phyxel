#pragma once
// D4 (docs/VoxelFoundationPlan.md step 7): today's Chunk store was deleted. Headless rigs that stood their terrain up
// in an empty Chunk (initializeForLoading + addCube + chunkMap) now stand it up in the switched world's store -- one
// 1/8 m BlockStore per engine chunk, a full 8^3 block per old cube -- and route ChunkManager's voxel reads
// (hasVoxelAt / getVoxelTypeAt / subVoxelFloor / occupiedMicro) to it through setVoxFoundReads, the same hook the
// running engine uses. Solidity only: the reads carry no material names.

#include <map>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "core/ChunkManager.h"
#include "core/voxfound/WorldReads.h"

namespace Phyxel::Testing {

class CubeWorldReads {
public:
    /// Fill (or clear) the whole 1 m cube at world cube coordinate `cube`.
    void addCube(const glm::ivec3& cube, uint8_t material = 1) {
        const glm::ivec3 c(fd(cube.x), fd(cube.y), fd(cube.z));
        auto it = m_stores.find(c);
        if (it == m_stores.end())
            it = m_stores.emplace(c, Core::VoxFound::BlockStore(glm::ivec3(Core::VoxFound::WorldReads::kChunkVoxels))).first;
        it->second.fillBlock(cube - c * 32, material);
        if (m_cm) install(*m_cm);   // a new chunk changes what "loaded" means for the reads
    }
    void removeCube(const glm::ivec3& cube) { addCube(cube, 0); }
    /// Route `cm`'s reads here (and keep them current as cubes are added).
    void install(ChunkManager& cm) {
        m_cm = &cm;
        std::vector<glm::ivec3> cs;
        for (const auto& kv : m_stores) cs.push_back(kv.first);
        m_reads = std::make_unique<Core::VoxFound::WorldReads>(
            [this](glm::ivec3 c) -> const Core::VoxFound::BlockStore* {
                auto it = m_stores.find(c);
                return it == m_stores.end() ? nullptr : &it->second;
            },
            cs);
        cm.setVoxFoundReads(m_reads.get());
    }

private:
    struct Less {
        bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
            return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
        }
    };
    static int fd(int a) { return a >= 0 ? a / 32 : -((-a + 31) / 32); }
    std::map<glm::ivec3, Core::VoxFound::BlockStore, Less> m_stores;
    std::unique_ptr<Core::VoxFound::WorldReads> m_reads;
    ChunkManager* m_cm = nullptr;
};

}  // namespace Phyxel::Testing
