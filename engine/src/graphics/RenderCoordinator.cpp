#include "graphics/RenderCoordinator.h"
#include "graphics/RaycastVisualizer.h"
#include "graphics/ShadowMap.h"
#include "graphics/PostProcessor.h"
#include "graphics/Camera.h"
#include "graphics/DebrisRenderPipeline.h"
#include "vulkan/RenderPipeline.h"
#include "ui/ImGuiRenderer.h"
#include "vulkan/VulkanDevice.h"
#include "ui/WindowManager.h"
#include "input/InputManager.h"
#include "core/ChunkManager.h"
#include "utils/CoordinateUtils.h"
#include "utils/Frustum.h"
#include "utils/Logger.h"
#include "utils/GpuProfiler.h"
#include "scene/Character.h"
#include "scene/Entity.h"
#include "scene/RagdollCharacter.h"
#include "scene/PhysicsCharacter.h"
#include "scene/AnimatedVoxelCharacter.h"
#include <glm/gtc/matrix_transform.hpp>
#include <map>

namespace Phyxel {
namespace Graphics {

RenderCoordinator::RenderCoordinator(
    Vulkan::VulkanDevice* vulkanDevice,
    Vulkan::RenderPipeline* renderPipeline,
    Vulkan::RenderPipeline* dynamicRenderPipeline,
    UI::ImGuiRenderer* imguiRenderer,
    UI::WindowManager* windowManager,
    Input::InputManager* inputManager,
    Camera* camera,
    ChunkManager* chunkManager,
    Utils::PerformanceMonitor* performanceMonitor,
    PerformanceProfiler* performanceProfiler,
    RaycastVisualizer* raycastVisualizer,
    ScriptingSystem* scriptingSystem
)
    : vulkanDevice(vulkanDevice)
    , renderPipeline(renderPipeline)
    , dynamicRenderPipeline(dynamicRenderPipeline)
    , imguiRenderer(imguiRenderer)
    , windowManager(windowManager)
    , inputManager(inputManager)
    , camera(camera)
    , chunkManager(chunkManager)
    , performanceMonitor(performanceMonitor)
    , performanceProfiler(performanceProfiler)
    , raycastVisualizer(raycastVisualizer)
    , scriptingSystem(scriptingSystem)
{
    shadowMap = std::make_unique<ShadowMap>(vulkanDevice);
    shadowMap->initialize();
    
    // Pass shadow map resources to VulkanDevice for descriptor updates
    vulkanDevice->setShadowMapResources(shadowMap->getDepthImageView(), shadowMap->getSampler());
    
    // Trigger descriptor set update to bind the shadow map
    vulkanDevice->updateDescriptorSetsWithTexture();

    // Initialize PostProcessor
    postProcessor = std::make_unique<PostProcessor>(vulkanDevice, windowManager->getWidth(), windowManager->getHeight());
    if (!postProcessor->initialize()) {
        LOG_ERROR("RenderCoordinator", "Failed to initialize PostProcessor!");
    }

    // Update Render Pipelines to use Offscreen Render Pass
    // This switches rendering from directly to swapchain to the offscreen buffer
    renderPipeline->setRenderPass(postProcessor->getSceneRenderPass());
    // We need to recreate the pipelines because they bake in the render pass
    renderPipeline->createGraphicsPipeline(); 
    renderPipeline->createDebugGraphicsPipeline();
    // Also recreate debug line pipeline if it exists/is used
    renderPipeline->createDebugLinePipeline();
    renderPipeline->createCharacterPipeline();
    renderPipeline->createInstancedCharacterPipeline();

    dynamicRenderPipeline->setRenderPass(postProcessor->getSceneRenderPass());
    dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes();

    // Create character instance buffer (max 10000 instances)
    vulkanDevice->createCharacterInstanceBuffer(10000);

    // Recreate Swapchain Framebuffers using PostProcess Render Pass
    // The swapchain framebuffers now need to be compatible with the post-process render pass
    // which outputs to the swapchain surface
    vulkanDevice->createFramebuffers(postProcessor->getPostProcessRenderPass());

    // Re-initialize ImGuiRenderer with the correct render pass (Swapchain Pass)
    // It was initialized with the old render pass in WorldInitializer
    imguiRenderer->cleanup();
    imguiRenderer->initialize(windowManager->getHandle(), vulkanDevice, postProcessor->getPostProcessRenderPass());

    // Initialize GPU Profiler
    gpuProfiler = std::make_unique<GpuProfiler>();
    gpuProfiler->init(vulkanDevice);

    // Initialize Debris Pipeline
    debrisPipeline = std::make_unique<DebrisRenderPipeline>();
    debrisPipeline->initialize(
        vulkanDevice->getDevice(), 
        vulkanDevice->getPhysicalDevice(), 
        postProcessor->getSceneRenderPass(), 
        vulkanDevice->getSwapChainExtent()
    );
}

RenderCoordinator::~RenderCoordinator() = default;

void RenderCoordinator::render() {
    drawFrame();
}

size_t RenderCoordinator::renderStaticGeometry() {
    // Render static cubes and static subcubes using the standard pipeline
    // Note: Pipeline is already bound in drawFrame() - don't rebind here
    // as it would overwrite the debug pipeline if debug mode is enabled
    
    size_t renderedChunks = 0;
    
    // Draw indexed cubes using chunk manager with proper culling
    if (chunkManager && !chunkManager->chunks.empty()) {
        
        // LEVEL 1: Distance-based culling (sphere of influence)
        // LEVEL 2: Frustum culling (camera view)
        visibleChunkIndices.clear();  // Reuse preallocated member vector
        
        // Compute camera position and frustum ONCE per frame (invariant across chunks)
        glm::vec3 cameraPos = camera->getPosition();
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + camera->getFront(), camera->getUp());
        glm::mat4 viewProjection = cachedProjectionMatrix * view;
        
        Utils::Frustum cameraFrustum;
        cameraFrustum.extractFromMatrix(viewProjection);
        
        for (size_t i = 0; i < chunkManager->chunks.size(); ++i) {
            const Chunk* chunk = chunkManager->chunks[i].get();
            
            // Skip chunks with no static faces (already optimized)
            if (chunk->getNumInstances() == 0) continue;
            
            // Get chunk bounding box
            glm::vec3 minBounds = chunk->getMinBounds();
            glm::vec3 maxBounds = chunk->getMaxBounds();
            glm::vec3 chunkCenter = (minBounds + maxBounds) * 0.5f;
            
            // LEVEL 1: Chunk inclusion distance culling (broader range for chunk loading)
            float distanceToCamera = glm::length(chunkCenter - cameraPos);
            if (distanceToCamera > chunkInclusionDistance) {
                continue; // Skip chunk - too far away even for loading
            }
            
            // LEVEL 2: Frustum culling (uses actual render distance)
            // Create AABB for frustum testing
            Utils::AABB chunkAABB(minBounds, maxBounds);
            
            // Test chunk against frustum (this uses the shorter render distance in projection matrix)
            if (!cameraFrustum.intersects(chunkAABB)) {
                continue; // Skip chunk - not visible in camera view
            }
            
            // Chunk passed both distance and frustum culling
            visibleChunkIndices.push_back(i);
        }
        
        // Render only the visible chunks
        for (size_t chunkIndex : visibleChunkIndices) {
            const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
            
            // Bind this chunk's instance buffer
            VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
            VkDeviceSize instanceOffsets[] = {0};
            vkCmdBindVertexBuffers(vulkanDevice->getCommandBuffer(currentFrame), 1, 1, instanceBuffers, instanceOffsets);
            
            // Set chunk origin as push constants for world positioning
            glm::ivec3 worldOrigin = chunk->getWorldOrigin();
            glm::vec3 chunkBaseOffset(worldOrigin.x, worldOrigin.y, worldOrigin.z);
            
            // Push constants with debug mode if enabled
            if (debugModeEnabled) {
                vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset, debugVisualizationMode);
            } else {
                vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset);
            }
            
