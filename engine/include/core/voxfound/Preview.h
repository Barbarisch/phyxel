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

/// F2.8: one chunk's slice of the face and sprig lists, drawn at its own origin (records are chunk-relative).
struct PreviewChunk {
    glm::dvec3 originMetres{0.0};
    uint32_t firstFace = 0, faceCount = 0, firstSprig = 0, sprigCount = 0;
    glm::ivec3 chunk{0};                                          // F2.10: engine chunk coordinate
    FaceConnect connect{0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F};      // F2.10: block-solidity visibility (Mesher.h)
};

/// F2.11: a run of one chunk's faces drawn by one indirect command (Mesher.h clusterFaces), box in metres relative to
/// Preview::anchorMetres. `chunk` indexes Preview::chunks.
struct PreviewCluster {
    glm::vec3 lo{0.0f}, hi{0.0f};
    uint32_t firstFace = 0, count = 0, chunk = 0;
    uint32_t level = 0;   // F2.14: the pyramid level its faces belong to (Preview::lodClusters)
};

struct Preview {
    std::vector<PreviewCluster> clusters;   // F2.11 (region previews only; empty = per-chunk direct draws)
    glm::dvec3 anchorMetres{0.0};           // F2.11: origin of the cluster boxes
    int clusterBlocks = 0;                  // F2.11: blocks per cluster side the faces were grouped by
    glm::dvec3 originMetres{0.0};    // world position of voxel (0,0,0)'s min corner (used when `chunks` is empty)
    std::vector<PackedFace> faces;   // drawn as-is, 8 B each
    std::vector<PreviewChunk> chunks;   // empty: one area, every face/sprig at originMetres
    /// The ranges to draw: `chunks`, or one range covering everything at originMetres.
    std::vector<PreviewChunk> ranges() const {
        if (!chunks.empty()) return chunks;
        return {PreviewChunk{originMetres, 0u, uint32_t(faces.size()), 0u, uint32_t(sprigs.size())}};
    }
    std::vector<uint32_t> faceTex = std::vector<uint32_t>(kFaceTexWords, 0u);   // the device table, see above
    std::vector<PreviewSprig> sprigs;
    bool shown = true;               // F2.8 A/B: false = kept on the device but not drawn in any pass
    bool castFaces = true;           // F2.4 diagnosis switches: which parts write the shadow maps
    bool castSprigs = true;
    uint64_t revision = 0;           // bumped on every change; the renderer re-uploads when it moves

    /// F2.13 (§4.6 item 7): the pyramid levels built with a region (index = level; level k voxels are 2^k / 8 m,
    /// faces in level-0 units). Per chunk (same order as `chunks`): its face range in that level's `faces`.
    struct Level {
        std::vector<PackedFace> faces;
        std::vector<uint32_t> firstFace, faceCount;
        std::vector<PreviewCluster> clusters;
    };
    std::vector<Level> levels;
    int drawLevel = 0;
    /// Draws level k for the whole region: copies its faces, ranges and clusters into the drawn fields and bumps
    /// the revision (the renderer re-uploads). Returns false if level k was not built.
    bool selectLevel(int k) {
        if (k < 0 || size_t(k) >= levels.size() || levels[size_t(k)].firstFace.size() != chunks.size()) return false;
        const Level& L = levels[size_t(k)];
        faces = L.faces;
        clusters = L.clusters;
        for (size_t i = 0; i < chunks.size(); ++i) {
            chunks[i].firstFace = L.firstFace[i];
            chunks[i].faceCount = L.faceCount[i];
        }
        drawLevel = k;
        lodClusters.clear();
        lodFirst.clear();
        lodCount.clear();
        lodMaxLevel = 0;
        ++revision;
        return true;
    }

    /// F2.14 (§3.5a point 3): the level chosen by size on screen (Mesher.h lodPickLevel / lodKeeps, per FRAGMENT).
    /// selectLod() puts EVERY built level in `faces`, level 0 first, so `chunks` and `clusters` keep naming level 0
    /// (what the shadow passes and occlusion read); lodFirst/lodCount[level][chunk] are each level's chunk ranges and
    /// lodClusters every level's clusters, tagged with their level. lodOn = the main view picks levels by size on
    /// screen at lodPxPerVoxel pixels per voxel (the tier knob); off = level 0 everywhere, as before.
    std::vector<PreviewCluster> lodClusters;
    std::vector<std::vector<uint32_t>> lodFirst, lodCount;
    int lodMaxLevel = 0;
    bool lodOn = false;
    float lodPxPerVoxel = 1.0f;
    bool selectLod() {
        if (levels.empty() || levels[0].firstFace.size() != chunks.size()) return false;
        for (const Level& L : levels)
            if (L.firstFace.size() != chunks.size()) return false;
        faces.clear();
        lodClusters.clear();
        lodFirst.assign(levels.size(), {});
        lodCount.assign(levels.size(), {});
        for (size_t k = 0; k < levels.size(); ++k) {
            const Level& L = levels[k];
            const uint32_t base = uint32_t(faces.size());
            faces.insert(faces.end(), L.faces.begin(), L.faces.end());
            for (size_t i = 0; i < chunks.size(); ++i) {
                lodFirst[k].push_back(base + L.firstFace[i]);
                lodCount[k].push_back(L.faceCount[i]);
            }
            for (PreviewCluster c : L.clusters) {
                c.firstFace += base;
                c.level = uint32_t(k);
                lodClusters.push_back(c);
            }
        }
        clusters = levels[0].clusters;
        for (size_t i = 0; i < chunks.size(); ++i) {
            chunks[i].firstFace = levels[0].firstFace[i];
            chunks[i].faceCount = levels[0].faceCount[i];
        }
        lodMaxLevel = int(levels.size()) - 1;
        drawLevel = 0;
        ++revision;
        return true;
    }
};

}  // namespace Phyxel::Core::VoxFound
