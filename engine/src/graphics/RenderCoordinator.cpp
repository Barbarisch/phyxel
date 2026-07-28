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
#include <algorithm>
#include <cstring>
#include <cmath>
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
    // Debug knob: occlusion culling is ON by default (Phase 3);
    // PHYXEL_OCCLUSION=0 disables it at startup for A/B comparison.
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

    // Character instance buffer, shared by every character in the scene (main pass +
    // shadow pass). Sized in PARTS, not characters: the hand-authored humanoid rigs are
    // a few hundred parts, but imported creature rigs are microcube-dense — 3.0-4.7k
    // parts each — so the old 10k cap fit barely three monsters and silently dropped
    // the rest (a 27-creature scene rendered ~2). 262144 x 40 B = 10.5 MB, which holds
    // ~70 dense creatures or several hundred humanoids.
    vulkanDevice->createCharacterInstanceBuffer(kCharacterInstanceCapacity);

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

    // Initialize Water surface pipeline (see docs/WaterSystem.md, docs/WaterSystemV3.md).
    // Built against the WATER render pass — water draws after the scene pass so it can sample
    // scene colour + depth (Phase 1).
    waterPipeline = std::make_unique<WaterRenderPipeline>();
    waterPipeline->initialize(
        vulkanDevice->getDevice(),
        vulkanDevice->getPhysicalDevice(),
        postProcessor->getWaterRenderPass(),
        vulkanDevice->getSwapChainExtent(),
        vulkanDevice->getDescriptorSetLayout()
    );
    // Phase 1: water samples the shared planar-reflection texture.
    waterPipeline->setReflectionTexture(
        postProcessor->getReflectionImageView(), postProcessor->getReflectionSampler());

    // Per-cell water surface pipeline (renders the CPU sim's actual field — Phase 2).
    waterCellPipeline = std::make_unique<WaterCellRenderPipeline>();
    waterCellPipeline->initialize(
        vulkanDevice->getDevice(),
        vulkanDevice->getPhysicalDevice(),
        postProcessor->getWaterRenderPass(),
        vulkanDevice->getSwapChainExtent(),
        vulkanDevice->getDescriptorSetLayout()
    );

    // WaterSystemV3 Phase 1: point both water pipelines at the post-scene taps (half-res scene
    // colour copy for refraction, scene depth for thickness/absorption/soft shorelines). Must be
    // repeated after every swapchain resize — both images are recreated there.
    waterPipeline->setSceneTextures(
        postProcessor->getRefractionImageView(), postProcessor->getRefractionSampler(),
        postProcessor->getSceneDepthImageView(), postProcessor->getSceneDepthSampler());
    waterCellPipeline->setSceneTextures(
        postProcessor->getRefractionImageView(), postProcessor->getRefractionSampler(),
        postProcessor->getSceneDepthImageView(), postProcessor->getSceneDepthSampler());

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
    if (foliagePipeline) {
        // Kinematic foliage (F3): felled trees keep their card canopy while falling.
        foliagePipeline->initializeKinematic(
            postProcessor->getSceneRenderPass(),
            vulkanDevice->getSwapChainExtent(),
            vulkanDevice->getDescriptorSetLayout());
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

void RenderCoordinator::setWaves(float amplitude, float wavelength, float windDirectionRadians) {
    if (waterPipeline) waterPipeline->setWaves(amplitude, wavelength, windDirectionRadians);
}

glm::vec3 RenderCoordinator::waveSettings() const {
    if (!waterPipeline) return glm::vec3(0.0f);
    return glm::vec3(waterPipeline->waveAmplitude(), waterPipeline->waveLength(),
                     waterPipeline->windDirection());
}

// Is the eye under water, and how deep? (WaterSystemV3 Phase 1 item 5.)
//
// Two sources, in priority order:
//   1. The SIM, whenever the camera is inside its region. It is the only thing that knows about
//      lakes at any altitude AND is connectivity-gated — a sealed dry cavity below sea level reads
//      DRY here, where a bare "camera.y < seaLevel" test would wrongly fog it.
//   2. Sea level, as the fallback outside the region (the implicit ocean the flat plane draws).
//
// Both fade over a short band at the surface so breaking through doesn't pop.
float RenderCoordinator::cameraSubmergence(float& depthBelow) const {
    depthBelow = 0.0f;
    if (!camera) return 0.0f;
    const glm::vec3 eye = camera->getPosition();

    // ⚑GROUND: 0.35 voxel ≈ the slice a swimmer's eye passes through as it breaks the surface —
    // long enough to read as a fade, short enough to feel immediate.
    const float BAND = 0.35f;

    if (m_waterManager) {
        const glm::ivec3 o = m_waterManager->origin();
        const glm::ivec3 d = m_waterManager->dims();
        const bool inRegion = eye.x >= o.x && eye.x < o.x + d.x &&
                              eye.y >= o.y && eye.y < o.y + d.y &&
                              eye.z >= o.z && eye.z < o.z + d.z;
        if (inRegion) {
            // Fill fraction matters: a cell holding 0.4 mass has its surface 0.4 of the way up, so
            // the eye is only submerged below that height.
            const float here = m_waterManager->massAtWorld(eye);
            if (here <= 0.0f) return 0.0f;
            const float cellFloor = std::floor(eye.y);
            depthBelow = std::max(0.0f, (cellFloor + std::min(here, 1.0f)) - eye.y);
            // If this cell is full there may be more water stacked above; the true depth drives how
            // dark/blue the fog gets, so walk up while the column stays wet.
            if (here >= 0.999f) {
                for (float y = cellFloor + 1.0f; y < static_cast<float>(o.y + d.y); y += 1.0f) {
                    const float m = m_waterManager->massAtWorld(glm::vec3(eye.x, y + 0.5f, eye.z));
                    if (m <= 0.0f) break;
                    depthBelow = (y + std::min(m, 1.0f)) - eye.y;
                    if (m < 0.999f) break;
                }
            }
            return glm::clamp(depthBelow / BAND, 0.0f, 1.0f);
        }
    }

    if (m_waterEnabled) {   // implicit ocean outside the sim region
        depthBelow = m_seaLevel - eye.y;
        if (depthBelow <= 0.0f) { depthBelow = 0.0f; return 0.0f; }
        return glm::clamp(depthBelow / BAND, 0.0f, 1.0f);
    }
    return 0.0f;
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

        // LEVEL 2.5: Occlusion culling (chunk visibility graph) — removes
        // frustum-visible chunks that are hidden behind solid chunks. ON by default
        // (docs/LargeWorldScalePlan.md Phase 3); conservative, so no false holes.
        if (m_occlusionCullingEnabled) {
            applyOcclusionCulling(cameraPos, cameraFrustum);
        }

        // Render only the visible chunks
        for (size_t chunkIndex : visibleChunkIndices) {
            const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
            
            // Bind this chunk's instance buffer
            VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
            VkDeviceSize instanceOffsets[] = {chunk->getInstanceBindOffset()};  // 4.3 A2: arena span offset (0 legacy)
            vkCmdBindVertexBuffers(vulkanDevice->getCommandBuffer(currentFrame), 1, 1, instanceBuffers, instanceOffsets);
            
            // Set chunk origin as push constants for world positioning
            glm::ivec3 worldOrigin = chunk->getWorldOrigin();
            glm::vec3 chunkBaseOffset = camera->relativeTo(glm::dvec3(worldOrigin));  // camera-relative (docs/CameraRelativeRendering.md)
            glm::vec3 chunkBaseAbs = glm::vec3(worldOrigin);  // exact absolute (varied-hash seed)

            // Push constants with debug mode if enabled
            if (debugModeEnabled) {
                vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset, debugVisualizationMode, chunkBaseAbs);
            } else {
                vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset, chunkBaseAbs);
            }

            // Draw this chunk's static geometry
            // LEVEL 3: Face culling already applied (only visible faces in buffer)
            // LEVEL 4: Face-direction bucketing (Phase 3) — submit only the faceID
            // ranges that can point toward the camera; the rest would be culled by the
            // rasterizer anyway (a +X face is never visible from its -X side). Falls
            // back to a full draw if the ranges are stale (chunk mid-remesh) or the
            // debug view wants everything (debug pipeline may render two-sided).
            const auto& dirRanges = chunk->getFaceDirRanges();
            if (!s_faceDirCull || debugModeEnabled ||
                dirRanges[6] != chunk->getNumInstances()) {
                vulkanDevice->drawIndexed(currentFrame, vulkanDevice->chunkIndexCount(), chunk->getNumInstances());
            } else {
                const glm::vec3 mn = chunk->getMinBounds();
                const glm::vec3 mx = chunk->getMaxBounds();
                uint32_t mask = 0;  // faceID order: 0=+Z 1=-Z 2=+X 3=-X 4=+Y 5=-Y
                if (cameraPos.z > mn.z - 0.5f) mask |= 1u << 0;
                if (cameraPos.z < mx.z + 0.5f) mask |= 1u << 1;
                if (cameraPos.x > mn.x - 0.5f) mask |= 1u << 2;
                if (cameraPos.x < mx.x + 0.5f) mask |= 1u << 3;
                if (cameraPos.y > mn.y - 0.5f) mask |= 1u << 4;
                if (cameraPos.y < mx.y + 0.5f) mask |= 1u << 5;
                int d = 0;
                while (d < 6) {
                    if (!(mask & (1u << d))) { ++d; continue; }
                    int e = d;
                    while (e + 1 < 6 && (mask & (1u << (e + 1)))) ++e;
                    const uint32_t first = dirRanges[d];
                    const uint32_t count = dirRanges[e + 1] - first;
                    if (count) vulkanDevice->drawIndexed(currentFrame, vulkanDevice->chunkIndexCount(), count, first);
                    d = e + 1;
                }
            }
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
                          glm::vec3(origin.x, origin.y, origin.z),
                          chunk->getGrassBindOffset() });
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
                // True iff EVERY 32x32 column in the rect actually RENDERS near-field
                // geometry: some chunk there has uploaded faces (numInstances > 0). The
                // contract must be "suppress only what the near field visibly draws" —
                // earlier tests keyed on DATA presence ("any chunk at any Y", then "any
                // non-uniform chunk") and both lied whenever meshing lagged streaming
                // (chunks resident but never meshed render NOTHING), which dropped far
                // tiles over invisible terrain and opened sky holes at the seam (user
                // repro 2026-07-18: 3,650 resident / 1,932 meshed at the pose). A column
                // with real surface always ends up with instances once meshed; until
                // then its far tile keeps covering, and z-buffering handles the overlap.
                const int cx0 = int(std::floor(minXZ.x / 32.0f));
                const int cx1 = int(std::floor((maxXZ.x - 1) / 32.0f));
                const int cz0 = int(std::floor(minXZ.y / 32.0f));
                const int cz1 = int(std::floor((maxXZ.y - 1) / 32.0f));
                for (int cz = cz0; cz <= cz1; ++cz) {
                    for (int cx = cx0; cx <= cx1; ++cx) {
                        bool rendered = false;
                        for (int cy = -4; cy <= 12 && !rendered; ++cy) {
                            const Chunk* c = cm->getChunkAtCoord(glm::ivec3(cx, cy, cz));
                            if (c && c->getNumInstances() > 0) rendered = true;
                        }
                        if (!rendered) return false;
                    }
                }
                return true;
            });
        farTerrainManager->configure(*chunkManager->getStreamingGenerator());
    }

    // Covered-tile suppression is gated to the guaranteed-complete interior of the
    // near field (see FarTerrainManager::setNearFieldRadius). Cheap; set per frame so
    // a runtime loadRadius change tracks.
    if (chunkManager) farTerrainManager->setNearFieldRadius(chunkManager->loadDistance);

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

    farTerrainPipeline->setCameraWorld(glm::dvec3(cameraPos));  // camera-relative rendering
    farTerrainPipeline->render(vulkanDevice->getCommandBuffer(currentFrame),
                               vulkanDevice->getDescriptorSet(currentFrame), draws);
}