            // Draw this chunk's static geometry
            // LEVEL 3: Face culling already applied (only visible faces in buffer)
            vulkanDevice->drawIndexed(currentFrame, 36, chunk->getNumInstances());
            renderedChunks++;
        }
    }
    
    return renderedChunks;
}

void RenderCoordinator::renderDynamicSubcubes() {
    // Get all global dynamic subcube faces from ChunkManager
    // All dynamic subcubes (both G key spawned and broken from chunks) are now global
    const auto& allDynamicSubcubeFaces = chunkManager->getGlobalDynamicSubcubeFaces();
    
    // Debug output for dynamic object count
    static int debugFrameCount = 0;
    if (debugFrameCount % 60 == 0) { // Every 60 frames
        size_t subcubeCount = chunkManager->getGlobalDynamicSubcubeCount();
        size_t cubeCount = chunkManager->getGlobalDynamicCubeCount();
        // std::cout << "[DEBUG] Dynamic objects: " << subcubeCount << " subcubes, " << cubeCount 
        //           << " cubes, " << allDynamicSubcubeFaces.size() << " total faces" << std::endl;
    }
    debugFrameCount++;
    
    // Only render if we have dynamic subcube faces
    if (!allDynamicSubcubeFaces.empty()) {
        // Update dynamic subcube buffer
        vulkanDevice->updateDynamicSubcubeBuffer(allDynamicSubcubeFaces);
        
        // Bind dynamic render pipeline
        vkCmdBindPipeline(vulkanDevice->getCommandBuffer(currentFrame), 
                         VK_PIPELINE_BIND_POINT_GRAPHICS, 
                         dynamicRenderPipeline->getGraphicsPipeline());
        
        // Bind vertex and dynamic instance buffers
        vulkanDevice->bindDynamicSubcubeBuffer(currentFrame);
        vulkanDevice->bindIndexBuffer(currentFrame);
        
        // Bind descriptor sets for dynamic pipeline
        vulkanDevice->bindDescriptorSets(currentFrame, dynamicRenderPipeline->getGraphicsLayout());
        
        // No push constants needed for dynamic subcubes (world position is in instance data)
        // Draw all dynamic subcubes (6 indices per quad face)
        vulkanDevice->drawIndexed(currentFrame, 6, static_cast<uint32_t>(allDynamicSubcubeFaces.size()));
    }
}

