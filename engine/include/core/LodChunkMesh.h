#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/LodBrick.h"
#include "core/Types.h"

namespace Phyxel {

class Chunk;

namespace Core {

/**
 * @brief C4 of docs/ContinuousLodPlan.md — THE CUT.
 *
 * Turns a chunk into renderable geometry at a CHOSEN LOD level: build the squash
 * pyramid (C0), take level N, and emit one quad per exposed coarse-cell face
 * encoded as `scaleLevel == 3` with `lodLevel = N`, which the vertex shaders
 * expand to a 2^N-cube quad.
 *
 * This is the piece the whole plan exists for. C0 produced the squash, C1 produced
 * the metric that picks N; this renders the result.
 *
 * Level 0 is intentionally supported and must reproduce the fine surface's face
 * SET exactly (same exposed faces, unit size) — that 1:1 identity is what makes the
 * coarse path testable against the fine path rather than only eyeballed.
 */
class LodChunkMesh {
public:
    /// Build a cube-resolution LodVolume from a chunk's actual voxels.
    /// A cube is solid if the chunk has a cube there; coverage is full. Sub/microcube
    /// detail is NOT a rung on this ladder (plan §2.1) — it is the cube's appearance,
    /// so a cube containing only sub/micro geometry still counts as occupied, at the
    /// coverage its finer content implies.
    static LodVolume volumeFromChunk(const Chunk& chunk,
                                     std::vector<std::string>* outPalette = nullptr);

    /// Emit coarse faces for `volume` (already squashed to the wanted level).
    /// A face is emitted where a solid cell abuts a non-solid neighbour — the same
    /// exposed-face rule the fine mesher uses, applied at cell resolution.
    /// `textureIndexFor` maps a palette id to a texture-array index for the face.
    static void emitFaces(const LodVolume& volume,
                          const std::vector<std::string>& palette,
                          std::vector<InstanceData>& out);

    /// Convenience: chunk -> pyramid -> level -> faces.
    /// `level` is clamped to the pyramid depth. level 0 == the fine cube surface.
    static void buildForLevel(const Chunk& chunk, int level, const SquashConfig& cfg,
                              std::vector<InstanceData>& out);

    /// Faces the fine (level-0) surface would emit, for the 1:1 identity check.
    static size_t fineFaceCount(const Chunk& chunk);
};

} // namespace Core
} // namespace Phyxel
