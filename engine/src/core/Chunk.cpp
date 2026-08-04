#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "core/MaterialRegistry.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <random>
#include <iomanip>
#include <unordered_set>

namespace Phyxel {

Chunk::Chunk(const glm::ivec3& origin)
    : worldOrigin(origin) {
    // 4.2b: no eager cubes.reserve — the overlay slot array (32768 pointers, 256 KB) is only
    // allocated if a Cube is ever materialized (ChunkVoxelManager::materializeAt).
    staticSubcubes.reserve(1000);             // Reserve reasonable space for static subcubes
}

Chunk::~Chunk() {
    // unique_ptr vectors auto-delete all owned voxels
    cubes.clear();
    staticSubcubes.clear();
    staticMicrocubes.clear();
    
    cleanupVulkanResources();
    cleanupPhysicsResources();
}

Chunk::Chunk(Chunk&& other) noexcept
    : cubes(std::move(other.cubes))
    , staticSubcubes(std::move(other.staticSubcubes))
    , staticMicrocubes(std::move(other.staticMicrocubes))
    , waterSpans(std::move(other.waterSpans))
    , worldOrigin(other.worldOrigin)
    , renderManager(std::move(other.renderManager))
    , physicsManager(std::move(other.physicsManager))
    , voxelManager(std::move(other.voxelManager))
    , device(other.device)
    , physicalDevice(other.physicalDevice) {

    // Reset other object's device handles
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;

    // 4.2b: voxelManager now OWNS the authoritative voxel store, so it must be moved (above) —
    // but its callbacks captured the moved-from object's `this`. Re-wire onto this object, and
    // point physicsManager at THIS chunk's (just-moved-in) store.
    if (voxelManager.hasCallbacks()) wireVoxelManagerCallbacks();
    physicsManager.setVoxelStore(&voxelManager.getVoxelStore());
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        cleanupVulkanResources();
        cleanupPhysicsResources();

        // Move data
        cubes = std::move(other.cubes);
        staticSubcubes = std::move(other.staticSubcubes);
        staticMicrocubes = std::move(other.staticMicrocubes);
        waterSpans = std::move(other.waterSpans);
        worldOrigin = other.worldOrigin;
        renderManager = std::move(other.renderManager);
        physicsManager = std::move(other.physicsManager);
        voxelManager = std::move(other.voxelManager);
        device = other.device;
        physicalDevice = other.physicalDevice;

        // Reset other object's device handles
        other.device = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;

        // Re-bind the moved manager's callbacks to this object (see move ctor).
        if (voxelManager.hasCallbacks()) wireVoxelManagerCallbacks();
        physicsManager.setVoxelStore(&voxelManager.getVoxelStore());
    }
    return *this;
}

// One place that binds voxelManager's callbacks to THIS chunk. Used by initialize() and by the
// move operations (a moved manager's callbacks would otherwise dangle to the moved-from object).
// Every bound method is safe headless: renderManager/physicsManager no-op without device/world.
void Chunk::wireVoxelManagerCallbacks() {
    voxelManager.setCallbacks(
        [this]() -> std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this]() -> std::vector<std::unique_ptr<Subcube>>& { return staticSubcubes; },
        [this]() -> std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this]() -> const glm::ivec3& { return worldOrigin; },
        [this](bool value) { setDirty(value); },
        [this](bool value) { renderManager.setNeedsUpdate(value); },
        [this]() { rebuildFaces(); },
        [this](const glm::ivec3& pos) { addCollisionEntity(pos); },
        [this](const glm::ivec3& pos) { removeCollisionEntities(pos); },
        [this](const glm::ivec3& pos) { updateNeighborCollisionShapes(pos); },
        [this]() { return physicsManager.isInBulkOperation(); },
        [this]() { updateVulkanBuffer(); }
    );
}

void Chunk::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
    // Initialize renderManager with device handles
    renderManager.initialize(dev, physDev);
    // Initialize physicsManager with chunk origin
    physicsManager.initialize(nullptr, worldOrigin); // physicsWorld set separately via setPhysicsWorld
    physicsManager.setVoxelStore(&voxelManager.getVoxelStore());   // 4.2b: occupancy reads the store
    
    // Initialize voxelBreaker with callbacks
    voxelBreaker.setCallbacks(
        [this]() -> std::vector<std::unique_ptr<Subcube>>& { return staticSubcubes; },
        [this](const glm::ivec3& parent, const glm::ivec3& sub) { return removeSubcube(parent, sub); },
        [this]() { rebuildFaces(); },
        [this]() { batchUpdateCollisions(); },
        [this](const glm::ivec3& p, const glm::ivec3& s) { return getMicrocubesAt(p, s); },
        [this](const glm::ivec3& p) { return getSubcubesAt(p); },
        [this](bool v) { renderManager.setNeedsUpdate(v); },
        [this]() -> const glm::ivec3& { return worldOrigin; }
    );

    // Initialize voxelManager with callbacks (stored once, not per-call)
    wireVoxelManagerCallbacks();
}

