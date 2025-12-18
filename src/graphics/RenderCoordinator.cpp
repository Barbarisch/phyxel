#include "graphics/RenderCoordinator.h"
#include "graphics/RaycastVisualizer.h"
#include "graphics/ShadowMap.h"
#include "graphics/PostProcessor.h"
#include "graphics/PostProcessor.h"
#include "graphics/Camera.h"
#include "vulkan/RenderPipeline.h"
#include "ui/ImGuiRenderer.h"
#include "vulkan/VulkanDevice.h"
#include "ui/WindowManager.h"
#include "input/InputManager.h"
#include "core/ChunkManager.h"
#include "utils/CoordinateUtils.h"
#include "utils/Frustum.h"
#include "utils/Logger.h"
#include "scene/Character.h"
#include "scene/Player.h"
#include "scene/Enemy.h"
#include "scene/PhysicsCharacter.h"
#include <glm/gtc/matrix_transform.hpp>

namespace VulkanCube {
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

    dynamicRenderPipeline->setRenderPass(postProcessor->getSceneRenderPass());
    dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes();

    // Recreate Swapchain Framebuffers using PostProcess Render Pass
    // The swapchain framebuffers now need to be compatible with the post-process render pass
    // which outputs to the swapchain surface
    vulkanDevice->createFramebuffers(postProcessor->getPostProcessRenderPass());

    // Re-initialize ImGuiRenderer with the correct render pass (Swapchain Pass)
    // It was initialized with the old render pass in WorldInitializer
    imguiRenderer->cleanup();
    imguiRenderer->initialize(windowManager->getHandle(), vulkanDevice, postProcessor->getPostProcessRenderPass());
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
        std::vector<size_t> visibleChunkIndices;
        
        for (size_t i = 0; i < chunkManager->chunks.size(); ++i) {
            const Chunk* chunk = chunkManager->chunks[i].get();
            
            // Skip chunks with no static faces (already optimized)
            if (chunk->getNumInstances() == 0) continue;
            
            // Get chunk bounding box
            glm::vec3 minBounds = chunk->getMinBounds();
            glm::vec3 maxBounds = chunk->getMaxBounds();
            glm::vec3 chunkCenter = (minBounds + maxBounds) * 0.5f;
            
            // LEVEL 1: Chunk inclusion distance culling (broader range for chunk loading)
            float distanceToCamera = glm::length(chunkCenter - camera->getPosition());
            if (distanceToCamera > chunkInclusionDistance) {
                continue; // Skip chunk - too far away even for loading
            }
            
            // LEVEL 2: Frustum culling (uses actual render distance)
            // Create AABB for frustum testing
            Utils::AABB chunkAABB(minBounds, maxBounds);
            
            // Extract frustum from current view/projection matrices (uses maxChunkRenderDistance)
            glm::vec3 cameraPos = camera->getPosition();
            glm::vec3 cameraFront = camera->getFront();
            glm::vec3 cameraUp = camera->getUp();
            
            glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
            glm::mat4 proj = cachedProjectionMatrix;
            glm::mat4 viewProjection = proj * view;
            
            Utils::Frustum cameraFrustum;
            cameraFrustum.extractFromMatrix(viewProjection);
            
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
    
    // Render Shadow Pass
    renderShadowPass(vulkanDevice->getCommandBuffer(currentFrame), lightSpaceMatrix);
    
    // Record occlusion culling statistics from chunk manager
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Get occlusion stats from chunk manager
        auto chunkStats = chunkManager->getPerformanceStats();
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
        size_t actuallyRenderedChunks = renderStaticGeometry();
        
        // Render dynamic subcubes with separate pipeline
        renderDynamicSubcubes();

        // Render entities (Characters)
        renderEntities(vulkanDevice->getCommandBuffer(currentFrame));
        
        // Get accurate performance statistics from chunk manager
        auto chunkStats = chunkManager->getPerformanceStats();
        
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
        // Bind debug line pipeline
        renderPipeline->bindDebugLinePipeline(vulkanDevice->getCommandBuffer(currentFrame));
        
        // Bind descriptor sets for view/projection matrices
        vulkanDevice->bindDescriptorSets(currentFrame, renderPipeline->getGraphicsLayout());
        
        // Render raycast debug geometry
        raycastVisualizer->render(vulkanDevice->getCommandBuffer(currentFrame), currentFrame);
    }
    
    // End Scene Render Pass
    postProcessor->endSceneRenderPass(vulkanDevice->getCommandBuffer(currentFrame));

    // Begin Post Process Render Pass (Swapchain)
    postProcessor->beginPostProcessRenderPass(vulkanDevice->getCommandBuffer(currentFrame), vulkanDevice->getSwapChainFramebuffer(imageIndex));

    // Draw Fullscreen Quad
    postProcessor->drawQuad(vulkanDevice->getCommandBuffer(currentFrame));

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
    }
}

void RenderCoordinator::renderEntities(VkCommandBuffer commandBuffer) {
    if (!entities || entities->empty()) return;

    renderPipeline->bindCharacterPipeline(commandBuffer);

    for (const auto& entity : *entities) {
        // Check for PhysicsCharacter first (new system)
        auto physicsChar = dynamic_cast<Scene::PhysicsCharacter*>(entity.get());
        if (physicsChar) {
            const auto& parts = physicsChar->getParts();
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
        auto character = dynamic_cast<Scene::Character*>(entity.get());
        if (character) {
            glm::mat4 model = character->getModelMatrix();
            glm::mat4 viewProj = cachedProjectionMatrix * cachedViewMatrix;
            
            glm::vec4 color(1.0f); // Default white
            if (dynamic_cast<Scene::Player*>(character)) {
                color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f); // Green for Player
            } else if (dynamic_cast<Scene::Enemy*>(character)) {
                color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // Red for Enemy
            }

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

} // namespace Graphics
} // namespace VulkanCube