void RenderCoordinator::renderShadowPass(VkCommandBuffer commandBuffer, const glm::mat4& lightSpaceMatrix) {
    if (!shadowMap) return;

    shadowMap->beginRenderPass(commandBuffer);

    glm::vec3 cameraPos = camera->getPosition();

    // Bind global vertex buffer (binding 0) and index buffer
    // We use bindVertexBuffers to bind the shared vertex buffer to binding 0
    // It also binds the global instance buffer to binding 1, but we'll override that per-chunk
    vulkanDevice->bindVertexBuffers(currentFrame);
    vulkanDevice->bindIndexBuffer(currentFrame);

    // Iterate chunks and draw
    if (chunkManager && !chunkManager->chunks.empty()) {
        for (const auto& chunk : chunkManager->chunks) {
             if (chunk->getNumInstances() == 0) continue;
             
             // Simple distance culling for shadows
             glm::vec3 minBounds = chunk->getMinBounds();
             glm::vec3 maxBounds = chunk->getMaxBounds();
             glm::vec3 chunkCenter = (minBounds + maxBounds) * 0.5f;
             if (glm::length(chunkCenter - cameraPos) > 100.0f + 32.0f) continue; // Shadow range + chunk radius

             // Bind chunk instance buffer
             VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
             VkDeviceSize instanceOffsets[] = {0};
             vkCmdBindVertexBuffers(commandBuffer, 1, 1, instanceBuffers, instanceOffsets);
             
             // Push constants
             struct ShadowPushConsts {
                 glm::mat4 lightSpaceMatrix;
                 glm::vec3 chunkBaseOffset;
             } pushConsts;
             
             pushConsts.lightSpaceMatrix = lightSpaceMatrix;
             glm::ivec3 worldOrigin = chunk->getWorldOrigin();
             pushConsts.chunkBaseOffset = glm::vec3(worldOrigin.x, worldOrigin.y, worldOrigin.z);
             
             vkCmdPushConstants(commandBuffer, shadowMap->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);
             
             // Draw
             vkCmdDrawIndexed(commandBuffer, 36, chunk->getNumInstances(), 0, 0, 0);
        }
    }

    shadowMap->endRenderPass(commandBuffer);
}

