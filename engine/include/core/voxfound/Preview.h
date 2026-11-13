#pragma once
// docs/VoxelFoundationPlan.md F2.2 — the 1/8 m face preview handed to the renderer.
//
// The editor builds a 1/8 m area with the engine's generators, runs the GPU face builder on it and fills
// this; RenderCoordinator::renderVoxFoundPreview draws it with shaders/voxfound_face.vert + voxel.frag in
// the main scene pass, and casts it into the mid + near shadow cascades (F2.4).

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/Mesher.h"

namespace Phyxel::Core::VoxFound {

/// The draw instance IS the 8 B face record (§4.6, F2.5): voxfound_face.vert reads PackedFace as-is and looks its
/// texture word up on the device: faceTex[material * 6 + faceID] = texture index (bits 0-15) | flags (16-31),
/// where material = the area's palette index (PackedFace b bits 16-23) and faceID 0 +Z 1 -Z 2 +X 3 -X 4 +Y 5 -Y.
/// The table always has all 256 palette slots (kFaceTexWords), so no 8-bit material can read past its end.
static_assert(sizeof(PackedFace) == 8, "voxfound_face.vert instance stride");
constexpr size_t kFaceTexWords = 256 * 6;

/// F2.3 leaf sprig, drawn as today's leaf cards (FoliageRenderPipeline::renderVoxFound, voxfound_foliage.vert).
/// Same 8 B shape as FoliageInstanceData: position word + texture/block-light word.
struct PreviewSprig {
    uint32_t cell = 0;   // x 0-7 | y 8-15 | z 16-23 (1/8 m cell in the area) | sky 24-27
    uint32_t tex = 0;    // low 16 leaf texture index | 16-19 R | 20-23 G | 24-27 B block light
};
static_assert(sizeof(PreviewSprig) == 8, "voxfound_foliage.vert instance stride");

struct Preview {
    glm::dvec3 originMetres{0.0};    // world position of voxel (0,0,0)'s min corner
    std::vector<PackedFace> faces;   // drawn as-is, 8 B each
    std::vector<uint32_t> faceTex = std::vector<uint32_t>(kFaceTexWords, 0u);   // the device table, see above
    std::vector<PreviewSprig> sprigs;
    bool castFaces = true;           // F2.4 diagnosis switches: which parts write the shadow maps
    bool castSprigs = true;
    uint64_t revision = 0;           // bumped on every change; the renderer re-uploads when it moves
};

}  // namespace Phyxel::Core::VoxFound
