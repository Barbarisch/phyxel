#include "graphics/Renderer.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "ui/ImGuiRenderer.h"
#include "scene/SceneManager.h"
#include "core/ChunkManager.h"
#include "utils/PerformanceProfiler.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include "core/Types.h"

#include <iostream>
#include <cstring>

namespace VulkanCube {
namespace Graphics {

Renderer::Renderer() 
    : window(nullptr)
    , windowWidth(0)
    , windowHeight(0) {
}

Renderer::~Renderer() {
    cleanup();
}

bool Renderer::initialize(GLFWwindow* window, int windowWidth, int windowHeight) {
    this->window = window;
    this->windowWidth = windowWidth;
    this->windowHeight = windowHeight;

    LOG_INFO("Rendering", "Initializing Renderer...");

    // Initialize Vulkan systems
    if (!initializeVulkan()) {
        LOG_ERROR("Rendering", "Failed to initialize Vulkan!");
        return false;
    }

    // Load shaders for pipelines
    if (!loadShaders()) {
        LOG_ERROR("Rendering", "Failed to load shaders!");
        return false;
    }

    // Initialize render pipelines after shaders are loaded
    if (!renderPipeline->createGraphicsPipeline()) {
        LOG_ERROR("Rendering", "Failed to create static render pipeline!");
        return false;
    }

    if (!dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes()) {
        LOG_ERROR("Rendering", "Failed to create dynamic render pipeline!");
        return false;
    }

    // Create framebuffers using the render pass from the static pipeline
    if (!vulkanDevice->createFramebuffers(renderPipeline->getRenderPass())) {
        LOG_ERROR("Rendering", "Failed to create framebuffers!");
        return false;
    }

    // Create command buffers for rendering
    if (!vulkanDevice->createCommandBuffers()) {
        LOG_ERROR("Rendering", "Failed to create command buffers!");
        return false;
    }

    // Create synchronization objects (semaphores, fences)
    if (!vulkanDevice->createSyncObjects()) {
        LOG_ERROR("Rendering", "Failed to create sync objects!");
        return false;
    }

    // Create vertex and instance buffers
    if (!vulkanDevice->createVertexBuffer()) {
        LOG_ERROR("Rendering", "Failed to create vertex buffer!");
        return false;
    }

    if (!vulkanDevice->createInstanceBuffer()) {
        LOG_ERROR("Rendering", "Failed to create instance buffer!");
        return false;
    }

    // Create index buffer for cube rendering
    if (!vulkanDevice->createIndexBuffer()) {
        LOG_ERROR("Rendering", "Failed to create index buffer!");
        return false;
    }

    // Create uniform buffers for MVP matrices
    if (!vulkanDevice->createUniformBuffers()) {
        LOG_ERROR("Rendering", "Failed to create uniform buffers!");
        return false;
    }

    LOG_INFO("Rendering", "Renderer initialized successfully");
    return true;
}

void Renderer::cleanup() {
    if (vulkanDevice) {
        vulkanDevice->cleanup();
    }
    
    dynamicRenderPipeline.reset();
    renderPipeline.reset();
    vulkanDevice.reset();
}

void Renderer::resize(int newWidth, int newHeight) {
    windowWidth = newWidth;
    windowHeight = newHeight;
    projectionMatrixNeedsUpdate = true;
    
    // Note: Resize handling would be done through recreating swapchain
    // but for simplicity we just update projection matrix for now
}

bool Renderer::initializeVulkan() {
    vulkanDevice = std::make_unique<Vulkan::VulkanDevice>();
    renderPipeline = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice);
    dynamicRenderPipeline = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice);

    // Initialize Vulkan instance and debug messenger first
    if (!vulkanDevice->createInstance()) {
        LOG_ERROR("Rendering", "Failed to create Vulkan instance!");
        return false;
    }

    if (!vulkanDevice->setupDebugMessenger()) {
        LOG_ERROR("Rendering", "Failed to set up debug messenger!");
        return false;
    }

    // Create surface before physical device selection
    if (!vulkanDevice->createSurface(window)) {
        LOG_ERROR("Rendering", "Failed to create Vulkan surface!");
        return false;
    }