Cube* Chunk::getCubeAt(const glm::ivec3& localPos) {
    if (!isValidLocalPosition(localPos)) return nullptr;
    // 4.2b: materialize on demand (nullptr for air) — see ChunkVoxelManager::materializeAt.
    return voxelManager.getCubeAtFast(localPos);
}

const Cube* Chunk::getCubeAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return nullptr;
    const ChunkVoxelManager& vm = voxelManager;
    return vm.getCubeAtFast(localPos);
}

Cube* Chunk::getCubeAtIndex(size_t index) {
    if (index >= cubes.size()) return nullptr;
    return cubes[index].get();
}

const Cube* Chunk::getCubeAtIndex(size_t index) const {
    if (index >= cubes.size()) return nullptr;
    return cubes[index].get();
}

// Every voxel mutation on a sealed chunk unseals it FIRST (4.4): the flag drops and the physics
// grid rejoins the query list synchronously with the edit; faces/occlusion refresh on the next
// managed rebuild.
bool Chunk::removeCube(const glm::ivec3& localPos, bool deferRebuild) {
    unsealForEdit();
    return voxelManager.removeCube(localPos, deferRebuild);
}

bool Chunk::addCube(const glm::ivec3& localPos) {
    unsealForEdit();
    return voxelManager.addCube(localPos);
}

bool Chunk::addCube(const glm::ivec3& localPos, const std::string& material, bool overwrite) {
    unsealForEdit();
    return voxelManager.addCube(localPos, material, overwrite);
}

void Chunk::setWaterSpans(std::vector<WaterSpanLocal> spans) {
#ifndef NDEBUG
    // Producers contract to (x,z)-sorted spans (ties broken by bottom for future multi-run
    // columns). Asserting instead of sorting makes an unsorted producer a caught bug rather
    // than a silent per-chunk sort cost on every generation.
    for (size_t i = 1; i < spans.size(); ++i) {
        const auto& a = spans[i - 1];
        const auto& b = spans[i];
        const uint32_t ka = (uint32_t(a.x) << 8) | a.z, kb = (uint32_t(b.x) << 8) | b.z;
        assert(ka < kb || (ka == kb && a.bottom < b.bottom));
    }
    for (const auto& s : spans) assert(s.x < 32 && s.z < 32 && s.top > s.bottom &&
                                       s.bottom >= 0.0f && s.top <= 32.0f);
#endif
    waterSpans = std::move(spans);
}

int Chunk::removeCubesBatch(const std::vector<glm::ivec3>& positions) {
    unsealForEdit();
    return voxelManager.removeCubesBatch(positions);
}

int Chunk::addCubesBatch(const std::vector<glm::ivec3>& positions, const std::string& material) {
    unsealForEdit();
    return voxelManager.addCubesBatch(positions, material);
}

void Chunk::clearAll() {
    unsealForEdit();
    // Bulk clear: reset all cubes to nullptr (no per-voxel callbacks)
    for (auto& cube : cubes) {
        cube.reset();
    }
    staticSubcubes.clear();
    staticMicrocubes.clear();

    // Clear all hash maps in one pass
    voxelManager.clearAllVoxels();

    physicsManager.getCollisionNeedsUpdateRef() = false;

    // Single rebuild of faces (now empty), GPU buffer, and physics
    rebuildFaces();
    updateVulkanBuffer();
    forcePhysicsRebuild();

    setDirty(true);

    LOG_INFO_FMT("Chunk", "Bulk cleared chunk at (" << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")");
}

void Chunk::fillAllCubes(const std::string& material) {
    unsealForEdit();
    voxelManager.fillAllVoxels(material);
}

// ── Phase 4.4 seal state ──

void Chunk::applySealedRenderState() {
    m_sealed = true;
    renderManager.clearForUniform();
    for (int f = 0; f < 6; ++f) m_faceConnect[f] = 0;      // fully occluding
    m_hasMirror = false;
    m_hasTransparent = false;
    physicsManager.unregisterGridFromWorld();              // interior unreachable
}

