#pragma once
// docs/VoxelFoundationPlan.md §4 + F2 — the CPU REFERENCE mesher for the 1/8 m store.
//
// Turns voxels into the faces the renderer would draw, in three forms, so face cost can be measured before
// any GPU code exists, and so the GPU face builder has an exact reference to equal later:
//   * culled  — one unit quad per exposed voxel face (a face whose neighbour across it is air). Hidden
//               faces dropped, nothing merged: the cost of drawing voxels with no merging at all.
//   * greedy  — exposed faces of the same material and direction merged into maximal rectangles per slice,
//               across the WHOLE area (crossing block borders, §3.5a point 4: merging only inside a block
//               would multiply faces on large flat surfaces).
//   * greedy with blockLimit = kBlock — the same, but no quad crosses an 8-voxel block border (what a
//               per-block GPU pass would produce without the cross-block merge).
// Lighting is NOT part of the merge key yet: today's fine merge also splits on per-corner light, so the
// counts here are a LOWER bound on what a lit merge will produce. Stated wherever they are reported.
//
// Every quad lies in a plane perpendicular to `axis` (0 = x, 1 = y, 2 = z); `dir` = +1 faces the +axis side
// of its voxels. `o` is the min voxel of the quad's voxels; it spans `du` voxels along u = (axis+1)%3 and
// `dv` along v = (axis+2)%3.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

struct Quad {
    glm::ivec3 o{0};
    uint8_t axis = 0;
    int8_t dir = 1;
    uint16_t du = 1, dv = 1;
    uint8_t material = 0;
};

