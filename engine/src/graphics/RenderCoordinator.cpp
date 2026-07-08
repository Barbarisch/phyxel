#include "graphics/RenderCoordinator.h"
#include "core/GpuParticlePhysics.h"
#include "graphics/LightManager.h"
#include "graphics/RaycastVisualizer.h"
#include "graphics/ShadowMap.h"
#include "graphics/PostProcessor.h"
#include "graphics/Camera.h"
#include "graphics/DebrisRenderPipeline.h"
#include "graphics/VfxRenderPipeline.h"
#include "graphics/WaterRenderPipeline.h"
#include "graphics/WaterCellRenderPipeline.h"
#include "core/WaterManager.h"
#include "core/VfxSystem.h"
#include "core/VfxDirector.h"
#include "graphics/KinematicVoxelPipeline.h"
#include "graphics/GrassRenderPipeline.h"
#include "graphics/FarTerrainRenderPipeline.h"
#include "graphics/FarTerrainManager.h"
#include "graphics/FoliageRenderPipeline.h"
#include "core/KinematicVoxelManager.h"
#include "vulkan/RenderPipeline.h"
#include "ui/ImGuiRenderer.h"
#include "ui/UISystem.h"
#include "ui/MenuDefinition.h"
#include "vulkan/VulkanDevice.h"
#include "ui/WindowManager.h"
#include "input/InputManager.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "graphics/FireEmitterManager.h"
#include "core/MaterialRegistry.h"
#include "core/Cube.h"
#include "utils/CoordinateUtils.h"
#include "utils/Frustum.h"
#include "utils/Logger.h"
#include "utils/GpuProfiler.h"
#include "scene/Entity.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdlib>
#include "scene/RagdollCharacter.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/NPCEntity.h"
#include "core/NPCManager.h"
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
    // Debug knob: PHYXEL_OCCLUSION=1 enables the experimental chunk occlusion
    // culling at startup (default OFF). Lets it be toggled without an API yet.
    if (const char* e = std::getenv("PHYXEL_OCCLUSION"))
        m_occlusionCullingEnabled = (e[0] == '1');

    // 4096² shadow map: with the view-frustum-fit covering a large shadow distance (see drawFrame),
    // the higher resolution keeps shadows crisp over that bigger area.
    // D1b MEASURED (docs/evidence/renderdensity_baseline.txt): 2048² only cut the shadow pass ~30%
    // (28→20 ms), NOT ~4× — so it is only partially fill-bound. The ~20 ms floor is not primitives
    // (D1 quad: no effect) nor mostly texels → suspected draw-call/per-chunk-bind bound (138 draws vs
    // 20 visible). The real lever is culling (D1c), not lower resolution (which also costs quality).
    // Kept at 4096² (2048² reverted — modest gain, quality loss, wrong lever).
    shadowMap = std::make_unique<ShadowMap>(vulkanDevice, 4096, 4096);
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
    // OIT transparent pass pipeline
    renderPipeline->createOITPipeline(postProcessor->getOITRenderPass());

    // Mirror reflective surface pipeline (uses scene render pass, separate descriptor for reflection texture)
    renderPipeline->createMirrorPipeline(postProcessor->getSceneRenderPass());
    renderPipeline->createReflectionScenePipeline(postProcessor->getSceneRenderPass());
    renderPipeline->updateMirrorReflectionDescriptor(
        postProcessor->getReflectionImageView(), postProcessor->getReflectionSampler());

    // Reflection UBO buffers (must be created after texture atlas + shadow map are bound to main descriptor sets)
    vulkanDevice->createReflectionBuffers();

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
    // Phase 4c: break-debris samples the baked light field (darkens indoors, lit by glow).
    debrisPipeline->setLightSampler([cm = chunkManager](const glm::vec3& wp) -> glm::vec4 {
        if (!cm) return glm::vec4(1.0f);
        auto bl = cm->sampleBakedLight(glm::ivec3(glm::floor(wp)));
        return glm::vec4(bl.sky, bl.r, bl.g, bl.b) / 15.0f;
    });

    // Initialize VFX particle system + its additive instanced-cube renderer.
    vfxSystem = std::make_unique<VfxSystem>();
    // Let projectiles carry transient point lights via the LightManager.
    vfxSystem->setLightCallbacks(
        [this](const glm::vec3& pos, const glm::vec3& color, float intensity, float radius) {
            return lightManager.addPointLight(pos, color, intensity, radius);
        },
        [this](int id, const glm::vec3& pos) { lightManager.updatePointLightPosition(id, pos); },
        [this](int id) { lightManager.removeLight(id); },
        // Intensity fade for transient explosion flashes (burst lights).
        [this](int id, float intensity) {
            if (const auto* pl = lightManager.getPointLight(id)) {
                auto upd = *pl;
                upd.intensity = intensity;
                lightManager.updatePointLight(id, upd);
            }
        });
    vfxDirector = std::make_unique<VfxDirector>(vfxSystem.get());
    // Continuous flame VFX manager: spawns a flame tongue per flaming ember voxel.
    fireEmitters = std::make_unique<FireEmitterManager>();
    fireEmitters->setVfx(vfxSystem.get());
    vfxPipeline = std::make_unique<VfxRenderPipeline>();
    vfxPipeline->initialize(
        vulkanDevice->getDevice(),
        vulkanDevice->getPhysicalDevice(),
        postProcessor->getSceneRenderPass(),
        vulkanDevice->getSwapChainExtent()
    );

    // Initialize Water surface pipeline (see docs/WaterSystem.md).
    waterPipeline = std::make_unique<WaterRenderPipeline>();
    waterPipeline->initialize(
        vulkanDevice->getDevice(),
        vulkanDevice->getPhysicalDevice(),
        postProcessor->getSceneRenderPass(),
        vulkanDevice->getSwapChainExtent()
    );
    // Phase 1: water samples the shared planar-reflection texture.
    waterPipeline->setReflectionTexture(
        postProcessor->getReflectionImageView(), postProcessor->getReflectionSampler());

    // Per-cell water surface pipeline (renders the CPU sim's actual field — Phase 2).
    waterCellPipeline = std::make_unique<WaterCellRenderPipeline>();
    waterCellPipeline->initialize(
        vulkanDevice->getDevice(),
        vulkanDevice->getPhysicalDevice(),
        postProcessor->getSceneRenderPass(),
        vulkanDevice->getSwapChainExtent()
    );

    // Initialize Kinematic Voxel Pipeline (doors, rotating platforms, etc.)
    kinematicPipeline = std::make_unique<KinematicVoxelPipeline>();
    if (!kinematicPipeline->initialize(
            vulkanDevice->getDevice(),
            vulkanDevice->getPhysicalDevice(),
            postProcessor->getSceneRenderPass(),
            vulkanDevice->getSwapChainExtent(),
            vulkanDevice->getDescriptorSetLayout(),
            vulkanDevice->getDescriptorSet(0))) {
        LOG_ERROR("RenderCoordinator", "Failed to initialize KinematicVoxelPipeline");
        kinematicPipeline.reset();
    }
    // Phase 4: let furniture/doors sample the baked light field so they darken indoors
    // and pick up glow/spell light (matches the static world + characters).
    if (kinematicPipeline) {
        kinematicPipeline->setLightSampler([cm = chunkManager](const glm::vec3& wp) -> glm::vec4 {
            if (!cm) return glm::vec4(1.0f);
            auto bl = cm->sampleBakedLight(glm::ivec3(glm::floor(wp)));
            return glm::vec4(bl.sky, bl.r, bl.g, bl.b) / 15.0f;
        });
    }

    // Lightweight grass-blade layer (distance-limited cutout; renders in the opaque scene pass).
    grassPipeline = std::make_unique<GrassRenderPipeline>();
    if (!grassPipeline->initialize(
            vulkanDevice->getDevice(),
            vulkanDevice->getPhysicalDevice(),
            postProcessor->getSceneRenderPass(),
            vulkanDevice->getSwapChainExtent(),
            vulkanDevice->getDescriptorSetLayout())) {
        LOG_ERROR("RenderCoordinator", "Failed to initialize GrassRenderPipeline");
        grassPipeline.reset();
    }

    // Leaf foliage card layer (cutout leaf cards replacing solid leaf voxels; opaque scene pass).
    foliagePipeline = std::make_unique<FoliageRenderPipeline>();
    if (!foliagePipeline->initialize(
            vulkanDevice->getDevice(),
            vulkanDevice->getPhysicalDevice(),
            postProcessor->getSceneRenderPass(),
            vulkanDevice->getSwapChainExtent(),
            vulkanDevice->getDescriptorSetLayout())) {
        LOG_ERROR("RenderCoordinator", "Failed to initialize FoliageRenderPipeline");
        foliagePipeline.reset();
    } else if (shadowMap) {
        // Shadow-caster variant: canopies cast dappled cutout shadows (previously leaf voxels
        // cast NONE — the mesher skips their solid faces, so only trunks shadowed the ground).
        foliagePipeline->initializeShadow(
            shadowMap->getRenderPass(),
            VkExtent2D{shadowMap->getWidth(), shadowMap->getHeight()});
    }

    // Far-terrain LOD tiles (blocky heightmap columns beyond the real-chunk radius).
    farTerrainPipeline = std::make_unique<FarTerrainRenderPipeline>();
    if (!farTerrainPipeline->initialize(
            vulkanDevice->getDevice(),
            vulkanDevice->getPhysicalDevice(),
            postProcessor->getSceneRenderPass(),
            vulkanDevice->getSwapChainExtent(),
            vulkanDevice->getDescriptorSetLayout())) {
        LOG_ERROR("RenderCoordinator", "Failed to initialize FarTerrainRenderPipeline");
        farTerrainPipeline.reset();
    }
    farTerrainManager = std::make_unique<FarTerrainManager>(
        vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice());
}

RenderCoordinator::~RenderCoordinator() = default;