void Chunk::applyAirRenderState() {
    m_sealed = false;
    renderManager.clearForUniform();
    for (int f = 0; f < 6; ++f) m_faceConnect[f] = 0x3F;   // sight passes freely
    m_hasMirror = false;
    m_hasTransparent = false;
    // U1a (§15.5): an all-air chunk has no surface to collide against, yet it stayed in
    // the broadphase scan list forever (only SEALED chunks unregistered). In a tall
    // streamed world air chunks are the majority of loaded chunks, so this was most of
    // the wasted scan. Drop it from the query set; a later edit re-registers via unseal.
    physicsManager.unregisterGridFromWorld();
}

void Chunk::unsealForEdit() {
    if (!m_sealed) return;
    m_sealed = false;
    // Grid contents stayed maintained while unregistered (collision callbacks update it on
    // every voxel op); it only needs to rejoin the query list. registerGrid dedups.
    physicsManager.registerPrebuiltGrid();
}

void Chunk::ensurePhysicsRegistered() {
    // Reached only on the full-mesh (collidable) branch — a uniform-air or sealed chunk
    // returns before this. Clear any stale seal flag and rejoin the query set. Grid contents
    // are maintained live by collision callbacks, so this only re-adds it to the scan; the
    // O(1) registerGrid dedup makes the call free when the grid is already registered.
    m_sealed = false;
    physicsManager.registerPrebuiltGrid();
}

void Chunk::populateWithCubes() {
    // 4.2b: a full chunk is a store fill (palette "Default" + 32768 indices), not 32,768 heap
    // Cube allocations. Any previously materialized overlay Cubes are dropped with the old data.
    cubes.clear();
    voxelManager.fillAllVoxels("Default");

    // Mark chunk as dirty since it has new content
    setDirty(true);

    // Initialize hash maps for O(1) lookups (sub/micro maps; the store is already filled)
    initializeVoxelMaps();
}

void Chunk::initializeForLoading() {
    // 4.2b: voxel data loads into the palette store; the overlay slot array is allocated lazily
    // on first materialization, so a freshly loaded chunk holds zero Cube pointers.
    cubes.clear();
    voxelManager.clearAllVoxels();

    // Give the occupancy grid its origin so an off-thread forcePhysicsRebuild (async
    // chunk generation) fills a correctly-positioned grid. Idempotent: the later
    // main-thread initialize() re-runs this with the same origin (grid data survives).
    physicsManager.initialize(nullptr, worldOrigin);
    physicsManager.setVoxelStore(&voxelManager.getVoxelStore());   // 4.2b: occupancy reads the store
    
    // Clear any existing subcubes (unique_ptr auto-deletes)
    staticSubcubes.clear();
    
    // Clear any existing microcubes (unique_ptr auto-deletes)
    staticMicrocubes.clear();
    
    // Set up minimal no-op callbacks if initialize() hasn't been called
    // (e.g. in unit tests where Vulkan/physics aren't available)
    if (!voxelManager.hasCallbacks()) {
        voxelManager.setCallbacks(
            [this]() -> std::vector<std::unique_ptr<Cube>>& { return cubes; },
            [this]() -> std::vector<std::unique_ptr<Subcube>>& { return staticSubcubes; },
            [this]() -> std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
            [this]() -> const glm::ivec3& { return worldOrigin; },
            [this](bool value) { setDirty(value); },
            [](bool) {},              // setNeedsUpdate - no-op without renderer
            []() {},                  // rebuildFaces - no-op without renderer
            [](const glm::ivec3&) {}, // addCollision - no-op without physics
            [](const glm::ivec3&) {}, // removeCollision - no-op without physics
            [](const glm::ivec3&) {}, // updateNeighborCollisions - no-op without physics
            [this]() { return physicsManager.isInBulkOperation(); },
            []() {}                   // updateVulkanBuffer - no-op without renderer
        );
    }
    
    // Set bulk operation flag to prevent neighbor collision updates during loading
    physicsManager.setInBulkOperation(true);
    
    LOG_DEBUG_FMT("Chunk", "Initialized chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
              << ") for database loading");
}

void Chunk::rebuildFaces() {
    // Call the cross-chunk version without a neighbor lookup function
    // This will only do intra-chunk culling
    rebuildFaces(nullptr);
}