void RenderCoordinator::drawFrame() {
    // Skip rendering when window is minimized (0x0 extent is invalid in Vulkan)
    if (windowManager->getWidth() == 0 || windowManager->getHeight() == 0) {
        return;
    }

    // Check if we need to recreate swapchain due to window resize
    if (vulkanDevice->getFramebufferResized() || windowManager->wasResized()) {
        LOG_INFO("RenderCoordinator", "Resize detected! VulkanFlag: {}, WindowFlag: {}", 
            vulkanDevice->getFramebufferResized(), windowManager->wasResized());
            
        // Resize PostProcessor resources
        postProcessor->resize(windowManager->getWidth(), windowManager->getHeight());
        
        // Recreate pipelines (viewport/scissor change)
        renderPipeline->createGraphicsPipeline();
        renderPipeline->createDebugGraphicsPipeline();
        renderPipeline->createDebugLinePipeline();
        dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes();

        if (!vulkanDevice->recreateSwapChain(windowManager->getWidth(), windowManager->getHeight(), postProcessor->getPostProcessRenderPass())) {
            LOG_INFO("RenderCoordinator", "recreateSwapChain returned false (minimized?)");
            return; // Try again next frame
        }
        windowManager->acknowledgeResize();
        projectionMatrixNeedsUpdate = true;
    }

    // Wait for previous frame
    vulkanDevice->waitForFence(currentFrame);
    vulkanDevice->resetFence(currentFrame);

    // Acquire next image
    uint32_t imageIndex;
    VkResult result = vulkanDevice->acquireNextImage(currentFrame, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Resize PostProcessor resources
        postProcessor->resize(windowManager->getWidth(), windowManager->getHeight());
        
        // Recreate pipelines
        renderPipeline->createGraphicsPipeline();
        renderPipeline->createDebugGraphicsPipeline();
        renderPipeline->createDebugLinePipeline();
        renderPipeline->createCharacterPipeline();
        dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes();

        // Swapchain is out of date, recreate it
        if (!vulkanDevice->recreateSwapChain(windowManager->getWidth(), windowManager->getHeight(), postProcessor->getPostProcessRenderPass())) {
            return; // Try again next frame
        }
        return; // Skip this frame and try again
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("RenderCoordinator", "Failed to acquire swapchain image!");
        return;
    }

    // ChunkManager handles its own data management - no instance buffer needed
    // Static chunk geometry is pre-built and doesn't change unless modified
    
    // Get chunk statistics for rendering
    auto chunkStats = chunkManager->getPerformanceStats();
    
    // Prepare uniform buffer data (optimized)
    auto uboStart = std::chrono::high_resolution_clock::now();
    
    // Cache projection matrix - only recalculate on window resize
    if (projectionMatrixNeedsUpdate) {
        cachedProjectionMatrix = glm::perspective(
            glm::radians(45.0f), 
            (float)windowManager->getWidth() / (float)windowManager->getHeight(), 
            0.1f, 
            maxChunkRenderDistance  // Use configurable render distance
        );
        cachedProjectionMatrix[1][1] *= -1; // Flip Y for Vulkan
        projectionMatrixNeedsUpdate = false;
    }

    // Use cached matrices from update()
    glm::mat4 view = cachedViewMatrix;
    glm::mat4 proj = cachedProjectionMatrix;
    
    // Calculate light space matrix for shadows
    glm::vec3 cameraPos = camera->getPosition();
    glm::mat4 lightSpaceMatrix = shadowMap ? shadowMap->getLightSpaceMatrix(sunDirection, cameraPos, 100.0f) : glm::mat4(1.0f);
    
    auto uboEnd = std::chrono::high_resolution_clock::now();
    
    // Update uniform buffer with camera matrices
    auto uniformUploadStart = std::chrono::high_resolution_clock::now();
    
    // Track memory bandwidth for uniform buffer update
    size_t uniformBufferSize = sizeof(glm::mat4) * 3 + sizeof(glm::vec3) * 2 + sizeof(uint32_t) + sizeof(float) * 2; // view + proj + lightSpace + sunDir + sunColor + cubeCount + ambient + emissive
    performanceProfiler->recordMemoryTransfer(uniformBufferSize);
    
    vulkanDevice->updateUniformBuffer(currentFrame, view, proj, lightSpaceMatrix, sunDirection, sunColor, static_cast<uint32_t>(chunkStats.totalCubes), ambientLightStrength, emissiveMultiplier);
    auto uniformUploadEnd = std::chrono::high_resolution_clock::now();

    // Record command buffer
    auto recordStart = std::chrono::high_resolution_clock::now();
    vulkanDevice->resetCommandBuffer(currentFrame);
    vulkanDevice->beginCommandBuffer(currentFrame);
    
    VkCommandBuffer cmd = vulkanDevice->getCommandBuffer(currentFrame);
    gpuProfiler->startFrame(currentFrame, cmd);
    
    {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Shadow Pass");
        // Render Shadow Pass
        renderShadowPass(cmd, lightSpaceMatrix);
    }
    
    // Record occlusion culling statistics from chunk manager
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Reuse chunkStats cached at top of drawFrame()
        performanceMonitor->getCurrentFrameTiming().fullyOccludedCubes = static_cast<int>(chunkStats.fullyOccludedCubes);
        performanceMonitor->getCurrentFrameTiming().partiallyOccludedCubes = static_cast<int>(chunkStats.partiallyOccludedCubes);
        performanceMonitor->getCurrentFrameTiming().totalHiddenFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        performanceMonitor->getCurrentFrameTiming().occlusionCulledInstances = static_cast<int>(chunkStats.fullyOccludedCubes);
        performanceMonitor->getCurrentFrameTiming().faceCulledFaces = static_cast<int>(chunkStats.totalHiddenFaces);
    } else {
        // No chunks available
        performanceMonitor->getCurrentFrameTiming().fullyOccludedCubes = 0;
        performanceMonitor->getCurrentFrameTiming().partiallyOccludedCubes = 0;
        performanceMonitor->getCurrentFrameTiming().totalHiddenFaces = 0;
        performanceMonitor->getCurrentFrameTiming().occlusionCulledInstances = 0;
        performanceMonitor->getCurrentFrameTiming().faceCulledFaces = 0;
    }
    
    // Begin Scene Render Pass (Offscreen)
    postProcessor->beginSceneRenderPass(vulkanDevice->getCommandBuffer(currentFrame));
    
    {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Scene Pass");

        // Bind graphics pipeline (debug or normal based on debug mode)
    if (debugModeEnabled) {
        renderPipeline->bindDebugGraphicsPipeline(vulkanDevice->getCommandBuffer(currentFrame));
    } else {
        renderPipeline->bindGraphicsPipeline(vulkanDevice->getCommandBuffer(currentFrame));
    }
    
    // Set viewport (required for dynamic viewport)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(windowManager->getWidth());
    viewport.height = static_cast<float>(windowManager->getHeight());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(vulkanDevice->getCommandBuffer(currentFrame), 0, 1, &viewport);
    
    // Set scissor (required for dynamic scissor)
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(windowManager->getWidth()), static_cast<uint32_t>(windowManager->getHeight())};
    vkCmdSetScissor(vulkanDevice->getCommandBuffer(currentFrame), 0, 1, &scissor);
    
    // Bind vertex and instance buffers
    vulkanDevice->bindVertexBuffers(currentFrame);
    vulkanDevice->bindIndexBuffer(currentFrame);
    
    // Bind descriptor sets (uniform buffers)
    vulkanDevice->bindDescriptorSets(currentFrame, renderPipeline->getGraphicsLayout());
    
    // Draw using dual rendering system
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Render static geometry first and capture how many chunks were actually rendered
        size_t actuallyRenderedChunks = 0;
        {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Static Geometry");
            actuallyRenderedChunks = renderStaticGeometry();
        }
        
        // Render dynamic subcubes with separate pipeline
        {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Dynamic Subcubes");
            renderDynamicSubcubes();
        }

        // Clear transient debug lines before rendering entities
        if (raycastVisualizer) {
            raycastVisualizer->beginFrame();
        }

        // Render entities (Characters)
        {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Entities");
            renderEntities(vulkanDevice->getCommandBuffer(currentFrame));
        }
        
        // Reuse chunkStats cached at top of drawFrame()
        // Update frame timing with chunk-based statistics using ACTUAL rendered chunks
        performanceMonitor->getCurrentFrameTiming().drawCalls = static_cast<int>(actuallyRenderedChunks);  // Only chunks that passed culling
        performanceMonitor->getCurrentFrameTiming().vertexCount = static_cast<int>(chunkStats.totalVertices);
        performanceMonitor->getCurrentFrameTiming().visibleInstances = static_cast<int>(chunkStats.totalCubes);
        performanceMonitor->getCurrentFrameTiming().fullyOccludedCubes = static_cast<int>(chunkStats.fullyOccludedCubes);
        performanceMonitor->getCurrentFrameTiming().partiallyOccludedCubes = static_cast<int>(chunkStats.partiallyOccludedCubes);
        performanceMonitor->getCurrentFrameTiming().totalHiddenFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        performanceMonitor->getCurrentFrameTiming().faceCulledFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        performanceMonitor->getCurrentFrameTiming().occlusionCulledInstances = static_cast<int>(chunkStats.fullyOccludedCubes);
        
        // Optional: Add culling statistics debug output
        static size_t lastRenderedChunks = 0;
        if (actuallyRenderedChunks != lastRenderedChunks) {
            LOG_DEBUG_FMT("RenderCoordinator", "[CULLING] Total chunks: " << chunkManager->chunks.size() 
                      << ", Rendered chunks: " << actuallyRenderedChunks 
                      << " (Culled: " << (chunkManager->chunks.size() - actuallyRenderedChunks) << ")");
            lastRenderedChunks = actuallyRenderedChunks;
        }
    } else {
        // No chunks available - render nothing
        performanceMonitor->getCurrentFrameTiming().drawCalls = 0;
        performanceMonitor->getCurrentFrameTiming().vertexCount = 0;
    }
    
    // Render raycast visualization if enabled
    if (raycastVisualizationEnabled && raycastVisualizer) {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Debug Lines");
        // Bind debug line pipeline
        renderPipeline->bindDebugLinePipeline(vulkanDevice->getCommandBuffer(currentFrame));
        
        // Bind descriptor sets for view/projection matrices
        vulkanDevice->bindDescriptorSets(currentFrame, renderPipeline->getGraphicsLayout());
        
        // Render raycast debug geometry
        raycastVisualizer->render(vulkanDevice->getCommandBuffer(currentFrame), currentFrame);
    }

    // Render Debris
    if (debrisPipeline && chunkManager) {
        auto* debrisSystem = chunkManager->m_dynamicObjectManager.getDebrisSystem();
        if (debrisSystem && debrisSystem->getActiveParticleCount() > 0) {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Debris");
            debrisPipeline->render(
                vulkanDevice->getCommandBuffer(currentFrame), 
                *camera, 
                cachedProjectionMatrix,
                debrisSystem->getParticles(), 
                debrisSystem->getActiveParticleCount()
            );
        }
    }
    
    // End Scene Render Pass
    } // End Scene Pass Scope
    postProcessor->endSceneRenderPass(vulkanDevice->getCommandBuffer(currentFrame));

    // Begin Post Process Render Pass (Swapchain)
    postProcessor->beginPostProcessRenderPass(vulkanDevice->getCommandBuffer(currentFrame), vulkanDevice->getSwapChainFramebuffer(imageIndex));

    {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Post Process");
        // Draw Fullscreen Quad
        postProcessor->drawQuad(vulkanDevice->getCommandBuffer(currentFrame));
    }

    // Render ImGui on top
    // Scripting console rendering is handled in Application::run() before endFrame()
    // Lighting controls rendering is handled in Application::run() before endFrame()
    imguiRenderer->render(currentFrame, imageIndex);
    
    // End Post Process Render Pass
    postProcessor->endPostProcessRenderPass(vulkanDevice->getCommandBuffer(currentFrame));
    vulkanDevice->endCommandBuffer(currentFrame);
    auto recordEnd = std::chrono::high_resolution_clock::now();

    // Submit command buffer
    auto submitStart = std::chrono::high_resolution_clock::now();
    if (!vulkanDevice->submitCommandBuffer(currentFrame)) {
        LOG_ERROR("RenderCoordinator", "Failed to submit command buffer!");
        return;
    }
    auto submitEnd = std::chrono::high_resolution_clock::now();

    // Present frame
    auto presentStart = std::chrono::high_resolution_clock::now();
    VkResult presentResult = vulkanDevice->presentFrame(imageIndex, currentFrame);
    m_lastImageIndex = imageIndex;  // Track for screenshot capture
    
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || vulkanDevice->getFramebufferResized()) {
        // Recreate swapchain on next frame
        vulkanDevice->setFramebufferResized(true);
    } else if (presentResult != VK_SUCCESS) {
        LOG_ERROR("RenderCoordinator", "Failed to present frame!");
        return;
    }
    auto presentEnd = std::chrono::high_resolution_clock::now();

    currentFrame = (currentFrame + 1) % 2; // MAX_FRAMES_IN_FLIGHT = 2
    
    // Note: frameTiming statistics are now set in the chunk rendering section above
    // This includes: drawCalls, vertexCount, visibleInstances, culledInstances, etc.
    
    // Use GPU culling results if available for frustum culling statistics
    if (lastVisibleInstances + lastCulledInstances > 0) {
        performanceMonitor->getCurrentFrameTiming().frustumCulledInstances = static_cast<int>(lastCulledInstances);
    } else {
        performanceMonitor->getCurrentFrameTiming().frustumCulledInstances = 0;
    }
    
    // Record detailed timing
    auto frameEnd = std::chrono::high_resolution_clock::now();
    DetailedFrameTiming detailedTiming;
    detailedTiming.totalFrameTime = std::chrono::duration<double, std::milli>(frameEnd - frameStartTime).count();
    detailedTiming.physicsTime = 0.0; // Physics timing integrated into main loop
    detailedTiming.mousePickTime = 0.0; // Not implemented yet
    detailedTiming.uboFillTime = std::chrono::duration<double, std::milli>(uboEnd - uboStart).count();
    detailedTiming.instanceUpdateTime = 0.0; // ChunkManager handles its own data
    detailedTiming.drawCmdUpdateTime = 0.0; // Not separate in our implementation
    detailedTiming.uniformUploadTime = std::chrono::duration<double, std::milli>(uniformUploadEnd - uniformUploadStart).count();
    detailedTiming.occlusionCullingTime = 0.0; // Occlusion culling is done once at scene creation, not per-frame
    detailedTiming.commandRecordTime = std::chrono::duration<double, std::milli>(recordEnd - recordStart).count();
    detailedTiming.gpuSubmitTime = std::chrono::duration<double, std::milli>(submitEnd - submitStart).count();
    detailedTiming.presentTime = std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
    
    performanceMonitor->addDetailedTiming(detailedTiming);
}