bool RenderCoordinator::initUISystem() {
    if (!postProcessor || !vulkanDevice || !windowManager) return false;

    m_uiSystem = std::make_unique<UI::UISystem>(
        vulkanDevice, windowManager->getWidth(), windowManager->getHeight());

    // Init against the SCENE render pass (offscreen HDR target), NOT the swapchain
    // post-process pass. The game HUD must render into the offscreen image so it is
    // (a) visible in the editor viewport — which samples the offscreen image — and
    // (b) carried to the swapchain by post-process for standalone builds. This keeps
    // the game HUD entirely off ImGui (see docs/HudSystem.md §2a, §5).
    if (!m_uiSystem->initialize(postProcessor->getSceneRenderPass())) {
        LOG_ERROR("RenderCoordinator", "Failed to initialize UISystem");
        m_uiSystem.reset();
        return false;
    }
    LOG_INFO("RenderCoordinator", "UISystem initialized successfully");
    return true;
}

void RenderCoordinator::render() {
    drawFrame();
}

void RenderCoordinator::updateVfx(float dt) {
    // Director first: it drains last frame's events and fires new emissions
    // (spawning into vfxSystem), which the integrate pass below then ticks.
    if (vfxDirector) vfxDirector->update(dt);

    // Drive continuous flame VFX from the world's state=flaming voxels: gather
    // every flaming ember position from the loaded chunks (each chunk caches its
    // own list at bake time) and let the fire manager reconcile flame tongues.
    // sync() only spawns/dismisses on change, so a steady fire costs ~nothing.
    if (fireEmitters && chunkManager) {
        std::vector<glm::vec3> flaming;
        for (const auto& ch : chunkManager->chunks) {
            if (!ch) continue;
            const auto& fv = ch->getFlamingVoxels();
            flaming.insert(flaming.end(), fv.begin(), fv.end());
        }
        fireEmitters->sync(flaming);
    }

    if (vfxSystem) vfxSystem->update(dt);
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

        // LEVEL 2.5: Occlusion culling (chunk visibility graph). Flag-gated, OFF by
        // default — removes frustum-visible chunks that are hidden behind solid chunks.
        if (m_occlusionCullingEnabled) {
            applyOcclusionCulling(cameraPos);
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
            vulkanDevice->drawIndexed(currentFrame, vulkanDevice->chunkIndexCount(), chunk->getNumInstances());
            renderedChunks++;
        }
    }
    
    return renderedChunks;
}

void RenderCoordinator::renderGrass() {
    // Grass rides on the chunks that just survived static-geometry culling (visibleChunkIndices is
    // populated by renderStaticGeometry, which runs immediately before this). We additionally clip
    // to the grass radius so far terrain stays bare — the hard cost bound. No per-frame CPU beyond
    // assembling the small draw list; blades are GPU-expanded.
    if (!grassPipeline || !grassPipeline->params().enabled || !chunkManager) return;
    if (visibleChunkIndices.empty()) return;

    const glm::vec3 cameraPos = camera->getPosition();
    // Cull whole chunks by center distance, padded by a chunk half-diagonal (~sqrt(3)*16) so a
    // chunk with grass near its edge inside the radius isn't dropped; the per-blade height fade in
    // the shader does the smooth cutoff at the true radius.
    const float radius = grassPipeline->params().radius + 27.8f;
    const float radiusSq = radius * radius;

    std::vector<GrassRenderPipeline::ChunkDraw> draws;
    draws.reserve(visibleChunkIndices.size());
    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (!chunk || chunk->getGrassCount() == 0) continue;

        // Clip by chunk-center distance (blades themselves fade to zero height near the edge).
        glm::vec3 center = (chunk->getMinBounds() + chunk->getMaxBounds()) * 0.5f;
        if (glm::dot(center - cameraPos, center - cameraPos) > radiusSq) continue;

        glm::ivec3 origin = chunk->getWorldOrigin();
        draws.push_back({ chunk->getGrassBuffer(), chunk->getGrassCount(),
                          glm::vec3(origin.x, origin.y, origin.z) });
    }
    if (draws.empty()) return;

    grassPipeline->render(vulkanDevice->getCommandBuffer(currentFrame),
                          vulkanDevice->getDescriptorSet(currentFrame), draws);
}

void RenderCoordinator::renderFarTerrain() {
    // Far-terrain LOD tiles. Drawn AFTER static geometry so near chunks fill depth
    // first and far-tile pixels behind them are z-rejected. Excluded from the shadow
    // pass by construction. Tiles are frustum-culled by their AABB here.
    lastFrameStats.farTilesDrawn    = 0;
    lastFrameStats.farTriangles     = 0;
    if (!farTerrainPipeline || !farTerrainManager) {
        lastFrameStats.farTilesResident = 0;
        return;
    }

    // Auto-configure from the world's streaming generator the first time far terrain
    // is enabled on a procedural world (game.json wiring lands with the config phase).
    if (farTerrainManager->params().enabled && !farTerrainManager->isConfigured() &&
        chunkManager && chunkManager->getStreamingGenerator()) {
        farTerrainManager->setChunkCoverageFn(
            [cm = chunkManager](const glm::ivec2& minXZ, const glm::ivec2& maxXZ) {
                // True iff EVERY 32x32 column in the rect has a loaded chunk (any Y).
                const int cx0 = int(std::floor(minXZ.x / 32.0f));
                const int cx1 = int(std::floor((maxXZ.x - 1) / 32.0f));
                const int cz0 = int(std::floor(minXZ.y / 32.0f));
                const int cz1 = int(std::floor((maxXZ.y - 1) / 32.0f));
                for (int cz = cz0; cz <= cz1; ++cz) {
                    for (int cx = cx0; cx <= cx1; ++cx) {
                        bool any = false;
                        for (int cy = -4; cy <= 8 && !any; ++cy) {
                            if (cm->getChunkAtCoord(glm::ivec3(cx, cy, cz))) any = true;
                        }
                        if (!any) return false;
                    }
                }
                return true;
            });
        farTerrainManager->configure(*chunkManager->getStreamingGenerator());
    }

    // Per-frame lifecycle: refresh wanted set on camera movement, drain worker
    // results (budgeted uploads), evict + frame-deferred-delete out-of-range tiles.
    farTerrainManager->update(camera->getPosition());

    lastFrameStats.farTilesResident = int(farTerrainManager->residentTiles());
    if (!farTerrainManager->params().enabled) return;
    const auto& tiles = farTerrainManager->tileDraws();
    if (tiles.empty()) return;

    const glm::vec3 cameraPos = camera->getPosition();
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + camera->getFront(), camera->getUp());
    Utils::Frustum frustum;
    frustum.extractFromMatrix(cachedProjectionMatrix * view);

    std::vector<FarTerrainRenderPipeline::TileDraw> draws;
    draws.reserve(tiles.size());
    for (const auto& t : tiles) {
        Utils::AABB aabb(t.aabbMin, t.aabbMax);
        if (!frustum.intersects(aabb)) continue;
        draws.push_back(t.draw);
        lastFrameStats.farTriangles += int(t.draw.indexCount / 3);
    }
    if (draws.empty()) return;
    lastFrameStats.farTilesDrawn = int(draws.size());

    farTerrainPipeline->render(vulkanDevice->getCommandBuffer(currentFrame),
                               vulkanDevice->getDescriptorSet(currentFrame), draws);
}

void RenderCoordinator::renderFoliage() {
    // Leaf cards ride on the chunks that survived static-geometry culling (visibleChunkIndices).
    // Unlike grass there's no radius fade by default — trees keep their leaves at any distance; only
    // frustum culling applies. Assembling the small draw list is the only per-frame CPU work.
    if (!foliagePipeline || !foliagePipeline->params().enabled || !chunkManager) return;
    if (visibleChunkIndices.empty()) return;

    std::vector<FoliageRenderPipeline::ChunkDraw> draws;
    draws.reserve(visibleChunkIndices.size());
    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (!chunk || chunk->getFoliageCount() == 0) continue;
        glm::ivec3 origin = chunk->getWorldOrigin();
        draws.push_back({ chunk->getFoliageBuffer(), chunk->getFoliageCount(),
                          glm::vec3(origin.x, origin.y, origin.z) });
    }
    if (draws.empty()) return;

    foliagePipeline->render(vulkanDevice->getCommandBuffer(currentFrame),
                            vulkanDevice->getDescriptorSet(currentFrame), draws);
}

void RenderCoordinator::setFoliageEnabled(bool on) {
    if (foliagePipeline) foliagePipeline->params().enabled = on;
}

void RenderCoordinator::setFoliageParams(float cardSize, float windStrength, int cardsPerVoxel, float radius) {
    if (!foliagePipeline) return;
    auto& p = foliagePipeline->params();
    if (cardSize     >= 0.0f) p.cardSize      = cardSize;
    if (windStrength >= 0.0f) p.windStrength  = windStrength;
    if (cardsPerVoxel > 0)    p.cardsPerVoxel = static_cast<uint32_t>(cardsPerVoxel);
    if (radius       >= 0.0f) p.radius        = radius;
}

bool RenderCoordinator::isFoliageEnabled() const {
    return foliagePipeline && foliagePipeline->params().enabled;
}

void RenderCoordinator::setGrassEnabled(bool on) {
    if (grassPipeline) grassPipeline->params().enabled = on;
}

void RenderCoordinator::setGrassParams(float radius, float bladeHeight, float windStrength, int bladesPerVoxel) {
    if (!grassPipeline) return;
    auto& p = grassPipeline->params();
    if (radius       >= 0.0f) p.radius        = radius;
    if (bladeHeight  >= 0.0f) p.bladeHeight   = bladeHeight;
    if (windStrength >= 0.0f) p.windStrength  = windStrength;
    if (bladesPerVoxel > 0)   p.bladesPerVoxel = static_cast<uint32_t>(bladesPerVoxel);
}

bool RenderCoordinator::isGrassEnabled() const {
    return grassPipeline && grassPipeline->params().enabled;
}

