#pragma once

#include "graphics/FarTerrainTypes.h"
#include <memory>

namespace Phyxel { class WorldGenerator; }

namespace Phyxel {
namespace Graphics {

/// Pure-CPU far-terrain tile mesher: samples WorldGenerator::sampleSurface over a
/// 64x64 column grid and emits blocky quantized columns — greedy-merged top faces,
/// exact side walls between columns of differing quantized height, and border skirts
/// that hang below tile edges (covers cross-ring quantization cracks).
///
/// No Chunk objects, no physics, no light BFS. Designed to run on the far-terrain
/// worker thread: owns a PRIVATE WorldGenerator copy (sampleColumn is non-const —
/// never share the streaming generator across threads).
class FarTerrainMesher {
public:
    static constexpr int kColumns    = 64;  ///< columns per tile side
    static constexpr int kSkirtSteps = 2;   ///< skirt depth in units of step
    /// Every tile's geometry is pushed this far BELOW its quantized surface so far
    /// terrain sits strictly under coincident real-chunk surfaces at all view angles
    /// (docs/FarRepresentationProviders.md compositing layer; the pipeline depth bias
    /// alone left flat top-down overlaps bleeding through). Coarser rings drop an extra
    /// 0.01/ring for cross-ring ordering. Kept small — invisible on ±step-quantized LOD.
    static constexpr float kBelowSurfaceBias = 0.5f;

    FarTerrainMesher(std::unique_ptr<WorldGenerator> generator, FarMaterialResolver resolver);
    ~FarTerrainMesher();

    /// Top surface plane of a column, quantized DOWN to a multiple of step. The real
    /// voxel surface plane is surfaceY + 1; quantizing down guarantees far terrain never
    /// rises above real chunk geometry, so real chunks always win the depth test in the
    /// near/far overlap band. Result is in (surfaceY + 1 - step, surfaceY + 1].
    static int quantizeTop(int surfaceY, int step);

    /// Build the mesh for one tile. Tile world size = kColumns * step; world origin =
    /// (key.x, key.z) * tileSize. Deterministic for a given generator config.
    FarTileMesh buildTile(const FarTileKey& key, int step);

private:
    std::unique_ptr<WorldGenerator> m_generator;
    FarMaterialResolver m_resolveTex;
};

} // namespace Graphics
} // namespace Phyxel
