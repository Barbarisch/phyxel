#pragma once

// ============================================================================
// VoxelLightOccupancy — the chunk's geometry, flattened into a form a shader can walk.
//
// M1 of the lighting rebuild (docs/UnifiedLightingPlan.md). Every part of the replacement asks the
// same question — "is there solid matter between this surface and that light" — and answering it
// needs voxel occupancy the GPU can traverse. This is that representation.
//
// WHY NOT THE PHYSICS OCCUPANCY BITFIELD (GpuParticlePhysics, 512x256x512) even though it is
// already GPU-resident:
//   * it is CUBE resolution only, so it cannot see the 2-micro walls and 3-micro floors that
//     produced every defect this rebuild exists to fix;
//   * its two population paths disagree — rebuildOccupancyFromChunks marks a cell solid if it holds
//     ANY sub-voxel content, while the streaming path skips sub-voxel-only cells entirely;
//   * it does not exist at all if GPU physics fails to initialise, and lighting must not inherit
//     that as a prerequisite.
//
// SOURCE OF TRUTH is Physics::VoxelOccupancyGrid — per chunk, persistent, already maintained on
// damage and on load, and already three-level. This flattens it; it does not re-derive it, so the
// CPU and GPU cannot drift apart.
//
// LAYOUT, chosen so the common case costs one bit test:
//   solid[]  — 1 bit per cube cell: the cube is FULLY solid. The overwhelming majority of terrain.
//   mixed[]  — 1 bit per cube cell: the cube carries sub-voxel detail. This is the case the old
//              per-cell light field could not represent, and the reason the interior wall-base
//              band existed: a cell that is 1/3 floor and 2/3 standing room is neither solid nor
//              empty, and rounding it either way is wrong.
//   mixedCubeIdx[] + microWords[] — for mixed cubes only, a 729-bit micro mask (23 words). Sorted
//              by cube index so a shader can binary-search it; dense per-cube indirection would
//              cost 64 KB per chunk, which is unaffordable across hundreds of resident chunks.
//
// A dense micro bitfield for a whole chunk would be 32^3 x 729 bits = 3 MB. Carrying detail only
// for mixed cubes is what makes sub-voxel-accurate occupancy affordable at all.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Phyxel {
namespace Physics { class VoxelOccupancyGrid; }
namespace Graphics {

struct ChunkLightOccupancy {
    static constexpr int kChunk            = 32;
    static constexpr int kCubeCells        = kChunk * kChunk * kChunk;   // 32768
    static constexpr int kCubeWords        = kCubeCells / 32;            // 1024 (4 KB)
    static constexpr int kMicroPerAxis     = 9;
    static constexpr int kMicroBitsPerCube = 729;
    static constexpr int kMicroWordsPerCube = (kMicroBitsPerCube + 31) / 32;   // 23 (92 B)

    std::array<uint32_t, kCubeWords> solid{};   ///< cube is fully solid
    std::array<uint32_t, kCubeWords> mixed{};   ///< cube carries sub-voxel detail

    /// Cube indices with detail, ASCENDING — the shader binary-searches this.
    std::vector<uint32_t> mixedCubeIdx;
    /// kMicroWordsPerCube words per entry in mixedCubeIdx, in the same order.
    std::vector<uint32_t> microWords;

    /// Cube linear index, matching Physics::VoxelOccupancyGrid exactly: z + y*32 + x*1024.
    static int cubeIndex(const glm::ivec3& localCube) {
        return localCube.z + localCube.y * 32 + localCube.x * 1024;
    }
    /// Micro index inside a cube, 0..728: mx + my*9 + mz*81, where m = sub*3 + micro.
    static int microIndex(const glm::ivec3& microInCube) {
        return microInCube.x + microInCube.y * 9 + microInCube.z * 81;
    }

    bool cubeIsSolid(int cubeIdx) const {
        return (solid[static_cast<size_t>(cubeIdx) >> 5] >> (cubeIdx & 31)) & 1u;
    }
    bool cubeIsMixed(int cubeIdx) const {
        return (mixed[static_cast<size_t>(cubeIdx) >> 5] >> (cubeIdx & 31)) & 1u;
    }

    /// Slot of `cubeIdx` in mixedCubeIdx, or -1. Binary search — the same walk the shader does.
    int mixedSlot(int cubeIdx) const;

    /// THE query the tracer makes: is this micro cell solid?
    /// `microLocal` is in chunk-local MICRO coordinates, 0..(32*9-1) per axis.
    bool solidAtMicro(const glm::ivec3& microLocal) const;

