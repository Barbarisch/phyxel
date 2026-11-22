#pragma once
// docs/VoxelFoundationPlan.md F7.2 (section 7 step 2; prediction docs/evidence/voxfound_f7_prediction.txt G1-G5) -- the
// world in the new store, and the generators' bulk-write API.
//
// One BlockStore (256^3 voxels of 1/8 m) per engine chunk (32 m) + one shared palette of material names (0 = air).
// Writes are bulk: fillBox (whole 8^3 blocks written as one directory entry), paint (a list of cells), paintCanvas (an
// 8-cells-per-metre MicroCanvas, the structure realizer's 1/8 m output: its solid cells; 1/9 m canvases are refused --
// they need conversion, plan 7a), fromRegion (terrain + flora through F2's forEachRegionChunk). reads() is F7.1's
// WorldReads over it. Part ids are NOT stored (the BlockStore has no part channel; owed for fracture, F7.5).

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/StorageLab.h"
#include "core/voxfound/WorldReads.h"

namespace Phyxel {
class WorldGenerator;
namespace Core {
class MicroCanvas;
}
}  // namespace Phyxel

namespace Phyxel::Core::VoxFound {

class WorldStore {
public:
    static constexpr int kChunkVoxels = WorldReads::kChunkVoxels;

    /// The palette index of a material name (added if new); 0 for "" / "air". -1 when the palette is full (255).
    int materialId(const std::string& name);
    const std::vector<std::string>& palette() const { return m_palette; }

    /// Writes material `name` into every voxel of the inclusive box [lo, hi] (world voxels). Blocks the box covers
    /// entirely are written as one uniform entry. false (nothing written) for an unknown palette overflow.
    bool fillBox(glm::ivec3 lo, glm::ivec3 hi, const std::string& name, std::string* err = nullptr);
    /// Writes each (world voxel, material name). All or nothing (palette overflow refused before any write).
    bool paint(const std::vector<std::pair<glm::ivec3, std::string>>& cells, std::string* err = nullptr);
    /// Writes an 8-cells-per-metre canvas's solid cells at originVoxel + cell. Refuses another grid.
    bool paintCanvas(const MicroCanvas& canvas, glm::ivec3 originVoxel, std::string* err = nullptr);
    /// Generated terrain (+ flora when floraDir is given) for the chunks [chunkLo, chunkHi], through F2's
    /// forEachRegionChunk (its palette mapped into this one by name).
    bool fromRegion(WorldGenerator& gen, glm::ivec3 chunkLo, glm::ivec3 chunkHi, const std::string* floraDir,
                    std::string* err = nullptr);

    /// The material name of world voxel v ("" = air or no chunk).
    std::string materialAt(glm::ivec3 v) const;
    uint8_t voxel(glm::ivec3 v) const;
    const BlockStore* chunkStore(glm::ivec3 chunk) const;
    std::vector<glm::ivec3> chunks() const;
    /// F7.1's reads over this store (valid while the store lives and gains no chunk).
    WorldReads reads() const;
    void collapse();   // ends an edit batch: changed detailed blocks that became uniform return to one entry

private:
    struct Less {
        bool operator()(glm::ivec3 a, glm::ivec3 b) const {
            return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
        }
    };
    BlockStore& storeFor(glm::ivec3 chunk);
    std::map<glm::ivec3, BlockStore, Less> m_chunks;
    std::vector<std::string> m_palette{"air"};
};

}  // namespace Phyxel::Core::VoxFound
