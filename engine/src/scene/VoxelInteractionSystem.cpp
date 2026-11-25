#include "scene/VoxelInteractionSystem.h"
#include "core/ChunkManager.h"
#include "core/AssetManager.h"
#include "core/PlacedObjectManager.h"
#include "core/DynamicFurnitureManager.h"
#include "physics/PhysicsWorld.h"
#include "ui/WindowManager.h"
#include "utils/CoordinateUtils.h"
#include "physics/Material.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <iomanip>
#include <limits>

namespace Phyxel {

VoxelInteractionSystem::VoxelInteractionSystem(ChunkManager* chunkManager,
                                             Physics::PhysicsWorld* physicsWorld,
                                             UI::WindowManager* windowManager,
                                             Core::AudioSystem* audioSystem)
    : m_chunkManager(chunkManager)
    , m_physicsWorld(physicsWorld)
    , m_windowManager(windowManager)
    , m_audioSystem(audioSystem)
    , m_hasHoveredCube(false)
    , m_lastHoveredCube(-1)
    , m_hoverDetectionTimeMs(0.0)
    , m_avgHoverDetectionTimeMs(0.0)
    , m_hoverDetectionSamples(0)
{
    // Configure VoxelManipulationSystem callbacks
    m_manipulator.setCallbacks(
        [this]() -> ChunkManager* { return m_chunkManager; },
        [this]() -> Physics::PhysicsWorld* { return m_physicsWorld; }
    );
    
    LOG_INFO("VoxelInteractionSystem", "VoxelInteractionSystem initialized");
}

// Main hover detection function
void VoxelInteractionSystem::updateMouseHover(const glm::vec3& cameraPos, const glm::vec3& cameraFront,
                                             const glm::vec3& cameraUp, double mouseX, double mouseY,
                                             bool isMouseCaptured) {
    // Skip hover detection if right mouse button is pressed (camera mode)
    if (isMouseCaptured) {
        return;
    }
    
    // Cache camera state for interaction tools
    m_lastCameraPos = cameraPos;
    m_lastCameraFront = cameraFront;
    m_lastCameraUp = cameraUp;
    
    // Performance timing
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Create ray from mouse position (delegated to VoxelRaycaster)
    glm::vec3 rayDirection = m_raycaster.screenToWorldRay(
        mouseX, mouseY, cameraPos, cameraFront, cameraUp,
        [this]() -> UI::WindowManager* { return m_windowManager; }
    );
    m_lastMouseRay = rayDirection;   // click actions ray-test with THIS, not cameraFront
    if (m_worldEditor) {   // F7.4: the new store's pick (the camera's forward ray without a window)
        updateHoverFromRay(cameraPos, glm::length(rayDirection) > 0.0f ? rayDirection : cameraFront);
        return;
    }
    
    // Without the world editor there is nothing to hover: today's chunk pick went with the Chunk store (D4).
    m_raycaster.clearDebugTarget();
    clearHoveredCubeInChunksOptimized();
    m_lastHoveredCube = -1;
    m_hoverDetectionTimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - startTime).count();
}

InteractionContext VoxelInteractionSystem::createContext() const {
    InteractionContext context;
    context.hoveredLocation = m_currentHoveredLocation;
    context.hasHovered = m_hasHoveredCube;
    context.cameraPosition = m_lastCameraPos;
    context.cameraFront = m_lastCameraFront;
    context.cameraUp = m_lastCameraUp;
    
    LOG_INFO_FMT("VoxelInteraction", "[CREATE CONTEXT] m_currentHoveredLocation: worldPos=(" << m_currentHoveredLocation.worldPos.x << "," << m_currentHoveredLocation.worldPos.y << "," << m_currentHoveredLocation.worldPos.z << ") hitFace=" << m_currentHoveredLocation.hitFace << " hitPoint=(" << m_currentHoveredLocation.hitPoint.x << "," << m_currentHoveredLocation.hitPoint.y << "," << m_currentHoveredLocation.hitPoint.z << ")");
    
    return context;
}

void VoxelInteractionSystem::updateHoverFromRay(const glm::vec3& origin, const glm::vec3& dir) {
    if (!m_worldEditor) return;
    m_editHover = m_worldEditor->pick(origin, dir, 64.0f);
    m_hasHoveredCube = m_editHover.hit;
}