void RenderCoordinator::renderTransparentGeometryOIT(uint32_t frameIndex) {
    // Re-renders visible chunks using the OIT pipeline (transparent fragments only).
    // The transparent_voxel.frag shader discards non-transparent fragments,
    // so opaque geometry is skipped automatically.
    if (!chunkManager || visibleChunkIndices.empty()) return;

    // Skip the expensive transparent geometry submission when no visible chunk
    // actually contains a transparent voxel (cached per-chunk flag, refreshed on
    // rebuildFaces). begin/endOITRenderPass in drawFrame still clear the OIT targets,
    // so the post-process composite stays correct. This avoids re-submitting every
    // visible face every frame for nothing — the common case (no glass in view).
    bool anyVisibleTransparent = false;
    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (chunk && chunk->getNumInstances() > 0 && chunk->hasTransparentVoxel()) {
            anyVisibleTransparent = true;
            break;
        }
    }
    if (!anyVisibleTransparent) return;

    VkCommandBuffer cmd = vulkanDevice->getCommandBuffer(frameIndex);

    // Bind OIT pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipeline->getOITPipeline());

    // Rebind cube vertex buffer at binding 0. Between the opaque pass and here, dynamic subcubes,
    // entities, kinematic objects, and mirror geometry all rebind binding 0 to their own buffers.
    // If we don't restore it, the OIT pass draws with the wrong geometry → invisible glass.
    vulkanDevice->bindVertexBuffers(frameIndex);
    vulkanDevice->bindIndexBuffer(frameIndex);
    vulkanDevice->bindDescriptorSets(frameIndex, renderPipeline->getGraphicsLayout());

    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (chunk->getNumInstances() == 0) continue;

        VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
        VkDeviceSize instanceOffsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 1, 1, instanceBuffers, instanceOffsets);

        glm::ivec3 worldOrigin = chunk->getWorldOrigin();
        glm::vec3 chunkBaseOffset(worldOrigin.x, worldOrigin.y, worldOrigin.z);
        vulkanDevice->pushConstants(frameIndex, renderPipeline->getGraphicsLayout(), chunkBaseOffset);

        vulkanDevice->drawIndexed(frameIndex, vulkanDevice->chunkIndexCount(), chunk->getNumInstances());
    }
}

bool RenderCoordinator::scanForMirrorVoxels() {
    if (!chunkManager) return false;

    // Per-chunk mirror presence is cached and recomputed only when a chunk's
    // contents change (Chunk::rebuildFaces). Here we just check the cached flag
    // for each visible chunk — O(visibleChunks), not O(visibleChunks * 32768).
    // (The old brute-force per-frame voxel scan cost ~46ms/frame; see git history.)
    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (!chunk || chunk->getNumInstances() == 0) continue;
        if (!chunk->hasMirrorVoxel()) continue;

        glm::ivec3 origin = chunk->getWorldOrigin();
        glm::ivec3 local  = chunk->getFirstMirrorLocal();
        mirrorPlaneNormal = glm::vec3(0.0f, 0.0f, 1.0f);
        // Put the reflection plane on the VISIBLE +Z face of the mirror voxel (z = local+1.0),
        // not the voxel center (z = local+0.5). The player sees the +Z face; computing the
        // reflection about the center plane misregisters reflections by a half-voxel (objects
        // don't line up with their reflections where they meet the mirror surface).
        mirrorPlanePoint  = glm::vec3(origin.x + local.x + 0.5f,
                                      origin.y + local.y + 0.5f,
                                      origin.z + local.z + 0.5f) + mirrorPlaneNormal * 0.5f;
        LOG_DEBUG("RenderCoordinator", "Mirror voxel found at ({},{},{}), plane normal ({},{},{})",
            mirrorPlanePoint.x, mirrorPlanePoint.y, mirrorPlanePoint.z,
            mirrorPlaneNormal.x, mirrorPlaneNormal.y, mirrorPlaneNormal.z);
        return true;
    }
    return false;
}

void RenderCoordinator::renderReflectionPass(uint32_t frameIndex) {
    lastFrameStats.mirrorPassRan       = true;
    lastFrameStats.reflectionDrawCalls = 0;
    lastFrameStats.mirrorPlaneX = mirrorPlanePoint.x;
    lastFrameStats.mirrorPlaneY = mirrorPlanePoint.y;
    lastFrameStats.mirrorPlaneZ = mirrorPlanePoint.z;
    lastFrameStats.mirrorNormalX = mirrorPlaneNormal.x;
    lastFrameStats.mirrorNormalY = mirrorPlaneNormal.y;
    lastFrameStats.mirrorNormalZ = mirrorPlaneNormal.z;

    glm::vec3 N = glm::normalize(mirrorPlaneNormal);
    float d = -glm::dot(N, mirrorPlanePoint);

    // Reflection matrix (mirrors any point about plane N*p + d = 0)
    glm::mat4 reflMat(1.0f);
    reflMat[0][0] = 1.0f - 2.0f*N.x*N.x;  reflMat[1][0] = -2.0f*N.x*N.y;        reflMat[2][0] = -2.0f*N.x*N.z;        reflMat[3][0] = 2.0f*N.x*(-d);
    reflMat[0][1] = -2.0f*N.x*N.y;         reflMat[1][1] = 1.0f - 2.0f*N.y*N.y;  reflMat[2][1] = -2.0f*N.y*N.z;        reflMat[3][1] = 2.0f*N.y*(-d);
    reflMat[0][2] = -2.0f*N.x*N.z;         reflMat[1][2] = -2.0f*N.y*N.z;        reflMat[2][2] = 1.0f - 2.0f*N.z*N.z;  reflMat[3][2] = 2.0f*N.z*(-d);

    glm::vec3 camPos   = camera->getPosition();
    glm::vec3 camFront = camera->getFront();
    glm::vec3 camUp    = camera->getUp();

    glm::vec3 reflCamPos   = glm::vec3(reflMat * glm::vec4(camPos, 1.0f));
    glm::vec3 reflCamFront = glm::vec3(reflMat * glm::vec4(camFront, 0.0f));
    glm::vec3 reflCamUp    = glm::vec3(reflMat * glm::vec4(camUp, 0.0f));

    lastFrameStats.reflCamX = reflCamPos.x;
    lastFrameStats.reflCamY = reflCamPos.y;
    lastFrameStats.reflCamZ = reflCamPos.z;
    LOG_DEBUG("RenderCoordinator", "Reflection pass: camPos=({},{},{}) reflCamPos=({},{},{}) reflFront=({},{},{}) visibleChunks={}",
        camPos.x, camPos.y, camPos.z,
        reflCamPos.x, reflCamPos.y, reflCamPos.z,
        reflCamFront.x, reflCamFront.y, reflCamFront.z,
        visibleChunkIndices.size());

    // Build the reflected view by composing the reflection into the main view matrix.
    // NOT glm::lookAt(reflected eye/front/up): lookAt re-derives the right axis via
    // cross(front,up), which picks up the reflection's det=-1 sign and (a) horizontally
    // mirrors the image and (b) yields a det=+1 view that does NOT flip triangle winding.
    // mainView * reflMat is the exact reflected view: correct handedness, det=-1 so winding
    // flips as the reflectionScenePipeline's BACK_BIT culling expects.
    glm::mat4 reflectedView = camera->getViewMatrix() * reflMat;
    cachedReflectedViewProj = cachedProjectionMatrix * reflectedView;

    // Store reflected VP in the main UBO so mirror_voxel.frag can use it for projective texturing
    vulkanDevice->setReflectedViewProj(frameIndex, cachedReflectedViewProj);

    // Build the projection for the reflection render pass.
    //
    // We do NOT set the near plane at the mirror surface. A perspective near plane is
    // perpendicular to the view direction, so when the camera looks at the mirror at an
    // angle it tilts relative to the mirror plane and clips a wedge of the floor nearest
    // the mirror base — that produced the dark band along the bottom edge of the mirror.
    //
    // Instead use a small near plane close to the reflected camera. This never clips valid
    // reflected geometry. The cost is that geometry directly BEHIND the mirror is also
    // rendered into the reflection; for a mirror mounted on a solid wall there is nothing
    // meaningful back there, and a continuous floor simply fills the bottom edge correctly.
    // (For a mirror with a real room behind it, the exact fix is oblique near-plane clipping
    // or a gl_ClipDistance world-plane clip at the mirror — a future enhancement.)
    const float reflNear = 0.3f;
    // Preserve the original far plane by extracting it from cachedProjectionMatrix.
    // Works for both OpenGL [-1,1] and Vulkan [0,1] depth conventions: far = B/(A+1).
    float A = cachedProjectionMatrix[2][2];
    float B = cachedProjectionMatrix[3][2];
    float farPlane = B / (A + 1.0f);
    float reflAspect = (float)windowManager->getWidth() / (float)windowManager->getHeight();
    glm::mat4 clippedProj = glm::perspective(glm::radians(45.0f), reflAspect, reflNear, farPlane);
    clippedProj[1][1] *= -1;  // Y-flip for Vulkan, matching cachedProjectionMatrix

    // Update the reflection-specific UBO with the reflected view matrix
    auto sunDir   = glm::vec3(0.0f, -1.0f, 0.0f); // Will be overridden by actual sun direction from last frame
    // Reuse current frame's UBO values (they're already set by the main updateUniformBuffer call above)
    // For simplicity: just use the same sun/ambient values. A full impl would capture these from the lighting pass.
    vulkanDevice->updateReflectionUniformBuffer(frameIndex, reflectedView, clippedProj,
        glm::mat4(1.0f), // lightSpaceMatrix (shadows in reflection not critical)
        glm::vec3(0.0f, -1.0f, 0.5f), glm::vec3(1.0f, 0.95f, 0.8f),
        0, 1.0f, 2.0f, reflCamPos);

    // Render the scene from the reflected camera into the reflection framebuffer
    postProcessor->beginReflectionRenderPass(vulkanDevice->getCommandBuffer(frameIndex));

    // Bind reflection scene pipeline (BACK_BIT culling — winding is flipped by camera reflection)
    vkCmdBindPipeline(vulkanDevice->getCommandBuffer(frameIndex),
        VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipeline->getReflectionScenePipeline());

    // Bind index buffer and reflection descriptor sets (reflected view matrix)
    vulkanDevice->bindIndexBuffer(frameIndex);
    vulkanDevice->bindReflectionDescriptorSets(frameIndex, renderPipeline->getGraphicsLayout());

    // Bind the shared cube geometry to vertex binding 0. The reflection pass runs at the
    // START of the frame, so binding 0 still holds whatever buffer the PREVIOUS frame left
    // there (mirror/entity/kinematic geometry). Without this rebind the reflected chunks pull
    // cube vertices from the wrong buffer → torn/garbage geometry in the reflection texture.
    // The per-chunk loop below rebinds binding 1 to each chunk's instance buffer.
    vulkanDevice->bindVertexBuffers(frameIndex);

    // Draw visible chunks from reflected camera (mirror faces discarded by voxel.frag)
    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (chunk->getNumInstances() == 0) continue;

        VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
        VkDeviceSize instanceOffsets[] = {0};
        vkCmdBindVertexBuffers(vulkanDevice->getCommandBuffer(frameIndex), 1, 1, instanceBuffers, instanceOffsets);

        glm::ivec3 worldOrigin = chunk->getWorldOrigin();
        glm::vec3 chunkBaseOffset(worldOrigin.x, worldOrigin.y, worldOrigin.z);
        vulkanDevice->pushConstants(frameIndex, renderPipeline->getGraphicsLayout(), chunkBaseOffset);
        vulkanDevice->drawIndexed(frameIndex, vulkanDevice->chunkIndexCount(), chunk->getNumInstances());
        lastFrameStats.reflectionDrawCalls++;
    }

    // Draw instanced characters (player + animated NPCs) into the reflection target so they
    // appear in mirrors. Uses the reflected view-projection (clippedProj * reflectedView) and
    // the FRONT_BIT reflection pipeline (the reflected view's det=-1 flips winding). This pass
    // runs first each frame, so it is responsible for uploading the shared character buffer.
    glm::mat4 reflViewProj = clippedProj * reflectedView;
    renderInstancedCharacters(vulkanDevice->getCommandBuffer(frameIndex), reflViewProj,
                              renderPipeline->getReflectionInstancedCharacterPipeline());

    // Draw kinematic objects (doors, furniture, fragments) into the reflection. They read
    // view/proj from the descriptor set, so we pass the reflected-camera descriptor set; the
    // renderReflection() variant uses the BACK_BIT pipeline for the flipped reflected winding.
    // (Object add/remove rebuilds the shared buffer in the main pass — moving objects, the
    // common case, need no rebuild, so they reflect correctly; add/remove may lag one frame.)
    if (kinematicPipeline && m_kinematicObjects && !m_kinematicObjects->getObjects().empty()) {
        kinematicPipeline->renderReflection(
            vulkanDevice->getCommandBuffer(frameIndex),
            m_kinematicObjects->getObjects(),
            vulkanDevice->getReflectionDescriptorSet(frameIndex));
    }

    LOG_DEBUG("RenderCoordinator", "Reflection pass complete: {} chunks drawn", lastFrameStats.reflectionDrawCalls);
    postProcessor->endReflectionRenderPass(vulkanDevice->getCommandBuffer(frameIndex));
}