void Chunk::rebuildFaces(const NeighborLookupFunc& getNeighborCube,
                         const NeighborLightFunc& getNeighborLight,
                         const std::vector<uint8_t>* columnOpenMask) {
    // Delegate to render manager (4.2b: the palette store carries the static voxels; `cubes` is
    // the materialized overlay that wins where present)
    renderManager.rebuildAllFaces(cubes, staticSubcubes, staticMicrocubes, worldOrigin, getNeighborCube, getNeighborLight, columnOpenMask,
                                  &voxelManager.getVoxelStore());
    // Refresh cached render flags (geometry/materials may have changed).
    recomputeRenderFlags();
    // Refresh the occlusion visibility graph (cheap flood-fill, only on rebuild).
    computeVisibilityMask();
    // A fine rebuild IS level 0 (sub/micro + greedy merge). Recording it here keeps the tracked
    // level honest for every caller — including the ones that return a coarse chunk to full
    // detail without going through the LOD path. See Chunk::setLodFaces for the desync this
    // prevents.
    m_lodLevel = 0;
}

// Build the per-chunk face visibility graph for occlusion culling. A cell blocks
// sight only if it holds a FULL OPAQUE cube; empty/subdivided cells and transparent
// cubes (alpha < 0.99) count as air, so we err toward "visible" and never cull
// geometry seen through gaps or glass. Flood-fills the air cells into 6-connected
// components and records, for each component, which of the 6 chunk faces it touches;
// all faces a component touches are mutually visible.
void Chunk::computeVisibilityMask() {
    constexpr int N = 32;
    constexpr int TOTAL = N * N * N;
    auto& registry = Phyxel::Core::MaterialRegistry::instance();

    const ChunkVoxelStore& store = voxelManager.getVoxelStore();
    std::vector<uint8_t> blocking(TOTAL, 0);
    for (size_t i = 0; i < (size_t)TOTAL; ++i) {
        // Hybrid read: materialized overlay Cube wins, else the store answers.
        const Cube* cube = i < cubes.size() ? cubes[i].get() : nullptr;
        const std::string* matName;
        if (cube) matName = &cube->getMaterialName();
        else if (store.solid(i)) matName = &store.material(i);
        else continue;                               // empty cell → air
        const auto* mat = registry.getMaterial(*matName);
        if (mat && mat->alpha < 0.99f) continue;     // transparent → air (see-through)
        blocking[i] = 1;                             // full opaque cube → blocks sight
    }

    // Index matches the chunk layout: index = z + y*32 + x*1024.
    auto idxOf = [](int x, int y, int z) { return z + y * 32 + x * 1024; };
    const int dx[6] = {-1, 1, 0, 0, 0, 0};
    const int dy[6] = { 0, 0,-1, 1, 0, 0};
    const int dz[6] = { 0, 0, 0, 0,-1, 1};

    std::vector<int> comp(TOTAL, -1);
    std::vector<int> stack;
    uint8_t connect[6] = {0, 0, 0, 0, 0, 0};
    int compId = 0;
    for (int s = 0; s < TOTAL; ++s) {
        if (blocking[s] || comp[s] != -1) continue;
        uint8_t faces = 0;
        comp[s] = compId;
        stack.clear();
        stack.push_back(s);
        while (!stack.empty()) {
            int c = stack.back(); stack.pop_back();
            int x = c / (N * N), y = (c / N) % N, z = c % N;
            if (x == 0)     faces |= 1u << 0; if (x == N - 1) faces |= 1u << 1;
            if (y == 0)     faces |= 1u << 2; if (y == N - 1) faces |= 1u << 3;
            if (z == 0)     faces |= 1u << 4; if (z == N - 1) faces |= 1u << 5;
            for (int d = 0; d < 6; ++d) {
                int nx = x + dx[d], ny = y + dy[d], nz = z + dz[d];
                if (nx < 0 || nx >= N || ny < 0 || ny >= N || nz < 0 || nz >= N) continue;
                int nc = idxOf(nx, ny, nz);
                if (blocking[nc] || comp[nc] != -1) continue;
                comp[nc] = compId;
                stack.push_back(nc);
            }
        }
        // Every face this air component touches is mutually visible through it.
        for (int a = 0; a < 6; ++a) if (faces & (1u << a))
            for (int b = 0; b < 6; ++b) if (faces & (1u << b)) connect[a] |= 1u << b;
        ++compId;
    }
    for (int f = 0; f < 6; ++f) m_faceConnect[f] = connect[f];
}