    // Continue with device initialization
    if (!vulkanDevice->pickPhysicalDevice()) {
        LOG_ERROR("Rendering", "Failed to find a suitable GPU!");
        return false;
    }

    if (!vulkanDevice->createLogicalDevice()) {
        LOG_ERROR("Rendering", "Failed to create logical device!");
        return false;
    }

    if (!vulkanDevice->createDescriptorSetLayout()) {
        LOG_ERROR("Rendering", "Failed to create descriptor set layout!");
        return false;
    }

    if (!vulkanDevice->createSwapChain(windowWidth, windowHeight)) {
        LOG_ERROR("Rendering", "Failed to create swap chain!");
        return false;
    }

    // Create depth buffer resources
    if (!vulkanDevice->createDepthResources()) {
        LOG_ERROR("Rendering", "Failed to create depth resources!");
        return false;
    }

    return true;
}

bool Renderer::loadShaders() {
    // Load shaders for pipelines
    if (!renderPipeline->loadShaders("shaders/static_voxel.vert.spv", "shaders/voxel.frag.spv")) {
        LOG_ERROR("Rendering", "Failed to load static pipeline shaders!");
        return false;
    }
    
    if (!dynamicRenderPipeline->loadShaders("shaders/dynamic_voxel.vert.spv", "shaders/voxel.frag.spv")) {
        LOG_ERROR("Rendering", "Failed to load dynamic pipeline shaders!");
        return false;
    }

    return true;
}