void RenderCoordinator::renderFoliage() {
    // Leaf cards ride on the chunks that survived static-geometry culling (visibleChunkIndices).
    // The params().radius bound (default 512u — generous, past today's inclusion distance) is now
    // ENFORCED (Phase 3): it was declared but never applied, which would make leaf-card cost scale
    // with render distance. Within the radius trees keep their leaves; beyond it the mid/far-field
    // LOD representation owns trees (Phase 5).
    if (!foliagePipeline || !foliagePipeline->params().enabled || !chunkManager) return;
    if (visibleChunkIndices.empty()) return;

    const glm::vec3 cameraPos = camera->getPosition();
    const float radius = foliagePipeline->params().radius + 27.8f;  // chunk half-diagonal pad
    const float radiusSq = radius * radius;

    std::vector<FoliageRenderPipeline::ChunkDraw> draws;
    draws.reserve(visibleChunkIndices.size());
    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (!chunk || chunk->getFoliageCount() == 0) continue;
        glm::vec3 center = (chunk->getMinBounds() + chunk->getMaxBounds()) * 0.5f;
        if (glm::dot(center - cameraPos, center - cameraPos) > radiusSq) continue;
        glm::ivec3 origin = chunk->getWorldOrigin();
        draws.push_back({ chunk->getFoliageBuffer(), chunk->getFoliageCount(),
                          glm::vec3(origin.x, origin.y, origin.z),
                          chunk->getFoliageBindOffset() });
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

void RenderCoordinator::setGrassParams(float radius, float bladeHeight, float windStrength, int bladesPerVoxel,
                                       int bladeStyle, float pushStrength) {
    if (!grassPipeline) return;
    auto& p = grassPipeline->params();
    if (radius       >= 0.0f) p.radius        = radius;
    if (bladeHeight  >= 0.0f) p.bladeHeight   = bladeHeight;
    if (windStrength >= 0.0f) p.windStrength  = windStrength;
    if (bladesPerVoxel > 0)   p.bladesPerVoxel = static_cast<uint32_t>(bladesPerVoxel);
    if (bladeStyle == 0 || bladeStyle == 1) p.bladeStyle = static_cast<uint32_t>(bladeStyle);
    if (pushStrength >= 0.0f) p.pushStrength  = pushStrength;
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
        VkDeviceSize instanceOffsets[] = {chunk->getInstanceBindOffset()};  // 4.3 A2: arena span offset (0 legacy)
        vkCmdBindVertexBuffers(cmd, 1, 1, instanceBuffers, instanceOffsets);

        glm::ivec3 worldOrigin = chunk->getWorldOrigin();
        glm::vec3 chunkBaseOffset = camera->relativeTo(glm::dvec3(worldOrigin));  // camera-relative (docs/CameraRelativeRendering.md)
        vulkanDevice->pushConstants(frameIndex, renderPipeline->getGraphicsLayout(), chunkBaseOffset, glm::vec3(worldOrigin));

        vulkanDevice->drawIndexed(frameIndex, 36, chunk->getNumInstances());  // 36-index cube: OIT/reflection/mirror keep both windings
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
        VkDeviceSize instanceOffsets[] = {chunk->getInstanceBindOffset()};  // 4.3 A2: arena span offset (0 legacy)
        vkCmdBindVertexBuffers(vulkanDevice->getCommandBuffer(frameIndex), 1, 1, instanceBuffers, instanceOffsets);

        glm::ivec3 worldOrigin = chunk->getWorldOrigin();
        glm::vec3 chunkBaseOffset = camera->relativeTo(glm::dvec3(worldOrigin));  // camera-relative (docs/CameraRelativeRendering.md)
        vulkanDevice->pushConstants(frameIndex, renderPipeline->getGraphicsLayout(), chunkBaseOffset, glm::vec3(worldOrigin));
        vulkanDevice->drawIndexed(frameIndex, 36, chunk->getNumInstances());  // 36-index cube: OIT/reflection/mirror keep both windings
        lastFrameStats.reflectionDrawCalls++;
    }

    // Draw instanced characters (player + animated NPCs) into the reflection target so they
    // appear in mirrors. Uses the reflected view-projection (clippedProj * reflectedView) and
    // the FRONT_BIT reflection pipeline (the reflected view's det=-1 flips winding). The
    // shared character buffer is uploaded once per frame by buildCharacterFrameData().
    glm::mat4 reflViewProj = clippedProj * reflectedView;
    renderInstancedCharacters(vulkanDevice->getCommandBuffer(frameIndex), reflViewProj,
                              renderPipeline->getReflectionInstancedCharacterPipeline(),
                              CharacterPassVisibility::All);

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
        VkDeviceSize instanceOffsets[] = {chunk->getInstanceBindOffset()};  // 4.3 A2: arena span offset (0 legacy)
        vkCmdBindVertexBuffers(cmd, 1, 1, instanceBuffers, instanceOffsets);

        glm::ivec3 worldOrigin = chunk->getWorldOrigin();
        glm::vec3 chunkBaseOffset = camera->relativeTo(glm::dvec3(worldOrigin));  // camera-relative (docs/CameraRelativeRendering.md)
        vulkanDevice->pushConstants(frameIndex, renderPipeline->getMirrorPipelineLayout(), chunkBaseOffset, glm::vec3(worldOrigin));
        vulkanDevice->drawIndexed(frameIndex, 36, chunk->getNumInstances());  // 36-index cube: OIT/reflection/mirror keep both windings
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
void RenderCoordinator::applyOcclusionCulling(const glm::vec3& cameraPos,
                                              const Utils::Frustum& cameraFrustum) {
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

    // The walk is bounded to a NEAR-FIELD sphere, not the full inclusion distance:
    // chunk-granularity occlusion only ever wins in interiors/caves near the camera,
    // and the air volume inside a large-render-distance frustum is enormous — an
    // unbounded air flood at 2048u view distance was a measured 360ms/frame (3 FPS).
    // Chunks beyond the bound are simply KEPT (never occlusion-culled).
    constexpr float kOcclusionMaxDist = 512.0f;
    const float occlusionBound = std::min(chunkInclusionDistance, kOcclusionMaxDist);
    // Hard budget on visited coords: if the walk still explodes (open vista at the
    // bound), bail out and keep everything — occlusion is an optimization, never
    // worth a frame hitch or a false hole.
    constexpr size_t kMaxOcclusionNodes = 20000;

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

    // Early-exit target: once every frustum-visible chunk (within the bound) has been
    // reached, further air-flooding cannot change the result. On open terrain this
    // fires almost immediately (everything is reachable) — the flood only keeps
    // going while some chunk remains unreached, which is exactly the cave/interior
    // case where the walk is naturally small. Without this, the bounded air flood
    // still cost ~4.7ms/frame in Debug for ZERO culling on open vistas.
    // Only within-bound chunks participate in the exit count on BOTH sides — a
    // beyond-bound chunk is kept unconditionally by the filter below, so counting
    // one as "reached" would let the walk exit while a within-bound chunk was
    // still unreached (a false hole).
    std::unordered_set<int64_t> withinBound;
    for (size_t idx : visibleChunkIndices) {
        const Chunk* c = chunkManager->chunks[idx].get();
        glm::vec3 ctr = (c->getMinBounds() + c->getMaxBounds()) * 0.5f;
        if (glm::length(ctr - cameraPos) <= occlusionBound)
            withinBound.insert(packCoord(toCoord(glm::vec3(c->getWorldOrigin()))));
    }
    const size_t visibleInBound = withinBound.size();
    size_t visibleReached = withinBound.count(packCoord(camCoord)) ? 1 : 0;

    bool budgetExceeded = false;
    while (!q.empty()) {
        if (visibleReached >= visibleInBound) break;  // everything visible is reachable — done
        if (reached.size() > kMaxOcclusionNodes) { budgetExceeded = true; break; }
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
            // Absent/air coords are pass-through (they can't occlude): the BFS must
            // cross open sky between the camera and terrain, otherwise a camera two
            // chunks above ground would cull the whole world. Bounded by the
            // near-field sphere + frustum (see occlusionBound above).
            if (!coordToIdx.count(key)) {
                glm::vec3 nCenter = glm::vec3(ncoord * 32) + glm::vec3(16.0f);
                if (glm::length(nCenter - cameraPos) > occlusionBound) continue;
                Utils::AABB naabb(glm::vec3(ncoord * 32), glm::vec3(ncoord * 32) + glm::vec3(32.0f));
                if (!cameraFrustum.intersects(naabb)) continue;
            } else if (withinBound.count(key)) {
                ++visibleReached;
            }
            reached.insert(key);
            q.push({ncoord, d ^ 1});               // entered neighbor via opposite face
        }
    }
    if (budgetExceeded) return;  // keep the full visible set — never trade holes/hitches for culling

    // Keep only frustum-visible chunks the BFS reached; chunks beyond the near-field
    // occlusion bound are always kept (the walk never extends that far).
    const size_t before = visibleChunkIndices.size();
    std::vector<size_t>& kept = m_occKept;
    kept.clear();
    kept.reserve(before);
    for (size_t idx : visibleChunkIndices) {
        const Chunk* c = chunkManager->chunks[idx].get();
        glm::vec3 center = (c->getMinBounds() + c->getMaxBounds()) * 0.5f;
        if (glm::length(center - cameraPos) > occlusionBound) { kept.push_back(idx); continue; }
        glm::ivec3 coord = toCoord(glm::vec3(c->getWorldOrigin()));
        if (reached.count(packCoord(coord))) kept.push_back(idx);
    }
    m_lastOcclusionCulled = static_cast<int>(before - kept.size());
    visibleChunkIndices.swap(kept);
}