// Rescan this chunk's cubes for mirror / transparent materials and cache the result.
// Called only when contents change (rebuildFaces), never per-frame. The renderer reads
// hasMirrorVoxel()/getFirstMirrorLocal() to decide on a reflection pass, and
// hasTransparentVoxel() to decide whether to run the OIT transparent pass.
void Chunk::recomputeRenderFlags() {
    m_hasMirror = false;
    m_hasTransparent = false;
    auto& registry = Phyxel::Core::MaterialRegistry::instance();
    const ChunkVoxelStore& store = voxelManager.getVoxelStore();
    for (size_t i = 0; i < ChunkVoxelStore::kVoxels; ++i) {
        // Hybrid read: materialized overlay Cube wins, else the store answers.
        const Cube* cube = i < cubes.size() ? cubes[i].get() : nullptr;
        const std::string* matName;
        if (cube) matName = &cube->getMaterialName();
        else if (store.solid(i)) matName = &store.material(i);
        else continue;
        const auto* mat = registry.getMaterial(*matName);
        if (!mat) continue;
        if (mat->isMirror && !m_hasMirror) {
            m_hasMirror = true;
            m_firstMirrorLocal = indexToLocal(i);
        }
        if (mat->alpha < 0.99f) {  // matches ChunkRenderManager transparency criterion
            m_hasTransparent = true;
        }
        if (m_hasMirror && m_hasTransparent) return; // both found, no need to scan further
    }
}

void Chunk::updateVulkanBuffer() {
    renderManager.setArenaRegionKey(worldOrigin);  // Phase 4.3: no-op unless arenas on
    renderManager.updateVulkanBuffer();
}

void Chunk::updateSingleCubeTexture(const glm::ivec3& localPos, uint16_t textureIndex) {
    if (!isValidLocalPosition(localPos)) return;
    // Presence from the store (4.2b): solid cube voxels may have no Cube object.
    if (getVoxelType(localPos) != VoxelLocation::CUBE) return;
    renderManager.updateSingleCubeTexture(localPos, textureIndex, cubes);
}

void Chunk::updateSingleSubcubeTexture(const glm::ivec3& parentLocalPos, const glm::ivec3& subcubePos, uint16_t textureIndex) {
    if (!isValidLocalPosition(parentLocalPos)) return;
    renderManager.updateSingleSubcubeTexture(parentLocalPos, subcubePos, textureIndex, staticSubcubes, worldOrigin);
}

void Chunk::createVulkanBuffer() {
    renderManager.setArenaRegionKey(worldOrigin);  // Phase 4.3: no-op unless arenas on
    renderManager.createVulkanBuffer();
}

void Chunk::cleanupVulkanResources() {
    renderManager.cleanupVulkanResources();
}

void Chunk::ensureBufferCapacity(size_t requiredInstances) {
    renderManager.ensureBufferCapacity(requiredInstances);
}

void Chunk::logBufferUtilization() const {
    renderManager.logBufferUtilization();
}

size_t Chunk::localToIndex(const glm::ivec3& localPos) {
    // CRITICAL: This must match the loop order in populateWithCubes()
    // X-major order (X outermost, Z innermost): z + y*32 + x*1024
    return localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
}

glm::ivec3 Chunk::indexToLocal(size_t index) {
    // Reverse the localToIndex calculation
    int x = index / (32 * 32);
    int y = (index % (32 * 32)) / 32;
    int z = index % 32;
    return glm::ivec3(x, y, z);
}

glm::vec3 Chunk::getMinBounds() const {
    // Convert world origin (ivec3) to world position (vec3) for bounding box
    return glm::vec3(worldOrigin);
}

glm::vec3 Chunk::getMaxBounds() const {
    // Chunk spans 32x32x32 units from worldOrigin to worldOrigin + (31,31,31)
    // For bounding box calculations, we want worldOrigin + (32,32,32) as max bounds
    return glm::vec3(worldOrigin) + glm::vec3(32.0f, 32.0f, 32.0f);
}

// =============================================================================
// SUBCUBE MANAGEMENT METHODS
// =============================================================================

Subcube* Chunk::getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    // Search in static subcubes only
    for (const auto& subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube.get();
        }
    }
    return nullptr;
}

const Subcube* Chunk::getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    // Search in static subcubes only
    for (const auto& subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube.get();
        }
    }
    return nullptr;
}