    size_t bytes() const {
        return sizeof(solid) + sizeof(mixed) +
               mixedCubeIdx.size() * sizeof(uint32_t) + microWords.size() * sizeof(uint32_t);
    }
};

/// Flatten one chunk's occupancy grid. Mirrors `VoxelOccupancyGrid::queryAABB`'s traversal
/// exactly: a cube counts only if its cube bit is set; if it is not subdivided it is fully solid;
/// otherwise its set-and-not-subdivided subcubes are solid, and subdivided subcubes fall through
/// to their microcube mask.
ChunkLightOccupancy buildLightOccupancy(const Physics::VoxelOccupancyGrid& grid);

// ---------------------------------------------------------------------------------------------
// THE PACKED POOL — what actually reaches the GPU.
//
// Two buffers. A DIRECTORY covering a world box of chunks (one uint32 per chunk: the word offset
// of that chunk's blob in the pool, or kNoChunk), and a POOL holding the blobs back to back. The
// directory is deliberately tiny — 32x16x32 chunks = 16384 entries = 64 KB — so a shader resolves
// world position to blob in one indexed read with no hashing and no search.
//
// ⚠️ THE BOX FOLLOWS THE VIEWER; IT IS NOT WORLD-FIXED. It was fixed at x,z in [-256,256) at first,
// which was measured to be unusable: this repo's own worlds/default.db has placed objects at
// x ~ 611-635, already outside it, and a WorldForge world spans kilometres. Chunks outside the box
// occlude NOTHING, so a settlement built past the edge would light as though it had no walls.
// `boxMinChunk` is therefore recentred on the camera each repack — the same player-following-region
// pattern the water sim uses — and every index function takes it explicitly so no caller can
// silently assume the old world-fixed constants. Outside the box, queries report NOT solid, which
// degrades to "no occlusion" rather than to a wrong answer.
//
// The box spans 1024 x 512 x 1024 world units, comfortably larger than the far shadow cascade
// (1600 u is the cascade; light tracing operates far nearer), so the edge is not reachable by any
// light this feeds. `RenderCoordinator` reports occupancy coverage numerically
// (`GET /api/debug/light_occupancy` -> loaded / resident / out_of_box) so a box that IS being
// outrun shows up as a number rather than as an unexplained bright interior.
//
// Packing and sampling are PURE FUNCTIONS on plain word arrays, with no Vulkan in sight, because
// the addressing is the part that silently goes wrong. Getting it wrong inside a shader produces a
// picture nobody can debug; getting it wrong here fails a unit test. The GPU class is then only a
// memcpy of these two arrays.
// ---------------------------------------------------------------------------------------------

struct PackedOccupancyPool {
    static constexpr int kDirChunksX = 32, kDirChunksY = 16, kDirChunksZ = 32;
    static constexpr int kDirEntries = kDirChunksX * kDirChunksY * kDirChunksZ;   // 16384 (64 KB)
    static constexpr uint32_t kNoChunk = 0xFFFFFFFFu;

    /// Min corner of the covered box, in CHUNK coordinates (world origin / 32). Moves with the
    /// viewer. A default-constructed pool covers the box around the world origin.
    glm::ivec3 boxMinChunk{-kDirChunksX / 2, -kDirChunksY / 2, -kDirChunksZ / 2};

    std::vector<uint32_t> directory;   ///< kDirEntries word offsets into `pool`, or kNoChunk
    std::vector<uint32_t> pool;        ///< blobs, back to back

    /// The box min corner that centres coverage on `centreWorld`. Snapped to chunk coordinates so
    /// the box only moves in whole-chunk steps — a box that slid continuously would repack on every
    /// camera nudge, and chunk-quantised motion is also what keeps the mapping stable frame to frame.
    static glm::ivec3 boxMinChunkFor(const glm::vec3& centreWorld);

    /// Directory slot for a chunk's WORLD origin, or -1 if outside the covered box.
    static int directoryIndex(const glm::ivec3& chunkWorldOrigin, const glm::ivec3& boxMinChunk);
    /// Directory slot containing a world MICRO position, or -1 if outside the box.
    static int directoryIndexForMicro(const glm::ivec3& worldMicro, const glm::ivec3& boxMinChunk);

    /// World origin of the chunk occupying `slot`. Inverse of directoryIndex.
    static glm::ivec3 chunkOriginForSlot(int slot, const glm::ivec3& boxMinChunk);