void RenderCoordinator::renderMirrorGeometry(uint32_t frameIndex) {
    if (!chunkManager || visibleChunkIndices.empty()) return;
    lastFrameStats.mirrorGeomDrawCalls = 0;

    VkCommandBuffer cmd = vulkanDevice->getCommandBuffer(frameIndex);

    // Bind mirror pipeline (also binds reflection descriptor set at set 1)
    renderPipeline->bindMirrorPipeline(cmd, frameIndex, renderPipeline->getMirrorReflectionDescriptorSet());

    // Bind main descriptor set at set 0 (original view + atlas + lights)
    vulkanDevice->bindDescriptorSets(frameIndex, renderPipeline->getMirrorPipelineLayout());
    vulkanDevice->bindIndexBuffer(frameIndex);

    // Rebind the shared cube geometry to vertex binding 0. The mirror pass runs after the
    // entity/kinematic/dynamic passes, which leave their own buffers bound at binding 0 (see
    // the comment in renderScene). Without this the mirror faces pull cube vertices from a
    // leftover buffer → torn coverage (visible triangle holes in the mirror surface).
    // The per-chunk loop below rebinds binding 1 to each chunk's instance buffer.
    vulkanDevice->bindVertexBuffers(frameIndex);

    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (chunk->getNumInstances() == 0) continue;

        VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
        VkDeviceSize instanceOffsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 1, 1, instanceBuffers, instanceOffsets);

        glm::ivec3 worldOrigin = chunk->getWorldOrigin();
        glm::vec3 chunkBaseOffset(worldOrigin.x, worldOrigin.y, worldOrigin.z);
        vulkanDevice->pushConstants(frameIndex, renderPipeline->getMirrorPipelineLayout(), chunkBaseOffset);
        vulkanDevice->drawIndexed(frameIndex, vulkanDevice->chunkIndexCount(), chunk->getNumInstances());
        lastFrameStats.mirrorGeomDrawCalls++;
    }
    LOG_DEBUG("RenderCoordinator", "Mirror geometry pass: {} chunks drawn", lastFrameStats.mirrorGeomDrawCalls);
}

void RenderCoordinator::renderDynamicSubcubes() {
    //
    // Rendering method:  vkCmdDrawIndirect (NON-INDEXED)
    //   vertexCount  = 6     (set once at init, never changes)
    //   instanceCount = N    (written by expand shader each frame via atomicAdd)
    //
    // The vertex shader (dynamic_voxel.vert) generates two triangles per face
    // from vertexID 0-5 using a corner remap table. CW winding for front-face
    // culling compatibility (cullMode=CULL_FRONT, frontFace=CCW).
    //
    // Binding 0: shared 8-vertex cube buffer (bound for pipeline compatibility,
    //            but the shader uses vertexID from the indirect draw count)
    // Binding 1: GPU face buffer (DynamicSubcubeInstanceData, 64 bytes/face)
    //
    // The index buffer IS bound but IGNORED by vkCmdDrawIndirect.
    // See docs/DynamicSubcubeRenderPipeline.md for full details.
    // ---------------------------------------------------------------------------
    if (m_gpuParticles && m_gpuParticles->isInitialized() && m_gpuParticles->getActiveParticleCount() > 0) {
        VkCommandBuffer cmd = vulkanDevice->getCommandBuffer(currentFrame);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          dynamicRenderPipeline->getGraphicsPipeline());

        vulkanDevice->bindVertexBuffers(currentFrame);       // binding 0: vertex IDs
        VkBuffer faceBuffer    = m_gpuParticles->getFaceBuffer();
        VkDeviceSize faceOffset = 0;
        vkCmdBindVertexBuffers(cmd, 1, 1, &faceBuffer, &faceOffset);  // binding 1: face instances
        vulkanDevice->bindIndexBuffer(currentFrame);         // bound but unused by indirect draw

        vulkanDevice->bindDescriptorSets(currentFrame, dynamicRenderPipeline->getGraphicsLayout());

        vkCmdDrawIndirect(cmd, m_gpuParticles->getIndirectDrawBuffer(), 0, 1, 16);
        // Fall through to also render Bullet dynamic objects (hybrid mode)
    }

    // ---------------------------------------------------------------------------
    // CPU Bullet path: dynamic cubes managed by DynamicObjectManager.
    // In hybrid mode this renders alongside the GPU path above.
    // ---------------------------------------------------------------------------
    const auto& allDynamicSubcubeFaces = chunkManager->getGlobalDynamicSubcubeFaces();
    if (!allDynamicSubcubeFaces.empty()) {
        vulkanDevice->updateDynamicSubcubeBuffer(allDynamicSubcubeFaces);

        VkCommandBuffer cmd = vulkanDevice->getCommandBuffer(currentFrame);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         dynamicRenderPipeline->getGraphicsPipeline());

        vulkanDevice->bindDynamicSubcubeBuffer(currentFrame);
        vulkanDevice->bindDescriptorSets(currentFrame, dynamicRenderPipeline->getGraphicsLayout());

        // Non-indexed draw: vertexID 0-5 procedurally, matching GPU path and
        // dynamic_voxel.vert's cornerRemap[6] expectations.
        // Clamp instance count to the buffer capacity: if face data exceeds the buffer,
        // updateDynamicSubcubeBuffer silently truncates the write, and drawing more
        // instances than were written causes the GPU to read stale buffer data,
        // producing ghost voxels at expired cube positions.
        uint32_t drawCount = static_cast<uint32_t>(
            std::min(allDynamicSubcubeFaces.size(),
                     static_cast<size_t>(vulkanDevice->getMaxDynamicSubcubes())));
        vkCmdDraw(cmd, 6, drawCount, 0, 0);
    }
}

