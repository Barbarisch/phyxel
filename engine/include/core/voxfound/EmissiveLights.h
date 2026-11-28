#pragma once
// docs/VoxelFoundationPlan.md step 7, D4 follow-up -- U3.2 "emissive voxels are real lights" on the switched world.
// Today's Chunk mesher collected these per chunk at bake time (ChunkRenderManager::EmissiveLight, deleted in D4); the
// switched world had no feed, so a glow block lit nothing. The same light model, restated on 1/8 m voxels:
//   * ONE light per 1 m cube (one 8^3 block): G-158, one light per fixture, never per voxel;
//   * position = the power-weighted centroid of the cube's emissive voxels; colour = their power-weighted hue;
//   * intensity = the BRIGHTEST constituent t (not the sum -- summing was the measured over-brightness);
//   * radius = max(2, 15 t sizeScale), sizeScale = clamp(1 + 0.25 ln(volume) / ln 27, 0.5, 1) with volume in cubes;
//   * extent = the bounding sphere of the emitting voxels' AABB around the centroid (G-157).
// t per material: emissive -> 1; masked (emissiveStrength > 0) -> clamp(strength 4, 2, 10) / 15; else not a light.
// Per cube, so the result cannot depend on the chunk grid (a cube lies in exactly one chunk).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/StorageLab.h"

namespace Phyxel::Core::VoxFound {

struct EmissiveLight {
    glm::vec3 worldPos{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 0.0f;
    float radius = 0.0f;
    float extent = 0.0f;
};

/// Per palette index: t (0 = not an emitter) and hue.
struct EmitterMaterial {
    float t = 0.0f;
    glm::vec3 hue{1.0f};
};

/// The emitter table of a palette, from MaterialRegistry (index 0 = air).
std::vector<EmitterMaterial> emitterMaterials(const std::vector<std::string>& palette);
/// True when any palette entry emits (the common no-glow world skips every scan).
bool anyEmitter(const std::vector<EmitterMaterial>& em);

/// The lights of engine chunk `chunk`'s store (256^3 voxels of 1/8 m; its voxel 0 = world voxel chunk * 256),
/// appended to `out` in cube order (x, y, z) -- a pure function of the voxels.
void emissiveLightsOfChunk(const BlockStore& store, glm::ivec3 chunk, const std::vector<EmitterMaterial>& em,
                           std::vector<EmissiveLight>& out);

// ---- D4 follow-up S3: the mirror plane (MaterialDef::isMirror; the renderer has ONE plane per frame) ----
/// Exposed mirror faces per direction (index 2 * axis + (dir > 0): -x, +x, -y, +y, -z, +z) and the sum of their
/// centres (world metres). Summed over chunks.
struct MirrorFaces {
    uint64_t count[6] = {0, 0, 0, 0, 0, 0};
    glm::dvec3 sum[6] = {};
};
/// Per palette index: 1 = a mirror material.
std::vector<uint8_t> mirrorMaterials(const std::vector<std::string>& palette);
bool anyMirror(const std::vector<uint8_t>& table);
/// Adds chunk `chunk`'s exposed mirror faces to `acc`. A neighbour outside the store is read through `outside`
/// (world voxel -> material) when given, else air -- give it, or a chunk border adds faces.
void mirrorFacesOfChunk(const BlockStore& store, glm::ivec3 chunk, const std::vector<uint8_t>& table, MirrorFaces& acc,
                        const std::function<uint8_t(glm::ivec3)>* outside = nullptr);
/// The plane: normal = the direction with the most faces (ties in index order), point = their mean centre.
bool mirrorPlane(const MirrorFaces& acc, glm::vec3& point, glm::vec3& normal);

}  // namespace Phyxel::Core::VoxFound