void RenderCoordinator::renderUI() {
    if (imguiRenderer) {
        if (showLightingControls) {
            LOG_DEBUG("RenderCoordinator", "Rendering Lighting Controls UI");
        }
        imguiRenderer->renderLightingControls(
            showLightingControls,
            sunDirection,
            sunColor,
            ambientLightStrength,
            emissiveMultiplier
        );

        imguiRenderer->renderProfilerWindow(
            showProfiler,
            performanceProfiler,
            gpuProfiler.get()
        );
    }
}

void RenderCoordinator::renderEntities(VkCommandBuffer commandBuffer) {
    if (!entities || entities->empty()) return;

    // Separate entities into instanced and standard
    std::vector<Scene::AnimatedVoxelCharacter*> instancedCharacters;
    std::vector<Scene::Entity*> standardEntities;

    for (const auto& entity : *entities) {
        auto animatedChar = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity.get());
        if (animatedChar) {
            instancedCharacters.push_back(animatedChar);
        } else {
            standardEntities.push_back(entity.get());
        }
    }

    // Render Instanced Characters
    if (!instancedCharacters.empty()) {
        std::vector<CharacterInstanceData> instanceData;
        struct Batch {
            glm::mat4 model;
            uint32_t firstInstance;
            uint32_t instanceCount;
        };
        std::vector<Batch> batches;

        for (auto* charPtr : instancedCharacters) {
            // Group parts by RigidBody
            std::map<btRigidBody*, std::vector<const Scene::RagdollPart*>> partsByBody;
            for (const auto& part : charPtr->getParts()) {
                if (part.rigidBody) {
                    partsByBody[part.rigidBody].push_back(&part);
                }
            }

            // Create batches
            for (const auto& [body, parts] : partsByBody) {
                // Calculate Body Transform
                btTransform trans;
                body->getMotionState()->getWorldTransform(trans);
                btVector3 pos = trans.getOrigin();
                btQuaternion rot = trans.getRotation();
                
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, glm::vec3(pos.x(), pos.y(), pos.z()));
                model = model * glm::mat4_cast(glm::quat(rot.w(), rot.x(), rot.y(), rot.z()));

                Batch batch;
                batch.model = model;
                batch.firstInstance = static_cast<uint32_t>(instanceData.size());
                batch.instanceCount = 0;

                for (const auto* part : parts) {
                    CharacterInstanceData data;
                    data.offset = part->offset;
                    data.scale = part->scale;
                    data.color = part->color;
                    instanceData.push_back(data);
                    batch.instanceCount++;
                }
                batches.push_back(batch);
            }
        }

        if (!instanceData.empty()) {
            vulkanDevice->updateCharacterInstanceBuffer(instanceData);
            renderPipeline->bindInstancedCharacterPipeline(commandBuffer);
            vulkanDevice->bindCharacterInstanceBuffer(commandBuffer);

            glm::mat4 viewProj = cachedProjectionMatrix * cachedViewMatrix;

            for (const auto& batch : batches) {
                struct PushConsts {
                    glm::mat4 model;
                    glm::mat4 viewProj;
                } pushConsts;
                pushConsts.model = batch.model;
                pushConsts.viewProj = viewProj;

                vkCmdPushConstants(commandBuffer, renderPipeline->getInstancedCharacterLayout(), 
                                 VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConsts), &pushConsts);
                
                // Draw 36 vertices (cube) * instanceCount
                vkCmdDraw(commandBuffer, 36, batch.instanceCount, 0, batch.firstInstance);
            }
        }
    }

    renderPipeline->bindCharacterPipeline(commandBuffer);

    for (const auto& entity : standardEntities) {
        // Check for RagdollCharacter (handles both PhysicsCharacter and SpiderCharacter)
        auto ragdollChar = dynamic_cast<Scene::RagdollCharacter*>(entity);
        if (ragdollChar) {
            // Allow character to do its own debug rendering (e.g. raycast lines)
            ragdollChar->render(this);

            const auto& parts = ragdollChar->getParts();
            for (const auto& part : parts) {
                if (!part.rigidBody) continue;

                btTransform trans;
                part.rigidBody->getMotionState()->getWorldTransform(trans);
                
                // Convert Bullet transform to GLM
                btVector3 pos = trans.getOrigin();
                btQuaternion rot = trans.getRotation();
                
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, glm::vec3(pos.x(), pos.y(), pos.z()));
                model = model * glm::mat4_cast(glm::quat(rot.w(), rot.x(), rot.y(), rot.z()));
                
                // Apply local offset if present
                if (part.offset != glm::vec3(0.0f)) {
                    model = glm::translate(model, part.offset);
                }
                
                model = glm::scale(model, part.scale);

                glm::mat4 viewProj = cachedProjectionMatrix * cachedViewMatrix;

                struct PushConsts {
                    glm::mat4 model;
                    glm::mat4 viewProj;
                    glm::vec4 color;
                } pushConsts;
                
                pushConsts.model = model;
                pushConsts.viewProj = viewProj;
                pushConsts.color = part.color;
                
                vkCmdPushConstants(commandBuffer, renderPipeline->getCharacterLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConsts), &pushConsts);
                vkCmdDraw(commandBuffer, 36, 1, 0, 0);
            }
            continue; // Done with this entity
        }

        // Fallback to old Character system
        auto character = dynamic_cast<Scene::Character*>(entity);
        if (character) {
            glm::mat4 model = character->getModelMatrix();
            glm::mat4 viewProj = cachedProjectionMatrix * cachedViewMatrix;
            
            glm::vec4 color = character->debugColor;

            struct PushConsts {
                glm::mat4 model;
                glm::mat4 viewProj;
                glm::vec4 color;
            } pushConsts;
            
            pushConsts.model = model;
            pushConsts.viewProj = viewProj;
            pushConsts.color = color;
            
            vkCmdPushConstants(commandBuffer, renderPipeline->getCharacterLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConsts), &pushConsts);
            
            // Draw cube (36 vertices generated in shader)
            vkCmdDraw(commandBuffer, 36, 1, 0, 0);
        }
    }
}