// Occlusion culling via the per-chunk visibility graph ("cave culling"). BFS from
// the camera chunk through air-connected, frustum-visible chunks; a fully solid
// chunk has no connected faces and so blocks propagation, hiding everything behind
// it. Frustum-visible chunks the BFS never reaches are occluded and removed from
// visibleChunkIndices. Conservative by design (no directional/anti-wraparound
// pruning yet): it can leave some occluded chunks in, but never culls a visible
// one — so it cannot produce holes. Phase 1; flag-gated, OFF by default.
void RenderCoordinator::applyOcclusionCulling(const glm::vec3& cameraPos) {
    m_lastOcclusionCulled = 0;
    if (!chunkManager || visibleChunkIndices.size() < 2) return;

    auto toCoord = [](const glm::vec3& p) -> glm::ivec3 {
        return glm::ivec3(glm::floor(p / 32.0f));
    };
    auto packCoord = [](const glm::ivec3& c) -> int64_t {
        return ((int64_t)(c.x + (1 << 20)) << 42)
             | ((int64_t)(c.y + (1 << 20)) << 21)
             |  (int64_t)(c.z + (1 << 20));
    };
    const glm::ivec3 dirVec[6] = {
        {-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}
    };

    // Index the frustum-visible chunks by packed chunk coord. Reused scratch maps
    // (.clear() keeps bucket arrays) so this hot path doesn't allocate every frame.
    std::unordered_map<int64_t, size_t>& coordToIdx = m_occCoordToIdx;
    coordToIdx.clear();
    coordToIdx.reserve(visibleChunkIndices.size() * 2);
    for (size_t idx : visibleChunkIndices) {
        glm::ivec3 coord = toCoord(glm::vec3(chunkManager->chunks[idx]->getWorldOrigin()));
        coordToIdx[packCoord(coord)] = idx;
    }

    // BFS outward from the camera's chunk. entryFace = -1 for the seed (you can see
    // out of your own chunk in any direction); thereafter sight must pass from the
    // entry face to the exit face through the chunk's air (facesConnected).
    glm::ivec3 camCoord = toCoord(cameraPos);
    std::unordered_set<int64_t>& reached = m_occReached;
    reached.clear();
    reached.reserve(visibleChunkIndices.size() * 2);
    std::queue<std::pair<glm::ivec3, int>> q;
    reached.insert(packCoord(camCoord));
    q.push({camCoord, -1});

    while (!q.empty()) {
        std::pair<glm::ivec3, int> node = q.front(); q.pop();
        const glm::ivec3 coord = node.first;
        const int entryFace = node.second;
        const Chunk* ch = chunkManager->getChunkAtCoord(coord);
        for (int d = 0; d < 6; ++d) {
            if (entryFace >= 0 && ch && !ch->facesConnected(entryFace, d))
                continue;                          // blocked through this chunk
            glm::ivec3 ncoord = coord + dirVec[d];
            int64_t key = packCoord(ncoord);
            if (reached.count(key)) continue;
            if (!coordToIdx.count(key)) continue;  // outside the frustum set → prune
            reached.insert(key);
            q.push({ncoord, d ^ 1});               // entered neighbor via opposite face
        }
    }

    // Keep only frustum-visible chunks the BFS reached.
    const size_t before = visibleChunkIndices.size();
    std::vector<size_t>& kept = m_occKept;
    kept.clear();
    kept.reserve(before);
    for (size_t idx : visibleChunkIndices) {
        glm::ivec3 coord = toCoord(glm::vec3(chunkManager->chunks[idx]->getWorldOrigin()));
        if (reached.count(packCoord(coord))) kept.push_back(idx);
    }
    m_lastOcclusionCulled = static_cast<int>(before - kept.size());
    visibleChunkIndices.swap(kept);
}

bool RenderCoordinator::s_shadowFrustumCull = true;  // D1c: light-frustum cull the shadow pass

void RenderCoordinator::renderShadowPass(VkCommandBuffer commandBuffer, const glm::mat4& lightSpaceMatrix,
                                         const glm::vec3& cullCenter, float cullRadius) {
    if (!shadowMap) return;

    shadowMap->beginRenderPass(commandBuffer);

    // Cull chunks to the fitted shadow volume (the view-frustum bounding sphere computed in
    // drawFrame). Pad by a chunk radius + caster margin so tall/edge casters aren't dropped.

    // Bind global vertex buffer (binding 0) and index buffer
    // We use bindVertexBuffers to bind the shared vertex buffer to binding 0
    // It also binds the global instance buffer to binding 1, but we'll override that per-chunk
    vulkanDevice->bindVertexBuffers(currentFrame);
    vulkanDevice->bindIndexBuffer(currentFrame);

    // Iterate chunks and draw
    // D1 diagnosis: count chunks/instances actually drawn (vs the frustum-culled main pass's
    // visibleChunkCount) + pipeline stats for the shadow chunk draws. See docs/RenderDensityPlan.md.
    int shadowChunks = 0;
    long long shadowInstances = 0;
    // D1c: light-frustum cull. lightSpaceMatrix is the fitted ortho shadow volume; a chunk whose AABB
    // doesn't intersect it cannot write to the shadow map, so this is a correct (loss-free) tightening
    // of the loose distance sphere. Cuts the 138-chunk draw count → the suspected draw-call floor.
    Utils::Frustum lightFrustum;
    if (s_shadowFrustumCull) lightFrustum.extractFromMatrix(lightSpaceMatrix);
    gpuProfiler->beginPipelineStats(commandBuffer, GpuProfiler::STATS_SLOT_SHADOW);
    if (chunkManager && !chunkManager->chunks.empty()) {
        for (const auto& chunk : chunkManager->chunks) {
             if (chunk->getNumInstances() == 0) continue;

             // Simple distance culling for shadows
             glm::vec3 minBounds = chunk->getMinBounds();
             glm::vec3 maxBounds = chunk->getMaxBounds();
             glm::vec3 chunkCenter = (minBounds + maxBounds) * 0.5f;
             if (glm::length(chunkCenter - cullCenter) > cullRadius + 160.0f) continue; // fitted sphere + chunk radius + caster margin

             // D1c: tight light-frustum cull (skip chunks that can't project into the shadow map)
             if (s_shadowFrustumCull && !lightFrustum.intersects(Utils::AABB(minBounds, maxBounds))) continue;

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
             vkCmdDrawIndexed(commandBuffer, vulkanDevice->chunkIndexCount(), chunk->getNumInstances(), 0, 0, 0);
             ++shadowChunks;
             shadowInstances += chunk->getNumInstances();
        }
    }
    gpuProfiler->endPipelineStats(commandBuffer, GpuProfiler::STATS_SLOT_SHADOW);
    // Stash in members — lastFrameStats is reset AFTER the shadow pass in drawFrame, so copy these
    // into lastFrameStats there (see the visibleChunkCount block).
    m_shadowChunksDrawn = shadowChunks;
    m_shadowInstancesDrawn = shadowInstances;

    // -------------------------------------------------------------------------
    // Character shadow pass (AnimatedVoxelCharacter / NPC ragdolls)
    // -------------------------------------------------------------------------
    if (shadowMap->getCharacterShadowPipeline() != VK_NULL_HANDLE) {
        // Collect same character list as renderEntities
        std::vector<Scene::RagdollCharacter*> instancedCharacters;
        if (entities) {
            for (const auto& entity : *entities) {
                if (auto* ac = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity.get()))
                    instancedCharacters.push_back(ac);
            }
        }
        if (m_npcManager) {
            for (const auto& name : m_npcManager->getAllNPCNames()) {
                auto* npc = m_npcManager->getNPC(name);
                if (npc) {
                    if (auto* renderable = npc->getRenderableCharacter())
                        instancedCharacters.push_back(renderable);
                }
            }
        }

        if (!instancedCharacters.empty()) {
            std::vector<CharacterInstanceData> instanceData;
            struct CharShadowBatch { glm::mat4 model; uint32_t firstInstance; uint32_t instanceCount; };
            std::vector<CharShadowBatch> batches;

            auto batchParts = [&](Scene::RagdollCharacter* ch) {
                const auto& charParts = ch->getParts();
                for (const auto& grp : ch->getPartGroups()) {
                    if (grp.partIndices.empty()) continue;
                    const auto& first = charParts[grp.partIndices[0]];
                    CharShadowBatch batch;
                    batch.model = glm::translate(glm::mat4(1.0f), first.worldPos) * glm::mat4_cast(first.worldRot);
                    batch.firstInstance = static_cast<uint32_t>(instanceData.size());
                    batch.instanceCount = 0;
                    for (int pi : grp.partIndices) {
                        const auto& part = charParts[pi];
                        if (!part.active) continue;
                        CharacterInstanceData data;
                        data.offset = part.offset;
                        data.scale  = part.scale;
                        data.color  = part.color;
                        instanceData.push_back(data);
                        batch.instanceCount++;
                    }
                    if (batch.instanceCount > 0) batches.push_back(batch);
                }
            };

            for (auto* charPtr : instancedCharacters) batchParts(charPtr);

            if (!instanceData.empty()) {
                vulkanDevice->updateCharacterInstanceBuffer(instanceData);
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMap->getCharacterShadowPipeline());
                vulkanDevice->bindCharacterInstanceBuffer(commandBuffer);

                struct CharShadowPC { glm::mat4 model; glm::mat4 lightSpaceMatrix; } charPC;
                charPC.lightSpaceMatrix = lightSpaceMatrix;
                for (const auto& batch : batches) {
                    charPC.model = batch.model;
                    vkCmdPushConstants(commandBuffer, shadowMap->getCharacterShadowLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(charPC), &charPC);
                    vkCmdDraw(commandBuffer, 36, batch.instanceCount, 0, batch.firstInstance);
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Kinematic voxel shadow pass (doors, rotating platforms, etc.)
    // -------------------------------------------------------------------------
    if (shadowMap->getKinematicShadowPipeline() != VK_NULL_HANDLE &&
        kinematicPipeline && m_kinematicObjects &&
        !m_kinematicObjects->getObjects().empty())
    {
        VkBuffer kinBuf = kinematicPipeline->getInstanceBuffer();
        if (kinBuf != VK_NULL_HANDLE) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMap->getKinematicShadowPipeline());

            struct KinShadowPC { glm::mat4 modelMatrix; glm::mat4 lightSpaceMatrix; } kinPC;
            kinPC.lightSpaceMatrix = lightSpaceMatrix;
            for (const auto& [id, range] : kinematicPipeline->getObjectRanges()) {
                auto it = m_kinematicObjects->getObjects().find(id);
                if (it == m_kinematicObjects->getObjects().end() || !it->second.visible) continue;
                kinPC.modelMatrix = it->second.currentTransform;
                vkCmdPushConstants(commandBuffer, shadowMap->getKinematicShadowLayout(),
                                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(kinPC), &kinPC);
                VkDeviceSize offset = range.startFace * sizeof(Core::KinematicFaceData);
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, &kinBuf, &offset);
                vkCmdDraw(commandBuffer, 6, range.faceCount, 0, 0);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Dynamic GPU particle shadow pass
    // -------------------------------------------------------------------------
    if (shadowMap->getDynamicShadowPipeline() != VK_NULL_HANDLE &&
        m_gpuParticles && m_gpuParticles->isInitialized() && m_gpuParticles->getActiveParticleCount() > 0)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMap->getDynamicShadowPipeline());
        glm::mat4 lsm = lightSpaceMatrix;
        vkCmdPushConstants(commandBuffer, shadowMap->getDynamicShadowLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &lsm);
        // Binding 0: vertex ID buffer (shared), binding 1: GPU face buffer
        vulkanDevice->bindVertexBuffers(currentFrame);
        VkBuffer faceBuffer = m_gpuParticles->getFaceBuffer();
        VkDeviceSize faceOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 1, 1, &faceBuffer, &faceOffset);
        vkCmdDrawIndirect(commandBuffer, m_gpuParticles->getIndirectDrawBuffer(), 0, 1, 16);
    }

    // -------------------------------------------------------------------------
    // Foliage leaf-card shadow pass — dappled canopy shadows (same cutout masks as the visible
    // cards; the vert projects with ubo.lightSpaceMatrix, which drawFrame updates every frame).
    // Same shadow-sphere culling as the static chunks above.
    // -------------------------------------------------------------------------
    if (foliagePipeline && foliagePipeline->params().enabled && chunkManager) {
        std::vector<FoliageRenderPipeline::ChunkDraw> foliageDraws;
        for (const auto& chunk : chunkManager->chunks) {
            if (!chunk || chunk->getFoliageCount() == 0) continue;
            glm::vec3 chunkCenter = (chunk->getMinBounds() + chunk->getMaxBounds()) * 0.5f;
            if (glm::length(chunkCenter - cullCenter) > cullRadius + 160.0f) continue;
            glm::ivec3 origin = chunk->getWorldOrigin();
            foliageDraws.push_back({ chunk->getFoliageBuffer(), chunk->getFoliageCount(),
                                     glm::vec3(origin.x, origin.y, origin.z) });
        }
        foliagePipeline->renderShadow(commandBuffer,
                                      vulkanDevice->getDescriptorSet(currentFrame), foliageDraws);
    }

    shadowMap->endRenderPass(commandBuffer);
}

