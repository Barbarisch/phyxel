#pragma once

#include "Types.h"
#include <vector>
#include <memory>
#include <functional>
#include <set>
#include <glm/glm.hpp>

namespace Phyxel {

// Forward declarations
class Chunk;
class Subcube;
class Cube;
class Microcube;

/**
 * FaceUpdateCoordinator - selective face updates when cubes are added, removed, subdivided or
 * broken: only affected faces are rebuilt, the up-to-6 neighbours are refreshed, and chunks are
 * marked dirty when a change spans a chunk border. Reaches ChunkManager through two callbacks
 * (chunk lookup, mark dirty). The global dynamic-face rebuild it used to own went with the CPU
 * debris path (DebrisInteractionPlan 1d).
 */
class FaceUpdateCoordinator {
public:
    // Callback types for accessing ChunkManager state
    using ChunkLookupFunc = std::function<Chunk*(const glm::ivec3&)>;
    using MarkChunkDirtyFunc = std::function<void(Chunk*)>;

    FaceUpdateCoordinator() = default;
    ~FaceUpdateCoordinator() = default;

    // Callback setup
    void setCallbacks(
        ChunkLookupFunc getChunkAtFunc,
        MarkChunkDirtyFunc markChunkDirtyFunc
    );


    // Selective update methods for different cube state changes
    void updateAfterCubeBreak(const glm::ivec3& worldPos);
    void updateAfterCubePlace(const glm::ivec3& worldPos);
    void updateAfterCubeSubdivision(const glm::ivec3& worldPos);
    void updateAfterSubcubeBreak(const glm::ivec3& parentWorldPos, const glm::ivec3& subcubeLocalPos);

    // Face update coordination
    void updateFacesForPositionChange(const glm::ivec3& worldPos, bool cubeAdded);
    void updateNeighborFaces(const glm::ivec3& worldPos);
    void updateSingleCubeFaces(const glm::ivec3& worldPos);
    void updateFacesAtPosition(const glm::ivec3& worldPos);

    // Helper methods
    std::vector<glm::ivec3> getAffectedNeighborPositions(const glm::ivec3& worldPos);

private:
    // Callbacks to access ChunkManager state
    ChunkLookupFunc m_getChunkAt;
    MarkChunkDirtyFunc m_markChunkDirty;
};

} // namespace Phyxel