    size_t bytes() const {
        return (directory.size() + pool.size()) * sizeof(uint32_t);
    }
};

/// Pack chunk blobs into the two GPU arrays. `chunks` maps a chunk's WORLD origin to its blob.
/// `boxMinChunk` is the covered box's min corner in chunk coordinates; chunks outside it are
/// skipped (they then read as "not solid" = no occlusion, never as wrong geometry).
PackedOccupancyPool packOccupancyPool(
    const std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>>& chunks,
    const glm::ivec3& boxMinChunk);

/// Words this blob occupies in the pool. Must match packOccupancyPool's layout exactly — it is the
/// basis of the overflow decision, and under-counting would truncate a blob.
size_t blobWords(const ChunkLightOccupancy& blob);

/// Choose which chunks fit in `capacityWords`, in the order given, and report how many were left
/// out. Pure, so the overflow policy is testable without a Vulkan device.
///
/// ⚠️ Chunks are DROPPED WHOLE, never truncated. A half-written blob would be read by the shader as
/// arbitrary geometry — phantom walls carved out of open air — whereas an absent chunk simply
/// reports "not solid", i.e. no occlusion. Wrong-and-bright is diagnosable; wrong-and-solid is not.
std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>> selectChunksThatFit(
    const std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>>& chunks,
    size_t capacityWords, size_t& droppedOut);

/// Result of a light-visibility march. `firstHitMicro` is meaningful only when blocked.
struct LightVisibility {
    bool  visible = true;
    int   steps = 0;            ///< cells actually sampled
    /// The march ALWAYS reaches the light; this says the step had to coarsen past micro resolution
    /// to do so, i.e. a very thin distant occluder could have been stepped over.
    /// The traversal ran out of its CELL budget before reaching the light. Distinct from `visible`:
    /// a capped ray reports visible because it found nothing, which on a long ray may be wrong.
    /// (Under the old fixed-step march this also meant "the step coarsened"; a DDA has no step
    /// length — it visits every cell it crosses — so that meaning is gone.)
    bool  cappedOut = false;
    glm::ivec3 firstHitMicro{0};
};

/// THE M2 visibility term, and the CPU mirror of `phxLightVisibility` in voxel.frag — step for
/// step, including the 2-micro start offset along the GEOMETRIC normal, the 1-micro stop short of
/// the light, and the step cap. It exists for the same reason `packedPoolSolidAt` does: a march
/// that is wrong inside a shader produces an image nobody can attribute, whereas a march that is
/// wrong here fails a test and can be queried point-by-point over HTTP.
/// All positions are ABSOLUTE world units.
LightVisibility packedPoolLightVisibility(const PackedOccupancyPool& packed,
                                          const glm::vec3& surfaceWorld,
                                          const glm::vec3& geomNormal,
                                          const glm::vec3& lightWorld,
                                          int maxSteps = 192);

/// M3 — SKY VISIBILITY. What fraction of the sky hemisphere above a surface is unoccluded,
/// cosine-weighted. This replaces the deleted per-cell skylight flood: instead of a number stored
/// per cube cell and decayed 1-per-cell from the nearest opening, "how much sky can this surface
/// see" is answered by tracing the real geometry, at sub-voxel resolution.
///
/// Returns 0.0 for a sealed interior, 1.0 for open ground, and something in between near an opening
/// — and the falloff comes out of the geometry rather than out of a decay constant.
///
/// ⚠️ HORIZON: rays are finite (`reach` world units), and this is the number that decides whether a
/// room reads as SEALED. A ray that runs out of reach inside a closed room hits nothing and is
/// counted as sky. MEASURED: with a 10 u reach, a diagonal ray inside a 9x7x9 sealed room escaped
/// the budget and the corner read 0.077 instead of 0. The default is therefore sized for real
/// interiors, not for the cheapest march. Raise it if large halls read as lit; that is the knob.
float packedPoolSkyVisibility(const PackedOccupancyPool& packed,
                              const glm::vec3& surfaceWorld,
                              const glm::vec3& geomNormal,
                              float reach = 24.0f,
                              int rays = 9);

/// THE query, exactly as the shader will perform it, on the packed words.
/// `worldMicro` is a world position in MICRO units (world unit * 9).
/// This is the CPU mirror of the GLSL; the two must stay identical, and the test suite is what
/// keeps them honest before any GLSL exists.
bool packedPoolSolidAt(const PackedOccupancyPool& packed, const glm::ivec3& worldMicro);

/// Cube-level occupancy of one CUBE cell. Three states, because the bake needs to tell them apart.
enum class CubeOccupancy : uint8_t {
    Empty = 0,   ///< nothing at all: neither a full cube nor any sub-voxel content
    Mixed = 1,   ///< carries sub-voxel detail — a 2-micro wall, a 3-micro floor
    Solid = 2,   ///< the whole cube is filled
};

/// Cube-resolution occupancy for the M3 bake's CELL GATHER (docs/UnifiedLightingPlan.md D21).
///
/// The gather used `m_solidVis`, which is cube-level ONLY — it records "a visible CUBE occupies this
/// cell" and knows nothing about sub-voxel content. Generated buildings are built from subcubes and
/// microcubes, so their walls registered as EMPTY: cells beside them failed the "touches something
/// solid" test, were never traced, and kept full sky. Measured symptom: `last_sky_bake_cells` pinned
/// at exactly 1024 (the flat ground layer, the only full-cube geometry) while every interior of an
/// engine-generated building read sky = 15.
///
/// This asks the packed pool instead, which carries the sub-voxel truth and — per the M3-REDESIGN
/// ordering rule — is already flushed before `updateDirtyChunks()` runs. That sidesteps the
/// `buildSubMicroOccupancy`-after-`rebuildCubeFaces` inversion (D5) that makes the mesher's own
/// sub-voxel data unavailable at bake time.
///
/// `worldCube` is a world position in CUBE units. Two bit tests and a directory lookup — no micro
/// mask decode — so scanning all 32768 cells of a chunk stays cheap.
CubeOccupancy packedPoolCubeOccupancy(const PackedOccupancyPool& packed, const glm::ivec3& worldCube);

}  // namespace Graphics
}  // namespace Phyxel
