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

// ---- F2.11 (§4.6 item 5 + item 3's per-block part): face clusters for the GPU draw-building pass ---------------
/// The box a face record covers, in chunk-local voxels (hi exclusive; zero thickness along the face's axis).
void faceBox(PackedFace f, glm::ivec3& lo, glm::ivec3& hi);
/// A run of faces drawn as one indirect command, with the exact union of their boxes (chunk-local voxels).
struct FaceCluster {
    glm::ivec3 lo{0}, hi{0};
    uint32_t first = 0, count = 0;
};
/// Reorders `faces` (one chunk's solid faces, no sprigs) so the faces of each cluster are contiguous. A face belongs
/// to the cell of `clusterBlocks`^3 blocks that holds its origin voxel (clusterBlocks 1 = per block, the plan's
/// unit). Clusters come in cell order; empty cells have none.
std::vector<FaceCluster> clusterFaces(std::vector<PackedFace>& faces, int clusterBlocks);
/// The cull pass's test, CPU reference: is the box [lo, hi] (metres) on the inner side of all six planes?
/// planes[i] = (n, d), inside where dot(n, p) + d >= 0 (the p-vertex test: conservative, never drops a visible box).
bool boxInFrustum(const glm::vec4 planes[6], glm::vec3 lo, glm::vec3 hi);

// ---- F2.12 (§4.6 item 6): shadow-caster culling ------------------------------------------------------------------
/// The six planes of a zero-to-one clip matrix `clip` (e.g. a cascade's ortho lightSpaceMatrix, which maps
/// CAMERA-RELATIVE world to light clip), expressed in a frame whose origin sits at `frameOrigin` (camera-relative):
/// p_cameraRelative = p_frame + frameOrigin. Inside where dot(n, p) + w >= 0 and -w <= x, y <= w, 0 <= z <= w;
/// normalized (|n| = 1, so w is a distance in metres). Same convention as boxInFrustum.
void clipPlanesZeroToOne(const glm::mat4& clip, glm::vec3 frameOrigin, glm::vec4 planes[6]);
/// Today's chunk caster test (RenderCoordinator::renderShadowPass) for a box [lo, hi] in any frame: its centre within
/// `reach` (cullRadius + the cascade's caster margin) of `cullCenter`, and (the light-frustum part, done in the same
/// frame as the box) the box inside `lightPlanes`.
bool casterBoxKept(glm::vec3 lo, glm::vec3 hi, glm::vec3 cullCenter, float reach, const glm::vec4 lightPlanes[6]);

// ---- F2.13 (§3.5a point 3, §4.6 item 7): the block LOD pyramid -------------------------------------------------
/// One level of the pyramid. Level k voxels are 2^k level-0 voxels a side. `count` = how many LEVEL-0 solid voxels
/// each covers; counts nest exactly (a level-k+1 count is the sum of its 8 children). A voxel is solid when its count
/// is at least half of the 8^k level-0 voxels it covers (no fattening: never drawn solid while mostly air). Its
/// material is the one SEEN FROM OUTSIDE: the most common among the level-0 voxels it covers that touch air (a voxel
/// on the grid's border counts as touching only what is inside the grid). When it covers none, the surface usually
/// lies in an AIR neighbour at the same level (a thin grass layer above a solid cell, too thin to make its own cell
/// solid): then the surface vote of its air face neighbours decides. Only a cell with neither (buried) takes its
/// volume vote. Ties: lowest material index. Measured on the forest floor seen from above: by volume alone the 1 m
/// level showed dirt on 39 % (1/8 m: 6 %); with the cell's own surface vote, still 39 %; with the neighbours' too,
/// see VoxelFoundationPlan F2.13. The neighbour step reads only the carried surface data and the volume vote reads
/// its own channel (`volMat`), so a level's materials depend on at most one cell around each cell (the 1 m margin
/// of a region chunk covers it up to the 1 m level).
/// Card (leaf) materials stay cards: the level is meshed with the same CardSet.
struct LodGrid {
    VoxelGrid grid;
    std::vector<uint32_t> count;
    std::vector<uint32_t> surf;     // level-0 voxels covered that touch air
    std::vector<uint8_t> surfMat;   // the material most of them have (0 when surf == 0)
    std::vector<uint8_t> volMat;    // the volume vote (what the next level's volume vote reads)
    int level = 0;
};
LodGrid lodBase(const VoxelGrid& g);
/// The next coarser level. Every side of `fine` must be even.
LodGrid lodDown(const LodGrid& fine);
/// A face meshed at level k, in the level-0 units of the record (position and extents x 2^k). The scaled extents
/// must fit the record (du, dv <= 256).
PackedFace lodScaleFace(PackedFace f, int level);
/// F2.14d: levels above a chunk's 1 m margin come from a REGION pyramid. Each chunk's 1 m level (exact with its margin)
/// is cut to its inner cells and pasted into one region level; lodDown then runs on the region, so a coarse cell sees
/// its real neighbours wherever the region has them (outside the region = the residency edge). lodCut = the cells
/// [lo, lo + size) of every channel (outside = air); lodPaste writes every channel of `part` at `at`; lodEmpty = an
/// all-air level of `dim` cells.
LodGrid lodCut(const LodGrid& g, glm::ivec3 lo, glm::ivec3 size);
void lodPaste(LodGrid& region, const LodGrid& part, glm::ivec3 at);
LodGrid lodEmpty(glm::ivec3 dim, int level);
/// F2.14d: the look gate for coarse levels, the view from straight above. Per column, the top of the highest solid
/// non-card cell (level-0 voxels; 0 when none) and its material. `coarse` vs a finer `ref` of the same region: the
/// mean / max of |coarse top - mean ref top over the ref columns it covers| in COARSE cells (the half-solid rule
/// bounds a ground column's error by about half a cell = P/2 pixels where the level is drawn), and each material's
/// share of the columns with a top (index = material).
struct LodTopStats {
    double meanErrCells = 0.0, maxErrCells = 0.0;
    std::vector<double> shareRef, share;
    /// coarse columns whose top cell has a CARD cell right above it: that cell hides its top face (cards hide their
    /// neighbours' faces), so from above the eye sees the sprigs there, not this top; shareHidden = materials of those
    double hiddenTop = 0.0;
    std::vector<double> shareHidden;
};
LodTopStats lodTopStats(const LodGrid& ref, const LodGrid& coarse, const CardSet& cards);