void RenderCoordinator::drawFrame() {
    // Skip rendering when window is minimized (0x0 extent is invalid in Vulkan)
    if (windowManager->getWidth() == 0 || windowManager->getHeight() == 0) {
        return;
    }

    // Flush dirty chunk meshes BEFORE drawing. Voxels placed after world-gen
    // (structures, scene loads, edits) only mark chunks dirty — without this
    // rebuild they never become faces. The editor also calls updateDirtyChunks
    // in its update loop (harmless O(0) duplicate); standalone games rely on
    // THIS call — MazeRunner's walls were invisible because nothing flushed
    // the dirty list outside the editor.
    if (chunkManager) {
        // Budgeted so a large dirty backlog (async world-gen/fill finalize) spreads
        // over frames instead of stalling for seconds — standalones flush here.
        constexpr double kDirtyChunkBudgetMs = 6.0;
        chunkManager->updateDirtyChunks(kDirtyChunkBudgetMs);
    }

    // Check if we need to recreate swapchain due to window resize
    if (vulkanDevice->getFramebufferResized() || windowManager->wasResized()) {
        LOG_INFO("RenderCoordinator", "Resize detected! VulkanFlag: {}, WindowFlag: {}", 
            vulkanDevice->getFramebufferResized(), windowManager->wasResized());
            
        // IMPORTANT: recreate swapchain FIRST — it calls vkDeviceWaitIdle internally,
        // ensuring all in-flight command buffers finish before we destroy any resources.
        if (!vulkanDevice->recreateSwapChain(windowManager->getWidth(), windowManager->getHeight(), postProcessor->getPostProcessRenderPass())) {
            LOG_INFO("RenderCoordinator", "recreateSwapChain returned false (minimized?)");
            return; // Try again next frame
        }

        // Now safe to resize PostProcessor and recreate pipelines (GPU is idle)
        postProcessor->resize(windowManager->getWidth(), windowManager->getHeight());
        renderPipeline->createGraphicsPipeline();
        renderPipeline->createDebugGraphicsPipeline();
        renderPipeline->createDebugLinePipeline();
        renderPipeline->createCharacterPipeline();
        renderPipeline->createOITPipeline(postProcessor->getOITRenderPass());
        renderPipeline->createMirrorPipeline(postProcessor->getSceneRenderPass());
        renderPipeline->createReflectionScenePipeline(postProcessor->getSceneRenderPass());
        renderPipeline->updateMirrorReflectionDescriptor(
            postProcessor->getReflectionImageView(), postProcessor->getReflectionSampler());
        if (waterPipeline) waterPipeline->setReflectionTexture(
            postProcessor->getReflectionImageView(), postProcessor->getReflectionSampler());
        dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes();

        windowManager->acknowledgeResize();
        projectionMatrixNeedsUpdate = true;
        return; // Skip this frame — render cleanly on the next one
    }

    // Wait for previous frame
    vulkanDevice->waitForFence(currentFrame);

    // Acquire next image (don't reset fence yet — if acquire fails, the still-signaled
    // fence lets the next frame's waitForFence pass instead of deadlocking)
    uint32_t imageIndex;
    VkResult result = vulkanDevice->acquireNextImage(currentFrame, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain is out of date — recreate FIRST (calls vkDeviceWaitIdle)
        if (!vulkanDevice->recreateSwapChain(windowManager->getWidth(), windowManager->getHeight(), postProcessor->getPostProcessRenderPass())) {
            return; // Try again next frame
        }

        // Then resize PostProcessor and recreate pipelines (GPU is idle)
        postProcessor->resize(windowManager->getWidth(), windowManager->getHeight());
        renderPipeline->createGraphicsPipeline();
        renderPipeline->createDebugGraphicsPipeline();
        renderPipeline->createDebugLinePipeline();
        renderPipeline->createCharacterPipeline();
        renderPipeline->createOITPipeline(postProcessor->getOITRenderPass());
        renderPipeline->createMirrorPipeline(postProcessor->getSceneRenderPass());
        renderPipeline->createReflectionScenePipeline(postProcessor->getSceneRenderPass());
        renderPipeline->updateMirrorReflectionDescriptor(
            postProcessor->getReflectionImageView(), postProcessor->getReflectionSampler());
        if (waterPipeline) waterPipeline->setReflectionTexture(
            postProcessor->getReflectionImageView(), postProcessor->getReflectionSampler());
        dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes();

        return; // Skip this frame and try again
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_WARN("RenderCoordinator", "Failed to acquire swapchain image (VkResult={}), recreating swapchain", static_cast<int>(result));
        
        // Attempt recovery — recreate swapchain first (calls vkDeviceWaitIdle)
        vulkanDevice->recreateSwapChain(windowManager->getWidth(), windowManager->getHeight(), postProcessor->getPostProcessRenderPass());
        postProcessor->resize(windowManager->getWidth(), windowManager->getHeight());
        renderPipeline->createGraphicsPipeline();
        renderPipeline->createDebugGraphicsPipeline();
        renderPipeline->createDebugLinePipeline();
        renderPipeline->createCharacterPipeline();
        renderPipeline->createOITPipeline(postProcessor->getOITRenderPass());
        renderPipeline->createMirrorPipeline(postProcessor->getSceneRenderPass());
        renderPipeline->createReflectionScenePipeline(postProcessor->getSceneRenderPass());
        renderPipeline->updateMirrorReflectionDescriptor(
            postProcessor->getReflectionImageView(), postProcessor->getReflectionSampler());
        if (waterPipeline) waterPipeline->setReflectionTexture(
            postProcessor->getReflectionImageView(), postProcessor->getReflectionSampler());
        dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes();
        return; // Skip this frame, try again next frame
    }

    // Acquire succeeded — now reset fence before submitting work.
    // The queue submit at the end of this frame will re-signal it.
    vulkanDevice->resetFence(currentFrame);

    // ChunkManager handles its own data management - no instance buffer needed
    // Static chunk geometry is pre-built and doesn't change unless modified
    
    // Get chunk statistics for rendering
    auto chunkStats = chunkManager->getPerformanceStats();
    
    // Prepare uniform buffer data (optimized)
    auto uboStart = std::chrono::high_resolution_clock::now();
    
    // Projection matrix. Recomputed each frame from the camera so a rig switching
    // between perspective and orthographic (overhead/isometric) takes effect
    // immediately. Camera::getProjectionMatrix() applies the Vulkan Y-flip and
    // keeps the engine's fixed 45deg perspective FOV.
    {
        const float aspect = (float)windowManager->getWidth() / (float)windowManager->getHeight();
        if (camera) {
            cachedProjectionMatrix = camera->getProjectionMatrix(aspect, 0.1f, maxChunkRenderDistance);
        } else {
            cachedProjectionMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, maxChunkRenderDistance);
            cachedProjectionMatrix[1][1] *= -1; // Flip Y for Vulkan
        }
        projectionMatrixNeedsUpdate = false;
    }

    // Use cached matrices from update()
    // Always get fresh view matrix from camera — ensures correctness even when
    // setCachedViewMatrix() hasn't been called (e.g. standalone games via EngineRuntime)
    if (camera) {
        cachedViewMatrix = camera->getViewMatrix();
    }
    glm::mat4 view = cachedViewMatrix;
    glm::mat4 proj = cachedProjectionMatrix;
    
    // Calculate light space matrix for shadows
    glm::vec3 cameraPos = camera->getPosition();

    // Update day/night cycle and apply to lighting
    {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - frameStartTime).count();
        // Clamp to avoid huge jumps on first frame or lag spikes
        dt = std::min(dt, 0.1f);
        m_dayNightCycle.update(dt);
        if (m_dayNightCycle.isEnabled()) {
            sunDirection = m_dayNightCycle.getSunDirection();
            sunColor = m_dayNightCycle.getSunColor();
            ambientLightStrength = m_dayNightCycle.getAmbientStrength();
        }
        // Drive the background sky colour: from the cycle when enabled, else a default day blue
        // (so the scene never clears to black). Shows behind the world in editor + standalone.
        if (postProcessor) {
            postProcessor->setSkyColor(m_dayNightCycle.isEnabled()
                ? m_dayNightCycle.getSkyColor() : glm::vec3(0.45f, 0.65f, 0.95f));
        }
    }

    // Fit the shadow frustum to the camera's VIEW FRUSTUM (bounding-sphere fit), capped at a max
    // shadow distance. The shadow box is sized/positioned to cover exactly what the camera can
    // see, so any on-screen caster is — by construction — always inside the shadow map (panning
    // can't cut off a visible shadow). Using the frustum's bounding SPHERE makes the box size
    // rotation-invariant, which avoids shadow-edge shimmer as the camera turns. Replaces the old
    // fixed-range sphere centred on/ahead of the camera, which dropped shadows near its edge.
    glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
    glm::vec3 shadowCullCenter = cameraPos;
    float shadowCullRadius = 1.0f;
    if (shadowMap) {
        // Shadow draw distance — how far shadows render (must be <= view distance; full view-
        // distance shadows at this quality would need cascades). Capped INDEPENDENTLY of the
        // render distance: raising the view distance (e.g. 2048 with far-terrain LOD) must not
        // stretch the single shadow map over a huge volume — at the old 400-unit cap shadows
        // degraded to smeared low-res blobs with acne. 160 + 4096² keeps ~0.05 units/texel,
        // close to the density everything was visually tuned at (96-unit render distance).
        const float kMaxShadowDist = std::min(maxChunkRenderDistance, 160.0f);
        const float kNear = 0.5f;
        float a00 = cachedProjectionMatrix[0][0];
        float a11 = cachedProjectionMatrix[1][1];
        float aspect = (std::abs(a00) > 1e-6f) ? std::abs(a11 / a00) : (16.0f / 9.0f);

        // World-space corners of the (distance-capped) view frustum via inverse view-proj.
        glm::mat4 shadowVP = camera->getProjectionMatrix(aspect, kNear, kMaxShadowDist) * view;
        glm::mat4 invVP = glm::inverse(shadowVP);
        glm::vec3 corners[8];
        int ci = 0;
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                for (int z = 0; z < 2; ++z) {
                    glm::vec4 c = invVP * glm::vec4(x ? 1.0f : -1.0f, y ? 1.0f : -1.0f, z ? 1.0f : 0.0f, 1.0f);
                    corners[ci++] = glm::vec3(c) / c.w;
                }

        glm::vec3 center(0.0f);
        for (auto& c : corners) center += c;
        center /= 8.0f;
        float radius = 0.0f;
        for (auto& c : corners) radius = std::max(radius, glm::length(c - center));
        // Expand beyond the tight frustum sphere so casters JUST outside the view (off-screen at
        // the edges, or a tall caster whose top pokes above the frustum) are still recorded and
        // cast shadows INTO the view. Without this, edge casters slice in/out as the camera moves
        // (one pillar shadowed, one half, one none). kCasterMargin ~ max caster reach.
        const float kCasterMargin = 48.0f;
        radius = std::ceil(radius) + kCasterMargin;

        shadowCullCenter = center;
        shadowCullRadius = radius;

        glm::vec3 lightDir = glm::normalize(sunDirection);
        glm::vec3 up = (std::abs(lightDir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        // Pull the light back past the sphere so casters between the sun and the frustum (e.g. a
        // tall pillar just off-screen toward the sun) still register in the depth map.
        const float kCasterBack = 120.0f;
        glm::vec3 lightPos = center - lightDir * (radius + kCasterBack);
        glm::mat4 lightView = glm::lookAt(lightPos, center, up);
        // orthoRH_ZO → Vulkan [0,1] clip depth, matching the D32 shadow buffer and the [0,1]
        // shadowCoord.z used in voxel.frag. Plain glm::ortho gives OpenGL [-1,1], which half-clips
        // the scene and breaks the depth compare (GLM_FORCE_DEPTH_ZERO_TO_ONE is NOT set here).
        glm::mat4 lightProj = glm::orthoRH_ZO(-radius, radius, -radius, radius,
                                              0.0f, 2.0f * radius + 2.0f * kCasterBack);
        lightProj[1][1] *= -1;  // Vulkan Y flip
        lightSpaceMatrix = lightProj * lightView;
    }
    
    auto uboEnd = std::chrono::high_resolution_clock::now();
    
    // Update uniform buffer with camera matrices
    auto uniformUploadStart = std::chrono::high_resolution_clock::now();
    
    // Track memory bandwidth for uniform buffer update
    size_t uniformBufferSize = sizeof(glm::mat4) * 3 + sizeof(glm::vec3) * 2 + sizeof(uint32_t) + sizeof(float) * 2; // view + proj + lightSpace + sunDir + sunColor + cubeCount + ambient + emissive
    performanceProfiler->recordMemoryTransfer(uniformBufferSize);
    
    // Seconds since first frame — drives grass wind + growth in the shaders (UBO.elapsedTime).
    static const auto renderStartTime = std::chrono::high_resolution_clock::now();
    float elapsedTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - renderStartTime).count();
    vulkanDevice->updateUniformBuffer(currentFrame, view, proj, lightSpaceMatrix, sunDirection, sunColor, static_cast<uint32_t>(chunkStats.totalCubes), ambientLightStrength, emissiveMultiplier, cameraPos, elapsedTime);
    
    // Upload light data to GPU SSBO
    auto gpuLightData = lightManager.getGPUData();
    vulkanDevice->updateLightBuffer(currentFrame, gpuLightData);
    
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
        renderShadowPass(cmd, lightSpaceMatrix, shadowCullCenter, shadowCullRadius);
    }

    // GPU particle physics compute (integrate → collide → expand)
    // Must run before the render pass because it writes the face vertex buffer
    if (m_gpuParticles && m_gpuParticles->isInitialized()) {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "GPU Particles");
        m_gpuParticles->recordComputeCommands(cmd, currentFrame, gpuProfiler.get());
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
    
    // Reset per-frame stats
    lastFrameStats = {};
    lastFrameStats.visibleChunkCount = static_cast<int>(visibleChunkIndices.size());
    if (chunkManager && !chunkManager->chunks.empty())
        lastFrameStats.totalVisibleFaces = static_cast<int>(chunkStats.totalVisibleFaces);
    // D1 shadow-pass diagnosis (computed in renderShadowPass above, before this reset).
    lastFrameStats.shadowChunksDrawn = m_shadowChunksDrawn;
    lastFrameStats.shadowInstancesDrawn = m_shadowInstancesDrawn;

    hasMirrorVoxels = scanForMirrorVoxels();
    LOG_DEBUG("RenderCoordinator", "Frame: visibleChunks={} hasMirrorVoxels={}", visibleChunkIndices.size(), hasMirrorVoxels);

    // Mirror reflection pass: render scene from reflected camera before the main scene pass.
    if (hasMirrorVoxels && renderPipeline->getMirrorPipeline() != VK_NULL_HANDLE) {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Reflection Pass");  // D0: was untimed
        renderReflectionPass(currentFrame);
    }

    // Water reflection: the surface shader uses a procedural sky+sun reflection, so we
    // no longer re-render the scene for water. True planar scene reflection is deferred
    // until a correct reflection pass exists (the shared mirror pass is broken — wrong
    // winding/projection). When that lands, set m_waterReflectionActive and run a
    // reflection pass with the sea plane; the water shader's dormant branch samples it.
    m_waterReflectionActive = false;

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
            // D0: count fragment invocations + primitives for the chunk pass (overdraw counter).
            gpuProfiler->beginPipelineStats(cmd, GpuProfiler::STATS_SLOT_STATIC);
            actuallyRenderedChunks = renderStaticGeometry();
            gpuProfiler->endPipelineStats(cmd, GpuProfiler::STATS_SLOT_STATIC);
        }

        // Grass blades on grass-topped terrain (opaque cutout; reuses the just-computed visible set)
        {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Grass");
            renderGrass();
        }

        // Leaf foliage cards on trees/bushes (opaque cutout; reuses the same visible set)
        {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Foliage");
            renderFoliage();
        }

        // Far-terrain LOD tiles (after static geometry: near chunks fill depth first)
        {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Far Terrain");
            renderFarTerrain();
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

    // Render VFX particles (additive glow — after opaque/debris geometry)
    if (vfxPipeline && vfxSystem && vfxSystem->getActiveCount() > 0) {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "VFX");
        vfxPipeline->render(
            vulkanDevice->getCommandBuffer(currentFrame),
            *camera,
            cachedProjectionMatrix,
            vfxSystem->getParticles(),
            vfxSystem->getActiveCount()
        );
    }
    
    // Render Kinematic Voxels (doors, rotating platforms, etc.)
    if (kinematicPipeline && m_kinematicObjects) {
        if (m_kinematicObjects->consumeBufferDirty()) {
            kinematicPipeline->rebuildBuffer(m_kinematicObjects->getObjects());
        }
        if (!m_kinematicObjects->getObjects().empty()) {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "KinematicVoxels");
            kinematicPipeline->render(
                vulkanDevice->getCommandBuffer(currentFrame),
                m_kinematicObjects->getObjects(),
                vulkanDevice->getDescriptorSet(currentFrame)
            );
        }
    }

    // Water surface (Phase 0): a translucent sea-level plane, after all opaque
    // geometry so it blends over the scene. Depth-tested (terrain occludes it) but
    // no depth-write, so the mirror pass below is unaffected.
    if (m_waterEnabled && waterPipeline) {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Water");
        waterPipeline->render(
            vulkanDevice->getCommandBuffer(currentFrame),
            *camera,
            cachedProjectionMatrix,
            m_seaLevel,
            2.0f * maxChunkRenderDistance,
            vulkanDevice->getSwapChainExtent(),
            m_waterReflectionActive
        );
    }

    // Per-cell water surface (the CPU sim's actual field): translucent quads at each
    // surface cell's fill height. Independent of the flat sea plane above.
    if (m_waterManager && waterCellPipeline && !m_waterManager->surfaceCells().empty()) {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "WaterCells");
        waterCellPipeline->render(
            vulkanDevice->getCommandBuffer(currentFrame),
            *camera,
            cachedProjectionMatrix,
            m_waterManager->surfaceCells()
        );
    }

    // Mirror surface pass (inside scene render pass, after all opaque/entity geometry)
    if (hasMirrorVoxels && renderPipeline->getMirrorPipeline() != VK_NULL_HANDLE) {
        renderMirrorGeometry(currentFrame);
    }

    // Game HUD / custom UI (non-ImGui) — drawn LAST in the scene pass, on top of all
    // geometry, into the offscreen image. Shows in the editor viewport AND is carried
    // to the swapchain by post-process for standalone builds. See docs/HudSystem.md.
    if (m_uiSystem) {
        // Pull live game state into the HUD widgets before drawing (single source of
        // truth — hosts register providers on hudData(); widgets just mirror values).
        // Applied to every screen so independently-anchored HUD panels (health,
        // combat round/turn/action, …) all bind; menu screens have no binds (no-op).
        for (const auto& [name, vis] : m_uiSystem->getScreenList()) {
            if (auto* s = m_uiSystem->getScreen(name)) UI::applyHudBindings(s, m_hudData);
        }
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Custom UI");
        m_uiSystem->render(vulkanDevice->getCommandBuffer(currentFrame));
    }

    // End Scene Render Pass
    } // End Scene Pass Scope
    postProcessor->endSceneRenderPass(vulkanDevice->getCommandBuffer(currentFrame));

    // SSAO pass (samples the depth buffer written by scene pass)
    {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "SSAO");
        postProcessor->renderSSAO(vulkanDevice->getCommandBuffer(currentFrame), cachedProjectionMatrix);
    }

    // OIT transparent pass (reads depth in read-only mode, writes accum + reveal)
    if (renderPipeline->getOITPipeline() != VK_NULL_HANDLE) {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "OIT");
        postProcessor->beginOITRenderPass(vulkanDevice->getCommandBuffer(currentFrame));
        renderTransparentGeometryOIT(currentFrame);
        postProcessor->endOITRenderPass(vulkanDevice->getCommandBuffer(currentFrame));
    }

    // Begin Post Process Render Pass (Swapchain)
    postProcessor->beginPostProcessRenderPass(vulkanDevice->getCommandBuffer(currentFrame), vulkanDevice->getSwapChainFramebuffer(imageIndex));

    {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Post Process");
        // Draw Fullscreen Quad
        postProcessor->drawQuad(vulkanDevice->getCommandBuffer(currentFrame));
    }

    // (Game HUD / custom UI is now rendered inside the SCENE pass — into the
    // offscreen image — so it is visible in the editor viewport and stays off ImGui.
    // See the scene-pass render call above and docs/HudSystem.md §5.)

    // Render ImGui on top
    // Scripting console rendering is handled in Application::run() before endFrame()
    // Lighting controls rendering is handled in Application::run() before endFrame()
    {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "ImGui");
        imguiRenderer->render(currentFrame, imageIndex);
    }
    
    // End Post Process Render Pass
    postProcessor->endPostProcessRenderPass(vulkanDevice->getCommandBuffer(currentFrame));
    vulkanDevice->endCommandBuffer(currentFrame);
    auto recordEnd = std::chrono::high_resolution_clock::now();

    // Submit command buffer
    auto submitStart = std::chrono::high_resolution_clock::now();
    if (!vulkanDevice->submitCommandBuffer(currentFrame)) {
        LOG_ERROR("RenderCoordinator", "Failed to submit command buffer!");
        // Recovery: fence was reset but submit didn't signal it.
        // Wait for device idle, recreate sync objects (fences start signaled),
        // and trigger full swapchain recreation on next frame.
        vulkanDevice->deviceWaitIdle();
        vulkanDevice->recreateSyncObjects();
        vulkanDevice->setFramebufferResized(true);
        return;
    }
    auto submitEnd = std::chrono::high_resolution_clock::now();

    // Present frame
    auto presentStart = std::chrono::high_resolution_clock::now();
    VkResult presentResult = vulkanDevice->presentFrame(imageIndex, currentFrame);
    m_lastImageIndex = imageIndex;  // Track for screenshot capture

    // Multi-viewport: update and render secondary platform windows (after main present)
    imguiRenderer->updatePlatformWindows();
    
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
            emissiveMultiplier,
            &lightManager,
            shadowMap.get()
        );

        imguiRenderer->renderProfilerWindow(
            showProfiler,
            performanceProfiler,
            gpuProfiler.get()
        );
    }
}

