#include "core/ChunkManager.h"
#include "core/voxfound/WorldReads.h"
#include "core/WorldGenerator.h"
#include "core/WorldStorage.h"
#include "core/GpuParticlePhysics.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include <climits>

namespace Phyxel {

ChunkManager::~ChunkManager() {
    cleanup();
}

void ChunkManager::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
}

void ChunkManager::setPhysicsWorld(Physics::PhysicsWorld* physics) {
    physicsWorld = physics;
}

bool ChunkManager::initializeWorldStorage(const std::string& worldPath) {
    return m_streamingManager.initializeWorldStorage(worldPath);
}

void ChunkManager::disconnectWorldStorage() {
    m_streamingManager.disconnectWorldStorage();
}

Core::PlayBounds ChunkManager::effectivePlayBounds() const {
    if (m_authoredPlayBounds.valid) return m_authoredPlayBounds;
    if (m_voxFoundBoundsSet) return m_voxFoundPlayBounds;   // F8.3b: the switched world's (invalid = unbounded)
    if (m_regionPlayBounds.valid) return m_regionPlayBounds;
    return Core::PlayBounds{};   // (the derived chunk extent went with today's chunks, D4)
}

const char* ChunkManager::playBoundsSource() const {
    if (m_authoredPlayBounds.valid) return "authored";
    if (m_voxFoundBoundsSet) return m_voxFoundPlayBounds.valid ? "switched world (region box)" : "switched world (streaming)";
    if (m_regionPlayBounds.valid) return "region (world.from/to)";
    if (m_streamingGenerationEnabled) return "none (streaming world)";
    return "none (no chunks)";
}

void ChunkManager::configureStreamingGeneration(bool enabled, WorldGenerator::GenerationType type, uint32_t seed) {
    m_streamingGenerationEnabled = enabled;
    if (enabled) {
        m_worldGenerator = std::make_unique<WorldGenerator>(type, seed);
    }
    LOG_INFO_FMT("Chunk", "Streaming generation " << (enabled ? "ENABLED" : "disabled")
                 << " (type=" << static_cast<int>(type) << ", seed=" << seed << ")");
}

void ChunkManager::setGpuParticlePhysics(GpuParticlePhysics* gpp) {
    // GPU debris is lit per particle at spawn through this sampler (sky / block RGB, each 0..15). It used to read
    // today's chunks' baked light and answered OPEN SKY (15, 0, 0, 0) wherever no chunk was loaded -- which, on the
    // switched world (no chunks), was every sample. D4 keeps that exact answer without the chunk read.
    if (gpp) {
        gpp->setLightSampler([](const glm::vec3&) -> glm::vec4 { return glm::vec4(15.0f, 0.0f, 0.0f, 0.0f); });
    }
}

bool ChunkManager::hasVoxelAt(const glm::ivec3& worldPos) const {
    if (m_voxFoundReads) return m_voxFoundReads->hasVoxelAt(worldPos);   // F7.1
    return false;
}

float ChunkManager::subVoxelFloor(const glm::ivec3& worldPos) const {
    if (m_voxFoundReads) return m_voxFoundReads->subVoxelFloor(worldPos);   // F7.1
    return 0.0f;
}

VoxelLocation::Type ChunkManager::getVoxelTypeAt(const glm::ivec3& worldPos) const {
    if (m_voxFoundReads) return m_voxFoundReads->typeAt(worldPos);   // F7.1
    return VoxelLocation::EMPTY;
}

bool ChunkManager::occupiedMicro(const glm::ivec3& micro) const {
    if (m_voxFoundReads) return m_voxFoundReads->occupiedMicro(micro);   // F7.1
    return false;
}

void ChunkManager::updateOccupancyVoxel(int worldX, int worldY, int worldZ, bool solid) {
    if (m_voxelOccupancyCallback) m_voxelOccupancyCallback(worldX, worldY, worldZ, solid);
    if (m_voxelEditCallback) m_voxelEditCallback(worldX, worldY, worldZ, solid);   // damage/blast breaks are edits
}

void ChunkManager::cleanup() {
    // The chunk store went with D4; the world's per-world registries go with it (a scene switch calls this).
    m_flamingPoints.clear();
}

} // namespace Phyxel