// ---- F2.14 (§3.5a point 3 "no fixed distances / no visible steps"): the level by size on screen ------------------
/// The CPU reference of what voxfound_face*.vert + voxel.frag and voxfound_cull.comp compute. Level k's voxel
/// (2^k / 8 m) covers P pixels (the tier knob) at distance d when L(d) = log2(8 P d / f) = k, f = focal length in
/// pixels. L is continuous and is evaluated at every FRAGMENT's world position, so looks depend on world position and
/// the pixel only (never on chunk or cluster); a cluster's box only bounds which levels are drawn (cost).
/// Pick: floor(L + band (u - 1/2)) clamped to [0, maxLevel], u = the pixel's 4x4 Bayer value: across a band of
/// kLodBand levels around each switch the share of pixels on the coarser level ramps linearly (no pop).
/// Keep: a level-k fragment is drawn iff pick(L - slack) <= k <= pick(L + slack): two levels whose surfaces at a pixel
/// lie within kLodSlack levels (2^0.25 = 19 % in distance) can never both discard there (no holes where levels meet).
constexpr float kLodBand = 0.5f;
constexpr float kLodSlack = 0.25f;
/// c = log2(8 P / f), so L(d) = log2(d) + c. It travels to voxel.frag as 8 bits: c = code / 8 - 16.
int lodScaleCode(float pxPerVoxel, float focalPx);
float lodScaleFromCode(int code);
float lodLevelAt(float distance, float c);
/// The 4x4 Bayer value of a pixel, (index + 0.5) / 16 (the tiers 5 / 8b pattern).
float lodBayer(int px, int py);
int lodPickLevel(float L, float u, int maxLevel);
bool lodKeeps(int level, float L, float u, int maxLevel, float slack = kLodSlack);
/// The levels a fragment anywhere in the box may keep (camera position in the box's frame): x = lowest, y = highest.
/// `floor` (F2.14 casters): L is raised to at least `floor` first, as voxfound_face_shadow_lod.frag does.
glm::ivec2 lodLevelsForBox(glm::vec3 lo, glm::vec3 hi, glm::vec3 cam, float c, int maxLevel, float floor = -1e9f);
/// F2.14, the light's view: the continuous level whose voxel is one shadow texel, log2(8 * texel). A caster finer than
/// that is not seen by the shadow map, so casters keep L >= this (inert while texel < 1/8 m: the result is < 0).
float lodTexelFloor(float texelMetres);
/// F2.14 casters lag the main view by this many levels. The dither picks per SCREEN pixel in the main view and per
/// shadow TEXEL in the caster, so inside a band a texel could hold the coarse surface (half-solid: it may stand above
/// the fine one) where the screen shows the fine one, which then shadows itself in a stipple (seen, top_down P=8).
/// With a one-level lag a caster only goes coarse where every pixel of the main view already has.
constexpr float kLodCasterLag = 1.0f;   // = voxfound_face_shadow_lod.frag's "- 1.0"
/// F2.14c: leaf sprigs hand off to coarse leaf mass at level `handoff` (levels >= handoff build leaves as solid faces;
/// sprigs belong to the levels below). A sprig is kept where the main view would keep a level below the handoff:
/// pick(L - slack, u) < handoff. lodSprigFade is that test as a threshold on u (kept iff u < fade), which is how
/// voxfound_foliage.vert feeds it to foliage.frag's dither (discard if vFade < bayer).
bool lodSprigKeeps(float L, float u, int handoff);
float lodSprigFade(float L, int handoff);
/// The caster keep test (voxfound_face_shadow_lod.frag): lodKeeps at max(L - kLodCasterLag, floor).
bool lodCasterKeeps(int level, float L, float u, int maxLevel, float floor);
/// The bits the face vertex shaders OR into the flags word (static faces use bits 0-15 only): 16-19 level,
/// 20-22 maxLevel, bit 23 = LOD test on, 24-31 the scale code.
uint32_t lodFlagBits(int level, int maxLevel, int code);

}  // namespace Phyxel::Core::VoxFound
