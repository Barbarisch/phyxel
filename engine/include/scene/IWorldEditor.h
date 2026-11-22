#pragma once
// docs/VoxelFoundationPlan.md F7.4 (section 7 step 4; prediction docs/evidence/voxfound_f7_prediction.txt E1-E4) --
// editing the world in the new store. VoxelInteractionSystem routes the player's hover / break / place here when an
// editor is set (the world switch sets the region's): picking returns a VOXEL position (1/8 m), edits are brushes of
// 1, 2, 4 or 8 voxels.

#include <cstddef>
#include <string>

#include <glm/glm.hpp>

namespace Phyxel {

struct WorldEditHit {
    bool hit = false;
    glm::ivec3 voxel{0};    // world 1/8 m voxel hit
    glm::ivec3 normal{0};   // the face the ray entered through (voxel + normal = the cell in front)
    std::string material;
};

class IWorldEditor {
public:
    virtual ~IWorldEditor() = default;
    /// The first solid voxel along the ray (origin and maxMetres in metres; dir need not be normalised).
    virtual WorldEditHit pick(const glm::vec3& origin, const glm::vec3& dir, float maxMetres) const = 0;
    /// Writes `material` ("" = air) into the brush cell of `size` voxels (1, 2, 4, 8) holding world voxel `voxel`.
    /// Returns the voxels changed.
    virtual size_t brush(const glm::ivec3& voxel, int size, const std::string& material) = 0;
};

}  // namespace Phyxel
