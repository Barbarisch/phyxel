#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

/**
 * @brief LOD squash operator — C0 of docs/ContinuousLodPlan.md.
 *
 * The ladder is POWER-OF-TWO FROM THE CUBE UP (design decision, plan §2.1):
 * level 0 = 1 cube, level n = 2^n cubes per cell. Sub/microcube detail is NOT
 * a rung on this ladder; it is the cube's *appearance*, carried here as a
 * coverage count so that sub-cube-thin geometry is still representable at
 * cube resolution.
 *
 *   1 cube = 3x3x3 subcubes = 9x9x9 = 729 microcubes  (Types.h, static_voxel.vert:200)
 *
 * A cell therefore stores how much of its volume is solid (0..kFullCoverage),
 * not merely whether it is solid. This is what lets the operator answer
 * "does a 1-microcube wall survive?" — the thinnest wall the structure
 * generator actually emits (resources/structure_styles.json: timber_cottage
 * interior_wall = 0.111 m = 1 microcube).
 *
 * KNOWN LIMITATION (C0, not yet fixed): the surface-area material vote reads
 * neighbours only within the LodVolume it is given. Out-of-bounds reads as
 * empty, so cells on the volume edge count as fully exposed. That is correct at
 * a true world edge but WRONG at a brick/chunk seam, where the real neighbour is
 * solid geometry living in an adjacent volume. Any building skin straddling a
 * seam will therefore get a biased skin vote once this is wired to real tiled
 * data. Fix = pass a 1-cell apron, or a neighbour-lookup callback.
 */
struct LodCell {
    /// Solid microcubes within this cell. A FULL level-L cell holds
    /// kFullCoverage * 8^L, so this accumulates as the pyramid deepens.
    /// 64-bit deliberately: uint32_t overflows around level 8
    /// (729 * 8^8 = 1.22e10 > UINT32_MAX), which is reachable on a real
    /// far-LOD pyramid over a streamed world.
    uint64_t coverage = 0;
    /// Material by VOLUME (the naive vote).
    uint16_t bulkMaterial = 0;
    /// Material by EXPOSED SURFACE AREA (plan §2.2b: a plaster-skinned stone
    /// wall must read as plaster, because the skin is what a viewer sees).
    uint16_t skinMaterial = 0;
    /// Set when this cell must not be filled in by an occupancy merge — a
    /// doorway/window reveal. Propagates upward so openings survive coarsening.
    bool preserveOpening = false;

    /// Microcubes of AUTHORED VOID (door/window reveal) inside this cell.
    /// This is the sub-brick opening MASK that replaces the binary carve at coarse
    /// levels. MEASURED reason it exists (docs/ContinuousLodPlan.md §2.3 sweep):
    /// blanking a whole brick because it contains an opening destroyed 49.7% of a
    /// settlement block's wall at 4³ and 100% at 16³. Carrying the opening as a
    /// conserved QUANTITY instead lets the brick stay solid while the renderer still
    /// knows an opening is there and how big it is.
    uint64_t openingCoverage = 0;

    bool solid() const { return coverage > 0; }
};

/// Occupancy reduction rule (plan §2.2a). Neither is right everywhere.
enum class OccupancyRule {
    /// Any solid child => solid parent. Preserves thin walls; closes openings.
    Or,
    /// Solid only if >= half the parent volume is solid. Preserves openings;
    /// DELETES thin walls — including the 1-microcube wall the generator emits,
    /// which is the same geometry SpawnGate relies on. Kept for A/B only.
    HalfThreshold,
    /// Or + a binary carve: any authored opening in the cell blanks it entirely.
    /// Correct at level 1 (2 cubes) and CATASTROPHIC at brick sizes — measured to
    /// erase 49.7% (4³) to 100% (16³) of a settlement block. Kept for A/B and to
    /// document why the mask rule exists. NOT recommended above level 1.
    OrPreserveOpenings,
    /// Or + a sub-brick opening MASK: the cell stays solid, and `openingCoverage`
    /// carries the authored void volume upward (conserved, never deleted). The
    /// renderer decides what to do with it. The recommended rule for brick levels.
    OrWithOpeningMask,
};