std::vector<Subcube*> Chunk::getSubcubesAt(const glm::ivec3& localPos) {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    // Collect from static subcubes only
    for (const auto& subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube.get());
        }
    }
    return result;
}

std::vector<Subcube*> Chunk::getStaticSubcubesAt(const glm::ivec3& localPos) {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    for (const auto& subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube.get());
        }
    }
    return result;
}

// =============================================================================
// Microcube Access Functions
// =============================================================================

Microcube* Chunk::getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    return const_cast<Microcube*>(voxelManager.getMicrocubeHelper(cubePos, subcubePos, microcubePos));
}

const Microcube* Chunk::getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) const {
    return voxelManager.getMicrocubeHelper(cubePos, subcubePos, microcubePos);
}

std::vector<Microcube*> Chunk::getMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    return voxelManager.getMicrocubesHelper(cubePos, subcubePos);
}

// =============================================================================
// NEW: O(1) VoxelLocation resolution system for optimized hover detection
// =============================================================================

VoxelLocation Chunk::resolveLocalPosition(const glm::ivec3& localPos) const {
    // Quick bounds check
    if (!isValidLocalPosition(localPos)) {
        return VoxelLocation();
    }
    
    // Delegate to voxel manager
    VoxelLocation location = voxelManager.resolveLocalPosition(localPos);
    
    // Fill in chunk-specific fields
    if (location.type != VoxelLocation::EMPTY) {
        location.chunk = const_cast<Chunk*>(this);
        location.localPos = localPos;
        location.worldPos = worldOrigin + localPos;
        // std::cout << "[Chunk::resolveLocalPosition] localPos=(" << localPos.x << "," << localPos.y << "," << localPos.z 
        //           << ") worldOrigin=(" << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z
        //           << ") worldPos=(" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ")" << std::endl;
        if (location.subcubePos == glm::ivec3(0)) {
            location.subcubePos = glm::ivec3(-1);
        }
    }
    
    return location;
}

bool Chunk::hasVoxelAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return false;
    return voxelManager.hasVoxelAt(localPos);
}

bool Chunk::hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    return voxelManager.hasSubcubeAt(localPos, subcubePos);
}

float Chunk::subVoxelFloor(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return 0.0f;
    return voxelManager.subVoxelFloor(localPos);
}

VoxelLocation::Type Chunk::getVoxelType(const glm::ivec3& localPos) const {
    return voxelManager.getVoxelType(localPos);
}

// O(1) optimized lookups (replace linear searches)
Cube* Chunk::getCubeAtFast(const glm::ivec3& localPos) {
    return voxelManager.getCubeAtFast(localPos);
}

const Cube* Chunk::getCubeAtFast(const glm::ivec3& localPos) const {
    return voxelManager.getCubeAtFast(localPos);
}

void Chunk::syncVoxelStoreAt(const glm::ivec3& localPos) {
    voxelManager.syncStoreAt(localPos);
}

void Chunk::setCubeVisible(const glm::ivec3& localPos, bool visible) {
    voxelManager.setCubeVisible(localPos, visible);
}

// Internal: Maintain hash map consistency (subdivided voxels only — see ChunkVoxelManager.h.
// The cube-keyed updateVoxelMaps/addToVoxelMaps/removeFromVoxelMaps trio is gone with the dense
// maps they maintained: cube presence/type now derive from the `cubes` array on read.)
void Chunk::addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube) {
    voxelManager.addSubcubeToMaps(localPos, subcubePos, subcube);
}

void Chunk::removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    voxelManager.removeSubcubeFromMaps(localPos, subcubePos);
}

void Chunk::addMicrocubeToMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, Microcube* microcube) {
    voxelManager.addMicrocubeToMaps(cubePos, subcubePos, microcubePos, microcube);
}

void Chunk::removeMicrocubeFromMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    voxelManager.removeMicrocubeFromMaps(cubePos, subcubePos, microcubePos);
}

void Chunk::initializeVoxelMaps() {
    voxelManager.initializeVoxelMaps();
}

bool Chunk::subdivideAt(const glm::ivec3& localPos) {
    unsealForEdit();
    return voxelManager.subdivideAt(localPos);
}

bool Chunk::addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const std::string& material, uint32_t tint, uint8_t state) {
    bool ok = voxelManager.addSubcube(parentPos, subcubePos, material);
    if (ok && (tint != 0xFFFFFFu || state != 0)) {
        if (Subcube* sc = getSubcubeAt(parentPos, subcubePos)) { sc->setTint(tint); sc->setState(state); }
    }
    return ok;
}