void Renderer::renderFrame(
    const glm::mat4& viewMatrix,
    const glm::mat4& projectionMatrix,
    Scene::SceneManager* sceneManager,
    ChunkManager* chunkManager,
    UI::ImGuiRenderer* imguiRenderer,
    PerformanceProfiler* performanceProfiler) {
    
    frameStartTime = std::chrono::high_resolution_clock::now();

    // Wait for previous frame
    vulkanDevice->waitForFence(currentFrame);
    vulkanDevice->resetFence(currentFrame);

    // Acquire next image
    uint32_t imageIndex;
    VkResult result = vulkanDevice->acquireNextImage(currentFrame, &imageIndex);
    
    if (result != VK_SUCCESS) {
        LOG_ERROR("Rendering", "Failed to acquire swapchain image!");
        return;
    }

    // Update scene data for rendering
    auto instanceUpdateStart = std::chrono::high_resolution_clock::now();
    bool instanceDataChanged = sceneManager->updateInstanceData();
    
    // Ensure instance buffer is uploaded at least once
    static bool instanceBufferUploaded = false;
    if (!instanceBufferUploaded) {
        instanceDataChanged = true;  // Force upload on first frame
        instanceBufferUploaded = true;
    }
    
    // Only update GPU buffer when data actually changes
    if (instanceDataChanged) {
        const auto& sceneInstanceData = sceneManager->getInstanceData();
        if (!sceneInstanceData.empty()) {
            // Optimize: Use static_cast to avoid copying - both structs are binary compatible
            static_assert(sizeof(VulkanCube::InstanceData) == sizeof(Vulkan::InstanceData), 
                         "Scene and Vulkan InstanceData must be the same size");
            
            std::vector<Vulkan::InstanceData> vulkanInstances;
            vulkanInstances.resize(sceneInstanceData.size());
            
            // Use memcpy for bulk copy instead of element-by-element copy
            std::memcpy(vulkanInstances.data(), sceneInstanceData.data(), 
                       sceneInstanceData.size() * sizeof(Vulkan::InstanceData));
            
            // Track memory bandwidth for instance buffer update
            size_t instanceDataSize = vulkanInstances.size() * sizeof(Vulkan::InstanceData);
            performanceProfiler->recordMemoryTransfer(instanceDataSize);
            
            vulkanDevice->updateInstanceBuffer(vulkanInstances);
        }
    }
    auto instanceUpdateEnd = std::chrono::high_resolution_clock::now();

    // Prepare uniform buffer data
    auto uboStart = std::chrono::high_resolution_clock::now();
    
    // Cache projection matrix - only recalculate on window resize
    if (projectionMatrixNeedsUpdate) {
        cachedProjectionMatrix = glm::perspective(
            glm::radians(45.0f), 
            (float)windowWidth / (float)windowHeight, 
            0.1f, 
            renderDistance  // Use configurable render distance
        );
        cachedProjectionMatrix[1][1] *= -1; // Flip Y for Vulkan
        projectionMatrixNeedsUpdate = false;
    }
    
    glm::mat4 view = viewMatrix;
    glm::mat4 proj = cachedProjectionMatrix;

    // Get actual cube count for rendering
    size_t cubeCount = sceneManager->getCubeCount();
    auto uboEnd = std::chrono::high_resolution_clock::now();
    
    // Update uniform buffer with camera matrices
    auto uniformUploadStart = std::chrono::high_resolution_clock::now();
    
    // Track memory bandwidth for uniform buffer update
    size_t uniformBufferSize = sizeof(glm::mat4) * 2 + sizeof(uint32_t); // view + proj + cubeCount
    performanceProfiler->recordMemoryTransfer(uniformBufferSize);
    
    vulkanDevice->updateUniformBuffer(currentFrame, view, proj, static_cast<uint32_t>(cubeCount));
    auto uniformUploadEnd = std::chrono::high_resolution_clock::now();

    // Perform CPU-based chunk-level frustum culling
    auto frustumCullingStart = std::chrono::high_resolution_clock::now();
    updateCameraFrustum(view, proj);
    std::vector<uint32_t> visibleChunks = getVisibleChunks(chunkManager);
    auto frustumCullingEnd = std::chrono::high_resolution_clock::now();
    
    // Update frustum culling statistics
    uint32_t totalChunks = chunkManager ? static_cast<uint32_t>(chunkManager->chunks.size()) : 0;
    uint32_t visibleChunkCount = static_cast<uint32_t>(visibleChunks.size());
    uint32_t culledChunks = totalChunks - visibleChunkCount;
    
    // Calculate CPU frustum culling time in milliseconds
    double frustumCullingTimeMs = std::chrono::duration<double, std::milli>(
        frustumCullingEnd - frustumCullingStart
    ).count();
    
    // Store results for UI display (estimate cubes per chunk for compatibility)
    const uint32_t estimatedCubesPerChunk = 32 * 32 * 32; // Maximum possible cubes per chunk
    lastVisibleInstances = visibleChunkCount * estimatedCubesPerChunk;
    lastCulledInstances = culledChunks * estimatedCubesPerChunk;
    
    performanceProfiler->recordFrustumCulling(totalChunks, culledChunks, frustumCullingTimeMs);

    // Record command buffer
    auto recordStart = std::chrono::high_resolution_clock::now();
    vulkanDevice->resetCommandBuffer(currentFrame);
    vulkanDevice->beginCommandBuffer(currentFrame);
    
    // Update performance statistics
    LOG_TRACE("Rendering", "[DEBUG] About to update performance stats...");
    updatePerformanceStats(chunkManager, sceneManager);
    LOG_TRACE("Rendering", "[DEBUG] Performance stats updated");
    
    // Begin render pass
    LOG_TRACE("Rendering", "[DEBUG] About to begin render pass...");
    vulkanDevice->beginRenderPass(currentFrame, imageIndex, renderPipeline->getRenderPass());
    LOG_TRACE("Rendering", "[DEBUG] Render pass begun");
    
    // Bind graphics pipeline
    LOG_TRACE("Rendering", "[DEBUG] About to bind graphics pipeline...");
    renderPipeline->bindGraphicsPipeline(vulkanDevice->getCommandBuffer(currentFrame));
    LOG_TRACE("Rendering", "[DEBUG] Graphics pipeline bound");
    
    // Set viewport (required for dynamic viewport)
    LOG_TRACE("Rendering", "[DEBUG] About to set viewport...");
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(windowWidth);
    viewport.height = static_cast<float>(windowHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    LOG_TRACE_FMT("Rendering", "[DEBUG] Viewport configured: " << windowWidth << "x" << windowHeight);
    vkCmdSetViewport(vulkanDevice->getCommandBuffer(currentFrame), 0, 1, &viewport);
    LOG_TRACE("Rendering", "[DEBUG] Viewport set successfully");
    
    // Set scissor (required for dynamic scissor)
    LOG_TRACE("Rendering", "[DEBUG] About to set scissor...");
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)};
    vkCmdSetScissor(vulkanDevice->getCommandBuffer(currentFrame), 0, 1, &scissor);
    LOG_TRACE("Rendering", "[DEBUG] Scissor set successfully");
    
    // Bind vertex and instance buffers
    vulkanDevice->bindVertexBuffers(currentFrame);
    vulkanDevice->bindIndexBuffer(currentFrame);
    
    // Bind descriptor sets (uniform buffers)
    vulkanDevice->bindDescriptorSets(currentFrame, renderPipeline->getGraphicsLayout());
    
    // Draw using dual rendering system
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Render static geometry first - only visible chunks
        renderStaticGeometry(chunkManager, visibleChunks);
        
        // Render dynamic subcubes with separate pipeline
        renderDynamicSubcubes(chunkManager, visibleChunks);
        
        // Get accurate performance statistics from chunk manager
        auto chunkStats = chunkManager->getPerformanceStats();
        
        // Update frame timing with chunk-based statistics
        frameTiming.drawCalls = static_cast<int>(chunkManager->chunks.size());
        frameTiming.vertexCount = static_cast<int>(chunkStats.totalVertices);
        frameTiming.visibleInstances = static_cast<int>(chunkStats.totalCubes);
        frameTiming.fullyOccludedCubes = static_cast<int>(chunkStats.fullyOccludedCubes);
        frameTiming.partiallyOccludedCubes = static_cast<int>(chunkStats.partiallyOccludedCubes);
        frameTiming.totalHiddenFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        frameTiming.faceCulledFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        frameTiming.occlusionCulledInstances = static_cast<int>(chunkStats.fullyOccludedCubes);
    } else if (cubeCount > 0) {
        // Fallback to old single-chunk rendering if chunk manager isn't available
        glm::vec3 chunkBaseOffset(0.0f, 0.0f, 0.0f);
        vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset);
        vulkanDevice->drawIndexed(currentFrame, 36, static_cast<uint32_t>(cubeCount));
        frameTiming.drawCalls = 1;
        frameTiming.vertexCount = static_cast<int>(cubeCount * 36);
    }
    
    // Render ImGui on top
    imguiRenderer->render(currentFrame, imageIndex);
    
    // End render pass
    vulkanDevice->endRenderPass(currentFrame);
    vulkanDevice->endCommandBuffer(currentFrame);
    auto recordEnd = std::chrono::high_resolution_clock::now();

    // Submit command buffer
    auto submitStart = std::chrono::high_resolution_clock::now();
    if (!vulkanDevice->submitCommandBuffer(currentFrame)) {
        LOG_ERROR("Rendering", "Failed to submit command buffer!");
        return;
    }
    auto submitEnd = std::chrono::high_resolution_clock::now();

    // Present frame
    auto presentStart = std::chrono::high_resolution_clock::now();
    if (!vulkanDevice->presentFrame(imageIndex, currentFrame)) {
        LOG_ERROR("Rendering", "Failed to present frame!");
        return;
    }
    auto presentEnd = std::chrono::high_resolution_clock::now();

    currentFrame = (currentFrame + 1) % 2; // MAX_FRAMES_IN_FLIGHT = 2
    
    // Use GPU culling results if available for frustum culling statistics
    if (lastVisibleInstances + lastCulledInstances > 0) {
        frameTiming.frustumCulledInstances = static_cast<int>(lastCulledInstances);
    } else {
        frameTiming.frustumCulledInstances = 0;
    }
    
    // Record detailed timing
    auto frameEnd = std::chrono::high_resolution_clock::now();
    DetailedFrameTiming detailedTiming;
    detailedTiming.totalFrameTime = std::chrono::duration<double, std::milli>(frameEnd - frameStartTime).count();
    detailedTiming.physicsTime = 0.0; // Physics timing integrated into main loop
    detailedTiming.mousePickTime = 0.0; // Not implemented yet
    detailedTiming.uboFillTime = std::chrono::duration<double, std::milli>(uboEnd - uboStart).count();
    detailedTiming.instanceUpdateTime = std::chrono::duration<double, std::milli>(instanceUpdateEnd - instanceUpdateStart).count();
    detailedTiming.drawCmdUpdateTime = 0.0; // Not separate in our implementation
    detailedTiming.uniformUploadTime = std::chrono::duration<double, std::milli>(uniformUploadEnd - uniformUploadStart).count();
    detailedTiming.occlusionCullingTime = 0.0; // Occlusion culling is done once at scene creation, not per-frame
    detailedTiming.commandRecordTime = std::chrono::duration<double, std::milli>(recordEnd - recordStart).count();
    detailedTiming.gpuSubmitTime = std::chrono::duration<double, std::milli>(submitEnd - submitStart).count();
    detailedTiming.presentTime = std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
    
    detailedTimings.push_back(detailedTiming);
    if (detailedTimings.size() > 60) {
        detailedTimings.erase(detailedTimings.begin());
    }
}

