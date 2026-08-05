#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Phyxel {

class VoxelTemplate;

namespace Core {

/**
 * @brief World Rendering v2, M1 (docs/WorldRenderV2Plan.md) — the template voxel mip chain.
 *
 * The user's bar, verbatim: "if a tree costs 1000 voxels up close, as you get away it would
 * step cleanly between 950 voxels, 900 voxels, 850 voxels…". Voxel grids can't decimate
 * continuously, but they can step at ~1.4x cell size per level (≈2.5-3x fewer cells) with
 * dithered cross-fades between levels — perceptually smooth, which is what the demo-class
 * engines ship.
 *
 * Because trees (and every placed object) are TEMPLATE STAMPS, decimation happens once per
 * template, not per instance: a forest of ten thousand oaks costs one chain. This is the
 * voxel-native equivalent of authored mesh LOD chains (the Knightland teardown: three
 * hand-made low_Tree meshes instanced everywhere).
 *
 * THE OPERATOR IS THE POINT. The old chunk-squash (OR-occupancy) was rejected by the user
 * ("weird floating voxels") for defects this builder rules out BY CONSTRUCTION, each pinned
 * by a test:
 *   - majority-material cells (a cell that is 90% leaf renders as leaf — never OR-promoted);
 *   - a coverage floor so sparse cells don't inflate the silhouette (bounded fattening);
 *   - trunk preservation (Log-class cells survive below the coverage floor, so a decimated
 *     tree never becomes a floating lollipop without a stem);
 *   - island culling (no 26-connected component below the debris threshold — stray floating
 *     cells cannot exist in the output).
 *
 * Levels are DERIVED representations for the distant-instanced tier (M2); the authored
 * template itself remains the native near-field representation.
 */
class TemplateLodChain {
public:
    /// Level cell edge lengths in MICROCUBE units (9 micros = 1 voxel). ~1.4-1.5x per step:
    /// 0.44, 0.67, 1.0, 1.44, 2.0, 3.0 voxels — six levels from near-native to horizon-coarse.
    static constexpr int kCellSizesMicros[6] = {4, 6, 9, 13, 18, 27};
    static constexpr int kLevelCount = 6;

    /// Minimum occupied fraction for a non-trunk cell to even be a CANDIDATE.
    static constexpr float kCoverageFloor = 0.10f;
    /// Trunk-class (Log*) content keeps a cell alive down to this coverage — the stem must
    /// survive decimation even when it is thinner than the cell.
    static constexpr float kTrunkFloor = 0.03f;
    /// VOLUME-CONSERVING emission: candidates are taken in descending-coverage order until the
    /// level's represented volume reaches inputMicroVolume x this factor. This is what bounds
    /// silhouette fattening at coarse levels — sparse fringe cells are the first to go, dense
    /// interior mass always survives. (A flat coverage threshold alone let 18%-full boundary
    /// cells each claim a whole fat cell: 2.9x inflation measured at the coarsest level.)
    static constexpr float kVolumeBudgetFactor = 1.30f;

    struct Cell {
        glm::ivec3 pos;         ///< level-grid coordinates (template-local, micro/cellSize)
        std::string material;   ///< majority material of the cell's occupied micro volume
    };

    struct Level {
        int cellSizeMicros = 0;          ///< cell edge in microcube units
        std::vector<Cell> cells;         ///< sorted by (y, z, x) — deterministic
        size_t occupiedMicroVolume = 0;  ///< sum of INPUT micro cells this level represents
    };

    /// The operator, parameterized: what defines the subject decides what must survive
    /// decimation. Trees protect the trunk; STRUCTURES protect the exposed shell — a
    /// 1-voxel wall loses every coverage contest at coarse cells and erodes into holes
    /// unless cells carrying surface voxels are exempt, and interior cells (room fill,
    /// invisible at range) can be dropped wholesale instead of fattening the box.
    struct Config {
        std::vector<int> cellSizesMicros;      ///< level cell edges, finest first
        float coverageFloor      = 0.10f;
        float trunkFloor         = 0.03f;
        float volumeBudgetFactor = 1.30f;
        bool  protectTrunk        = true;   ///< Log*-material stem rescue (trees)
        bool  protectExposedShell = false;  ///< cells with air-exposed micros always survive
        bool  hollowInterior      = false;  ///< drop cells with NO exposed micros entirely
        size_t islandCullDivisor  = 50;     ///< minComponent = max(2, n/div); 0 = only kill
                                            ///< single-cell floaters (fences stay)
    };
    static Config treeConfig();
    static Config structureConfig();

    /// Raw rasterized input for non-template subjects (a placed structure extracted from
    /// chunks). Positions are subject-local MICRO coordinates (9 micros = 1 voxel).
    struct MicroSoup {
        std::vector<std::string> materials;                    ///< id -> material name
        std::vector<std::pair<glm::ivec3, uint16_t>> micros;   ///< (micro pos, material id)
    };

    /// Build the full chain for a template. Deterministic. Returns kLevelCount levels
    /// (finest first); levels whose cell count would round to nothing come back empty.
    static std::vector<Level> build(const VoxelTemplate& t);

    /// Build with an explicit config (build(t) == build(t, treeConfig())).
    static std::vector<Level> build(const VoxelTemplate& t, const Config& cfg);

    /// Build from a raw micro raster — the structure-LOD entry point.
    static std::vector<Level> buildFromSoup(const MicroSoup& soup, const Config& cfg);
};

} // namespace Core
} // namespace Phyxel
