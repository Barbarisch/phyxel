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

/// One far-tree impostor instance (world-look A1 rethink, 2026-08-02). Built on the
/// far-terrain worker from the DETERMINISTIC flora plan (WorldGenerator::planFlora) — a pure
/// function of the seed, so trees exist for tiles the camera has NEVER visited and no chunk
/// data is involved. This replaces the rejected chunk-squash representation for far trees
/// ("weird floating voxels"): each tree renders as a camera-facing procedural card.
struct FarTreeInstance {
    float localX = 0.0f;    ///< tile-local X (0..tileSize)
    float worldY = 0.0f;    ///< ABSOLUTE trunk-base Y, anchored to the tile's QUANTIZED surface
                            ///< (quantizeTop - bias) so trees sit ON the far mesh, never float
    float localZ = 0.0f;    ///< tile-local Z (0..tileSize)
    float height = 8.0f;    ///< world units
    float canopyR = 2.5f;   ///< canopy half-width (world units)
    /// bits 0-1: shape class (0 broadleaf, 1 conifer, 2 palm, 3 dead/bare)
    /// bits 2-7: species id (index into TreeSpeciesTable — the instanced-mesh tier keys on it)
    /// bits 8-15 / 16-23 / 24-31: canopy tint R/G/B
    uint32_t packed = 0;
    uint32_t _pad = 0;      ///< keep 4-float + 2-uint = 32B stride, GPU-friendly
};

inline uint32_t packFarTree(uint32_t shapeClass, uint32_t speciesId,
                            uint8_t r, uint8_t g, uint8_t b) {
    return (shapeClass & 0x3u) | ((speciesId & 0x3Fu) << 2) |
           (uint32_t(r) << 8) | (uint32_t(g) << 16) | (uint32_t(b) << 24);
}
inline uint32_t farTreeSpecies(uint32_t packed) { return (packed >> 2) & 0x3Fu; }

/// Contiguous run of one species inside a tile's tree-instance array (instances are sorted
/// by species at build). Lets the instanced-mesh tier draw per (tile, species) with a plain
/// firstInstance offset — no per-frame re-bucketing.
struct TreeSpeciesRange {
    uint16_t speciesId = 0;
    uint32_t first = 0;
    uint32_t count = 0;
};

/// CPU-side mesh for one far-terrain tile, ready for GPU upload.
struct FarTileMesh {
    std::vector<FarVertex> vertices;
    std::vector<uint32_t>  indices;
    std::vector<FarTreeInstance> trees;  ///< impostor instances, SORTED by species id
    std::vector<TreeSpeciesRange> treeRanges;  ///< per-species runs into `trees`
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