void Renderer::renderStaticGeometry(ChunkManager* chunkManager, const std::vector<uint32_t>& visibleChunks) {
    // Render static cubes and static subcubes using the standard pipeline
    // Bind graphics pipeline
    renderPipeline->bindGraphicsPipeline(vulkanDevice->getCommandBuffer(currentFrame));
    
    // Draw indexed cubes using chunk manager - only render visible chunks
    if (chunkManager && !chunkManager->chunks.empty() && !visibleChunks.empty()) {
        // Render only visible chunks
        for (uint32_t chunkIndex : visibleChunks) {
            if (chunkIndex >= chunkManager->chunks.size()) continue; // Safety check
            
            const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
            
            // Skip chunks with no static faces
            if (chunk->getNumInstances() == 0) continue;
            
            // Bind this chunk's instance buffer
            VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
            VkDeviceSize instanceOffsets[] = {0};
            vkCmdBindVertexBuffers(vulkanDevice->getCommandBuffer(currentFrame), 1, 1, instanceBuffers, instanceOffsets);
            
            // Set chunk origin as push constants for world positioning
            glm::ivec3 worldOrigin = chunk->getWorldOrigin();
            glm::vec3 chunkBaseOffset(worldOrigin.x, worldOrigin.y, worldOrigin.z);
            vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset);
            
            // Draw this chunk's static geometry
            vulkanDevice->drawIndexed(currentFrame, 36, chunk->getNumInstances());
        }
    }
}