// ============================================================================
// Screenshot Capture
// ============================================================================

std::vector<uint8_t> RenderCoordinator::captureScreenshot() {
    VkDevice device = vulkanDevice->getDevice();
    VkExtent2D extent = vulkanDevice->getSwapChainExtent();
    uint32_t width = extent.width;
    uint32_t height = extent.height;
    VkFormat format = vulkanDevice->getSwapChainImageFormat();
    VkImage srcImage = vulkanDevice->getSwapChainImage(m_lastImageIndex);

    // Ensure all GPU work is done before touching the swapchain image
    vkDeviceWaitIdle(device);

    // Create a host-visible staging buffer to copy the image into
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width) * height * 4;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    vulkanDevice->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingMemory
    );

    // Use single-time command buffer for the copy
    VkCommandBuffer cmd = vulkanDevice->beginSingleTimeCommands();

    // Transition swapchain image: PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier toTransferSrc{};
    toTransferSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransferSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransferSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferSrc.image = srcImage;
    toTransferSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransferSrc.subresourceRange.baseMipLevel = 0;
    toTransferSrc.subresourceRange.levelCount = 1;
    toTransferSrc.subresourceRange.baseArrayLayer = 0;
    toTransferSrc.subresourceRange.layerCount = 1;
    toTransferSrc.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    toTransferSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toTransferSrc
    );

    // Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;   // tightly packed
    region.bufferImageHeight = 0; // tightly packed
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    // Transition swapchain image back: TRANSFER_SRC_OPTIMAL → PRESENT_SRC_KHR
    VkImageMemoryBarrier toPresent{};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = srcImage;
    toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toPresent.subresourceRange.baseMipLevel = 0;
    toPresent.subresourceRange.levelCount = 1;
    toPresent.subresourceRange.baseArrayLayer = 0;
    toPresent.subresourceRange.layerCount = 1;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toPresent
    );

    // Submit and wait (endSingleTimeCommands does vkQueueWaitIdle)
    vulkanDevice->endSingleTimeCommands(cmd);

    // Map staging buffer and read pixel data
    void* data = nullptr;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);

    // Convert BGRA → RGBA (swapchain uses VK_FORMAT_B8G8R8A8_SRGB)
    std::vector<uint8_t> pixels(width * height * 4);
    const uint8_t* src = static_cast<const uint8_t*>(data);

    bool isBGRA = (format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM);
    if (isBGRA) {
        for (uint32_t i = 0; i < width * height; ++i) {
            pixels[i * 4 + 0] = src[i * 4 + 2]; // R ← B
            pixels[i * 4 + 1] = src[i * 4 + 1]; // G ← G
            pixels[i * 4 + 2] = src[i * 4 + 0]; // B ← R
            pixels[i * 4 + 3] = src[i * 4 + 3]; // A ← A
        }
    } else {
        std::memcpy(pixels.data(), src, pixels.size());
    }

    vkUnmapMemory(device, stagingMemory);

    // Clean up staging resources
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    LOG_INFO("RenderCoordinator", "Screenshot captured: {}x{} pixels", width, height);
    return pixels;
}

} // namespace Graphics
} // namespace Phyxel
