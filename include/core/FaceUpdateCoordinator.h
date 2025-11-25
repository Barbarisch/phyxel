#pragma once

#include "Types.h"
#include <vector>
#include <memory>
#include <functional>
#include <set>
#include <glm/glm.hpp>

namespace VulkanCube {

// Forward declarations
class Chunk;
class Subcube;
class Cube;
class Microcube;

/**
 * FaceUpdateCoordinator - Manages face rebuilding and updates for voxel objects
 * 
 * EXTRACTED FROM CHUNKMANAGER.CPP (November 2025)
 * Original ChunkManager: 1,086 lines → TBD lines after extraction
 * FaceUpdateCoordinator: TBD lines (TBD header + TBD implementation)
 * Reduction: TBD lines from ChunkManager
 * 
 * PURPOSE:
 * Coordinates face updates when cubes are added, removed, subdivided, or broken.
 * Manages global dynamic face rebuilding for physics-enabled objects.
 * 
 * RESPONSIBILITIES:
 * 1. Global Dynamic Face Rebuilding: Generate faces for all dynamic subcubes/cubes/microcubes
 * 2. Selective Updates: Update only affected faces when cubes change
 * 3. Cross-Chunk Coordination: Mark dirty chunks when changes span chunk boundaries
 * 4. Neighbor Management: Update faces of up to 6 neighbors when cube state changes
 * 
 * DESIGN PATTERN:
 * Uses callbacks to access ChunkManager's state without tight coupling:
 * - Access to dynamic object vectors (subcubes, cubes, microcubes)
 * - Access to face data structures
 * - Chunk lookup and dirty marking
 * 
 * USAGE:
 * FaceUpdateCoordinator coordinator;
 * coordinator.setCallbacks(
 *     [this]() -> auto& { return globalDynamicSubcubes; },
 *     [this]() -> auto& { return globalDynamicCubes; },
 *     [this]() -> auto& { return globalDynamicMicrocubes; },
 *     [this]() -> auto& { return globalDynamicSubcubeFaces; },
 *     [this](const glm::ivec3& pos) { return getChunkAt(pos); },
 *     [this](Chunk* chunk) { markChunkDirty(chunk); }
 * );
 * coordinator.rebuildGlobalDynamicFaces();
 */
class FaceUpdateCoordinator {
public:
    // Callback types for accessing ChunkManager state
    using DynamicSubcubeVectorAccessFunc = std::function<std::vector<std::unique_ptr<Subcube>>&()>;
    using DynamicCubeVectorAccessFunc = std::function<std::vector<std::unique_ptr<Cube>>&()>;
    using DynamicMicrocubeVectorAccessFunc = std::function<std::vector<std::unique_ptr<Microcube>>&()>;
    using FaceDataAccessFunc = std::function<std::vector<DynamicSubcubeInstanceData>&()>;
    using ChunkLookupFunc = std::function<Chunk*(const glm::ivec3&)>;
    using MarkChunkDirtyFunc = std::function<void(Chunk*)>;

    FaceUpdateCoordinator() = default;
    ~FaceUpdateCoordinator() = default;

    // Callback setup
    void setCallbacks(
        DynamicSubcubeVectorAccessFunc getSubcubesFunc,
        DynamicCubeVectorAccessFunc getCubesFunc,
        DynamicMicrocubeVectorAccessFunc getMicrocubesFunc,
        FaceDataAccessFunc getFaceDataFunc,
        ChunkLookupFunc getChunkAtFunc,
        MarkChunkDirtyFunc markChunkDirtyFunc
    );

    // Global dynamic face rebuilding
    void rebuildGlobalDynamicFaces();

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
    DynamicSubcubeVectorAccessFunc m_getSubcubes;
    DynamicCubeVectorAccessFunc m_getCubes;
    DynamicMicrocubeVectorAccessFunc m_getMicrocubes;
    FaceDataAccessFunc m_getFaceData;
    ChunkLookupFunc m_getChunkAt;
    MarkChunkDirtyFunc m_markChunkDirty;
};

} // namespace VulkanCube