void Renderer::renderDynamicSubcubes(ChunkManager* chunkManager, const std::vector<uint32_t>& visibleChunks) {
    // Get all global dynamic subcube faces from ChunkManager
    const auto& allDynamicSubcubeFaces = chunkManager->getGlobalDynamicSubcubeFaces();
    
    if (allDynamicSubcubeFaces.empty()) {
        return; // No dynamic subcubes to render
    }
    
    // Note: Dynamic objects can move between chunks, so for now we render all of them
    // TODO: In the future, we could add per-object frustum culling for dynamic objects
    
    // Switch to dynamic pipeline
    dynamicRenderPipeline->bindGraphicsPipeline(vulkanDevice->getCommandBuffer(currentFrame));
    
    // Bind descriptor sets for dynamic pipeline
    vulkanDevice->bindDescriptorSets(currentFrame, dynamicRenderPipeline->getGraphicsLayout());
    
    // Upload dynamic subcube instance data
    vulkanDevice->updateDynamicSubcubeBuffer(allDynamicSubcubeFaces);
    
    // Bind dynamic subcube buffer
    vulkanDevice->bindDynamicSubcubeBuffer(currentFrame);
    
    // No push constants needed for dynamic subcubes (positions are in world space)
    glm::vec3 zeroPushConstants(0.0f, 0.0f, 0.0f);
    vulkanDevice->pushConstants(currentFrame, dynamicRenderPipeline->getGraphicsLayout(), zeroPushConstants);
    
    // Draw all dynamic subcubes in one call
    vulkanDevice->drawIndexed(currentFrame, 36, static_cast<uint32_t>(allDynamicSubcubeFaces.size()));
}