// F7.4: one brush at the hover (break: the hit voxel's cell; place: the cell in front of the hit face)
bool VoxelInteractionSystem::editorAction(int size, bool place) {
    if (!m_worldEditor) return false;
    m_lastEditVoxels = 0;
    if (!m_editHover.hit) return true;
    const glm::ivec3 v = place ? m_editHover.voxel + m_editHover.normal : m_editHover.voxel;
    m_lastEditVoxels = m_worldEditor->brush(v, size, place ? m_manipulator.currentMaterial() : std::string());
    if (m_lastEditVoxels) {
        auto fd = [](int a) { return a >= 0 ? a / 8 : -((-a + 7) / 8); };
        if (m_onVoxelChanged) m_onVoxelChanged(glm::ivec3(fd(v.x), fd(v.y), fd(v.z)));   // the edit's cube
        if (place) playPlaceSound(true, glm::ivec3(fd(v.x), fd(v.y), fd(v.z)));
    }
    m_editHover = WorldEditHit{};   // the world changed under the hover: re-picked next update
    m_hasHoveredCube = false;
    return true;
}

void VoxelInteractionSystem::breakHoveredCube(const glm::vec3& cameraPos) {
    (void)cameraPos;
    editorAction(8, false);   // F7.4; no Chunk fallback (today's Chunk store went with D4)
}

void VoxelInteractionSystem::breakHoveredSubcube() {
    editorAction(2, false);   // F7.4; no Chunk fallback (the 1/9 m store was deleted in D3c-5)
}

void VoxelInteractionSystem::breakHoveredMicrocube() {
    editorAction(1, false);   // F7.4; no Chunk fallback (the 1/9 m store was deleted in D3c-5)
}

void VoxelInteractionSystem::playPlaceSound(bool hadHover, const glm::ivec3& placedPos) {
    // Catalog path: a 3D "voxel.place" at the placed voxel's center — placing a
    // block to your left now sounds like it. Legacy path (registry unset): the
    // old 2D direct-file play, so missed wiring degrades instead of silencing.
    if (m_soundRegistry && hadHover) {
        m_soundRegistry->playEvent("voxel.place", glm::vec3(placedPos) + glm::vec3(0.5f));
    } else if (m_audioSystem) {
        m_audioSystem->playSound(Core::AssetManager::instance().resolveSound("place.wav"));
    }
}

void VoxelInteractionSystem::placeVoxelAtHover() {
    editorAction(8, true);   // F7.4; no Chunk fallback (today's Chunk store went with D4)
}

void VoxelInteractionSystem::placeSubcubeAtHover() {
    editorAction(2, true);   // F7.4; no Chunk fallback (the 1/9 m store was deleted in D3c-5)
}

void VoxelInteractionSystem::placeMicrocubeAtHover() {
    editorAction(1, true);   // F7.4; no Chunk fallback (the 1/9 m store was deleted in D3c-5)
}

void VoxelInteractionSystem::clearHoveredCubeInChunksOptimized() {
    if (m_hasHoveredCube && m_currentHoveredLocation.isValid()) {
        LOG_TRACE_FMT("HoverDetection", "Clearing hover - was: isMicrocube=" << m_currentHoveredLocation.isMicrocube 
                  << " isSubcube=" << m_currentHoveredLocation.isSubcube);
        
        m_hasHoveredCube = false;
        m_currentHoveredLocation = CubeLocation(); // Reset to invalid state
    }
}

bool VoxelInteractionSystem::rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                                             const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                                             float& distance) const {
    // Delegate to VoxelRaycaster
    return m_raycaster.rayAABBIntersect(rayOrigin, rayDir, aabbMin, aabbMax, distance);
}

glm::vec3 VoxelInteractionSystem::screenToWorldRay(double mouseX, double mouseY, const glm::vec3& cameraPos,
                                                   const glm::vec3& cameraFront, const glm::vec3& cameraUp) const {
    // Delegate to VoxelRaycaster
    return m_raycaster.screenToWorldRay(
        mouseX, mouseY, cameraPos, cameraFront, cameraUp,
        [this]() -> UI::WindowManager* { return m_windowManager; }
    );
}

