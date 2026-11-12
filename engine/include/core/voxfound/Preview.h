#pragma once
// docs/VoxelFoundationPlan.md F2.2 — the 1/8 m face preview handed to the renderer.
//
// The editor builds a 1/8 m area with the engine's generators, runs the GPU face builder on it and fills
// this; RenderCoordinator::renderVoxFoundPreview draws it with shaders/voxfound_face.vert + voxel.frag in
// the main scene pass. Main view only for now: the faces RECEIVE shadows but do not CAST them (the shadow
// passes draw chunk InstanceData only) — §4.6 item 6, owed.

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Phyxel::Core::VoxFound {

struct PreviewFace {
    uint32_t a = 0, b = 0;   // PackedFace (Mesher.h)
    uint32_t tex = 0;        // bits 0-15 texture index (MaterialRegistry, per material and face), 16-31 flags
};
static_assert(sizeof(PreviewFace) == 12, "voxfound_face.vert instance stride");

/// F2.3 leaf sprig, drawn as today's leaf cards (FoliageRenderPipeline::renderVoxFound, voxfound_foliage.vert).
/// Same 8 B shape as FoliageInstanceData: position word + texture/block-light word.
struct PreviewSprig {
    uint32_t cell = 0;   // x 0-7 | y 8-15 | z 16-23 (1/8 m cell in the area) | sky 24-27
    uint32_t tex = 0;    // low 16 leaf texture index | 16-19 R | 20-23 G | 24-27 B block light
};
static_assert(sizeof(PreviewSprig) == 8, "voxfound_foliage.vert instance stride");

struct Preview {
    glm::dvec3 originMetres{0.0};    // world position of voxel (0,0,0)'s min corner
    std::vector<PreviewFace> faces;
    std::vector<PreviewSprig> sprigs;
    bool castFaces = true;           // F2.4 diagnosis switches: which parts write the shadow maps
    bool castSprigs = true;
    uint64_t revision = 0;           // bumped on every change; the renderer re-uploads when it moves
};

}  // namespace Phyxel::Core::VoxFound