bool Chunk::removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    unsealForEdit();
    return voxelManager.removeSubcube(parentPos, subcubePos);
}

int Chunk::clearCellsBulk(const std::vector<glm::ivec3>& localCells) {
    return voxelManager.clearCellsBulk(localCells);
}

bool Chunk::clearSubdivisionAt(const glm::ivec3& localPos) {
    unsealForEdit();
    return voxelManager.clearSubdivisionAt(localPos);
}

size_t Chunk::subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    return ChunkVoxelManager::subcubeToIndex(parentPos, subcubePos);
}

bool Chunk::isValidLocalPosition(const glm::ivec3& localPos) const {
    return localPos.x >= 0 && localPos.x < 32 &&
           localPos.y >= 0 && localPos.y < 32 &&
           localPos.z >= 0 && localPos.z < 32;
}

// =============================================================================
// PHYSICS-RELATED METHODS
// =============================================================================

bool Chunk::breakSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, 
                        Physics::PhysicsWorld* physicsWorld, ChunkManager* chunkManager, const glm::vec3& impulseForce) {
    return voxelBreaker.breakSubcube(parentPos, subcubePos, physicsWorld, chunkManager, impulseForce);
}

// =============================================================================
// Physics Management - Delegated to ChunkPhysicsManager
// =============================================================================

void Chunk::setPhysicsWorld(Physics::PhysicsWorld* world) {
    physicsManager.setPhysicsWorld(world);
}

void Chunk::registerPrebuiltPhysics() {
    physicsManager.registerPrebuiltGrid();
}

void Chunk::validateCollisionSystem() const {
    physicsManager.validateCollisionSystem();
}

void Chunk::debugLogSpatialGrid() const {
    physicsManager.debugLogSpatialGrid();
}

size_t Chunk::getCollisionEntityCount() const {
    return physicsManager.getCollisionEntityCount();
}

size_t Chunk::getCubeEntityCount() const {
    return physicsManager.getCubeEntityCount();
}

size_t Chunk::getSubcubeEntityCount() const {
    return physicsManager.getSubcubeEntityCount();
}

void Chunk::debugPrintSpatialGridStats() const {
    physicsManager.debugPrintSpatialGridStats();
}

// NOTE: Physics body creation and collision methods still directly access physics members
// These will be fully extracted in Phase 2 completion:
// - Move createChunkPhysicsBody implementation to ChunkPhysicsManager
// - Move updateChunkPhysicsBody implementation to ChunkPhysicsManager  
// - Move addCollisionEntity/removeCollisionEntities implementations
// - Move buildInitialCollisionShapes implementation
// - Move updateNeighborCollisionShapes implementation
// - Remove compatibility macros once all methods are extracted

void Chunk::createChunkPhysicsBody() {
    physicsManager.createChunkPhysicsBody(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3&) -> std::vector<Subcube*> {
            std::vector<Subcube*> result;
            result.reserve(staticSubcubes.size());
            for (const auto& s : staticSubcubes) { if (s) result.push_back(s.get()); }
            return result;
        },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); }
    );
}

void Chunk::updateChunkPhysicsBody() {
    physicsManager.updateChunkPhysicsBody(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3&) -> std::vector<Subcube*> {
            std::vector<Subcube*> result;
            result.reserve(staticSubcubes.size());
            for (const auto& s : staticSubcubes) { if (s) result.push_back(s.get()); }
            return result;
        },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); }
    );
}

void Chunk::forcePhysicsRebuild() {
    physicsManager.forcePhysicsRebuild(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3&) -> std::vector<Subcube*> {
            std::vector<Subcube*> result;
            result.reserve(staticSubcubes.size());
            for (const auto& s : staticSubcubes) { if (s) result.push_back(s.get()); }
            return result;
        },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); }
    );
}

void Chunk::cleanupPhysicsResources() {
    physicsManager.cleanupPhysicsResources();
}

// ============================================================================
// COLLISION SHAPE CREATION HELPERS - Delegated to ChunkPhysicsManager
// ============================================================================
// These wrappers provide cube access to the physics manager

void Chunk::createCubeCollisionShape(const glm::ivec3& localPos) {
    auto probe = [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); };
    physicsManager.createCubeCollisionShape(localPos, probe);
}

void Chunk::createSubcubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    auto getSubcube = [this](const glm::ivec3& cPos, const glm::ivec3& sPos) -> Subcube* {
        return this->getSubcubeAt(cPos, sPos);
    };
    physicsManager.createSubcubeCollisionShape(cubePos, subcubePos, getSubcube);
}