VkImageView RenderCoordinator::getViewportImageView() const {
    return postProcessor ? postProcessor->getOffscreenImageView() : VK_NULL_HANDLE;
}

VkSampler RenderCoordinator::getViewportSampler() const {
    return postProcessor ? postProcessor->getOffscreenSampler() : VK_NULL_HANDLE;
}

void RenderCoordinator::renderInstancedCharacters(VkCommandBuffer commandBuffer,
        const glm::mat4& viewProj, VkPipeline pipeline) {
    bool hasEntities = entities && !entities->empty();
    bool hasNPCs = m_npcManager && m_npcManager->getNPCCount() > 0;
    if (!hasEntities && !hasNPCs) return;

    // Collect instanced characters: the animated player + animated/physics NPCs.
    std::vector<Scene::RagdollCharacter*> instancedCharacters;
    if (hasEntities) {
        for (const auto& entity : *entities) {
            auto animatedChar = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity.get());
            // Hide player when F5 debug overlay is active so segment boxes are visible.
            if (animatedChar && !raycastVisualizationEnabled) {
                instancedCharacters.push_back(animatedChar);
            }
        }
    }
    if (hasNPCs) {
        for (const auto& name : m_npcManager->getAllNPCNames()) {
            auto* npc = m_npcManager->getNPC(name);
            if (npc) {
                if (auto* renderable = npc->getRenderableCharacter())
                    instancedCharacters.push_back(renderable);
            }
        }
    }
    if (instancedCharacters.empty()) return;

    std::vector<CharacterInstanceData> instanceData;
    struct Batch { glm::mat4 model; uint32_t firstInstance; uint32_t instanceCount; glm::vec4 bakedLight; };
    std::vector<Batch> batches;

    auto batchParts = [&](Scene::RagdollCharacter* ch) {
        const auto& charParts = ch->getParts();
        // Sample the baked light field ONCE per character (uniform across limbs — avoids
        // per-bone popping and sampling floor solids). Use a torso-height point ~1 cube
        // above the first active part. Phase 4: dynamic objects react to baked lighting.
        glm::vec4 charLight(1.0f); // default full-bright (no chunk manager / outside world)
        if (chunkManager) {
            for (const auto& p : charParts) {
                if (!p.active) continue;
                glm::ivec3 wp = glm::ivec3(glm::floor(p.worldPos + glm::vec3(0.0f, 1.0f, 0.0f)));
                auto bl = chunkManager->sampleBakedLight(wp);
                charLight = glm::vec4(bl.sky, bl.r, bl.g, bl.b) / 15.0f;
                break;
            }
        }
        for (const auto& grp : ch->getPartGroups()) {
            if (grp.partIndices.empty()) continue;
            const auto& first = charParts[grp.partIndices[0]];
            glm::mat4 model = glm::translate(glm::mat4(1.0f), first.worldPos)
                            * glm::mat4_cast(first.worldRot);
            Batch batch;
            batch.model = model;
            batch.bakedLight = charLight;
            batch.firstInstance = static_cast<uint32_t>(instanceData.size());
            batch.instanceCount = 0;
            for (int pi : grp.partIndices) {
                const auto& part = charParts[pi];
                if (!part.active) continue;
                CharacterInstanceData data;
                data.offset = part.offset;
                data.scale  = part.scale;
                data.color  = part.color;
                instanceData.push_back(data);
                batch.instanceCount++;
            }
            if (batch.instanceCount > 0) batches.push_back(batch);
        }
    };
    for (auto* charPtr : instancedCharacters) batchParts(charPtr);
    if (instanceData.empty()) return;

    // Upload the shared (single, host-visible) character instance buffer. If both the
    // reflection pass and the main pass run this frame they upload byte-identical data (same
    // character state, same batch offsets), so the redundant memcpy is harmless — both draws
    // read the same final buffer contents at GPU execution time.
    vulkanDevice->updateCharacterInstanceBuffer(instanceData);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vulkanDevice->bindCharacterInstanceBuffer(commandBuffer);
    vulkanDevice->bindDescriptorSets(currentFrame, renderPipeline->getInstancedCharacterLayout());

    for (const auto& batch : batches) {
        struct PushConsts { glm::mat4 model; glm::mat4 viewProj; glm::vec4 bakedLight; } pushConsts;
        pushConsts.model = batch.model;
        pushConsts.viewProj = viewProj;
        pushConsts.bakedLight = batch.bakedLight;
        vkCmdPushConstants(commandBuffer, renderPipeline->getInstancedCharacterLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConsts), &pushConsts);
        vkCmdDraw(commandBuffer, 36, batch.instanceCount, 0, batch.firstInstance);
    }
}

