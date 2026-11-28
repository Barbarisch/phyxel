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

}  // namespace Phyxel::Core::VoxFound