// D1c: light-frustum cull the shadow pass. Default OFF — measured to give NO perf win (the fitted
// shadow volume legitimately contains ~all chunks); kept as an opt-in for future cascade work.
bool RenderCoordinator::s_shadowFrustumCull = false;

// Phase 3 face-direction bucketing: ON by default; /api/debug/face_dir_cull for A/B.
bool RenderCoordinator::s_faceDirCull = true;

void RenderCoordinator::renderShadowPass(VkCommandBuffer commandBuffer, const glm::mat4& lightSpaceMatrix,
                                         const glm::vec3& cullCenter, float cullRadius) {
    if (!shadowMap) return;

    // NOTE (Phase 3): face-direction bucketing is NOT applicable to the shadow pass.
    // The 36-index draw makes every instance rasterize BOTH windings on its plane, so
    // even toward-light faces write shadow depth — and terrain TOP faces are the
    // primary occluders at high sun. A direction split here measurably shifted
    // shadows (clean A/B pixel diff 0.33% >8/255); the main pass is where the
    // bucketing win lives (single-winding 6-index quads → the skip is exact).

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
             VkDeviceSize instanceOffsets[] = {chunk->getInstanceBindOffset()};  // 4.3 A2: arena span offset (0 legacy)
             vkCmdBindVertexBuffers(commandBuffer, 1, 1, instanceBuffers, instanceOffsets);

             // Push constants
             struct ShadowPushConsts {
                 glm::mat4 lightSpaceMatrix;
                 glm::vec3 chunkBaseOffset;
             } pushConsts;

             pushConsts.lightSpaceMatrix = lightSpaceMatrix;
             glm::ivec3 worldOrigin = chunk->getWorldOrigin();
             pushConsts.chunkBaseOffset = camera->relativeTo(glm::dvec3(worldOrigin));  // camera-relative

             vkCmdPushConstants(commandBuffer, shadowMap->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);

             // Draw
             // 36-index cube REQUIRED here: the shadow pipeline front-culls (ShadowMap.cpp:429,
             // renders back faces of closed casters). A 6-index quad has ONE winding → front-culled →
             // the face casts no shadow. Do NOT use chunkIndexCount() in the shadow pass.
             // (And do NOT direction-bucket here — see the note at the top of this function.)
             vkCmdDrawIndexed(commandBuffer, 36, chunk->getNumInstances(), 0, 0, 0);
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
    if (shadowMap->getCharacterShadowPipeline() != VK_NULL_HANDLE && !m_charBatches.empty()
        && m_shadowCharactersEnabled) {
        // Batches + the instance upload come from buildCharacterFrameData(), which ran
        // once before this pass. This used to re-walk every character in the world and
        // re-upload a byte-identical buffer. Draw only the light-frustum subset — note
        // that is NOT the camera subset: an off-screen character can cast into view.
        GPU_PROFILE_SCOPE(gpuProfiler.get(), commandBuffer, "Character Shadows");
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shadowMap->getCharacterShadowPipeline());
        vulkanDevice->bindCharacterInstanceBuffer(commandBuffer);

        struct CharShadowPC { glm::mat4 model; glm::mat4 lightSpaceMatrix; } charPC;
        charPC.lightSpaceMatrix = lightSpaceMatrix;
        for (const auto& batch : m_charBatches) {
            if (batch.charIndex < 0 || !m_charVisibleShadow[batch.charIndex]) continue;
            charPC.model = batch.model;
            vkCmdPushConstants(commandBuffer, shadowMap->getCharacterShadowLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(charPC), &charPC);
            vkCmdDraw(commandBuffer, 36, batch.instanceCount, 0, batch.firstInstance);
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
                // camera-relative: translation column -> (world - camera), double subtract
                kinPC.modelMatrix[3] = glm::vec4(
                    glm::vec3(glm::dvec3(glm::vec3(kinPC.modelMatrix[3])) - glm::dvec3(camera->getPosition())), 1.0f);
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
        // camera-relative: shader subtracts cameraWorld from the absolute GPU-buffer positions
        struct DynShadowPC { glm::mat4 lightSpaceMatrix; glm::vec4 cameraWorld; } dynPC;
        dynPC.lightSpaceMatrix = lightSpaceMatrix;
        dynPC.cameraWorld = glm::vec4(camera->getPosition(), 0.0f);
        vkCmdPushConstants(commandBuffer, shadowMap->getDynamicShadowLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(dynPC), &dynPC);
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
        // Shadow casters clip to the SAME foliage radius as the visible pass (a card
        // that is distance-culled from view shouldn't cast either), on top of the
        // shadow-sphere cull.
        const glm::vec3 camPos = camera ? camera->getPosition() : cullCenter;
        const float folRadius = foliagePipeline->params().radius + 27.8f;
        const float folRadiusSq = folRadius * folRadius;
        std::vector<FoliageRenderPipeline::ChunkDraw> foliageDraws;
        for (const auto& chunk : chunkManager->chunks) {
            if (!chunk || chunk->getFoliageCount() == 0) continue;
            glm::vec3 chunkCenter = (chunk->getMinBounds() + chunk->getMaxBounds()) * 0.5f;
            if (glm::length(chunkCenter - cullCenter) > cullRadius + 160.0f) continue;
            if (glm::dot(chunkCenter - camPos, chunkCenter - camPos) > folRadiusSq) continue;
            glm::ivec3 origin = chunk->getWorldOrigin();
            foliageDraws.push_back({ chunk->getFoliageBuffer(), chunk->getFoliageCount(),
                                     glm::vec3(origin.x, origin.y, origin.z),
                                     chunk->getFoliageBindOffset() });
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
        // WaterSystemV3 Phase 1: the refraction image and the scene depth image are both recreated
        // by postProcessor->resize(), so water's set-1 descriptors must be re-pointed or it samples
        // freed views. Also rebuild the water pipelines: their viewport/scissor are static state
        // baked from the old extent (the water render pass itself is size-independent and survives).
        if (waterPipeline) {
            waterPipeline->recreatePipeline(postProcessor->getWaterRenderPass(),
                                            vulkanDevice->getSwapChainExtent());
            waterPipeline->setSceneTextures(
                postProcessor->getRefractionImageView(), postProcessor->getRefractionSampler(),
                postProcessor->getSceneDepthImageView(), postProcessor->getSceneDepthSampler());
        }
        if (waterCellPipeline) {
            waterCellPipeline->recreatePipeline(postProcessor->getWaterRenderPass(),
                                                vulkanDevice->getSwapChainExtent());
            waterCellPipeline->setSceneTextures(
                postProcessor->getRefractionImageView(), postProcessor->getRefractionSampler(),
                postProcessor->getSceneDepthImageView(), postProcessor->getSceneDepthSampler());
        }
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
        // WaterSystemV3 Phase 1: the refraction image and the scene depth image are both recreated
        // by postProcessor->resize(), so water's set-1 descriptors must be re-pointed or it samples
        // freed views. Also rebuild the water pipelines: their viewport/scissor are static state
        // baked from the old extent (the water render pass itself is size-independent and survives).
        if (waterPipeline) {
            waterPipeline->recreatePipeline(postProcessor->getWaterRenderPass(),
                                            vulkanDevice->getSwapChainExtent());
            waterPipeline->setSceneTextures(
                postProcessor->getRefractionImageView(), postProcessor->getRefractionSampler(),
                postProcessor->getSceneDepthImageView(), postProcessor->getSceneDepthSampler());
        }
        if (waterCellPipeline) {
            waterCellPipeline->recreatePipeline(postProcessor->getWaterRenderPass(),
                                                vulkanDevice->getSwapChainExtent());
            waterCellPipeline->setSceneTextures(
                postProcessor->getRefractionImageView(), postProcessor->getRefractionSampler(),
                postProcessor->getSceneDepthImageView(), postProcessor->getSceneDepthSampler());
        }
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
        // WaterSystemV3 Phase 1: the refraction image and the scene depth image are both recreated
        // by postProcessor->resize(), so water's set-1 descriptors must be re-pointed or it samples
        // freed views. Also rebuild the water pipelines: their viewport/scissor are static state
        // baked from the old extent (the water render pass itself is size-independent and survives).
        if (waterPipeline) {
            waterPipeline->recreatePipeline(postProcessor->getWaterRenderPass(),
                                            vulkanDevice->getSwapChainExtent());
            waterPipeline->setSceneTextures(
                postProcessor->getRefractionImageView(), postProcessor->getRefractionSampler(),
                postProcessor->getSceneDepthImageView(), postProcessor->getSceneDepthSampler());
        }
        if (waterCellPipeline) {
            waterCellPipeline->recreatePipeline(postProcessor->getWaterRenderPass(),
                                                vulkanDevice->getSwapChainExtent());
            waterCellPipeline->setSceneTextures(
                postProcessor->getRefractionImageView(), postProcessor->getRefractionSampler(),
                postProcessor->getSceneDepthImageView(), postProcessor->getSceneDepthSampler());
        }
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
    // CAMERA-RELATIVE RENDERING (docs/CameraRelativeRendering.md): the view matrix is
    // rotation-only with the eye at the ORIGIN, and every world position handed to the GPU
    // below is (world - cameraPos), subtracted in doubles. At continental coordinates
    // (~60k units) the old eye-at-world lookAt cancelled catastrophically in world->clip,
    // re-rolling contested edge pixels per frame (character speckle, merge-seam dashes).
    if (camera) {
        cachedViewMatrix = camera->getRelativeViewMatrix();
    }
    glm::mat4 view = cachedViewMatrix;
    glm::mat4 proj = cachedProjectionMatrix;

    // True world-space camera position (doubles for all CPU-side subtraction).
    glm::vec3 cameraPos = camera->getPosition();
    const glm::dvec3 camWorld(cameraPos);
    // (world - camera) helper: double subtract, truncate last.
    auto rel = [&camWorld](const glm::dvec3& worldP) { return glm::vec3(worldP - camWorld); };

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
    // NOTE (camera-relative): lightSpaceMatrix maps CAMERA-RELATIVE world -> light clip
    // (shadow-pass inputs are relative offsets too). shadowCullCenter stays ABSOLUTE
    // world — renderShadowPass culls against absolute chunk origins.
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

        // With the relative view, corners (and center) come out CAMERA-RELATIVE.
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

        shadowCullCenter = glm::vec3(glm::dvec3(center) + camWorld);   // cull in ABSOLUTE world
        shadowCullRadius = radius;

        glm::vec3 lightDir = glm::normalize(sunDirection);
        glm::vec3 up = (std::abs(lightDir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        // Pull the light back past the sphere so casters between the sun and the frustum (e.g. a
        // tall pillar just off-screen toward the sun) still register in the depth map.
        const float kCasterBack = 120.0f;

        // Texel snapping (anti shadow-crawl, user-reported jitter while moving): the
        // sphere center follows the camera CONTINUOUSLY, so the shadow volume — and
        // every shadow edge — shifts by sub-texel amounts each frame and shimmers.
        // Quantize the center's light-space XY to whole shadow-map texels so camera
        // translation moves the volume in exact texel steps (edges stay put). The
        // sphere fit already handles rotation invariance; this handles translation.
        // The snap frame must be WORLD-ANCHORED (rotation-only, origin at the world
        // origin): snapping in a frame built around the center itself is a no-op,
        // because the center always maps to the same local point in its own frame.
        const float kShadowMapSize = 4096.0f;  // matches the ShadowMap resolution in the ctor
        const float texelSize = (2.0f * radius) / kShadowMapSize;
        glm::mat4 lightRot = glm::lookAt(glm::vec3(0.0f), lightDir, up);
        // CAMERA-RELATIVE CAVEAT: `center` is camera-relative here, but the snap MUST quantize
        // in the ABSOLUTE world frame — snapping relative coords re-anchors the grid to the
        // moving camera and the crawl returns. Re-anchor through doubles: absolute center,
        // snap in the light-rotation frame (values ~60k but texel 0.05-0.1 >> the 4 mm float
        // ULP there — use doubles anyway so the floor() is exact), back to relative.
        {
            const glm::dmat4 lightRotD(lightRot);
            glm::dvec3 centerAbs = glm::dvec3(center) + camWorld;
            glm::dvec3 centerLS = glm::dvec3(lightRotD * glm::dvec4(centerAbs, 1.0));
            centerLS.x = std::floor(centerLS.x / double(texelSize)) * double(texelSize);
            centerLS.y = std::floor(centerLS.y / double(texelSize)) * double(texelSize);
            centerAbs = glm::dvec3(glm::inverse(lightRotD) * glm::dvec4(centerLS, 1.0));
            center = glm::vec3(centerAbs - camWorld);          // back to camera-relative
            shadowCullCenter = glm::vec3(centerAbs);           // cull stays absolute
        }

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

    // Camera-relative rendering: hand the true camera position to pipelines that push their
    // own world transforms, BEFORE any pass records draws this frame.
    if (kinematicPipeline) kinematicPipeline->setCameraWorld(cameraPos);
    if (grassPipeline)     grassPipeline->setCameraWorld(cameraPos);
    if (foliagePipeline)   foliagePipeline->setCameraWorld(cameraPos);

    // Advance the shared wind field on the same clock the shaders scroll it with, and write
    // its state into BOTH vegetation pipelines before any pass records push constants (the
    // shadow pass below reads foliage params too) — single source of truth, no divergence.
    windSystem.tick(elapsedTime);
    if (grassPipeline)   grassPipeline->params().wind   = windSystem.state();
    if (foliagePipeline) foliagePipeline->params().wind = windSystem.state();

    // Grass interaction displacers (docs/VegetationWindPlan.md Phase 4 v1): characters within
    // the grass radius bend blades aside. Collected from the same sources the character passes
    // draw (player/animated entities + NPC characters), uploaded CAMERA-RELATIVE after
    // updateUniformBuffer (which zero-fills the arrays — nobody nearby means the shader path
    // stays entirely inert). Engine-side, so editor and standalone games share the behavior.
    // STATEFUL: each displacer carries a strength envelope (fast attack, slow eased release)
    // so grass rises back gently behind a character instead of popping upright.
    if (grassPipeline && grassPipeline->params().enabled &&
        grassPipeline->params().pushStrength > 0.0f) {
        const float dt = (m_grassDispLastTime >= 0.0f)
                             ? glm::clamp(elapsedTime - m_grassDispLastTime, 0.0f, 0.1f)
                             : 0.0f;
        m_grassDispLastTime = elapsedTime;
        const float grassRadius = grassPipeline->params().radius + 2.0f;
        const float radiusSq    = grassRadius * grassRadius;
        constexpr float kCharPushRadius = 1.1f;   // reach around a character's feet, world units
        constexpr float kAttackTau  = 0.07f;      // seconds to engage
        constexpr float kReleaseTau = 0.28f;      // seconds to ease back out

        for (auto& [key, st] : m_grassDispStates) st.present = false;
        auto consider = [&](const void* key, const glm::vec3& p) {
            if (glm::dot(p - cameraPos, p - cameraPos) > radiusSq) return;
            auto& st   = m_grassDispStates[key];
            st.pos     = p;                       // position tracks directly (motion is smooth)
            st.present = true;
        };
        if (entities) {
            for (const auto& entity : *entities)
                if (auto* ac = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity.get()))
                    consider(ac, ac->getPosition());
        }
        if (m_npcManager) {
            for (const auto& name : m_npcManager->getAllNPCNames())
                if (auto* npc = m_npcManager->getNPC(name)) consider(npc, npc->getPosition());
        }
        // Advance envelopes; departed displacers fade out in place, then drop.
        std::vector<std::pair<float, const GrassDisplacerState*>> active;
        for (auto it = m_grassDispStates.begin(); it != m_grassDispStates.end();) {
            auto& st = it->second;
            if (st.present) {
                st.envelope += (1.0f - st.envelope) * glm::min(1.0f, dt / kAttackTau);
            } else {
                st.envelope *= std::exp(-dt / kReleaseTau);
                if (st.envelope < 0.02f) { it = m_grassDispStates.erase(it); continue; }
            }
            active.emplace_back(glm::dot(st.pos - cameraPos, st.pos - cameraPos), &st);
            ++it;
        }
        if (!active.empty()) {
            if (active.size() > 16) {   // keep the 16 nearest to the camera
                std::partial_sort(active.begin(), active.begin() + 16, active.end(),
                                  [](const auto& a, const auto& b) { return a.first < b.first; });
                active.resize(16);
            }
            glm::vec4 displacers[16];
            glm::vec4 aux[16];
            const int n = static_cast<int>(active.size());
            for (int i = 0; i < n; ++i) {
                displacers[i] = glm::vec4(
                    glm::vec3(glm::dvec3(active[i].second->pos) - glm::dvec3(cameraPos)),
                    kCharPushRadius);
                aux[i] = glm::vec4(active[i].second->envelope, 0.0f, 0.0f, 0.0f);
            }
            vulkanDevice->setGrassDisplacers(currentFrame, displacers, aux, n);
        }
    }
    
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
    
    // Cull, sort and batch every character ONCE for the whole frame — the shadow pass,
    // the main pass and the mirror pass all consume this (docs/CharacterPipelineScaling.md).
    // Must run before the shadow pass, which is the first consumer.
    buildCharacterFrameData(cachedProjectionMatrix * cachedViewMatrix, lightSpaceMatrix);

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
            // Kinematic foliage rides the same object set — rebuild its instances too.
            if (foliagePipeline) foliagePipeline->rebuildKinematicBuffer(m_kinematicObjects->getObjects());
        }
        if (!m_kinematicObjects->getObjects().empty()) {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "KinematicVoxels");
            kinematicPipeline->render(
                vulkanDevice->getCommandBuffer(currentFrame),
                m_kinematicObjects->getObjects(),
                vulkanDevice->getDescriptorSet(currentFrame)
            );
            // Falling canopies: card-rendered leaves on moving fragments (F3).
            if (foliagePipeline) {
                foliagePipeline->renderKinematic(
                    vulkanDevice->getCommandBuffer(currentFrame),
                    vulkanDevice->getDescriptorSet(currentFrame),
                    m_kinematicObjects->getObjects());
            }
        }
    }

    // Water is NO LONGER drawn here — it moved to its own pass after the scene pass
    // (renderWaterPass(), called from drawFrame) so it can sample the scene color + depth it is
    // blending over: refraction, depth-based absorption, soft shorelines. See
    // docs/WaterSystemV3.md Phase 1.

    // Mirror surface pass (inside scene render pass, after all opaque/entity geometry)
    if (hasMirrorVoxels && renderPipeline->getMirrorPipeline() != VK_NULL_HANDLE) {
        renderMirrorGeometry(currentFrame);
    }

    // Game HUD / custom UI moved to the post-scene OVERLAY pass below, so water (which now
    // also draws after the scene pass) cannot paint over it. See docs/WaterSystemV3.md Phase 1.

    // End Scene Render Pass
    } // End Scene Pass Scope
    postProcessor->endSceneRenderPass(vulkanDevice->getCommandBuffer(currentFrame));

    // SSAO pass (samples the depth buffer written by scene pass)
    {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "SSAO");
        postProcessor->renderSSAO(vulkanDevice->getCommandBuffer(currentFrame), cachedProjectionMatrix);
    }

    // ---------------------------------------------------------------------------------------
    // WATER + OVERLAY pass (WaterSystemV3 Phase 1). Runs AFTER the scene pass so water can
    // sample the scene it blends over (refraction / depth absorption / soft shorelines) — a
    // render pass cannot sample the attachment it writes, which is why this is a separate pass.
    // Color LOADs the finished scene; depth is bound READ-ONLY (both water pipelines already
    // run depthWriteEnable=FALSE), so terrain still occludes water exactly as before.
    //
    // The HUD rides along at the END of this pass — it used to be last in the scene pass, and
    // water drawing afterwards would have covered it. Pipelines built against the scene pass are
    // render-pass-COMPATIBLE with this one (identical attachment formats/counts), so nothing is
    // rebuilt.
    // ---------------------------------------------------------------------------------------
    const bool drawWaterPlane = m_waterEnabled && waterPipeline;
    const bool drawWaterCells = m_waterManager && waterCellPipeline &&
                                !m_waterManager->surfaceCells().empty();
    if (drawWaterPlane || drawWaterCells || m_uiSystem) {
        // Snapshot the scene colour for refraction BEFORE the pass begins (outside any pass).
        if (drawWaterPlane || drawWaterCells) {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "WaterRefractCapture");
            postProcessor->captureRefraction(vulkanDevice->getCommandBuffer(currentFrame));
        }
        postProcessor->beginWaterRenderPass(vulkanDevice->getCommandBuffer(currentFrame));

        if (drawWaterPlane) {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Water");
            waterPipeline->render(
                vulkanDevice->getCommandBuffer(currentFrame),
                vulkanDevice->getDescriptorSet(currentFrame),
                *camera,
                cachedProjectionMatrix,
                m_seaLevel,
                2.0f * maxChunkRenderDistance,
                vulkanDevice->getSwapChainExtent(),
                m_waterReflectionActive
            );
        }
        if (drawWaterCells) {
            GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "WaterCells");
            waterCellPipeline->render(
                vulkanDevice->getCommandBuffer(currentFrame),
                vulkanDevice->getDescriptorSet(currentFrame),
                *camera,
                cachedProjectionMatrix,
                m_waterManager->surfaceCells(),
                vulkanDevice->getSwapChainExtent()
            );
        }

        // Underwater fog (WaterSystemV3 Phase 1 item 5) — AFTER the surfaces so it also fogs the
        // underside of the water above the camera, but BEFORE the HUD so the HUD stays readable.
        if (drawWaterPlane) {
            float depthBelow = 0.0f;
            const float submergence = cameraSubmergence(depthBelow);
            if (submergence > 0.0f) {
                GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "WaterUnderwater");
                waterPipeline->renderUnderwater(
                    vulkanDevice->getCommandBuffer(currentFrame),
                    vulkanDevice->getDescriptorSet(currentFrame),
                    *camera, cachedProjectionMatrix,
                    submergence, depthBelow,
                    vulkanDevice->getSwapChainExtent());
            }
        }

        // Game HUD / custom UI (non-ImGui) — on top of all geometry AND water, into the
        // offscreen image. Shows in the editor viewport AND is carried to the swapchain by
        // post-process for standalone builds. See docs/HudSystem.md.
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

        postProcessor->endWaterRenderPass(vulkanDevice->getCommandBuffer(currentFrame));
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

