#pragma once
// docs/VoxelFoundationPlan.md F7.1 (section 7 step 1; prediction docs/evidence/voxfound_f7_prediction.txt R1-R6, N1) --
// the engine's voxel read interfaces answered from the new store.
//
// The store: one BlockStore of 256^3 voxels (1/8 m) per engine chunk (32 m); one 8^3 block = one old 1 m cube. The OLD
// semantics, restated on 1/8 m voxels:
//   typeAt(cube)        no solid voxel in the block -> EMPTY, all 512 -> CUBE, else SUBDIVIDED (O(1): the directory entry
//                       or the detailed block's solid-mask count); hasVoxelAt = not EMPTY.
//   subVoxelFloor(cube) empty -> 0; full -> -1 (solid); else the full voxel layers from the bottom, all the content in
//                       them -> layers / 8, otherwise -1 (the old rule, 8 layers instead of 3).
//   occupiedMicro(m)    the 1/8 m voxel under the centre of the 1/9 m micro cell m is solid (a stated sampling: 1/9 and
//                       1/8 m content cannot coincide).
// A chunk the store does not have reads empty / 0 / false, as an unloaded chunk does today.

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "core/Types.h"
#include "core/voxfound/StorageLab.h"

namespace Phyxel::Core::VoxFound {

class WorldReads {
public:
    static constexpr int kChunkVoxels = 256;   // an engine chunk (32 m) in 1/8 m voxels
    /// The store of engine chunk c (kChunkVoxels^3, its voxel (0,0,0) = world voxel c * 256), or nullptr.
    using StoreOf = std::function<const BlockStore*(glm::ivec3 chunk)>;

    WorldReads(StoreOf storeOf, std::vector<glm::ivec3> chunks)
        : m_storeOf(std::move(storeOf)), m_chunks(std::move(chunks)) {}

    VoxelLocation::Type typeAt(glm::ivec3 cube) const;
    bool hasVoxelAt(glm::ivec3 cube) const { return typeAt(cube) != VoxelLocation::EMPTY; }
    float subVoxelFloor(glm::ivec3 cube) const;
    bool occupiedMicro(glm::ivec3 micro) const;
    /// The material of world voxel v (air where the store has no chunk).
    uint8_t voxel(glm::ivec3 v) const;
    /// The chunks the store has (engine chunk coordinates): what "loaded" means for the routed reads.
    const std::vector<glm::ivec3>& chunks() const { return m_chunks; }

private:
    StoreOf m_storeOf;
    std::vector<glm::ivec3> m_chunks;
};

}  // namespace Phyxel::Core::VoxFound
