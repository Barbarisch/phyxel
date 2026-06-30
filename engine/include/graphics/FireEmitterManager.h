#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>

namespace Phyxel {

class VfxSystem;

namespace Graphics {

// Drives continuous flame VFX from the world's state=flaming voxels, so fire
// "materialises" from the embers automatically instead of being placed by hand.
//
// Each frame the RenderCoordinator gathers every flaming leaf-voxel world
// position (from the chunks' ChunkRenderManager seed lists) and calls sync().
// We reduce those to the PEAK voxel per horizontal cell — the highest ember in
// each spot — and spawn one small tapering "flame tongue" (a VfxSystem Fountain
// field) per peak, so a campfire reads as a few flame points rather than a
// dense wall of jets. Tongues whose voxel disappears (broken / unloaded /
// extinguished) are dismissed.
class FireEmitterManager {
public:
    void setVfx(VfxSystem* vfx) { m_vfx = vfx; }

    // Reconcile live tongues with the current set of flaming voxel world positions.
    void sync(const std::vector<glm::vec3>& flamingPositions);

    // Dismiss every live tongue (e.g. on scene teardown).
    void clear();

private:
    // Stable key for a voxel position quantised to the 1/9 (microcube) grid.
    static uint64_t voxelKey(const glm::vec3& p);
    // Key for a horizontal cell (used to keep only the highest ember per spot).
    static uint64_t columnKey(const glm::vec3& p);

    VfxSystem* m_vfx = nullptr;
    std::unordered_map<uint64_t, std::string> m_tongues;  // voxel key -> VFX field id
};

} // namespace Graphics
} // namespace Phyxel