const RenderCoordinator::CharacterBlob&
RenderCoordinator::getCharacterBlob(const Scene::RagdollCharacter* ch, int lod) {
    CharacterBlob& blob = m_charBlobs[ch];
    if (blob.version == ch->partsVersion() && blob.lod == lod) return blob;

    blob.version = ch->partsVersion();
    blob.lod     = lod;
    blob.instances.clear();
    blob.groupOrder.clear();
    blob.groupSpans.clear();

    const auto& charParts = ch->getParts();
    if (lod > 0) {
        const auto& level = ch->getLodLevel(lod);
        for (const auto& range : level.groups) {
            const uint32_t local = static_cast<uint32_t>(blob.groupOrder.size());
            const uint32_t start = static_cast<uint32_t>(blob.instances.size());
            for (uint32_t k = 0; k < range.count; ++k) {
                const auto& lp = level.parts[range.start + k];
                CharacterInstanceData d;
                d.offset = lp.offset; d.scale = lp.scale; d.color = lp.color;
                d.boneIndex = local;
                blob.instances.push_back(d);
            }
            blob.groupOrder.push_back(range.boneGroupId);
            blob.groupSpans.push_back({start, range.count});
        }
    } else {
        for (const auto& grp : ch->getPartGroups()) {
            if (grp.partIndices.empty()) continue;
            const uint32_t local = static_cast<uint32_t>(blob.groupOrder.size());
            const uint32_t start = static_cast<uint32_t>(blob.instances.size());
            for (int pi : grp.partIndices) {
                const auto& p = charParts[pi];
                if (!p.active) continue;
                CharacterInstanceData d;
                d.offset = p.offset; d.scale = p.scale; d.color = p.color;
                d.boneIndex = local;
                blob.instances.push_back(d);
            }
            const uint32_t count = static_cast<uint32_t>(blob.instances.size()) - start;
            if (count > 0) {
                blob.groupOrder.push_back(grp.boneGroupId);
                blob.groupSpans.push_back({start, count});
            }
        }
    }
    return blob;
}

