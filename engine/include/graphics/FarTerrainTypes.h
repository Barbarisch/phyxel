#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Phyxel {
namespace Graphics {

/// Vertex for far-terrain LOD tiles. Dedicated 16-byte format — deliberately NOT the
/// chunk InstanceData: its 5-bit chunk-local coords and mandatory light words don't fit
/// a 64-column tile, and sharing it would couple far terrain to the fragile static
/// voxel pipeline state.
struct FarVertex {
    glm::vec3 pos;     ///< tile-local X/Z (0..tileSize), absolute world Y
    uint32_t  packed;  ///< bits 0-15: atlas texture index (voxel.frag encoding, bit15 = res class)
                       ///< bits 16-18: faceID (0=+Z, 1=-Z, 2=+X, 3=-X, 4=+Y top, 5=-Y)
};

inline uint32_t packFarVertex(uint16_t texIndex, uint32_t faceID) {
    return uint32_t(texIndex) | ((faceID & 0x7u) << 16);
}
inline uint16_t farVertexTexIndex(uint32_t packed) { return uint16_t(packed & 0xFFFFu); }
inline uint32_t farVertexFaceID(uint32_t packed)   { return (packed >> 16) & 0x7u; }

/// Identifies one far-terrain tile: ring index + tile grid coords (worldXZ / tileSize).
struct FarTileKey {
    int ring = 0;
    int x = 0;
    int z = 0;
    bool operator==(const FarTileKey& o) const { return ring == o.ring && x == o.x && z == o.z; }
};

struct FarTileKeyHash {
    size_t operator()(const FarTileKey& k) const {
        uint64_t h = (uint64_t(uint32_t(k.x)) << 32) ^ uint64_t(uint32_t(k.z)) ^ (uint64_t(uint32_t(k.ring)) << 17);
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33;
        return size_t(h);
    }
};

/// CPU-side mesh for one far-terrain tile, ready for GPU upload.
struct FarTileMesh {
    std::vector<FarVertex> vertices;
    std::vector<uint32_t>  indices;
    glm::ivec2 originXZ{0, 0};      ///< world-space min corner of the tile
    int   ring = 0;
    int   step = 0;                 ///< world units per column
    int   tileSize = 0;             ///< world units per side (= columns * step)
    float minY = 0.0f;              ///< world-space Y bounds (frustum-cull AABB)
    float maxY = 0.0f;
};

/// Resolves a material name + faceID to a packed atlas texture index. Injectable so the
/// mesher is unit-testable without MaterialRegistry/Vulkan (tests pass a fake).
using FarMaterialResolver = std::function<uint16_t(const std::string& material, int faceID)>;

} // namespace Graphics
} // namespace Phyxel