void Chunk::createMicrocubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos,
                                          const Microcube* microcube) {
    physicsManager.createMicrocubeCollisionShape(cubePos, subcubePos, microcube);
}

// ============================================================================
// COLLISION ENTITY MANAGEMENT
// ============================================================================

// IMPROVED collision system - memory-safe reference-counted shapes with individual subcube tracking
// This method replaces the old system that used nullptr placeholders and geometric distance heuristics
// Now provides proper individual tracking for each collision shape with automatic memory management
void Chunk::addCollisionEntity(const glm::ivec3& localPos) {
    // Delegate to physics manager with callbacks for accessing chunk data
    physicsManager.addCollisionEntity(
        localPos,
        [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); },
        [this](const glm::ivec3& pos, const glm::ivec3& subPos) { return getMicrocubesAt(pos, subPos); },
        [this](const glm::ivec3& pos) { return getStaticSubcubesAt(pos); }
    );
}

void Chunk::removeCollisionEntities(const glm::ivec3& localPos) {
    // Delegate to physics manager
    physicsManager.removeCollisionEntities(localPos);
}

void Chunk::batchUpdateCollisions() {
    physicsManager.batchUpdateCollisions(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3&) -> std::vector<Subcube*> {
            std::vector<Subcube*> result;
            result.reserve(staticSubcubes.size());
            for (const auto& s : staticSubcubes) { if (s) result.push_back(s.get()); }
            return result;
        },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); }
    );
}

void Chunk::setPhysicsBulkMode(bool bulk) {
    physicsManager.setInBulkOperation(bulk);
}

// Helper method to check if a cube has exposed faces (for collision optimization)
bool Chunk::hasExposedFaces(const glm::ivec3& localPos) const {
    // Delegate to physics manager with callback for accessing cubes
    return physicsManager.hasExposedFaces(
        localPos,
        [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); }
    );
}

void Chunk::buildInitialCollisionShapes() {
    physicsManager.buildInitialCollisionShapes(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3&) -> std::vector<Subcube*> {
            std::vector<Subcube*> result;
            result.reserve(staticSubcubes.size());
            for (const auto& s : staticSubcubes) { if (s) result.push_back(s.get()); }
            return result;
        },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); }
    );
}

void Chunk::updateNeighborCollisionShapes(const glm::ivec3& localPos) {
    physicsManager.updateNeighborCollisionShapes(
        localPos,
        [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); },
        [this](const glm::ivec3& pos, const glm::ivec3& subPos) { return getMicrocubesAt(pos, subPos); },
        [this](const glm::ivec3& pos) { return getStaticSubcubesAt(pos); }
    );
}

void Chunk::beginBulkOperation() {
    // Skip per-voxel collision-shape creation during a bulk load (structure place / template spawn);
    // endBulkOperation() rebuilds all collision once. Without this, placing tens of thousands of
    // subcubes pays a per-voxel collision add — the build-freeze hot spot.
    physicsManager.setInBulkOperation(true);
}

void Chunk::endBulkOperation() {
    physicsManager.endBulkOperation(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3&) -> std::vector<Subcube*> {
            std::vector<Subcube*> result;
            result.reserve(staticSubcubes.size());
            for (const auto& s : staticSubcubes) { if (s) result.push_back(s.get()); }
            return result;
        },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) { return visibleSolidCubeAt(pos); }
    );
}

// =============================================================================
// Microcube Manipulation Functions (Phase 3 - Placeholders for now)
// =============================================================================

bool Chunk::subdivideSubcubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    unsealForEdit();
    return voxelManager.subdivideSubcubeAt(cubePos, subcubePos);
}

bool Chunk::addMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, const std::string& material, uint32_t tint, uint8_t state) {
    bool ok = voxelManager.addMicrocube(parentCubePos, subcubePos, microcubePos, material);
    if (ok && (tint != 0xFFFFFFu || state != 0)) {
        if (Microcube* mc = getMicrocubeAt(parentCubePos, subcubePos, microcubePos)) { mc->setTint(tint); mc->setState(state); }
    }
    return ok;
}

bool Chunk::removeMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    return voxelManager.removeMicrocube(parentCubePos, subcubePos, microcubePos);
}

bool Chunk::clearMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    return voxelManager.clearMicrocubesAt(cubePos, subcubePos);
}

} // namespace Phyxel