void RenderCoordinator::buildCharacterFrameData(const glm::mat4& cameraViewProj,
                                                const glm::mat4& lightSpaceMatrix) {
    const auto buildStart = std::chrono::high_resolution_clock::now();
    struct BuildTimer {
        std::chrono::high_resolution_clock::time_point t0;
        double& out;
        ~BuildTimer() {
            out = std::chrono::duration<double, std::milli>(
                      std::chrono::high_resolution_clock::now() - t0).count();
        }
    } buildTimer{buildStart, m_charStats.buildMs};

    m_charBatches.clear();
    m_charDrawsMain.clear();
    m_charBoneTransforms.clear();
    m_charVisibleMain.clear();
    m_charVisibleShadow.clear();
    m_charStats = CharacterRenderStats{};

    const bool hasEntities = entities && !entities->empty();
    const bool hasNPCs = m_npcManager && m_npcManager->getNPCCount() > 0;
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

    m_charStats.considered = static_cast<uint32_t>(instancedCharacters.size());

    // --- Visibility ------------------------------------------------------------
    // Cull against BOTH frusta separately: a character behind the camera can still
    // cast a shadow into view, so the shadow set is not a subset of the main set.
    Utils::Frustum cameraFrustum, lightFrustum;
    cameraFrustum.extractFromMatrix(cameraViewProj);
    lightFrustum.extractFromMatrix(lightSpaceMatrix);

    const float cullDistSq = m_charCullDistance * m_charCullDistance;

    struct Candidate {
        Scene::RagdollCharacter* ch;
        float distSq;
        bool  mainVisible;
        bool  shadowVisible;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(instancedCharacters.size());

    for (auto* ch : instancedCharacters) {
        // Camera-relative throughout: the frusta come from camera-relative matrices, and
        // relativeTo() does the subtraction in doubles so this stays exact at continental
        // coordinates (docs/CameraRelativeRendering.md).
        const glm::vec3 rel = camera->relativeTo(glm::dvec3(ch->getPosition()));
        const float distSq = glm::dot(rel, rel);
        if (distSq > cullDistSq) { ++m_charStats.culled; continue; }

        const bool inMain   = cameraFrustum.intersects(rel, kCharacterCullRadius);
        const bool inShadow = lightFrustum.intersects(rel, kCharacterCullRadius);
        if (!inMain && !inShadow) { ++m_charStats.culled; continue; }

        candidates.push_back({ch, distSq, inMain, inShadow});
    }
    if (candidates.empty()) return;

    // Nearest-first, so if the budget IS exhausted the characters that disappear are
    // the far ones. Before this, drop order was NPC-map iteration order — the creature
    // in front of you could vanish while one behind you rendered.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.distSq < b.distSq; });

    m_charVisibleMain.assign(candidates.size(), 0);
    m_charVisibleShadow.assign(candidates.size(), 0);

    // Reused across frames and reserved to the running high-water mark. This vector holds
    // one entry PER PART (100 characters = 102,400), so the old reserve(4096) meant ~5
    // reallocations and multi-MB copies every single frame.
    std::vector<CharacterInstanceData>& instanceData = m_charInstanceScratch;
    instanceData.clear();
    if (instanceData.capacity() < m_charInstanceHighWater)
        instanceData.reserve(m_charInstanceHighWater);

    // A batch whose firstInstance lands past the buffer draws from stale memory, so the
    // whole character vanishes with no error anywhere. Reserve each character's slice
    // all-or-nothing: splitting one across the cap renders half a creature, which reads
    // as a worse bug than a missing one.
    // The effective budget is a SOFT cap that can never exceed the physical buffer, so a
    // project (or a repro) can lower it at runtime without reallocating GPU memory.
    const uint32_t instanceCapacity =
        std::min(m_charInstanceCapacity, vulkanDevice->getMaxCharacterInstances());

    // Candidates are sorted nearest-first, so once the budget is exhausted the remaining
    // (farther) characters will almost all fail too. Optimistic batching means every
    // failure does a full batch-then-rollback, so blindly continuing is expensive:
    // measured at 1024 characters with a 256-character budget, the 768 doomed characters
    // tripled build_ms. Allow a few retries — a single oversized creature must not
    // starve smaller ones behind it — then stop.
    constexpr uint32_t kMaxConsecutiveDrops = 4;
    uint32_t consecutiveDrops = 0;

    for (size_t ci = 0; ci < candidates.size(); ++ci) {
        if (consecutiveDrops >= kMaxConsecutiveDrops) {
            m_charStats.dropped += static_cast<uint32_t>(candidates.size() - ci);
            break;
        }
        Scene::RagdollCharacter* ch = candidates[ci].ch;
        const auto& charParts = ch->getParts();

        // Batch optimistically, then roll back if this character overran the budget.
        // The obvious alternative — pre-counting active parts — costs a second full
        // sweep of the parts array, and RagdollPart is fat (~112 B, it carries a
        // std::string), so that sweep alone touched ~11 MB per frame at 100 characters.
        // Overruns are rare, so paying for one wasted character beats paying for a
        // second pass over every character every frame.
        const size_t instanceMark = instanceData.size();
        const size_t batchMark    = m_charBatches.size();

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

        const size_t boneMark = m_charBoneTransforms.size();

        // Part-count LOD by distance. Bone transforms always come from the
        // full-resolution groups (the animation path only updates those); the LOD level
        // supplies decimated offset/scale/color for the same bone groups.
        const int lod = lodForDistanceSq(candidates[ci].distSq);
        const CharacterBlob& blob = getCharacterBlob(ch, lod);

        // Per-frame work is now only the bone matrices — one per group, ~20 per
        // character — appended in the blob's group order so a local index + boneBase
        // resolves correctly.
        const uint32_t boneBase = static_cast<uint32_t>(m_charBoneTransforms.size());

        // blob.groupOrder is always a SUBSEQUENCE of getPartGroups() order (both LOD and
        // full-res blobs are built by walking that list), so the two can be matched in
        // lockstep. An id->group map here cost 1024 unordered_map allocations per frame.
        bool boneOk = true;
        {
            const auto& groups = ch->getPartGroups();
            size_t gi = 0;
            for (int gid : blob.groupOrder) {
                while (gi < groups.size() && groups[gi].boneGroupId != gid) ++gi;
                if (gi >= groups.size() || groups[gi].partIndices.empty()) { boneOk = false; break; }
                const auto& first = charParts[groups[gi].partIndices[0]];
                // Camera-relative rendering (docs/CameraRelativeRendering.md): the GPU sees
                // (world - camera) so bone transforms stay float-exact at continental
                // coordinates — this is THE fix for the per-voxel character speckle.
                m_charBoneTransforms.push_back(
                    glm::translate(glm::mat4(1.0f), camera->relativeTo(glm::dvec3(first.worldPos)))
                    * glm::mat4_cast(first.worldRot));
                ++gi;
            }
        }

        if (boneOk && !blob.instances.empty()) {
            // The instance payload is static, so this is a bulk copy rather than a
            // per-part gather out of the fat RagdollPart array. insert() copies into
            // uninitialized storage; resize()+memcpy would value-initialize all 186k
            // elements first and then immediately overwrite them.
            const size_t base = instanceData.size();
            instanceData.insert(instanceData.end(),
                                blob.instances.begin(), blob.instances.end());

            // Per-group spans for the shadow pass (which still draws per bone group).
            // Precomputed in the blob — deriving them here would rescan every instance.
            for (size_t g = 0; g < blob.groupSpans.size(); ++g) {
                const auto& span = blob.groupSpans[g];
                CharacterBatch b;
                b.model         = m_charBoneTransforms[boneBase + g];
                b.bakedLight    = charLight;
                b.firstInstance = static_cast<uint32_t>(base + span.first);
                b.instanceCount = span.second;
                b.charIndex     = static_cast<int>(ci);
                m_charBatches.push_back(b);
            }
        }

        if (!boneOk) {
            instanceData.resize(instanceMark);
            m_charBatches.resize(batchMark);
            m_charBoneTransforms.resize(boneMark);
            continue;
        }

        if (instanceData.size() > instanceCapacity ||
            m_charBoneTransforms.size() > vulkanDevice->getMaxCharacterBones()) {
            instanceData.resize(instanceMark);      // capacity retained, no realloc
            m_charBatches.resize(batchMark);
            m_charBoneTransforms.resize(boneMark);
            ++m_charStats.dropped;
            ++consecutiveDrops;
            continue;
        }
        consecutiveDrops = 0;

        // All of this character's parts are contiguous, and each carries its own bone
        // index — so the main pass draws the whole character in one call.
        const uint32_t charInstances =
            static_cast<uint32_t>(instanceData.size() - instanceMark);
        if (charInstances > 0) {
            CharacterDraw draw;
            draw.firstInstance = static_cast<uint32_t>(instanceMark);
            draw.instanceCount = charInstances;
            draw.bakedLight    = charLight;
            draw.charIndex     = static_cast<int>(ci);
            draw.boneBase      = boneBase;
            m_charDrawsMain.push_back(draw);
        }

        const uint32_t groupsAdded = static_cast<uint32_t>(m_charBatches.size() - batchMark);
        m_charVisibleMain[ci]   = candidates[ci].mainVisible   ? 1 : 0;
        m_charVisibleShadow[ci] = candidates[ci].shadowVisible ? 1 : 0;
        // Main pass = 1 draw per character; shadow pass is still 1 per bone group.
        if (candidates[ci].mainVisible)   { ++m_charStats.drawnMain;   m_charStats.drawCallsMain   += 1; }
        if (candidates[ci].shadowVisible) { ++m_charStats.drawnShadow; m_charStats.drawCallsShadow += groupsAdded; }
    }

    if (m_charStats.dropped > 0) {
        static uint32_t s_lastDropped = 0;
        if (m_charStats.dropped != s_lastDropped) {
            s_lastDropped = m_charStats.dropped;
            LOG_WARN_FMT("RenderCoordinator",
                "Character instance buffer full — " << m_charStats.dropped << " of "
                << candidates.size() << " visible characters not rendered (capacity "
                << instanceCapacity << " parts). Raise the character instance budget.");
        }
    }
    if (instanceData.empty()) return;

    m_charStats.partsBatched = static_cast<uint32_t>(instanceData.size());
    m_charInstanceHighWater  = std::max(m_charInstanceHighWater, instanceData.size());

    // ONE upload per frame, shared by the shadow pass, the main pass and the mirror
    // pass. Each pass previously rebuilt and re-uploaded byte-identical data.
    vulkanDevice->updateCharacterInstanceBuffer(instanceData);
    vulkanDevice->updateCharacterBoneBuffer(m_charBoneTransforms);
}

void RenderCoordinator::renderInstancedCharacters(VkCommandBuffer commandBuffer,
        const glm::mat4& viewProj, VkPipeline pipeline, CharacterPassVisibility visibility) {
    if (m_charDrawsMain.empty()) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vulkanDevice->bindCharacterInstanceBuffer(commandBuffer);
    vulkanDevice->bindDescriptorSets(currentFrame, renderPipeline->getInstancedCharacterLayout());

    // One draw per CHARACTER — each instance looks its own bone matrix up in the SSBO,
    // so the ~20 per-bone-group draws this used to emit collapse into one. The instance
    // carries a LOCAL bone index and the draw supplies boneBase, which is what lets the
    // per-character instance blob be cached across frames even though the frame's bone
    // layout changes.
    struct PushConsts { glm::mat4 viewProj; glm::vec4 bakedLight; uint32_t boneBase; } pushConsts;
    pushConsts.viewProj = viewProj;

    for (const auto& draw : m_charDrawsMain) {
        // The mirror pass reflects an arbitrary view, so it takes everything batched
        // (All) rather than re-culling against a third frustum.
        if (visibility == CharacterPassVisibility::Main &&
            (draw.charIndex < 0 || !m_charVisibleMain[draw.charIndex])) continue;

        pushConsts.bakedLight = draw.bakedLight;
        pushConsts.boneBase   = draw.boneBase;
        vkCmdPushConstants(commandBuffer, renderPipeline->getInstancedCharacterLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConsts), &pushConsts);
        vkCmdDraw(commandBuffer, 36, draw.instanceCount, 0, draw.firstInstance);
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
    {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), commandBuffer, "Characters");
        gpuProfiler->beginPipelineStats(commandBuffer, GpuProfiler::STATS_SLOT_CHARACTER);
        renderInstancedCharacters(commandBuffer, mainViewProj,
                                  renderPipeline->getInstancedCharacterPipeline(),
                                  CharacterPassVisibility::Main);
        gpuProfiler->endPipelineStats(commandBuffer, GpuProfiler::STATS_SLOT_CHARACTER);
    }

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