void Renderer::updatePerformanceStats(ChunkManager* chunkManager, Scene::SceneManager* sceneManager) {
    // Record occlusion culling statistics from chunk manager (if using chunks)
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Get occlusion stats from chunk manager
        auto chunkStats = chunkManager->getPerformanceStats();
        frameTiming.fullyOccludedCubes = static_cast<int>(chunkStats.fullyOccludedCubes);
        frameTiming.partiallyOccludedCubes = static_cast<int>(chunkStats.partiallyOccludedCubes);
        frameTiming.totalHiddenFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        frameTiming.occlusionCulledInstances = static_cast<int>(chunkStats.fullyOccludedCubes);
        frameTiming.faceCulledFaces = static_cast<int>(chunkStats.totalHiddenFaces);
    } else {
        // Fallback to old SceneManager occlusion culling
        int fullyOccluded, partiallyOccluded, totalHiddenFaces;
        sceneManager->getOcclusionCullStats(fullyOccluded, partiallyOccluded, totalHiddenFaces);
        frameTiming.fullyOccludedCubes = fullyOccluded;
        frameTiming.partiallyOccludedCubes = partiallyOccluded;
        frameTiming.totalHiddenFaces = totalHiddenFaces;
        frameTiming.occlusionCulledInstances = fullyOccluded;
        frameTiming.faceCulledFaces = totalHiddenFaces;
    }
}

void Renderer::updateCameraFrustum(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
    // Extract frustum planes from view-projection matrix
    glm::mat4 viewProjection = projectionMatrix * viewMatrix;
    cameraFrustum.extractFromMatrix(viewProjection);
    
    // Extract camera position from view matrix for distance culling
    // Camera position is the inverse translation of the view matrix
    glm::mat4 invView = glm::inverse(viewMatrix);
    cameraPosition = glm::vec3(invView[3]);
}

std::vector<uint32_t> Renderer::getVisibleChunks(ChunkManager* chunkManager) {
    std::vector<uint32_t> visibleChunks;
    
    if (!chunkManager) {
        return visibleChunks;
    }
    
    // Calculate camera chunk coordinate
    glm::ivec3 cameraChunkCoord = Utils::CoordinateUtils::worldToChunkCoord(glm::ivec3(cameraPosition));
    
    // Calculate how many chunks to check in each direction based on render distance
    // Each chunk is 32x32x32, so we need to check a radius of chunks
    int chunkRadius = static_cast<int>(std::ceil(renderDistance / 32.0f)) + 1; // +1 for safety margin
    
    // Only iterate through chunks near the camera (scalable for infinite worlds)
    for (int dx = -chunkRadius; dx <= chunkRadius; dx++) {
        for (int dy = -chunkRadius; dy <= chunkRadius; dy++) {
            for (int dz = -chunkRadius; dz <= chunkRadius; dz++) {
                glm::ivec3 chunkCoord = cameraChunkCoord + glm::ivec3(dx, dy, dz);
                
                // Use spatial hash map for O(1) chunk lookup
                auto it = chunkManager->chunkMap.find(chunkCoord);
                if (it == chunkManager->chunkMap.end()) {
                    continue; // Chunk doesn't exist at this coordinate
                }
                
                Chunk* chunk = it->second;
                if (!chunk) continue;
                
                // Find chunk index in the chunks vector for the return value
                uint32_t chunkIndex = 0;
                bool found = false;
                for (uint32_t i = 0; i < chunkManager->chunks.size(); ++i) {
                    if (chunkManager->chunks[i].get() == chunk) {
                        chunkIndex = i;
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
                
                // Calculate chunk AABB
                glm::vec3 minBounds = chunk->getMinBounds();
                glm::vec3 maxBounds = chunk->getMaxBounds();
                glm::vec3 chunkCenter = (minBounds + maxBounds) * 0.5f;
                
                // Distance-based culling (early rejection for performance)
                float distanceToCamera = glm::length(chunkCenter - cameraPosition);
                if (distanceToCamera > renderDistance) {
                    continue; // Skip this chunk - too far away
                }
                
                // Create AABB object for frustum testing
                Utils::AABB chunkAABB(minBounds, maxBounds);
                
                // Test AABB against frustum
                if (cameraFrustum.intersects(chunkAABB)) {
                    visibleChunks.push_back(chunkIndex);
                }
            }
        }
    }
    
    return visibleChunks;
}

} // namespace Graphics
} // namespace VulkanCube