void RenderCoordinator::renderEntities(VkCommandBuffer commandBuffer) {
    bool hasEntities = entities && !entities->empty();
    bool hasNPCs = m_npcManager && m_npcManager->getNPCCount() > 0;
    if (!hasEntities && !hasNPCs) return;

    // Collect non-instanced (standard) entities. Instanced characters (player + animated
    // NPCs) are drawn by renderInstancedCharacters(), which is shared with the mirror
    // reflection pass so characters appear in mirrors too.
    std::vector<Scene::Entity*> standardEntities;
    if (hasEntities) {
        for (const auto& entity : *entities) {
            if (!dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity.get()))
                standardEntities.push_back(entity.get());
        }
    }

    // Instanced characters in the main pass (main camera view-projection).
    const glm::mat4 mainViewProj = cachedProjectionMatrix * cachedViewMatrix;
    renderInstancedCharacters(commandBuffer, mainViewProj,
                              renderPipeline->getInstancedCharacterPipeline());

    renderPipeline->bindCharacterPipeline(commandBuffer);
    vulkanDevice->bindDescriptorSets(currentFrame, renderPipeline->getCharacterLayout());

    for (const auto& entity : standardEntities) {
        auto ragdollChar = dynamic_cast<Scene::RagdollCharacter*>(entity);
        if (ragdollChar) {
            ragdollChar->render(this);
            continue;
        }

        // Fallback generic entity rendering
        {
            glm::mat4 model = glm::mat4(1.0f);
            const glm::mat4& viewProj = mainViewProj;
            glm::vec4 color = glm::vec4(1.0f);

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