/// Materials drawn as LEAF CARDS instead of solid faces (today: MaterialDef::billboarded — Leaf, LeafBirch,
/// LeafSpruce, LeafJungle, LeafAutumn; ChunkRenderManager + FoliageRenderPipeline). A card voxel stays solid
/// in the store: it hides its neighbours' faces exactly like any voxel (today's mesher keeps leaf subcubes in
/// occupancy so wood inside a canopy is culled), but emits no faces of its own. Its exposed cells become
/// sprigs instead (foliageSprigs).
struct CardSet {
    uint32_t bits[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    void set(uint8_t m) { bits[m >> 5] |= 1u << (m & 31); }
    bool has(uint8_t m) const { return (bits[m >> 5] >> (m & 31)) & 1u; }
};

/// What lies outside the grid. Air: the area is an object in empty space (every outer face is drawn).
/// Skip: the area is a window into a larger world, so faces on its outer boundary are not counted
/// (their neighbour is unknown; in a world they are usually hidden).
enum class Outside { Air, Skip };

/// CHUNK BORDERS (F2.7, §4.6 item 1). The solidity of the one-voxel layer just outside each face of a grid, taken
/// from the neighbour chunks, so a face on the grid's border is decided by the real neighbour voxel. Only solidity
/// is needed: a border voxel never emits a face here (its own chunk draws it); it only hides or exposes ours. A card
/// (leaf) voxel is solid, as everywhere. One bit per voxel: plane [2*axis + (dir > 0)], bit u + nu * v with
/// u = (axis+1)%3, v = (axis+2)%3 coordinates of the grid voxel the plane touches. At most 6 * 256^2 bits = 48 KB.
/// All-air planes reproduce Outside::Air exactly; all-solid planes reproduce Outside::Skip.
struct Neighbours {
    glm::ivec3 dim{0};                    // must equal the grid's dim
    std::vector<uint32_t> bits[6];
    static size_t planeBits(glm::ivec3 d, int axis) { return size_t(d[(axis + 1) % 3]) * size_t(d[(axis + 2) % 3]); }
    void resize(glm::ivec3 d, bool solid);
    bool solid(int axis, int dir, int u, int v) const {
        const size_t b = size_t(u) + size_t(dim[(axis + 1) % 3]) * size_t(v);
        return (bits[2 * axis + (dir > 0)][b >> 5] >> (b & 31)) & 1u;
    }
    void set(int axis, int dir, int u, int v, bool s);
};

/// What lies outside a grid: one Outside mode for every side, or the real neighbours. Built implicitly from either,
/// so every mesher below takes both.
struct Edges {
    Outside mode = Outside::Air;
    const Neighbours* n = nullptr;
    Edges(Outside o) : mode(o) {}
    Edges(const Neighbours& nb) : n(&nb) {}
    /// Is the voxel one step from in-grid voxel p along (axis, dir), outside the grid, solid?
    bool solidOutside(glm::ivec3 p, int axis, int dir) const {
        return n ? n->solid(axis, dir, p[(axis + 1) % 3], p[(axis + 2) % 3]) : mode == Outside::Skip;
    }
};

/// The chunk [lo, lo + size) of a larger grid, and its border solidity from that grid. Voxels beyond the larger grid
/// are air for Outside::Air and solid for Outside::Skip.
VoxelGrid subGrid(const VoxelGrid& world, glm::ivec3 lo, glm::ivec3 size);
Neighbours neighboursOf(const VoxelGrid& world, glm::ivec3 lo, glm::ivec3 size, Outside beyond);

/// `cards` (optional, every mesher below): voxels of these materials emit no faces but still hide others.
std::vector<Quad> meshCulled(const VoxelGrid& g, Edges outside, const CardSet* cards = nullptr);
/// blockLimit 0 = merge across the whole grid; > 0 = no quad crosses a multiple of blockLimit.
std::vector<Quad> meshGreedy(const VoxelGrid& g, Edges outside, int blockLimit = 0, const CardSet* cards = nullptr);

/// RUN MERGE (F2.1, the form the GPU builds). Per slice row: maximal runs of one material along u. A run
/// stacks onto the run in the previous row when start, width and material are all equal. Rows are
/// independent, so the GPU builds it with one thread per row and no ordering between threads, and this CPU
/// version is its exact reference (same quads, as a set). Exact coverage like greedy, merges across block
/// borders like greedy, but it does not search for wider rectangles, so it can produce more quads.
std::vector<Quad> meshRuns(const VoxelGrid& g, Edges outside, const CardSet* cards = nullptr);

/// The compact face record (§4.2, §4.6): 8 bytes, position relative to a chunk of at most 256 voxels a
/// side (today's InstanceData is 16 B with a 5+5+5-bit cube position).
///   a: x bits 0-7 | y 8-15 | z 16-23 | axis 24-25 | dir>0 bit 26 | 27-31 free
///   b: du-1 bits 0-7 | dv-1 8-15 | material 16-23 | 24-31 free (tint index / flags later)
constexpr int kMaxChunkVoxels = 256;
struct PackedFace {
    uint32_t a = 0, b = 0;
    uint64_t key() const { return (uint64_t(a) << 32) | b; }
};
PackedFace packFace(const Quad& q);
Quad unpackFace(PackedFace f);

/// Exactness check for a merged mesh: every exposed unit face (as meshCulled defines it) is covered by
/// exactly one quad of the same material, and no quad covers anything else. Returns the number of unit
/// faces in error (missing + doubled + extra + wrong material); `why` gets the first few.
size_t coverageErrors(const VoxelGrid& g, Edges outside, const std::vector<Quad>& quads,
                      std::string* why = nullptr, const CardSet* cards = nullptr);

/// LEAF SPRIGS (F2.3). Today one FoliageInstanceData is emitted per EXPOSED leaf subcube (any face neighbour
/// air), kept when foliageCellHash01(world cell) < s_foliageDensity (0.25), and foliage.vert fans it into
/// cardsPerVoxel cutout cards of a fixed size in metres. At 1/8 m the same rule per voxel would put
/// (8/3)^2 = 7.1x the sprigs on the same canopy shell, so the density is defined in METRES instead: sprigs per
/// m^2 of exposed cell area, a 1/8 m cell kept with probability kSprigsPerM2/64. The look is a function of world
/// position, not of voxel size. Thinning uses today's hash on the absolute 1/8 m cell (seam-free across chunks).
/// kSprigsPerM2 = 4.5 is MEASURED, not derived (F2.4, VoxelFoundationPlan): the 0.25 knob alone (2.25/m^2)
/// under-fills, because today's micro leaves bypass that knob. 4.5 matched today's canopy coverage + luminance on
/// oak and birch (same tree, same relative pose, shadows on).
///
/// A sprig is a PackedFace with axis = 3 (unused by faces): a = x | y<<8 | z<<16 | 3<<24, b = material<<16.
/// Outside::Air: the grid border counts as air; Outside::Skip: it does not expose a cell.
constexpr double kSprigsPerM2 = 4.5;
uint32_t sprigThreshold24(double sprigsPerM2, int voxelsPerM);   // keep a cell when sprigHash24 < this
uint32_t sprigHash24(glm::ivec3 worldCell);                      // == today's foliageCellHash01 * 2^24
struct SprigParams {
    CardSet cards;
    glm::ivec3 worldOriginVoxel{0};   // absolute cell of grid voxel (0,0,0)
    uint32_t threshold24 = 0;
};
PackedFace packSprig(glm::ivec3 cell, uint8_t material);
inline bool isSprig(PackedFace f) { return ((f.a >> 24) & 3u) == 3u; }
std::vector<PackedFace> foliageSprigs(const VoxelGrid& g, Edges outside, const SprigParams& p);

// ---- F2.10 (§4.6 item 4): occlusion from BLOCK solidity ------------------------------------------------------
/// Today's chunk visibility graph (Chunk::computeVisibilityMask: m_faceConnect[f] = the faces sight can reach from
/// face f through the chunk), rebuilt from 8^3 blocks: a block BLOCKS sight only if every voxel in it is solid and
/// not in `seeThrough` (leaf cards, transparent materials); any other block passes sight. Flood over the passing
/// blocks; a face is touched by a component that has a block on that face. Faces 0 -X 1 +X 2 -Y 3 +Y 4 -Z 5 +Z.
/// Block-level is conservative: a block with one air voxel passes, so it can only connect MORE than voxels would.
/// Grid sides must be multiples of 8.
using FaceConnect = std::array<uint8_t, 6>;
FaceConnect blockFaceConnectivity(const VoxelGrid& g, const CardSet& seeThrough);
inline bool facesConnected(const FaceConnect& c, int a, int b) { return (c[a] >> b) & 1u; }

/// Today's occlusion walk (RenderCoordinator::applyOcclusionCulling), on any set of chunks: breadth-first from the
/// camera's chunk, entering a neighbour through face d only if the current chunk connects its entry face to d.
/// `known`: chunks with a connectivity (absent coords pass sight freely). A coord may be entered if it is a candidate
/// or `openSpace(coord)` (today: inside the frustum and the 512 m bound). Returns, per candidate, whether the walk
/// reached it. `maxNodes` exceeded -> *budgetExceeded = true and every candidate is returned reached (never a hole).
struct OccChunk {
    glm::ivec3 coord{0};
    FaceConnect connect{};
};
std::vector<char> occlusionReach(const std::vector<OccChunk>& known, const std::vector<glm::ivec3>& candidates,
                                 glm::ivec3 cameraChunk, const std::function<bool(glm::ivec3)>& openSpace,
                                 size_t maxNodes, bool* budgetExceeded = nullptr);

}  // namespace Phyxel::Core::VoxFound