glm::ivec3 CubeLocation::getAdjacentPlacementPosition() const {
    if (hitFace < 0) return worldPos; // No face data available
    
    // Calculate offset based on which face was hit
    // Face numbering matches raycaster: 0=left(-X), 1=right(+X), 2=bottom(-Y), 3=top(+Y), 4=back(-Z), 5=front(+Z)
    glm::ivec3 offset(0);
    switch (hitFace) {
        case 0: offset = glm::ivec3(-1, 0, 0); break;  // left (-X face)
        case 1: offset = glm::ivec3(1, 0, 0); break;   // right (+X face)
        case 2: offset = glm::ivec3(0, -1, 0); break;  // bottom (-Y face)
        case 3: offset = glm::ivec3(0, 1, 0); break;   // top (+Y face)
        case 4: offset = glm::ivec3(0, 0, -1); break;  // back (-Z face)
        case 5: offset = glm::ivec3(0, 0, 1); break;   // front (+Z face)
    }
    
    glm::ivec3 placementPos = worldPos + offset;
    LOG_DEBUG_FMT("VoxelInteraction", "[FACE DEBUG] Hit face " << hitFace << " on cube at (" 
              << worldPos.x << "," << worldPos.y << "," << worldPos.z 
              << "), placing at (" << placementPos.x << "," << placementPos.y << "," << placementPos.z << ")");
    
    return placementPos;
}

bool VoxelInteractionSystem::tryActivateFurnitureAtHover(const glm::vec3& cameraPos,
                                                          const glm::vec3& cameraFront) {
    if (!m_placedObjects || !m_dynamicFurniture) return false;
    if (!m_hasHoveredCube || !m_currentHoveredLocation.isValid()) return false;

    // Check if the hovered voxel belongs to a placed object
    glm::ivec3 worldPos = m_currentHoveredLocation.worldPos;
    auto objectIds = m_placedObjects->getAt(worldPos);
    if (objectIds.empty()) return false;

    // Pick the first placed object found at this position
    const std::string& objId = objectIds[0];

    // If already active, just consume the click (no impulse)
    if (m_dynamicFurniture->isActive(objId)) {
        return true;  // Consumed the click — don't break voxels
    }

    // Skip objects that aren't template-based (structures are not activatable)
    const auto* placed = m_placedObjects->get(objId);
    if (!placed || placed->category != "template") return false;

    // Activate with zero impulse — just convert static to dynamic
    bool activated = m_dynamicFurniture->activate(objId);
    if (activated) {
        LOG_INFO_FMT("VoxelInteraction", "Activated furniture '" << objId << "'");
        // 3D thunk at the furniture itself via the event catalog; legacy 2D
        // direct-file fallback when no registry is wired. (This call site was
        // the hit.wav dead reference — silent for months because nothing
        // validated that the file existed. CatalogValidation now does.)
        if (m_soundRegistry) {
            m_soundRegistry->playEvent("furniture.activate", glm::vec3(worldPos) + glm::vec3(0.5f));
        } else if (m_audioSystem) {
            m_audioSystem->playSound(Core::AssetManager::instance().resolveSound("hit.wav"));
        }
    }
    return activated;
}

std::string VoxelInteractionSystem::getActiveFurnitureAtHover() const {
    if (!m_placedObjects || !m_dynamicFurniture) return "";
    if (!m_hasHoveredCube || !m_currentHoveredLocation.isValid()) return "";

    glm::ivec3 worldPos = m_currentHoveredLocation.worldPos;
    auto objectIds = m_placedObjects->getAt(worldPos);

    for (const auto& objId : objectIds) {
        if (m_dynamicFurniture->isActive(objId)) {
            return objId;
        }
    }
    return "";
}

void VoxelInteractionSystem::cycleTargetMode(int direction) {
    int current = static_cast<int>(m_targetMode);
    current = (current + direction % 3 + 3) % 3;
    m_targetMode = static_cast<TargetMode>(current);

    const char* names[] = {"Cube", "Subcube", "Microcube"};
    LOG_INFO("VoxelInteractionSystem", "Switched to {} placement mode", names[current]);
}

void VoxelInteractionSystem::placeActiveVoxelAtHover() {
    switch (m_targetMode) {
        case TargetMode::Cube:      placeVoxelAtHover();     break;
        case TargetMode::Subcube:   placeSubcubeAtHover();   break;
        case TargetMode::Microcube: placeMicrocubeAtHover(); break;
    }
}

} // namespace Phyxel