/// Material reduction rule (plan §2.2b).
enum class MaterialRule {
    /// Majority by volume. Wrong for skinned walls (reads the core, not the skin).
    VolumeMajority,
    /// Majority by exposed surface area. Recommended.
    SurfaceAreaMajority,
};

struct SquashConfig {
    /// OrWithOpeningMask, NOT OrPreserveOpenings. The renderer coarsens to maxLevel = 5
    /// (32-cube cells, RenderCoordinator::updateChunkLod), and OrPreserveOpenings blanks a
    /// whole cell whenever any child carries an authored opening — with preserveOpening
    /// propagating upward, one window erases an entire walled room from level 3 on. The mask
    /// rule keeps the mass solid and conserves the opening volume for the renderer to use.
    /// Pinned by LodQuadFootprintTest.DefaultSquashConfigIsSafeAtRendererMaxLevel.
    OccupancyRule occupancy = OccupancyRule::OrWithOpeningMask;
    MaterialRule material = MaterialRule::SurfaceAreaMajority;
};

/**
 * @brief A dense, level-N grid of LodCells. Level 0 cells are single cubes.
 *
 * Index order matches the engine's canonical z-minor layout
 * (CLAUDE.md "Coordinate System"): index = z + y*dim.z + x*dim.z*dim.y.
 */
class LodVolume {
public:
    /// Microcubes in one cube: 9^3. A cube-resolution cell that is fully solid
    /// has coverage == kFullCoverage.
    static constexpr uint32_t kFullCoverage = 729u;

    LodVolume() = default;
    LodVolume(glm::ivec3 dim, int level);

    const glm::ivec3& dim() const { return m_dim; }
    int level() const { return m_level; }
    /// Edge length of one cell, in cubes (2^level).
    int cellSizeInCubes() const { return 1 << m_level; }
    bool empty() const { return m_cells.empty(); }

    bool inBounds(int x, int y, int z) const {
        return x >= 0 && y >= 0 && z >= 0 && x < m_dim.x && y < m_dim.y && z < m_dim.z;
    }
    size_t index(int x, int y, int z) const {
        return static_cast<size_t>(z) + static_cast<size_t>(y) * m_dim.z +
               static_cast<size_t>(x) * static_cast<size_t>(m_dim.z) * m_dim.y;
    }
    LodCell& at(int x, int y, int z) { return m_cells[index(x, y, z)]; }
    const LodCell& at(int x, int y, int z) const { return m_cells[index(x, y, z)]; }
    /// Out-of-bounds reads as empty, so callers need no edge special-casing.
    const LodCell& atClamped(int x, int y, int z) const;

    const std::vector<LodCell>& cells() const { return m_cells; }
    std::vector<LodCell>& cells() { return m_cells; }

    /// Total solid coverage — a cheap whole-volume invariant for tests.
    uint64_t totalCoverage() const;
    /// Number of solid cells.
    size_t solidCellCount() const;

private:
    glm::ivec3 m_dim{0, 0, 0};
    int m_level = 0;
    std::vector<LodCell> m_cells;
    static const LodCell s_empty;
};

/**
 * @brief Reduce one level: a 2x2x2 group of cells becomes one parent cell.
 *
 * Deterministic: the same input always produces a bitwise-identical result
 * (required for persistence and for workflow resume). Odd dimensions round up;
 * missing children read as empty.
 */
LodVolume squash(const LodVolume& src, const SquashConfig& cfg);

/// Squash repeatedly until the volume is a single cell, returning every level
/// (result[0] is a copy of `src`). This is the pyramid the LOD cut walks.
std::vector<LodVolume> buildPyramid(const LodVolume& src, const SquashConfig& cfg);

/**
 * @brief Watertightness check across a LOD boundary (plan §2.5).
 *
 * Cracks appear where a coarse cell abuts finer geometry. Returns the number of
 * boundary positions where the coarse level is EMPTY but the fine level is
 * SOLID — i.e. a hole the coarse mesh would let the viewer see through.
 * Zero means the coarse level covers everything the fine level does.
 */
size_t countWatertightViolations(const LodVolume& fine, const LodVolume& coarse);

} // namespace Core
} // namespace Phyxel
