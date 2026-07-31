#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/LodBrick.h"
#include "core/Types.h"

namespace Phyxel {

class Chunk;
class WorldStorage;

namespace Core {

/**
 * @brief C3.2 (docs/ContinuousLodPlan.md) — build, persist, invalidate and serve the LOD pyramid.
 *
 * The join between C3.0 (the format) and C3.3 (the renderer). Its reason to exist is the wall in
 * docs/evidence/lod_residency_wall_20260730.txt: ~1.28 MB of working set per RESIDENT chunk,
 * chunk count growing as R^2. `facesFromStorage` is the payoff — it produces renderable geometry
 * for a chunk that is NOT loaded, so distance stops costing residency.
 */
class LodPyramidService {
public:
    /// The renderer coarsens to this level (RenderCoordinator::updateChunkLod, maxLevel = 5),
    /// so there is no point persisting beyond it.
    static constexpr int kMaxLevel = 5;

    /// Build levels 1..maxLevel from a chunk's voxels and persist each one.
    /// Returns the number of levels written; 0 means nothing was stored (see shouldPersist).
    static int buildAndPersist(const Chunk& chunk, WorldStorage& storage,
                               int maxLevel = kMaxLevel);

    /// The ONE call the save path should make. Drops any existing pyramid, then rebuilds it if
    /// the chunk still warrants one. Doing it in that order is what makes the "was a structure,
    /// now it is plain terrain" case correct: a bare buildAndPersist would leave the old levels
    /// behind and keep serving a building that no longer exists.
    static bool refreshPyramid(const Chunk& chunk, WorldStorage& storage,
                               int maxLevel = kMaxLevel);

    /// Drop every persisted level for a chunk. MUST be called whenever its voxels change: a
    /// stale pyramid renders the pre-edit world at distance, which reads as "the world does not
    /// update until I walk up to it" rather than as a cache bug.
    static bool invalidate(WorldStorage& storage, const glm::ivec3& chunkCoord);

    /// C3's payoff: renderable faces for a chunk WITHOUT loading it.
    /// Returns false when no pyramid is stored at that level — a normal state, not an error.
    /// The faces carry world-space offsets relative to the chunk origin, exactly as
    /// LodChunkMesh::emitFaces produces for a resident chunk.
    static bool facesFromStorage(WorldStorage& storage, const glm::ivec3& chunkCoord, int lod,
                                 std::vector<InstanceData>& outFaces);

    /// C3.4 policy — is this chunk worth persisting at all?
    ///
    /// The field's #1 correction (LargeWorldScalePlan.md:756) is that terrain LOD should come
    /// from CoarseWorldModel DIRECTLY; only edited chunks and structures are worth downsampling.
    /// Persisting every chunk would store, at scale, a coarse copy of terrain the generator can
    /// reproduce for free.
    ///
    /// The signal used here is sub/microcube content: the procedural terrain generator emits
    /// whole cubes, while structures and player edits are what introduce finer voxels.
    /// LIMITATION, stated rather than hidden: flora templates are stamped as sub/microcubes too,
    /// so a chunk holding only generated trees currently reads as "worth persisting". That is a
    /// conservative error (it stores more than strictly needed, never less), and narrowing it
    /// needs a real edited-vs-generated flag on the chunk, which does not exist yet.
    static bool shouldPersist(const Chunk& chunk);

    /// In-RAM cost of serving one level from storage: the decoded volume plus its face buffer.
    /// C3.0's storage ratio compared blob BYTES against process working set, which is not the
    /// same thing; this is the number that decides whether C3 actually beats residency.
    struct ServeCost {
        size_t blobBytes = 0;      ///< what the row occupies on disk
        size_t volumeBytes = 0;    ///< decoded LodVolume cells held in RAM
        size_t faceBytes = 0;      ///< emitted InstanceData buffer
        size_t faceCount = 0;
        size_t totalRamBytes() const { return volumeBytes + faceBytes; }
    };
    /// Measure what serving `lod` for this chunk costs. Returns false if nothing is stored.
    static bool measureServeCost(WorldStorage& storage, const glm::ivec3& chunkCoord, int lod,
                                 ServeCost& out);
};

} // namespace Core
} // namespace Phyxel
