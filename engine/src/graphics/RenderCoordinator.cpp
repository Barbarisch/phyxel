#include "graphics/DepthConvention.h"
#include "graphics/RenderCoordinator.h"

#include "core/LodChunkMesh.h"
#include "core/WorldStorage.h"
#include "core/LodPyramidService.h"
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
#include "graphics/FarTreeRenderPipeline.h"
#include "graphics/TreeLodMeshRegistry.h"
#include "graphics/TreeLodRenderPipeline.h"
#include "graphics/TreeSpeciesTable.h"
#include "core/MaterialRegistry.h"
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
#include "core/WorldGenerator.h"
#include "core/HydrologyMap.h"
#include "core/WaterProfile.h"   // v4 W1: per-body appearance profile + hydrology texture packing
#include "core/Chunk.h"
#include "core/Subcube.h"
#include "core/TemplateLodChain.h"
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
    // 8192² (was 4096²): the shadow DISTANCE went 160 -> 420 u, which stretches the same
    // map over ~2.6x the extent. Doubling the linear resolution keeps texel density at
    // ~0.125 u/texel (vs 0.100 before) — a 25% coarsening for 2.6x the reach, which PCSS's
    // variable penumbra absorbs. Costs 256 MB of D32 and 4x the depth-raster fill; the
    // shadow pass's dominant cost is per-DRAW submission, not fill (RenderDensityPlan §2d).
    shadowMap = std::make_unique<ShadowMap>(vulkanDevice, 8192, 8192);
    shadowMap->initialize();

    // Pass shadow map resources to VulkanDevice for descriptor updates
    vulkanDevice->setShadowMapResources(shadowMap->getDepthImageView(), shadowMap->getSampler());

    // Near shadow cascade (docs/NearShadowCascade.md): 4096² fitted to ~40 u = 0.0195 u/texel,
    // where a grass blade's 0.080 u shadow proxy spans 4 texels and resolves as a blade —
    // against the mid map's 0.1125 u texel it is 0.71 texel and rasterizes as random noise
    // (measured: toggling grass castShadows changed 40.1% of the view as unstructured blobs).
    shadowMapNear = std::make_unique<ShadowMap>(vulkanDevice, 4096, 4096);
    if (shadowMapNear->initialize()) {
        vulkanDevice->setShadowMapNearResources(shadowMapNear->getDepthImageView(),
                                                shadowMapNear->getSampler());
    } else {
        LOG_ERROR("RenderCoordinator", "Near shadow cascade init failed — running mid-map only");
        shadowMapNear.reset();
    }

    // Far shadow cascade: 4096² over ~1600 u so the LOD band (far tiles + tree meshes +
    // structure proxies) stops rendering unshadowed past the mid map's 420 u.
    shadowMapFar = std::make_unique<ShadowMap>(vulkanDevice, 4096, 4096);
    if (shadowMapFar->initialize()) {
        vulkanDevice->setShadowMapFarResources(shadowMapFar->getDepthImageView(),
                                               shadowMapFar->getSampler());
    } else {
        LOG_ERROR("RenderCoordinator", "Far shadow cascade init failed — LOD band unshadowed");
        shadowMapFar.reset();
    }

    // Trigger descriptor set update to bind the shadow map(s)
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

    // Initialize Water surface pipeline (see docs/Water.md, docs/Water.md).
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
    } else if (shadowMap) {
        // Shadow-caster variant: grass darkens the ground it stands on. Same procedural
        // blade cutout as the visible pass, so a depth-only pass can't stamp solid
        // rectangles into the map.
        // Built against the NEAR cascade map (docs/NearShadowCascade.md): grass casts ONLY
        // there, and this pipeline bakes a STATIC viewport — built against the 8192 mid map
        // but drawn into the 4096 near framebuffer, blade depth landed at 2x the intended
        // UVs (the first post-cascade sweep measured NO SHADOW at every width because of it).
        ShadowMap* grassCasterMap = shadowMapNear ? shadowMapNear.get() : shadowMap.get();
        grassPipeline->initializeShadow(
            grassCasterMap->getRenderPass(),
            VkExtent2D{grassCasterMap->getWidth(), grassCasterMap->getHeight()});
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
    // Far-cascade caster variant — against the FAR map's render pass/extent (static-viewport rule).
    if (farTerrainPipeline && shadowMapFar) {
        farTerrainPipeline->initializeShadow(
            shadowMapFar->getRenderPass(),
            VkExtent2D{shadowMapFar->getWidth(), shadowMapFar->getHeight()},
            vulkanDevice->getDescriptorSetLayout());
    }
    farTerrainManager = std::make_unique<FarTerrainManager>(
        vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice());

    // Far-tree impostors ride the same tiles (world-look A1 rethink — trees to ~2 km).
    farTreePipeline = std::make_unique<FarTreeRenderPipeline>();
    if (!farTreePipeline->initialize(
            vulkanDevice->getDevice(),
            vulkanDevice->getPhysicalDevice(),
            postProcessor->getSceneRenderPass(),
            vulkanDevice->getSwapChainExtent(),
            vulkanDevice->getDescriptorSetLayout())) {
        LOG_ERROR("RenderCoordinator", "Failed to initialize FarTreeRenderPipeline");
        farTreePipeline.reset();
    }

    // WRv2 M2: instanced tree LOD meshes for the mid band (real voxel geometry, same atlas
    // and lighting as far terrain). The registry activates once the application wires a
    // template provider; until then the whole band stays on cards.
    treeLodPipeline = std::make_unique<TreeLodRenderPipeline>();
    if (!treeLodPipeline->initialize(
            vulkanDevice->getDevice(),
            vulkanDevice->getPhysicalDevice(),
            postProcessor->getSceneRenderPass(),
            vulkanDevice->getSwapChainExtent(),
            vulkanDevice->getDescriptorSetLayout())) {
        LOG_ERROR("RenderCoordinator", "Failed to initialize TreeLodRenderPipeline");
        treeLodPipeline.reset();
    }
    if (treeLodPipeline && shadowMapFar) {
        treeLodPipeline->initializeShadow(
            shadowMapFar->getRenderPass(),
            VkExtent2D{shadowMapFar->getWidth(), shadowMapFar->getHeight()},
            vulkanDevice->getDescriptorSetLayout());
    }
    treeLodMeshes = std::make_unique<TreeLodMeshRegistry>(
        vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice());
    treeLodMeshes->setMaterialResolver([](const std::string& material, int faceID) -> uint16_t {
        return Core::MaterialRegistry::instance().getTextureIndex(material, faceID);
    });
}

void RenderCoordinator::setTreeTemplateProvider(
    std::function<const VoxelTemplate*(const std::string&)> p) {
    if (treeLodMeshes) treeLodMeshes->setTemplateProvider(std::move(p));
}

void RenderCoordinator::setFarTreesEnabled(bool on) {
    if (treeLodPipeline) treeLodPipeline->params().enabled = on;
    if (farTreePipeline) farTreePipeline->params().enabled = on;
}

void RenderCoordinator::setStructureLodTargets(
    const std::vector<std::tuple<std::string, glm::ivec3, glm::ivec3>>& targets) {
    // RECONCILING, not insert-only (the original insert-only version kept a demolished or
    // moved structure's proxy rendering forever — half of the 2026-08-05 stale-proxy bug).
    std::unordered_set<std::string> incoming;
    incoming.reserve(targets.size());
    for (const auto& [uuid, mn, mx] : targets) {
        incoming.insert(uuid);
        auto it = structureLod.find(uuid);
        if (it != structureLod.end()) {
            if (it->second.mn == mn && it->second.mx == mx) continue;
            // Moved/rotated (AABB changed): the snapshot is stale — rebuild from scratch.
            structureLodRetiring.push_back(std::move(it->second));
            structureLod.erase(it);
        }
        StructureLod e;
        e.mn = mn;
        e.mx = mx;
        structureLod.emplace(uuid, std::move(e));
    }
    for (auto it = structureLod.begin(); it != structureLod.end();) {
        if (!incoming.count(it->first)) {
            structureLodRetiring.push_back(std::move(it->second));
            it = structureLod.erase(it);
        } else {
            ++it;
        }
    }
}

void RenderCoordinator::retireStructureLodEntry(StructureLod& e) {
    StructureLodGrave g;
    for (auto& gl : e.lv) {
        if (gl.vertexBuffer != VK_NULL_HANDLE) g.bufs.push_back({gl.vertexBuffer, gl.vertexMemory});
        if (gl.indexBuffer != VK_NULL_HANDLE) g.bufs.push_back({gl.indexBuffer, gl.indexMemory});
        gl = {};
    }
    if (e.inst != VK_NULL_HANDLE) {
        g.bufs.push_back({e.inst, e.instMem});
        e.inst = VK_NULL_HANDLE;
        e.instMem = VK_NULL_HANDLE;
    }
    if (!g.bufs.empty()) structureLodGraveyard.push_back(std::move(g));
}

void RenderCoordinator::tickStructureLodGraveyard() {
    // Land retiring entries whose async build finished (a running future must not be
    // destroyed — its dtor blocks), then age out the buffer graveyard.
    for (auto it = structureLodRetiring.begin(); it != structureLodRetiring.end();) {
        if (it->job.valid() &&
            it->job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        if (it->job.valid()) it->job.get();   // discard the CPU meshes
        retireStructureLodEntry(*it);
        it = structureLodRetiring.erase(it);
    }
    for (auto it = structureLodGraveyard.begin(); it != structureLodGraveyard.end();) {
        if (--it->framesLeft <= 0) {
            if (treeLodMeshes)
                for (auto& [b, m] : it->bufs) treeLodMeshes->destroyBuffer(b, m);
            it = structureLodGraveyard.erase(it);
        } else {
            ++it;
        }
    }
}

RenderCoordinator::StructureGateResult RenderCoordinator::structureGateProbe(
    const glm::ivec3& mn, const glm::ivec3& mx, const glm::vec3& cameraPos,
    float fadeGateEnd,
    const std::function<bool(const glm::ivec3&)>& hasRenderedGeometryAt) {
    // The tree tile gate's escapes, ported (tileHandoffMinFade in renderFarTerrain — the
    // gate that earned them twice). The original structure gate had NONE of them: a single
    // mid-Y plane where every column voted. Consequences, each red-proven in
    // StructureLodGateTest before this implementation:
    //   - a settlement wider than the residency radius had permanent out-of-range columns
    //     → readiness pinned 0 → minFade 1 → the low-poly proxy rendered SOLID on top of
    //     the real buildings at point-blank range, forever (the 2026-08-05 user bug);
    //   - a tall tower's mid-Y chunk is often pure air (renders no instances) → same veto;
    //   - a structure entirely beyond the band still "voted", overriding the distance fade.
    StructureGateResult res;
    const float midY = (float(mn.y) + float(mx.y)) * 0.5f;
    for (int x = mn.x; x <= mx.x && res.ready; x += 31) {
        for (int z = mn.z; z <= mx.z && res.ready; z += 31) {
            // Columns beyond the fade band don't vote: the per-vertex distance fade already
            // keeps far portions solid, and out-of-residency columns must not veto the part
            // the player is standing in.
            if (glm::length(glm::vec3(float(x), midY, float(z)) - cameraPos) > fadeGateEnd)
                continue;
            ++res.votes;
            // Full Y-span probe (32u chunk stride, one band of slack each way — tree-gate
            // parity): "rendered" means ANY chunk in the column's span has geometry; a
            // mid-plane-only probe reads air chunks on tall structures.
            bool any = false;
            for (int y = mn.y - 32; y <= mx.y + 31 && !any; y += 32)
                any = hasRenderedGeometryAt(glm::ivec3(x, y, z));
            res.ready = any;
        }
    }
    return res;
}

void RenderCoordinator::tickStructureLod(std::vector<TreeLodRenderPipeline::MeshDraw>& meshDraws,
                                         const glm::vec3& cameraPos) {
    if (!chunkManager || !treeLodMeshes) return;
    tickStructureLodGraveyard();

    bool extractedThisFrame = false;   // extraction is main-thread: at most one per frame
    for (auto& [uuid, e] : structureLod) {
        // ---- 1. Extract the structure's voxels once its chunks are resident. -----------
        if (e.state == 0 && !extractedThisFrame) {
            bool allResident = true;
            Core::TemplateLodChain::MicroSoup soup;
            std::unordered_map<std::string, uint16_t> matIds;
            auto matId = [&](const std::string& m) -> uint16_t {
                auto it = matIds.find(m);
                if (it != matIds.end()) return it->second;
                const uint16_t id = uint16_t(soup.materials.size());
                soup.materials.push_back(m);
                matIds.emplace(m, id);
                return id;
            };
            for (int x = e.mn.x; x <= e.mx.x && allResident; ++x)
                for (int y = e.mn.y; y <= e.mx.y && allResident; ++y)
                    for (int z = e.mn.z; z <= e.mx.z && allResident; ++z) {
                        const glm::ivec3 wp(x, y, z);
                        Chunk* chunk = chunkManager->getChunkAtFast(wp);
                        if (!chunk) { allResident = false; break; }
                        const glm::ivec3 lp = wp - chunk->getWorldOrigin();
                        const glm::ivec3 rel = wp - e.mn;   // structure-local voxel
                        if (const Cube* c =
                                static_cast<const Chunk*>(chunk)->getCubeAt(lp)) {
                            const uint16_t m = matId(c->getMaterialName());
                            for (int mx2 = 0; mx2 < 9; ++mx2)
                                for (int my = 0; my < 9; ++my)
                                    for (int mz = 0; mz < 9; ++mz)
                                        soup.micros.push_back(
                                            {rel * 9 + glm::ivec3(mx2, my, mz), m});
                        }
                        for (Subcube* s : chunk->getSubcubesAt(lp)) {
                            if (!s) continue;
                            const uint16_t m = matId(s->getMaterialName());
                            const glm::ivec3 sp = s->getLocalPosition();
                            for (int mx2 = 0; mx2 < 3; ++mx2)
                                for (int my = 0; my < 3; ++my)
                                    for (int mz = 0; mz < 3; ++mz)
                                        soup.micros.push_back(
                                            {rel * 9 + sp * 3 + glm::ivec3(mx2, my, mz), m});
                        }
                    }
            if (allResident && !soup.micros.empty()) {
                extractedThisFrame = true;
                e.state = 1;
                e.job = std::async(std::launch::async, [soup = std::move(soup)]() {
                    const auto levels = Core::TemplateLodChain::buildFromSoup(
                        soup, Core::TemplateLodChain::structureConfig());
                    std::array<TreeLodMeshRegistry::CpuMesh,
                               Core::TemplateLodChain::kLevelCount> out;
                    const auto resolve = [](const std::string& mat, int face) -> uint16_t {
                        return Core::MaterialRegistry::instance().getTextureIndex(mat, face);
                    };
                    for (size_t i = 0; i < out.size() && i < levels.size(); ++i)
                        out[i] = TreeLodMeshRegistry::buildLevelMesh(
                            levels[i], resolve, glm::vec3(-0.5f, 0.0f, -0.5f));
                    return out;
                });
            }
        }
        // ---- 2. Land finished builds (GPU upload, main thread). ------------------------
        if (e.state == 1 && e.job.valid() &&
            e.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto cpu = e.job.get();
            bool any = false;
            for (size_t i = 0; i < cpu.size(); ++i)
                any |= treeLodMeshes->uploadLevel(cpu[i], e.lv[i]);
            FarTreeInstance inst{};
            inst.localX = 0.5f;
            inst.localZ = 0.5f;
            inst.worldY = float(e.mn.y);
            inst.height = 1.0f;
            inst.canopyR = 1.0f;
            inst.packed = 0;
            e.state = (any && treeLodMeshes->createInstanceBuffer(inst, e.inst, e.instMem))
                          ? 2 : 3;
        }
        // ---- 3. Draw: same fades + residency gate as trees. ----------------------------
        e.lastDist = -1.0f;
        e.lastLevel = -1;
        e.lastMinFade = 0.0f;
        if (e.state != 2) continue;
        const glm::vec3 center = (glm::vec3(e.mn) + glm::vec3(e.mx)) * 0.5f;
        const float dist = glm::length(center - cameraPos);
        e.lastDist = dist;
        if (dist > (treeLodPipeline ? treeLodPipeline->params().bandEnd : 1600.0f)) continue;
        // Residency gate (inverse handoff): while the structure's real chunks are resident
        // the distance fade dissolves the LOD; when they evict, readiness drops and
        // minFade holds it solid — the settlement never just vanishes. The probe itself is
        // the pure structureGateProbe (test-pinned): out-of-band columns must not vote and
        // zero votes must release the proxy, or a structure wider than the load radius pins
        // minFade at 1.0 and renders SOLID on top of the real building forever (the
        // 2026-08-05 stale-proxy bug — same defect class the tree gate fixed twice).
        const float fadeGateEnd =
            treeLodPipeline ? treeLodPipeline->params().fadeNear1 : 260.0f;
        const auto gate = structureGateProbe(
            e.mn, e.mx, cameraPos, fadeGateEnd, [this](const glm::ivec3& wp) {
                const auto* c = chunkManager->getChunkAtFast(wp);
                return c && c->getNumInstances() > 0;
            });
        const float target = (gate.votes == 0 || gate.ready) ? 1.0f : 0.0f;
        e.readiness += (target - e.readiness) * 0.06f;
        // Full 6-level chain, 0-based: L0 (⅓-voxel cells) owns the band right past the
        // handoff, L5 (3-voxel) the tail. Fall back to the nearest coarser built level,
        // then the nearest finer, if the requested one is empty (tiny structures can
        // produce empty coarse levels).
        constexpr int kStructLevels = int(Core::TemplateLodChain::kLevelCount);
        int li = int(std::size(kStructureLevelDist));
        for (int i = 0; i < int(std::size(kStructureLevelDist)); ++i)
            if (dist < kStructureLevelDist[i]) { li = i; break; }
        int chosen = -1;
        for (int c = li; c < kStructLevels && chosen < 0; ++c)
            if (e.lv[size_t(c)].indexCount) chosen = c;
        for (int c = li - 1; c >= 0 && chosen < 0; --c)
            if (e.lv[size_t(c)].indexCount) chosen = c;
        if (chosen < 0) continue;
        const auto& gl = e.lv[size_t(chosen)];
        if (gl.vertexBuffer == VK_NULL_HANDLE) continue;
        e.lastLevel = chosen;
        e.lastMinFade = 1.0f - e.readiness;
        meshDraws.push_back({gl.vertexBuffer, gl.indexBuffer, gl.indexCount, e.inst, 0, 1,
                             glm::vec2(float(e.mn.x), float(e.mn.z)), 8.0f,
                             e.lastMinFade});
        // Far-cascade caster: this structure at its coarsest BUILT level (single instance,
        // cheap) so distant settlements shade the terrain around them.
        for (int c = kStructLevels - 1; c >= 0; --c) {
            if (e.lv[size_t(c)].indexCount &&
                e.lv[size_t(c)].vertexBuffer != VK_NULL_HANDLE) {
                m_cachedTreeMeshDraws.push_back(
                    {e.lv[size_t(c)].vertexBuffer, e.lv[size_t(c)].indexBuffer,
                     e.lv[size_t(c)].indexCount, e.inst, 0, 1,
                     glm::vec2(float(e.mn.x), float(e.mn.z)), 8.0f, 0.0f});
                break;
            }
        }
    }
}

std::vector<RenderCoordinator::StructureLodInfo> RenderCoordinator::structureLodReport() const {
    std::vector<StructureLodInfo> out;
    out.reserve(structureLod.size());
    for (const auto& [uuid, e] : structureLod) {
        StructureLodInfo info;
        info.uuid = uuid;
        info.mn = e.mn;
        info.mx = e.mx;
        info.state = e.state;
        info.readiness = e.readiness;
        info.lastDist = e.lastDist;
        info.lastLevel = e.lastLevel;
        info.lastMinFade = e.lastMinFade;
        out.push_back(std::move(info));
    }
    return out;
}

size_t RenderCoordinator::farTilesPending() const {
    return farTerrainManager ? farTerrainManager->pendingTiles() : 0;
}

RenderCoordinator::LodTierThresholds RenderCoordinator::lodTierThresholds() const {
    LodTierThresholds t;
    if (treeLodPipeline) {
        t.treeFadeNear0 = treeLodPipeline->params().fadeNear0;
        t.treeFadeNear1 = treeLodPipeline->params().fadeNear1;
        t.treeBandEnd   = treeLodPipeline->params().bandEnd;
    }
    if (farTreePipeline) {
        t.cardFadeFar0 = farTreePipeline->params().fadeFar0;
        t.cardFadeFar1 = farTreePipeline->params().fadeFar1;
    }
    if (grassPipeline) {
        t.grassRadius    = grassPipeline->params().radius;
        t.grassFadeRange = grassPipeline->params().fadeRange;
    }
    if (foliagePipeline) t.foliageRadius = foliagePipeline->params().radius;
    t.shadowDistance = s_shadowDistance;
    return t;
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

void RenderCoordinator::setMaxChunkRenderDistance(float distance) {
    maxChunkRenderDistance = distance;
    // The sea's wave zone has to outreach the far plane, or its taper is a ring of flattening water
    // centred on the camera (a visible vortex from above). 1.35x gives the taper somewhere no one
    // can see it, including into the screen corners where the diagonal FOV reaches furthest.
    if (waterPipeline) waterPipeline->setWaveRadius(distance * 1.35f);
}

void RenderCoordinator::setWaves(float amplitude, float wavelength, float windDirectionRadians) {
    if (!waterPipeline) return;
    const float oldDir = waterPipeline->windDirection();
    waterPipeline->setWaves(amplitude, wavelength, windDirectionRadians);
    // v4 W3: wave energy is now FETCH-limited, and fetch depends on the wind HEADING — so changing
    // direction changes every body's sea and the hydrology texture must be rebuilt. Before W3 the
    // direction only rotated the swell in the vertex shader and no CPU state depended on it.
    if (oldDir != waterPipeline->windDirection())
        m_lastHydroUploaded = reinterpret_cast<const void*>(~uintptr_t(0));
}

void RenderCoordinator::setWindSpeed(float metresPerSecond) {
    if (!waterPipeline) return;
    waterPipeline->setWindSpeed(metresPerSecond);
    // Speed drives both fetch-limited energy and Cox-Munk roughness, both baked into the texture.
    m_lastHydroUploaded = reinterpret_cast<const void*>(~uintptr_t(0));
}

float RenderCoordinator::windSpeed() const {
    return waterPipeline ? waterPipeline->windSpeed() : 0.0f;
}

glm::vec3 RenderCoordinator::waveSettings() const {
    if (!waterPipeline) return glm::vec3(0.0f);
    return glm::vec3(waterPipeline->waveAmplitude(), waterPipeline->waveLength(),
                     waterPipeline->windDirection());
}

void RenderCoordinator::setWaterLook(bool active, float turbidity, float roughness) {
    m_waterLookActive = active;
    m_waterLookTurbidity = turbidity;
    m_waterLookRoughness = roughness;
    // Force the hydrology texture to be rebuilt on the next frame. Reusing the "never uploaded"
    // sentinel (not nullptr — a null bake is a real, uploadable state: the 1×1 dry dummy) is what
    // makes the override travel the SAME path a real per-body profile will.
    m_lastHydroUploaded = reinterpret_cast<const void*>(~uintptr_t(0));
}

void RenderCoordinator::uploadGroundedWaterGrid(const std::vector<float>& rgba, int cellsX,
                                                int cellsZ, float originX, float originZ) {
    if (!waterPipeline || !vulkanDevice || cellsX <= 0 || cellsZ <= 0) return;
    if (rgba.size() < static_cast<size_t>(cellsX) * cellsZ * 4) return;   // short buffer: refuse
    // Descriptor rewrite on a possibly in-flight set — same once-per-rebind idle the bake path
    // takes. Grounded syncs are command-driven (world edit / explicit call), not per-frame.
    vkDeviceWaitIdle(vulkanDevice->getDevice());
    VkCommandBuffer oneShot = vulkanDevice->beginSingleTimeCommands();
    // cellSize -1: one voxel per cell, NEGATIVE = grounded mode (off-grid is dry; see the header).
    waterPipeline->recordHydrologyUpload(oneShot, rgba.data(), cellsX, cellsZ,
                                         originX, originZ, -1.0f);
    vulkanDevice->endSingleTimeCommands(oneShot);
    // Pin the per-frame rebind guard to "null bake already uploaded" so the next frame does NOT
    // overwrite this grid with the 1×1 dry sentinel (worlds using this path have hydro == null).
    m_lastHydroUploaded = nullptr;
}

void RenderCoordinator::updateSpanWaterGrid() {
    // ── THE SANE BASELINE (user order 2026-08-04): where there is water in the world, the engine
    // renders water — and nothing else renders any. ONE placement rule at every distance: chunk
    // spans over resident chunks, off-grid DRY (the grounded shader mode). Coverage follows chunk
    // RESIDENCY — exactly the rule terrain itself obeys — so water is visible precisely where its
    // ground is visible, and the CONTENT at any world position is span truth regardless of the
    // viewer (the camera invariant: residency picks what is shown, never what exists).
    // The coarse bake no longer places any water on screen; it remains generation's connectivity
    // hint only. Streaming baked worlds only — authored worlds keep their existing paths
    // (grounded via water_ground_sync, or the implicit flat sea).
    if (!waterPipeline || !chunkManager || !vulkanDevice) return;
    const WorldGenerator* gen = chunkManager->getStreamingGenerator();
    const Phyxel::HydrologyMap* hydro = gen ? gen->hydrology() : nullptr;
    if (!hydro) return;
    if (chunkManager->chunkMap.empty()) return;

    // Rebuild when chunks stream in/out, rate-limited (the fine-window lesson: a stride-only or
    // build-once grid captures an empty post-teleport chunk map as "all dry" forever).
    const size_t chunkCount = chunkManager->chunkMap.size();
    if (m_spanGridCooldown > 0) --m_spanGridCooldown;
    if (m_spanGridChunkCount == chunkCount) return;
    if (m_spanGridCooldown > 0) return;

    // Bounds of resident chunks, clamped to a hard cap. ⚑No silent caps: if residency outruns the
    // cap the excess is logged — those chunks' terrain renders with dry water, which is exactly
    // the defect class, so the log line is the tripwire.
    glm::ivec3 lo(INT_MAX), hi(INT_MIN);
    for (const auto& [cc, chunk] : chunkManager->chunkMap) {
        lo = glm::min(lo, cc); hi = glm::max(hi, cc);
    }
    constexpr int kMaxCells = 2048;                       // 2048² cols × 16 B = 64 MB ceiling
    int minX = lo.x * 32, minZ = lo.z * 32;
    int w = (hi.x - lo.x + 1) * 32, d = (hi.z - lo.z + 1) * 32;
    if (w > kMaxCells) {
        const int cx = minX + w / 2;
        LOG_WARN("RenderCoordinator", "span water grid clamped in X ({} > {}): outer chunks render dry water", w, kMaxCells);
        minX = cx - kMaxCells / 2; w = kMaxCells;
    }
    if (d > kMaxCells) {
        const int cz = minZ + d / 2;
        LOG_WARN("RenderCoordinator", "span water grid clamped in Z ({} > {}): outer chunks render dry water", d, kMaxCells);
        minZ = cz - kMaxCells / 2; d = kMaxCells;
    }

    // The per-BODY look survives: the bake's coarse RGBA (level+energy+turbidity+roughness) is
    // built once and its G/B/A are copied per column, so W2/W3 appearance is unchanged — only
    // PLACEMENT (R and wet/dry) comes from the spans.
    std::vector<float> coarse;
    {
        Phyxel::WaterLookOverride ovr;
        ovr.active = m_waterLookActive; ovr.turbidity = m_waterLookTurbidity; ovr.roughness = m_waterLookRoughness;
        Phyxel::WaterWind wind;
        wind.speedMs = waterPipeline->windSpeed(); wind.dirRadians = waterPipeline->windDirection();
        Phyxel::buildHydroUpload(*hydro, gen->waterBodies(), ovr, coarse, wind);
    }
    const float invCell = 1.0f / hydro->cellSize();
    const int cw = hydro->cellsX(), ch = hydro->cellsZ();
    const float cox = hydro->originX(), coz = hydro->originZ();

    std::vector<float> rgba(static_cast<size_t>(w) * d * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = -1e30f; rgba[i + 1] = 1.0f; rgba[i + 2] = 0.0f; rgba[i + 3] = 1.0f;
    }
    long wet = 0;
    for (const auto& [cc, chunk] : chunkManager->chunkMap) {
        if (!chunk) continue;
        const int bx = cc.x * 32, bz = cc.z * 32;
        if (bx + 31 < minX || bx >= minX + w || bz + 31 < minZ || bz >= minZ + d) continue;
        for (const auto& s : chunk->getWaterSpans()) {
            const int gx = bx + s.x - minX, gz = bz + s.z - minZ;
            if (gx < 0 || gx >= w || gz < 0 || gz >= d) continue;
            float* px = &rgba[(static_cast<size_t>(gz) * w + gx) * 4];
            const float top = static_cast<float>(cc.y) * 32.0f + s.top;
            if (px[0] < -1e5f) {
                ++wet;
                // Look from the coarse body cell under this column (neutral where the bake is dry).
                const int ccx = static_cast<int>((static_cast<float>(bx + s.x) + 0.5f - cox) * invCell);
                const int ccz = static_cast<int>((static_cast<float>(bz + s.z) + 0.5f - coz) * invCell);
                if (ccx >= 0 && ccx < cw && ccz >= 0 && ccz < ch) {
                    const float* cp = &coarse[(static_cast<size_t>(ccz) * cw + ccx) * 4];
                    if (cp[0] > -1e5f) { px[1] = cp[1]; px[2] = cp[2]; px[3] = cp[3]; }
                }
            }
            if (top > px[0]) px[0] = top;              // topmost clip wins
        }
    }

    // Same swap discipline as the grounded path: the image size can change between rebuilds
    // (residency bounds move), which recreates the image + rewrites the descriptor — idle first.
    vkDeviceWaitIdle(vulkanDevice->getDevice());
    VkCommandBuffer oneShot = vulkanDevice->beginSingleTimeCommands();
    waterPipeline->recordHydrologyUpload(oneShot, rgba.data(), w, d,
                                         static_cast<float>(minX), static_cast<float>(minZ), -1.0f);
    vulkanDevice->endSingleTimeCommands(oneShot);
    // Pin the per-frame bake rebind guard so it never overwrites this grid.
    m_lastHydroUploaded = hydro;
    m_spanGridChunkCount = chunkCount;
    m_spanGridCooldown = 30;
    LOG_INFO("RenderCoordinator", "Span water grid: {}x{} at ({}, {}), {} wet columns, {} chunks",
             w, d, minX, minZ, wet, chunkCount);
}

glm::vec3 RenderCoordinator::waterLook() const {
    return glm::vec3(m_waterLookActive ? 1.0f : 0.0f, m_waterLookTurbidity, m_waterLookRoughness);
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
        // The submergence walk moved to WaterManager::sampleWater (small-scale Phase 4.1) so the
        // camera fog, buoyancy and wading all read the SAME facts. Two deliberate upgrades over
        // the old inline walk: sub-voxel floors raise the surface correctly, and outside the sim
        // region a bound baked table answers per-body levels (the old code jumped straight to the
        // flat sea level, wrong over an inland lake). The manager's implicit-sea flag mirrors
        // m_waterEnabled (wired at world load).
        const Core::WaterManager::WaterSample s = m_waterManager->sampleWater(eye);
        depthBelow = s.depthBelow;
        return glm::clamp(depthBelow / BAND, 0.0f, 1.0f);
    }

    if (m_waterEnabled) {   // implicit ocean when no manager exists at all
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
        cameraFrustum.extractFromMatrix(viewProjection, Utils::Frustum::ClipConvention::ReverseZeroToOne);
        
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

    // C3.3: chunks that are NOT resident, served from the persisted pyramid. Drawn here so they
    // share the bound pipeline, descriptor sets and index buffer with the resident chunks above
    // -- their InstanceData is ordinary scaleLevel==3 LOD cells, so nothing else differs.
    drawFarLodChunks(currentFrame);

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
    // C1: screen-space corrected (no-op at the reference config). updateLodView() ran at
    // the top of drawFrame(), so the scale is fresh. The +27.8 chunk half-diagonal pad is a
    // GEOMETRIC safety margin for center-distance culling and must NOT scale — shrinking it
    // below the true half-diagonal would reintroduce edge-of-chunk grass popping.
    const float radius = effectiveVegetationRadius(grassPipeline->params().radius,
                                                   screenSpaceLodScale()) + 27.8f;
    const float radiusSq = radius * radius;

    std::vector<GrassRenderPipeline::ChunkDraw> draws;
    draws.reserve(visibleChunkIndices.size());
    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (!chunk || chunk->getGrassCount() == 0) continue;

        // NO GRASS ON COARSENED CHUNKS. Grass instances are emitted during the FINE rebuild, at
        // true per-voxel surface positions, and a LOD swap does not touch them. The coarse mesh's
        // OR-occupancy rule puts its surface at or ABOVE the fine surface, so blades rooted at the
        // real ground end up INSIDE the coarse blocks — grass vanishes into terrain, or z-fights
        // its way out in patches. Skipping is correct rather than cosmetic: a coarsened chunk is
        // one whose cells no longer earn their pixels, so per-blade detail on it is meaningless.
        if (chunk->getLodLevel() != 0) continue;

        // Clip by chunk-center distance (blades themselves fade to zero height near the edge).
        glm::vec3 center = (chunk->getMinBounds() + chunk->getMaxBounds()) * 0.5f;
        float distSq = glm::dot(center - cameraPos, center - cameraPos);
        if (distSq > radiusSq) continue;

        glm::ivec3 origin = chunk->getWorldOrigin();
        // centerDist drives the pipeline's density LOD (blades/voxel fall off with distance, which
        // is what makes a 320-unit radius affordable at near-field density).
        // NEAREST-point distance, not centre. The pipeline uses this to shorten the draw, and that
        // bound must never be tighter than the shader's own per-blade density test anywhere in the
        // chunk — a blade at the near corner is up to a half-diagonal (27.7u) closer than the
        // centre, and clipping it would put the chunk seam straight back.
        constexpr float kChunkHalfDiagonal = 27.71f;   // sqrt(3) * 16
        const float nearDist = std::max(0.0f, std::sqrt(distSq) - kChunkHalfDiagonal);
        draws.push_back({ chunk->getGrassBuffer(), chunk->getGrassCount(),
                          glm::vec3(origin.x, origin.y, origin.z),
                          chunk->getGrassBindOffset(),
                          nearDist });
    }
    if (draws.empty()) return;

    grassPipeline->render(vulkanDevice->getCommandBuffer(currentFrame),
                          vulkanDevice->getDescriptorSet(currentFrame), draws);
}

void RenderCoordinator::renderFarTerrain() {
    // C1: keep the far-terrain horizon on the shared screen-space metric (no-op at the
    // reference config). Must be set before update()/computeRings() runs this frame.
    if (farTerrainManager) farTerrainManager->params().viewScale = screenSpaceLodScale();

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

    // Tree fade-in bands track the ACTUAL streaming load radius (same 3D metric): trees
    // appear exactly where fresh chunk loading stops, whatever the world's loadRadius is.
    // A fixed band left a bare ring between the residency sphere and the trees (the
    // user-reported deadzone). Slight overlap is deliberate: an instanced tree coinciding
    // with its still-resident real counterpart is the SAME template at the same spot, so
    // the double is masked; a gap is what must never exist.
    if (chunkManager) {
        // Band starts AT the load edge and stretches ~90u past it (just inside the unload
        // radius): real trees visually own everything they exist for, and the LOD dithers
        // in across the load→unload hysteresis ring where real trees are still resident on
        // zoom-out (user: "the distance that lower detail things kick in should be farther
        // out"). Safe in BOTH directions only because of the residency gate — approaching,
        // chunks past the load edge don't exist yet and minFade holds the LOD solid.
        const float load = chunkManager->loadDistance;
        const float fadeEnd = std::min(load + 90.0f, chunkManager->unloadDistance - 6.0f);
        if (treeLodPipeline) {
            treeLodPipeline->params().fadeNear0 = load;
            treeLodPipeline->params().fadeNear1 = fadeEnd;
        }
        if (farTreePipeline) {
            farTreePipeline->params().fadeNear0 = load;
            farTreePipeline->params().fadeNear1 = fadeEnd;
        }
    }

    // Per-frame lifecycle: refresh wanted set on camera movement, drain worker
    // results (budgeted uploads), evict + frame-deferred-delete out-of-range tiles.
    farTerrainManager->update(camera->getPosition());
    if (treeLodMeshes) treeLodMeshes->tick();   // finalize off-thread species builds (bounded)

    lastFrameStats.farTilesResident = int(farTerrainManager->residentTiles());
    if (!farTerrainManager->params().enabled) return;
    const auto& tiles = farTerrainManager->tileDraws();
    if (tiles.empty()) return;

    const glm::vec3 cameraPos = camera->getPosition();
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + camera->getFront(), camera->getUp());
    Utils::Frustum frustum;
    frustum.extractFromMatrix(cachedProjectionMatrix * view, Utils::Frustum::ClipConvention::ReverseZeroToOne);

    std::vector<FarTerrainRenderPipeline::TileDraw> draws;
    std::vector<FarTreeRenderPipeline::TreeDraw> treeDraws;
    std::vector<TreeLodRenderPipeline::MeshDraw> meshDraws;
    draws.reserve(tiles.size());
    // Far-cascade caster list rebuilt per frame — at COARSEST chain level only. A shadow
    // caster at 0.9 u/texel needs blob-shape depth, not L1 detail; replaying the visible
    // draws as casters made every cadence frame's shadow pass spike ~+10 ms (measured).
    m_cachedTreeMeshDraws.clear();
    lastFrameStats.farTrees = 0;
    std::fill(std::begin(lastFrameStats.farTreeMeshAnnuli),
              std::end(lastFrameStats.farTreeMeshAnnuli), 0);
    std::fill(std::begin(lastFrameStats.farTreeCardAnnuli),
              std::end(lastFrameStats.farTreeCardAnnuli), 0);
    std::fill(std::begin(lastFrameStats.farTreeMeshDrawsByLevel),
              std::end(lastFrameStats.farTreeMeshDrawsByLevel), 0);
    lastFrameStats.farTreeCardDraws = 0;
    // Residency-gated handoff (user: "lower detail trees fade out before the detailed trees
    // render... for a bit of time there is nothing there"). The distance fade assumes chunks
    // at fade range are loaded, but streaming is ASYNC — flying in, the LOD tree dissolved on
    // schedule while its real chunk was still generating. So: a tile's trees may only fade
    // out once the chunks under it actually EXIST, ramped over ~0.3s to avoid a pop when
    // they land. minFade = 1 - readiness floors the shader's dither factor.
    const float fadeGateEnd = (treeLodPipeline ? treeLodPipeline->params().fadeNear1 : 260.0f);
    auto tileHandoffMinFade = [&](const FarTerrainManager::TileDraw& t, float dMin) -> float {
        if (dMin > fadeGateEnd) return 0.0f;   // outside the fade band: distance keeps it solid
        // TERRAIN-HIDDEN tiles are the deep interior: FarTerrainManager sets that flag only
        // after its chunk-coverage test proved EVERY 32x32 column under the tile has a chunk
        // with rendered geometry. That is strictly stronger evidence than the 4-quadrant
        // probe below, so the real world demonstrably owns this ground and the LOD copy MUST
        // be free to dissolve. Without this, one failed quadrant probe pinned minFade to 1.0
        // and held LOD trees fully SOLID on top of the real ones at point-blank range —
        // user repro at (0,43,-89): doubled canopies ~30 u from the camera, 36 vs 43 FPS.
        if (t.terrainHidden) return 0.0f;
        // Ready = every IN-BAND quadrant column of the tile has a resident chunk WITH
        // RENDERED GEOMETRY in the tile's vertical span. Bare existence is not enough (an
        // unmeshed chunk renders nothing). Quadrants BEYOND the fade band don't vote:
        // coarse rings overlap one tile inward, so a 256u ring-2 tile can cover the camera
        // while its far corners legitimately sit outside residency — letting those corners
        // veto readiness froze the whole tile's trees SOLID on top of fully-resident
        // forest (user repro: pale ghost trees over the jungle, 2026-08-02). The per-
        // vertex distance fade already keeps the tile's far portions solid.
        bool ready = true;
        int votes = 0;
        const float qx[2] = {t.aabbMin.x + 16.0f, t.aabbMax.x - 16.0f};
        const float qz[2] = {t.aabbMin.z + 16.0f, t.aabbMax.z - 16.0f};
        const float midY = (t.aabbMin.y + t.aabbMax.y) * 0.5f;
        for (int ix = 0; ix < 2 && ready; ++ix) {
            for (int iz = 0; iz < 2 && ready; ++iz) {
                if (glm::length(glm::vec3(qx[ix], midY, qz[iz]) - cameraPos) > fadeGateEnd)
                    continue;   // outside the band: distance fade owns it, no vote
                ++votes;
                bool any = false;
                for (float y = t.aabbMin.y - 32.0f; y <= t.aabbMax.y + 31.0f && !any; y += 32.0f) {
                    const Chunk* c = chunkManager->getChunkAtFast(
                        glm::ivec3(int(qx[ix]), int(std::floor(y)), int(qz[iz])));
                    any = c && c->getNumInstances() > 0;
                }
                ready = any;
            }
        }
        if (votes == 0) return 0.0f;   // nothing in-band: distance fade governs entirely
        const uint64_t key = (uint64_t(uint32_t(int(t.aabbMin.x))) << 32) |
                              uint64_t(uint32_t(int(t.aabbMin.z)));
        float& h = treeHandoffReadiness[key];
        h += ((ready ? 1.0f : 0.0f) - h) * 0.06f;   // ~0.3s ramp at 60 FPS
        h = std::clamp(h, 0.0f, 1.0f);
        return 1.0f - h;
    };

    // Spread a tile's instance count across every 50u annulus its XZ extent overlaps,
    // proportionally. Bucketing at the tile CENTER aliased against the 64u tile grid — whole
    // annuli read 0 with trees plainly on screen, which is indistinguishable from a real
    // deadzone (the exact false-negative this metric exists to rule out).
    auto spreadAnnuli = [](int* arr, uint32_t count, float dmin, float dmax) {
        dmax = std::max(dmax, dmin + 1.0f);
        const int lo = std::clamp(int(dmin / 50.0f), 0, RenderStats::kTreeAnnuli - 1);
        const int hi = std::clamp(int(dmax / 50.0f), 0, RenderStats::kTreeAnnuli - 1);
        const float span = dmax - dmin;
        for (int i = lo; i <= hi; ++i) {
            const float overlap = std::min(dmax, (i + 1) * 50.0f) - std::max(dmin, i * 50.0f);
            arr[i] += int(std::lround(count * std::max(0.0f, overlap) / span));
        }
    };

    // WRv2 M2: tiles inside the mesh band draw REAL instanced voxel trees per species run;
    // tiles beyond it (and species whose mesh set isn't available) stay on cards.
    const bool meshTier = treeLodPipeline && treeLodPipeline->params().enabled &&
                          treeLodMeshes && treeLodMeshes->ready();
    const float meshBandEnd = treeLodPipeline ? treeLodPipeline->params().bandEnd : 0.0f;
    size_t speciesN = 0;
    const TreeSpecies* speciesRows = treeSpeciesTable(speciesN);

    for (const auto& t : tiles) {
        // Trees rise above the terrain AABB — cull with the extended box so a forest whose
        // ground is just below the frustum edge doesn't pop its canopies off.
        Utils::AABB aabb(t.aabbMin, t.aabbMax + glm::vec3(0.0f, t.treeMaxHeight, 0.0f));
        if (!frustum.intersects(aabb)) continue;
        // terrainHidden: interior tile fully covered by real chunks — its terrain must not
        // draw (dug-hole protection) but its TREES stay available for the zoom-out handoff.
        if (!t.terrainHidden) {
            draws.push_back(t.draw);
            lastFrameStats.farTriangles += int(t.draw.indexCount / 3);
        }
        if (t.treeCount == 0 || t.treeBuffer == VK_NULL_HANDLE) continue;
        lastFrameStats.farTrees += int(t.treeCount);

        const glm::vec3 tileCenter = (t.aabbMin + t.aabbMax) * 0.5f;
        const float tileDist =
            glm::length(glm::vec2(tileCenter.x, tileCenter.z) -
                        glm::vec2(cameraPos.x, cameraPos.z));
        // Closest/farthest XZ distance of the tile footprint — the annulus span its trees occupy.
        const glm::vec2 camXZ(cameraPos.x, cameraPos.z);
        const glm::vec2 nearPt = glm::clamp(camXZ, glm::vec2(t.aabbMin.x, t.aabbMin.z),
                                                    glm::vec2(t.aabbMax.x, t.aabbMax.z));
        const float tileDMin = glm::length(nearPt - camXZ);
        const glm::vec2 farPt(  // farthest tile corner, per axis
            (std::abs(camXZ.x - t.aabbMin.x) > std::abs(camXZ.x - t.aabbMax.x)) ? t.aabbMin.x
                                                                                : t.aabbMax.x,
            (std::abs(camXZ.y - t.aabbMin.z) > std::abs(camXZ.y - t.aabbMax.z)) ? t.aabbMin.z
                                                                                : t.aabbMax.z);
        const float tileDMax = glm::length(farPt - camXZ);
        const float minFade = tileHandoffMinFade(t, tileDMin);
        if (meshTier && tileDist < meshBandEnd && !t.treeRanges.empty()) {
            // Chain level by PER-INSTANCE distance (2026-08-05, was tile-centre): the tile's
            // distance span [tileDMin, tileDMax] picks the bracketing chain levels, one draw
            // per level in range, and the shader partitions instances between adjacent-level
            // draws with complementary dithers (levelBand). A ladder boundary is now crossed
            // one TREE at a time (crossfade), never one 128-512u TILE at a time (pop).
            // The FULL ladder, stretched long and stepped gently (user: "the drop off...
            // would continue out for a lot longer and be more gradual").
            auto levelForDist = [](float d) {
                for (int i = 0; i < 4; ++i)
                    if (d < s_treeMeshLevelDist[i]) return i + 1;
                return 5;
            };
            // A/B: with per-instance levels OFF, both brackets collapse to the tile-centre
            // level — one draw per (tile, species), tile-pop boundaries, no straddle cost.
            // DISTANCE-GATED (2026-08-06): the split pays ~2 ms of double vertex work at
            // dense poses (measured, straddle A/B), and past ~700 u a tree subtends so few
            // pixels that a level pop is invisible under haze — so the crossfade runs only
            // where it can be seen. Look-safe: the near boundaries (360/560) keep
            // per-instance dissolves; 820/1150 revert to per-tile.
            const float kSplitGateDist = 700.0f;
            const bool split = s_treePerInstanceLevels && tileDist < kSplitGateDist;
            const int liNear = split ? levelForDist(tileDMin) : levelForDist(tileDist);
            const int liFar  = split ? levelForDist(tileDMax) : liNear;
            for (int li = liNear; li <= liFar; ++li) {
                const glm::vec2 band(li <= 1 ? 0.0f : s_treeMeshLevelDist[li - 2],
                                     li >= 5 ? 3.0e8f : s_treeMeshLevelDist[li - 1]);
                for (const auto& range : t.treeRanges) {
                    const auto* gl = treeLodMeshes->level(int(range.speciesId), li);
                    if (gl) {
                        meshDraws.push_back({gl->vertexBuffer, gl->indexBuffer, gl->indexCount,
                                             t.treeBuffer, range.first, range.count,
                                             t.draw.origin,
                                             speciesRows[range.speciesId % speciesN].height,
                                             minFade, band});
                        ++lastFrameStats.farTreeMeshDrawsByLevel[li];
                        // Far-cascade caster: same instances at the COARSEST level, once per
                        // range (guarded so split tiles don't duplicate their casters).
                        if (li == liNear) {
                            if (const auto* gl5 =
                                    treeLodMeshes->level(int(range.speciesId), 5)) {
                                m_cachedTreeMeshDraws.push_back(
                                    {gl5->vertexBuffer, gl5->indexBuffer, gl5->indexCount,
                                     t.treeBuffer, range.first, range.count, t.draw.origin,
                                     speciesRows[range.speciesId % speciesN].height, 0.0f});
                            }
                        }
                    } else if (li == liNear) {
                        // No mesh set for this species (missing template) — cards cover.
                        // Once per tile (cards have no level partition).
                        treeDraws.push_back({t.treeBuffer, range.count, t.draw.origin,
                                             range.first, minFade});
                        spreadAnnuli(lastFrameStats.farTreeCardAnnuli, range.count,
                                     tileDMin, tileDMax);
                        ++lastFrameStats.farTreeCardDraws;
                    }
                }
            }
            // Annuli count instances once per tile (draw splitting must not double-count
            // the deadzone metric).
            for (const auto& range : t.treeRanges)
                if (treeLodMeshes->level(int(range.speciesId), liNear))
                    spreadAnnuli(lastFrameStats.farTreeMeshAnnuli, range.count,
                                 tileDMin, tileDMax);
        } else {
            treeDraws.push_back({t.treeBuffer, t.treeCount, t.draw.origin, 0, minFade});
            spreadAnnuli(lastFrameStats.farTreeCardAnnuli, t.treeCount, tileDMin, tileDMax);
            ++lastFrameStats.farTreeCardDraws;
        }
    }
    // NO early-out on empty terrain draws: hidden interior tiles may still have tree draws
    // (returning here silently dropped every LOD tree whenever only hidden tiles were in
    // frame — precisely the handoff moment).
    lastFrameStats.farTilesDrawn = int(draws.size());

    if (!draws.empty()) {
        farTerrainPipeline->setCameraWorld(glm::dvec3(cameraPos));  // camera-relative rendering
        farTerrainPipeline->render(vulkanDevice->getCommandBuffer(currentFrame),
                                   vulkanDevice->getDescriptorSet(currentFrame), draws);
    }

    // Structure LOD (WRv2 §8): distant settlements join the same instanced-mesh draw list.
    tickStructureLod(meshDraws, cameraPos);

    // Cache this frame's far-tile draws for the FAR shadow cascade's caster pass (which
    // records BEFORE this function runs — one frame of staleness is sub-texel out there).
    // Tree casters were collected during the tile loop above at the coarsest chain level;
    // structure proxies appended their own coarse casters in tickStructureLod.
    m_cachedFarTileDraws = draws;

    // Instanced tree LOD meshes first (mid band), then the card tail — both depth-write, so
    // the z-buffer composites them against terrain and each other.
    if (treeLodPipeline && !meshDraws.empty()) {
        treeLodPipeline->setCameraWorld(glm::dvec3(cameraPos));
        treeLodPipeline->render(vulkanDevice->getCommandBuffer(currentFrame),
                                vulkanDevice->getDescriptorSet(currentFrame), meshDraws);
    }
    if (farTreePipeline && !treeDraws.empty()) {
        farTreePipeline->setCameraWorld(glm::dvec3(cameraPos));
        farTreePipeline->render(vulkanDevice->getCommandBuffer(currentFrame),
                                vulkanDevice->getDescriptorSet(currentFrame), treeDraws);
    }
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
    // C1: screen-space corrected (no-op at the reference config); pad is geometric, unscaled.
    const float radius = effectiveVegetationRadius(foliagePipeline->params().radius,
                                                   screenSpaceLodScale()) + 27.8f;
    const float radiusSq = radius * radius;

    std::vector<FoliageRenderPipeline::ChunkDraw> draws;
    draws.reserve(visibleChunkIndices.size());
    for (size_t chunkIndex : visibleChunkIndices) {
        const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
        if (!chunk || chunk->getFoliageCount() == 0) continue;

        // NO LEAF CARDS ON COARSENED CHUNKS — and here this is a HANDOFF, not just a skip.
        //
        // Leaf voxels are `isBillboarded`: ChunkRenderManager deliberately omits their solid faces
        // from the fine mesh and emits cards instead, so at level 0 a canopy is cards and nothing
        // else. But `LodChunkMesh::volumeFromChunk` counts every visible cube/sub/microcube as
        // coverage with no billboard exclusion, so from level 1 up the canopy becomes SOLID coarse
        // cells of Leaf material. Without this gate both would draw at once: a solid green mass
        // with cards layered over it.
        //
        // Including leaves in the coarse volume is the right call and this gate is what makes it
        // safe — the solid mass IS the canopy's distant impostor, which is what the field does
        // (billboards near, solid mass far). Excluding them instead would make forests evaporate
        // into bare trunks at range. So: cards below the cut, mass above it, never both.
        // Mirrors the same rule in renderGrass(). See docs/ContinuousLodPlan.md.
        if (chunk->getLodLevel() != 0) continue;

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
                                       int bladeStyle, float pushStrength, float bladeWidthScale) {
    if (!grassPipeline) return;
    auto& p = grassPipeline->params();
    if (radius       >= 0.0f) p.radius        = radius;
    if (bladeHeight  >= 0.0f) p.bladeHeight   = bladeHeight;
    if (bladeWidthScale > 0.0f) p.bladeWidthScale = bladeWidthScale;
    if (windStrength >= 0.0f) p.windStrength  = windStrength;
    // ⚑CLAMPED TO THE LATTICE. Blades occupy one cell each of a 16x16 grid (grass_sites.glsl);
    // grass.vert indexes it with `blade & 255`, so a 257th blade would WRAP and land exactly on
    // top of blade 0 — silent, exact overlap, and the non-overlap guarantee gone with no error
    // anywhere. Clamp rather than reject: this is a debug knob, and a silently-capped 256 is far
    // easier to notice in a screenshot than a rejected request buried in a log.
    if (bladesPerVoxel > 0) {
        p.bladesPerVoxel = std::min(static_cast<uint32_t>(bladesPerVoxel),
                                    Graphics::kGrassSiteCount);
    }
    if (bladeStyle == 0 || bladeStyle == 1) p.bladeStyle = static_cast<uint32_t>(bladeStyle);
    if (pushStrength >= 0.0f) p.pushStrength  = pushStrength;
}

void RenderCoordinator::setGrassMeadowParams(float meadowScale, float meadowDetailScale,
                                             float meadowDetailWeight, float heightMin,
                                             float heightMax, float edgeTaperFloor,
                                             float edgeTaperCurve) {
    if (!grassPipeline) return;
    auto& p = grassPipeline->params();
    // Negative = leave unchanged (same convention as setGrassParams). Periods are WORLD UNITS and
    // must stay above 1 or the noise degenerates to a constant; the shader guards this too, but
    // clamping here means the value read back over the API is the value actually in use.
    if (meadowScale       > 0.0f) p.meadowScale        = std::max(meadowScale, 1.0f);
    if (meadowDetailScale > 0.0f) p.meadowDetailScale  = std::max(meadowDetailScale, 1.0f);
    if (meadowDetailWeight >= 0.0f) p.meadowDetailWeight = std::min(meadowDetailWeight, 1.0f);
    if (heightMin >= 0.0f) p.heightMin = heightMin;
    if (heightMax >= 0.0f) p.heightMax = heightMax;
    // A caller that inverts the range would otherwise get lush zones SHORTER than cropped ones,
    // which reads as a bug rather than as a setting. Order them instead of rejecting.
    if (p.heightMax < p.heightMin) std::swap(p.heightMin, p.heightMax);
    if (edgeTaperFloor >= 0.0f) p.edgeTaperFloor = std::min(edgeTaperFloor, 1.0f);
    if (edgeTaperCurve  > 0.0f) p.edgeTaperCurve = edgeTaperCurve;
}

RenderCoordinator::GrassParamSnapshot RenderCoordinator::grassParams() const {
    GrassParamSnapshot s{};
    if (!grassPipeline) return s;   // all-zero: "no grass pipeline", distinguishable from real values
    const auto& p = grassPipeline->params();
    s.enabled            = p.enabled;
    s.bladesPerVoxel     = p.bladesPerVoxel;
    s.bladeHeight        = p.bladeHeight;
    s.bladeWidthScale    = p.bladeWidthScale;
    s.radius             = p.radius;
    s.meadowScale        = p.meadowScale;
    s.meadowDetailScale  = p.meadowDetailScale;
    s.meadowDetailWeight = p.meadowDetailWeight;
    s.heightMin          = p.heightMin;
    s.heightMax          = p.heightMax;
    s.edgeTaperFloor     = p.edgeTaperFloor;
    s.edgeTaperCurve     = p.edgeTaperCurve;
    return s;
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
    //
    // visibleChunkIndices is LAST frame's cull result (renderStaticGeometry refills it
    // later in this frame), and the streaming update may have unloaded chunks since —
    // a camera teleport can shrink the list by hundreds, leaving stale out-of-range
    // indices here. Worst case of an in-range-but-swapped index is one frame with a
    // wrong mirror plane, which the next frame corrects.
    const size_t numChunks = chunkManager->chunks.size();
    for (size_t chunkIndex : visibleChunkIndices) {
        if (chunkIndex >= numChunks) continue;
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
    float reflAspect = (float)windowManager->getWidth() / (float)windowManager->getHeight();
    // REVERSE-Z: build the reflection projection with the SAME convention as the main scene, or
    // the mirror pass writes depth the scene's GREATER test rejects wholesale.
    // The old code recovered a far plane from cachedProjectionMatrix via far = B/(A+1). That is
    // undefined here by construction: the infinite matrix has A = m[2][2] = 0 and B = m[3][2] =
    // near, so it would return `near` as the far plane and collapse the reflection frustum to
    // nothing. There is no far plane to preserve any more — that is the point.
    glm::mat4 clippedProj =
        DepthConvention::infiniteReverseZPerspective(glm::radians(45.0f), reflAspect, reflNear);
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
// C1 (docs/ContinuousLodPlan.md): screen-space correction for the character LOD/cull
// thresholds. ON by default -- it is EXACTLY a no-op at the reference config (1600x900,
// fovY 45) that the 35/80/400 numbers were tuned at, and a correction elsewhere.
bool RenderCoordinator::s_screenSpaceLod = true;
// C2.1 (docs/ContinuousLodPlan.md): one multidraw per arena block for the SHADOW pass, replacing
// one draw per chunk. DEFAULT ON since 2026-08-06: with the 768-byte arena alignment the guard
// stopped firing and the path finally ran with REAL data — measured at the DenseForestPerf
// ground pose (727 resident chunks): shadow pass 11.1 -> 5.1 ms (8 interleaved pairs, every ON
// below every OFF), and pixel-verified below the scene's own noise floor (105 px vs the 120 px
// off-vs-off control, vegetation-off method). The historical "no effect" null was taken at a
// 3.4 ms operating point with nothing to recover. The stride guard (spanIsStrideAddressable)
// still protects the path; live A/B: POST /api/debug/gpu_driven_shadow.
// Evidence: docs/evidence/dense_forest_perf_20260806.txt.
bool RenderCoordinator::s_gpuDrivenShadow = true;
// C5 distance-driven LOD. DEFAULT OFF -- and the reason is arithmetic worth writing down, because
// "turn on LOD to get render distance" is the obvious wrong move here.
//
// This coarsens RESIDENT chunks only, and residency is ChunkManager::loadDistance 256 /
// unloadDistance 352. At the reference config (900 px, fovY 45) a 1-cube cell projects
// 8 px (s_lodTargetPixels) at ~136 units. So its entire working window is ~136-352 units -- a thin
// band, beyond which there are no resident chunks to coarsen. Render distance past 352 is served by
// far terrain and the far-LOD chunk path, NOT by this.
//
// Against that small win sit two real costs: the measured unbounded-fattening defect (see
// s_lodMaxLevel), and a direct conflict with grass -- that 136-352 band is exactly where the
// 320-unit grass radius lives, and renderGrass has to skip coarsened chunks (the coarse surface
// sits above the fine one and buries the blades). Enabling this by default would trade visible
// grass for a face reduction that C5's own A/B could not measure as a speedup.
//
// Keep it as the live A/B toggle it was built as: POST /api/debug/distance_lod.
bool  RenderCoordinator::s_distanceDrivenLod = false;
// C3.3: default ON. This is the tier that carries STRUCTURES and player EDITS past the residency
// radius -- far terrain is generator-only and structurally cannot show a building. Coverage is now
// driven by WorldStorage::getChunksWithLodBlobs() (a handful of rows), not a speculative volume
// scan, so there is no reach cap and the cost is proportional to what actually exists.
bool  RenderCoordinator::s_farLodChunks = true;
int   RenderCoordinator::s_farLodBudgetPerFrame = 4;
bool  RenderCoordinator::s_treePerInstanceLevels = true;
// Tree mesh ladder (level i+1 below entry i; L5 beyond) — runtime-tunable, see the header.
float RenderCoordinator::s_treeMeshLevelDist[4] = {360.0f, 560.0f, 820.0f, 1150.0f};
float RenderCoordinator::s_lodTargetPixels = 8.0f;
// Coarsest level distance LOD may select. Held at 3 (8-cube cells) rather than the ladder's 5,
// because of the MEASURED unbounded-fattening defect (ContinuousLodPlan C5): LodCell::solid() is
// `coverage > 0` and nothing dilutes it up the pyramid, so an ISOLATED thin detail -- a fence rail,
// a lone microcube -- keeps emitting a full cell quad however coarse the cell gets. Level 5 turns
// one microcube into a whole-chunk slab (288x). Level 3 bounds that to 72x on isolated detail while
// still collapsing bulk terrain, which is where the face savings actually are. Raise this only
// after the appearance tier (M2) makes quad size follow fractional coverage.
int   RenderCoordinator::s_lodMaxLevel = 3;
int   RenderCoordinator::s_lodRebuildBudgetPerFrame = 2;
float RenderCoordinator::s_forcedViewScale = 0.0f;  // 0 = derive from the live view
// Shadow draw distance (world units), runtime-tunable via POST /api/debug/shadow.
// 420 (was a hard 160): 160 was chosen for texel density against a 4096² map; the map is
// now 8192², so the reach can grow ~2.6x at ~0.125 u/texel. This is the SINGLE knob that
// trades shadow reach against both texel density AND shadow-pass draw count (the cull
// sphere grows with it), so tune it with FPS in view, not by feel.
float RenderCoordinator::s_shadowDistance = 420.0f;
// Near shadow cascade (docs/NearShadowCascade.md): 40 u at 4096² = 0.0195 u/texel — the
// density where a grass blade's shadow resolves as a blade instead of scatter noise.
bool  RenderCoordinator::s_nearShadowEnabled  = true;
float RenderCoordinator::s_nearShadowDistance = 40.0f;
// Far shadow cascade: the LOD band's shadows (docs/NearShadowCascade.md status block).
// 1600 u = the tree-mesh band end; cadence 4 = the caster pass records every 4th frame
// (a 0.9 u texel a kilometre out cannot show quarter-second staleness).
bool  RenderCoordinator::s_farShadowEnabled  = true;
float RenderCoordinator::s_farShadowDistance = 1600.0f;
int   RenderCoordinator::s_farShadowCadence  = 4;
bool  RenderCoordinator::s_shadowQuadDraw    = false;   // M5 A/B — OFF until pixel-derived

// Fit one shadow volume to the camera's view frustum, capped at maxShadowDist. Bounding-SPHERE
// fit (rotation-invariant → no shadow-edge shimmer as the camera turns), caster margin +
// caster-back pull so off-screen casters still write depth, world-anchored texel snapping
// (anti-crawl), and the non-finite guard. Extracted verbatim from drawFrame 2026-08-05 so the
// near cascade reuses the identical math — the corner build stays ANALYTICAL (camera basis +
// FOV): unprojecting through inverse(proj*view) went NaN under reverse-Z + infinite far plane
// (the 2026-08-02 no-shadows regression) and must not return.
RenderCoordinator::ShadowFit RenderCoordinator::fitShadowVolume(
    float maxShadowDist, float mapSize, const glm::dvec3& camWorld,
    const glm::vec3& sunDirection) const {
    ShadowFit fit;
    const float kNear = 0.5f;
    float a00 = cachedProjectionMatrix[0][0];
    float a11 = cachedProjectionMatrix[1][1];
    float aspect = (std::abs(a00) > 1e-6f) ? std::abs(a11 / a00) : (16.0f / 9.0f);

    const float fovY = glm::radians(camera->getFovYDegrees());
    const glm::vec3 fwd = camera->getFront();
    const glm::vec3 rgt = camera->getRight();
    const glm::vec3 upv = camera->getUp();
    glm::vec3 corners[8];
    int ci = 0;
    for (int zi = 0; zi < 2; ++zi) {
        const float d = zi ? maxShadowDist : kNear;
        const float halfH = std::tan(fovY * 0.5f) * d;
        const float halfW = halfH * aspect;
        const glm::vec3 c = fwd * d;                 // camera sits at the origin here
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                corners[ci++] = c + rgt * (x ? halfW : -halfW) + upv * (y ? halfH : -halfH);
    }

    // With the relative view, corners (and center) come out CAMERA-RELATIVE.
    glm::vec3 center(0.0f);
    for (auto& c : corners) center += c;
    center /= 8.0f;
    float radius = 0.0f;
    for (auto& c : corners) radius = std::max(radius, glm::length(c - center));
    // Casters JUST outside the view must still be recorded (edge casters sliced in/out of
    // shadow as the camera moved without this margin).
    const float kCasterMargin = 48.0f;
    radius = std::ceil(radius) + kCasterMargin;

    fit.cullCenterAbs = glm::vec3(glm::dvec3(center) + camWorld);
    fit.cullRadius = radius;

    glm::vec3 lightDir = glm::normalize(sunDirection);
    glm::vec3 up = (std::abs(lightDir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    // Pull the light back past the sphere so casters between the sun and the frustum still
    // register in the depth map.
    const float kCasterBack = 120.0f;

    // World-anchored texel snapping (anti shadow-crawl): quantize the center's light-space XY
    // to whole map texels in the ABSOLUTE world frame, through doubles so floor() is exact.
    const float texelSize = (2.0f * radius) / std::max(mapSize, 1.0f);
    glm::mat4 lightRot = glm::lookAt(glm::vec3(0.0f), lightDir, up);
    {
        const glm::dmat4 lightRotD(lightRot);
        glm::dvec3 centerAbs = glm::dvec3(center) + camWorld;
        glm::dvec3 centerLS = glm::dvec3(lightRotD * glm::dvec4(centerAbs, 1.0));
        centerLS.x = std::floor(centerLS.x / double(texelSize)) * double(texelSize);
        centerLS.y = std::floor(centerLS.y / double(texelSize)) * double(texelSize);
        centerAbs = glm::dvec3(glm::inverse(lightRotD) * glm::dvec4(centerLS, 1.0));
        center = glm::vec3(centerAbs - camWorld);          // back to camera-relative
        fit.cullCenterAbs = glm::vec3(centerAbs);          // cull stays absolute
    }

    fit.texelWorld = (2.0f * radius) / std::max(mapSize, 1.0f);
    fit.depthRange = 2.0f * radius + 2.0f * kCasterBack;

    glm::vec3 lightPos = center - lightDir * (radius + kCasterBack);
    glm::mat4 lightView = glm::lookAt(lightPos, center, up);
    // orthoRH_ZO → Vulkan [0,1] clip depth, matching the D32 shadow buffer and the [0,1]
    // shadowCoord.z compare. Plain glm::ortho gives OpenGL [-1,1], which half-clips the scene.
    glm::mat4 lightProj = glm::orthoRH_ZO(-radius, radius, -radius, radius,
                                          0.0f, fit.depthRange);
    lightProj[1][1] *= -1;  // Vulkan Y flip
    fit.lightSpaceMatrix = lightProj * lightView;

    // Guard: a non-finite center means the corner math degenerated. NaN propagates into every
    // shadowCoord and NaN compares FALSE, silently skipping shadows — fail loudly instead.
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
        !std::isfinite(radius) || radius <= 0.0f) {
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            LOG_ERROR("Shadow", "Shadow volume fit produced a non-finite center/radius — "
                      "shadows disabled this frame. Check the frustum-corner math against "
                      "the current depth convention (docs: reverse-Z).");
        }
        fit.lightSpaceMatrix = glm::mat4(1.0f);
    }
    return fit;
}

void RenderCoordinator::renderShadowPass(VkCommandBuffer commandBuffer, ShadowMap& map,
                                         const glm::mat4& lightSpaceMatrix,
                                         const glm::vec3& cullCenter, float cullRadius,
                                         int cascade) {

    // NOTE (Phase 3): face-direction bucketing is NOT applicable to the shadow pass.
    // The 36-index draw makes every instance rasterize BOTH windings on its plane, so
    // even toward-light faces write shadow depth — and terrain TOP faces are the
    // primary occluders at high sun. A direction split here measurably shifted
    // shadows (clean A/B pixel diff 0.33% >8/255); the main pass is where the
    // bucketing win lives (single-winding 6-index quads → the skip is exact).

    map.beginRenderPass(commandBuffer);

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
    if (s_shadowFrustumCull) lightFrustum.extractFromMatrix(lightSpaceMatrix, Utils::Frustum::ClipConvention::ForwardZeroToOne);
    // C2.1: the shadow pipeline layout now declares set 0 (per-draw chunk origins). Bind it
    // unconditionally -- the legacy path ignores its contents (push-constant flag 0), but a
    // declared set must still be bound.
    if (map.getChunkDataSet(currentFrame) != VK_NULL_HANDLE) {
        VkDescriptorSet set = map.getChunkDataSet(currentFrame);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                map.getPipelineLayout(), 0, 1, &set, 0, nullptr);
    }

    if (cascade == kCascadeMid)
        gpuProfiler->beginPipelineStats(commandBuffer, GpuProfiler::STATS_SLOT_SHADOW);
    bool chunksDrawnViaMultidraw = false;

    // ---- C2.1 GPU-DRIVEN PATH: one multidraw per arena buffer ----------------------------
    // Chunks sharing an arena VkBuffer are in the same allocator block, so they can be
    // submitted together: bind that buffer once, then issue ONE vkCmdDrawIndexedIndirect whose
    // commands select each chunk's slice via firstInstance. The per-chunk origin comes from the
    // SSBO indexed by gl_DrawIDARB, which is why this needs shaderDrawParameters (C2.0b) and
    // drawIndirectFirstInstance (C2.0). MID cascade only (the near pass's caster set is tiny).
    if (cascade == kCascadeMid && s_gpuDrivenShadow && chunkManager && !chunkManager->chunks.empty() &&
        vulkanDevice->supportsGpuDrivenSubmission() && map.getIndirectMapped(currentFrame)) {
        Utils::Frustum lightFrustumG;
        if (s_shadowFrustumCull) lightFrustumG.extractFromMatrix(lightSpaceMatrix, Utils::Frustum::ClipConvention::ForwardZeroToOne);

        // Gather survivors grouped by arena buffer, preserving the legacy cull tests exactly.
        std::unordered_map<VkBuffer, std::vector<const Chunk*>> byBuffer;
        for (const auto& chunk : chunkManager->chunks) {
            if (chunk->getNumInstances() == 0) continue;
            glm::vec3 minB = chunk->getMinBounds(), maxB = chunk->getMaxBounds();
            glm::vec3 centre = (minB + maxB) * 0.5f;
            if (glm::length(centre - cullCenter) > cullRadius + 160.0f) continue;
            if (s_shadowFrustumCull && !lightFrustumG.intersects(Utils::AABB(minB, maxB))) continue;
            byBuffer[chunk->getInstanceBuffer()].push_back(chunk.get());
        }

        auto* cmds = static_cast<VkDrawIndexedIndirectCommand*>(map.getIndirectMapped(currentFrame));
        std::vector<glm::vec4> origins;
        origins.reserve(ShadowMap::kMaxChunkDataEntries);
        uint32_t cmdCursor = 0;
        int gpuDraws = 0;
        long long gpuInstances = 0;
        const uint32_t stride = static_cast<uint32_t>(sizeof(Phyxel::Vulkan::InstanceData));

        // HARD GUARD (root-caused 2026-07-29): firstInstance addresses instances by STRIDE, so a
        // chunk's arena span offset must be an exact multiple of sizeof(InstanceData). Arena spans
        // are kAlignment=256-byte aligned (ChunkArenaAllocator.h:48) and the stride is 24 bytes;
        // 256 % 24 == 16, so span offsets are generally NOT stride multiples and offset/stride
        // TRUNCATES -- every chunk would read the wrong instances. Refuse and fall back rather
        // than render silent garbage. Fix is in the allocator (align spans to lcm(256,24)=768) or
        // by padding InstanceData to 32B; see docs/ContinuousLodPlan.md C2.1.
        bool strideMisaligned = false;
        for (auto& kv : byBuffer) {
            if (strideMisaligned) break;
            for (const Chunk* ch : kv.second) {
                if (!spanIsStrideAddressable(ch->getInstanceBindOffset(), stride)) {
                    static bool warned = false;
                    if (!warned) {
                        warned = true;
                        LOG_WARN("RenderCoordinator",
                                 "C2.1 GPU-driven shadow DISABLED: arena span offset " +
                                 std::to_string(ch->getInstanceBindOffset()) +
                                 " is not a multiple of the " + std::to_string(stride) +
                                 "-byte instance stride, so firstInstance cannot address it. "
                                 "Align arena spans to lcm(kAlignment, stride) first.");
                    }
                    strideMisaligned = true; break;
                }
            }
        }

      if (!strideMisaligned) {
        struct Batch { VkBuffer buf; uint32_t first; uint32_t count; };
        std::vector<Batch> batches;
        for (auto& kv : byBuffer) {
            const uint32_t firstCmd = cmdCursor;
            uint32_t n = 0;
            for (const Chunk* ch : kv.second) {
                if (cmdCursor >= ShadowMap::kMaxIndirectCommands ||
                    origins.size() >= ShadowMap::kMaxChunkDataEntries) break;
                VkDrawIndexedIndirectCommand& c = cmds[cmdCursor];
                c.indexCount    = s_shadowQuadDraw ? 6u : 36u;   // M5 A/B (see the note above)
                c.instanceCount = ch->getNumInstances();
                c.firstIndex    = 0;
                c.vertexOffset  = 0;
                c.firstInstance = static_cast<uint32_t>(ch->getInstanceBindOffset() / stride);
                glm::ivec3 wo = ch->getWorldOrigin();
                glm::vec3 rel = camera->relativeTo(glm::dvec3(wo));
                origins.emplace_back(rel.x, rel.y, rel.z, 0.0f);
                gpuInstances += c.instanceCount;
                ++cmdCursor; ++n;
            }
            if (n) batches.push_back({kv.first, firstCmd, n});
        }
        map.uploadChunkOrigins(currentFrame, origins.data(), static_cast<uint32_t>(origins.size()));

        struct ShadowPushConstsG {
            glm::mat4 lightSpaceMatrix;
            glm::vec3 chunkBaseOffset;
            uint32_t  useChunkDataSsbo;
            uint32_t  drawIndexBase;
        } pcG;
        pcG.lightSpaceMatrix = lightSpaceMatrix;
        pcG.chunkBaseOffset = glm::vec3(0.0f);
        pcG.useChunkDataSsbo = 1u;      // read origins[drawIndexBase + gl_DrawIDARB]

        for (const Batch& b : batches) {
            // gl_DrawIDARB restarts at 0 for EVERY indirect call, so each batch must be told
            // where its slice of `origins` begins. Push constants may be re-recorded between
            // draws (unlike vertex bindings), which is what makes this legal.
            pcG.drawIndexBase = b.first;
            vkCmdPushConstants(commandBuffer, map.getPipelineLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pcG), &pcG);
            VkBuffer bufs[] = { b.buf };
            VkDeviceSize offs[] = { 0 };   // firstInstance selects the span, not the bind offset
            vkCmdBindVertexBuffers(commandBuffer, 1, 1, bufs, offs);
            vkCmdDrawIndexedIndirect(commandBuffer, map.getIndirectBuffer(currentFrame),
                                     b.first * sizeof(VkDrawIndexedIndirectCommand),
                                     b.count, sizeof(VkDrawIndexedIndirectCommand));
            ++gpuDraws;
        }
        if (cascade == kCascadeMid) {
            gpuProfiler->endPipelineStats(commandBuffer, GpuProfiler::STATS_SLOT_SHADOW);
            // Write the MEMBERS, not lastFrameStats: drawFrame() does `lastFrameStats = {}`
            // after the shadow pass and repopulates from these, so direct writes here were
            // discarded and the API reported stale legacy counts (which is why
            // shadow_multidraw_calls read 0 even though this path was executing).
            m_shadowChunksDrawn = static_cast<int>(cmdCursor);
            m_shadowInstancesDrawn = gpuInstances;
            m_shadowMultidrawCalls = gpuDraws;
        }
        if (cascade == kCascadeFar)
            m_farShadowChunksDrawn = static_cast<int>(cmdCursor);
        // 🐞 NO early return here (fixed 2026-08-06). The original C2.1 A/B block ended with
        // endRenderPass+return, silently skipping the character/kinematic/dynamic/foliage
        // caster sections below — invisible while the toggle was OFF-by-default and its
        // pixel diffs ran vegetation-off, but a REAL regression (canopy dapple + character
        // shadows gone from the mid map) the moment multidraw became the default. Fall
        // through; the flag below skips only the legacy CHUNK loop.
        chunksDrawnViaMultidraw = true;
      }   // !strideMisaligned
    }

    // Caster margin beyond the fitted sphere: the MID pass needs generous reach (tall
    // casters far outside the view can still shadow into it); the NEAR pass covers ~40 u
    // and tree-height casters are its tallest concern — a 160 u margin there admitted
    // essentially every resident chunk into a map that clips them all (measured: the
    // dense-meadow near pass cost +5.5 ms, most of it clipped vertex work).
    const float kCasterMargin = (cascade == kCascadeNear) ? 48.0f : 160.0f;
    // FAR cascade: NO chunk casters (2026-08-06). Far-terrain tiles UNDERLAP the resident
    // chunks (their quantized surface sits just below the real one), so tile depth already
    // approximates the terrain that chunks would write — while drawing ~900 chunks made
    // every 4th (cadence) frame's shadow pass spike to ~20 ms (measured: visible judder).
    // The far map's casters are the far field itself: tiles + tree/structure meshes below.
    if (!chunksDrawnViaMultidraw && cascade != kCascadeFar &&
        chunkManager && !chunkManager->chunks.empty()) {
        for (const auto& chunk : chunkManager->chunks) {
             if (chunk->getNumInstances() == 0) continue;

             // Simple distance culling for shadows
             glm::vec3 minBounds = chunk->getMinBounds();
             glm::vec3 maxBounds = chunk->getMaxBounds();
             glm::vec3 chunkCenter = (minBounds + maxBounds) * 0.5f;
             if (glm::length(chunkCenter - cullCenter) > cullRadius + kCasterMargin) continue; // fitted sphere + chunk radius + caster margin

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
                 uint32_t  useChunkDataSsbo;   // C2.1: 0 = legacy origin from this push constant
                 uint32_t  drawIndexBase;      // unused when useChunkDataSsbo == 0
             } pushConsts;
             pushConsts.useChunkDataSsbo = 0u;
             pushConsts.drawIndexBase = 0u;

             pushConsts.lightSpaceMatrix = lightSpaceMatrix;
             glm::ivec3 worldOrigin = chunk->getWorldOrigin();
             pushConsts.chunkBaseOffset = camera->relativeTo(glm::dvec3(worldOrigin));  // camera-relative

             vkCmdPushConstants(commandBuffer, map.getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);

             // Draw
             // M5 SETTLED EMPIRICALLY (2026-08-06, docs/evidence/m5_quad.png): the 6-index
             // quad breaks the shadow pass — 4.785% of pixels differ vs a 0.022% control
             // (dense pose, sun paused, vegetation off). MECHANISM: face quads are wound for
             // the CAMERA-facing convention; the 36-index cube rasterizes both windings so
             // light-back-facing quads survive this pipeline's VK_CULL_MODE_BACK_BIT, while
             // a single-winding quad drops them from the depth map (missing casters = light
             // leaks). 36 stays REQUIRED under back-cull. Possible future experiment: 6-index
             // + CULL_NONE pipeline (would need its own acne/bias re-tune + pixel gate).
             // (And do NOT direction-bucket here — see the note at the top of this function.)
             vkCmdDrawIndexed(commandBuffer, s_shadowQuadDraw ? 6u : 36u,
                              chunk->getNumInstances(), 0, 0, 0);
             ++shadowChunks;
             shadowInstances += chunk->getNumInstances();
        }
    }
    if (cascade == kCascadeMid && !chunksDrawnViaMultidraw) {
        gpuProfiler->endPipelineStats(commandBuffer, GpuProfiler::STATS_SLOT_SHADOW);
        // Stash in members — lastFrameStats is reset AFTER the shadow pass in drawFrame, so
        // copy these into lastFrameStats there (see the visibleChunkCount block).
        m_shadowChunksDrawn = shadowChunks;
        m_shadowMultidrawCalls = 0;   // legacy path issues no indirect calls
        m_shadowInstancesDrawn = shadowInstances;
    } else if (cascade == kCascadeNear) {
        m_nearShadowChunksDrawn = shadowChunks;
        m_nearShadowInstancesDrawn = shadowInstances;
    }

    // -------------------------------------------------------------------------
    // Character shadow pass (AnimatedVoxelCharacter / NPC ragdolls)
    // -------------------------------------------------------------------------
    if (cascade != kCascadeFar &&
        map.getCharacterShadowPipeline() != VK_NULL_HANDLE && !m_charBatches.empty()
        && m_shadowCharactersEnabled) {
        // Batches + the instance upload come from buildCharacterFrameData(), which ran
        // once before this pass. This used to re-walk every character in the world and
        // re-upload a byte-identical buffer. Draw only the light-frustum subset — note
        // that is NOT the camera subset: an off-screen character can cast into view.
        GPU_PROFILE_SCOPE(gpuProfiler.get(), commandBuffer, "Character Shadows");
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          map.getCharacterShadowPipeline());
        vulkanDevice->bindCharacterInstanceBuffer(commandBuffer);

        struct CharShadowPC { glm::mat4 model; glm::mat4 lightSpaceMatrix; } charPC;
        charPC.lightSpaceMatrix = lightSpaceMatrix;
        for (const auto& batch : m_charBatches) {
            if (batch.charIndex < 0 || !m_charVisibleShadow[batch.charIndex]) continue;
            charPC.model = batch.model;
            vkCmdPushConstants(commandBuffer, map.getCharacterShadowLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(charPC), &charPC);
            vkCmdDraw(commandBuffer, 36, batch.instanceCount, 0, batch.firstInstance);
        }
    }

    // -------------------------------------------------------------------------
    // Kinematic voxel shadow pass (doors, rotating platforms, etc.)
    // -------------------------------------------------------------------------
    if (cascade != kCascadeFar &&
        map.getKinematicShadowPipeline() != VK_NULL_HANDLE &&
        kinematicPipeline && m_kinematicObjects &&
        !m_kinematicObjects->getObjects().empty())
    {
        VkBuffer kinBuf = kinematicPipeline->getInstanceBuffer();
        if (kinBuf != VK_NULL_HANDLE) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, map.getKinematicShadowPipeline());

            struct KinShadowPC { glm::mat4 modelMatrix; glm::mat4 lightSpaceMatrix; } kinPC;
            kinPC.lightSpaceMatrix = lightSpaceMatrix;
            for (const auto& [id, range] : kinematicPipeline->getObjectRanges()) {
                auto it = m_kinematicObjects->getObjects().find(id);
                if (it == m_kinematicObjects->getObjects().end() || !it->second.visible) continue;
                kinPC.modelMatrix = it->second.currentTransform;
                // camera-relative: translation column -> (world - camera), double subtract
                kinPC.modelMatrix[3] = glm::vec4(
                    glm::vec3(glm::dvec3(glm::vec3(kinPC.modelMatrix[3])) - glm::dvec3(camera->getPosition())), 1.0f);
                vkCmdPushConstants(commandBuffer, map.getKinematicShadowLayout(),
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
    if (cascade != kCascadeFar &&
        map.getDynamicShadowPipeline() != VK_NULL_HANDLE &&
        m_gpuParticles && m_gpuParticles->isInitialized() && m_gpuParticles->getActiveParticleCount() > 0)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, map.getDynamicShadowPipeline());
        // camera-relative: shader subtracts cameraWorld from the absolute GPU-buffer positions
        struct DynShadowPC { glm::mat4 lightSpaceMatrix; glm::vec4 cameraWorld; } dynPC;
        dynPC.lightSpaceMatrix = lightSpaceMatrix;
        dynPC.cameraWorld = glm::vec4(camera->getPosition(), 0.0f);
        vkCmdPushConstants(commandBuffer, map.getDynamicShadowLayout(),
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
    if (cascade == kCascadeMid && foliagePipeline && foliagePipeline->params().enabled && chunkManager) {
        // MID cascade only: foliage_shadow.vert projects with ubo.lightSpaceMatrix (the mid
        // matrix). Near receivers still get canopy shadows because they take
        // min(nearFactor, midFactor) — a mid-only caster can never vanish up close.
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
        m_shadowFoliageChunks = int(foliageDraws.size());
        foliagePipeline->renderShadow(commandBuffer,
                                      vulkanDevice->getDescriptorSet(currentFrame), foliageDraws);
    }

    // -------------------------------------------------------------------------
    // Grass blade shadow pass — grass darkens the ground it stands on. Clipped to the SAME
    // grass radius as the visible pass (a blade culled from view must not cast either) and
    // driven by the SAME per-chunk density LOD, so shadows track the blades exactly.
    // -------------------------------------------------------------------------
    if (cascade == kCascadeNear && grassPipeline && grassPipeline->params().enabled && chunkManager) {
        // NEAR cascade ONLY (docs/NearShadowCascade.md): a blade's 0.080 u shadow proxy is
        // 0.71 of a mid-map texel — sub-texel casters rasterize sporadically and the result
        // was a random scatter of blobs changing 40.1% of the view (strictly worse than
        // casting nothing). In the near map the same proxy spans 4.1 texels and resolves.
        // grass_shadow.vert projects with ubo.lightSpaceMatrixNear to match.
        const glm::vec3 camPos = camera ? camera->getPosition() : cullCenter;
        // Blades are ~1 u tall: a chunk outside the near volume by more than a few units
        // cannot write a single blade texel — drawing it is pure clipped vertex work. In a
        // 0.95-density meadow the old full-grass-radius cull cost ~5 ms of the near pass.
        const float grassRadius =
            std::min(grassPipeline->params().radius, cullRadius + 8.0f);
        const float grassRadiusSq = grassRadius * grassRadius;
        std::vector<GrassRenderPipeline::ChunkDraw> grassDraws;
        for (const auto& chunk : chunkManager->chunks) {
            if (!chunk || chunk->getGrassCount() == 0) continue;
            glm::vec3 chunkCenter = (chunk->getMinBounds() + chunk->getMaxBounds()) * 0.5f;
            if (glm::length(chunkCenter - cullCenter) > cullRadius + 8.0f) continue;
            const float dSq = glm::dot(chunkCenter - camPos, chunkCenter - camPos);
            if (dSq > grassRadiusSq) continue;
            glm::ivec3 origin = chunk->getWorldOrigin();
            grassDraws.push_back({ chunk->getGrassBuffer(), chunk->getGrassCount(),
                                   glm::vec3(origin.x, origin.y, origin.z),
                                   chunk->getGrassBindOffset(), std::sqrt(dSq) });
        }
        m_shadowGrassChunks = int(grassDraws.size());
        grassPipeline->renderShadow(commandBuffer,
                                    vulkanDevice->getDescriptorSet(currentFrame), grassDraws);
    }

    // -------------------------------------------------------------------------
    // FAR cascade: the LOD band's own content casts — far terrain tiles + far-tree/structure
    // LOD meshes (last frame's assembled lists; 1 frame of staleness is sub-texel out there).
    // Their shadow verts project with ubo.lightSpaceMatrixFar.
    // -------------------------------------------------------------------------
    if (cascade == kCascadeFar) {
        m_farShadowChunksDrawn = shadowChunks;
        m_farShadowTileCasters = int(m_cachedFarTileDraws.size());
        m_farShadowTreeCasters = int(m_cachedTreeMeshDraws.size());
        if (farTerrainPipeline)
            farTerrainPipeline->renderShadow(commandBuffer,
                                             vulkanDevice->getDescriptorSet(currentFrame),
                                             m_cachedFarTileDraws);
        if (treeLodPipeline)
            treeLodPipeline->renderShadow(commandBuffer,
                                          vulkanDevice->getDescriptorSet(currentFrame),
                                          m_cachedTreeMeshDraws);
    }

    map.endRenderPass(commandBuffer);
}


// C5 (docs/ContinuousLodPlan.md) — DISTANCE-DRIVEN LOD: the join between C1's metric and C4's cut.
//
// For each chunk, ask LodService what cell size still earns its pixels at that chunk's distance,
// then re-mesh chunks whose level changed -- at most s_lodRebuildBudgetPerFrame per frame,
// because a full re-mesh is ~40-50 ms (RenderOptimization.md) and doing them unbounded would turn
// camera motion into a stutter storm.
//
// Hysteresis: a chunk only changes level when the metric disagrees by a FULL level. Without it a
// chunk hovering on a boundary re-meshes every frame, which costs far more than the LOD saves.
int RenderCoordinator::updateChunkLod() {
    if (!s_distanceDrivenLod || !chunkManager || !camera) return 0;

    const auto view = Core::LodService::makeView(
        vulkanDevice ? static_cast<float>(vulkanDevice->getSwapChainExtent().height) : 900.0f,
        camera->getFovYDegrees());
    const glm::vec3 camPos = camera->getPosition();
    Core::SquashConfig cfg;

    int rebuilt = 0;
    for (const auto& chunk : chunkManager->chunks) {
        if (!chunk || rebuilt >= s_lodRebuildBudgetPerFrame) break;
        if (chunk->getNumInstances() == 0 && chunk->getLodLevel() == 0) continue;

        const glm::vec3 centre = (chunk->getMinBounds() + chunk->getMaxBounds()) * 0.5f;
        const float dist = glm::length(centre - camPos);
        // baseCellSize 1.0 == one cube. s_lodMaxLevel caps the coarsest cell (see its definition:
        // the fattening defect makes the ladder's top levels unsafe for isolated thin detail).
        const int wanted = Core::LodService::levelForDistance(1.0f, dist, s_lodTargetPixels, view,
                                                             s_lodMaxLevel);
        const int current = chunk->getLodLevel();
        if (wanted == current) continue;

        if (wanted == 0) {
            chunk->rebuildFaces();          // back to the real fine mesh (sub/micro + greedy merge)
        } else {
            std::vector<InstanceData> faces;
            Core::LodChunkMesh::buildForLevel(*chunk, wanted, cfg, faces);
            chunk->setLodFaces(std::move(faces), wanted);
        }
        chunk->updateVulkanBuffer();
        ++rebuilt;
    }
    m_lodRebuiltLastFrame = rebuilt;
    return rebuilt;
}


// --- C3.3: the far-chunk draw path --------------------------------------------------------
// A chunk beyond the residency radius is served from its persisted pyramid instead of being
// loaded. It costs its coarse face buffer (~18.7 KB at lod 2, measured) rather than the
// ~1.28 MB a resident chunk costs, which is what breaks the R^2 wall.

size_t RenderCoordinator::farLodInstanceCount() const {
    size_t n = 0;
    for (const auto& f : m_farLod) if (f) n += f->instanceCount;
    return n;
}

int RenderCoordinator::updateFarLodChunks() {
    if (!s_farLodChunks || !chunkManager || !camera) { m_farLod.clear(); return 0; }
    Phyxel::WorldStorage* storage = chunkManager->getWorldStorage();
    // Two sources: the persisted pyramid (storage, may be null on storage-less worlds) and the
    // in-memory evicted-LOD cache (world-look A1/A2 — unsaved generated chunks). The cache feed
    // is QUARANTINED default-off until the coarse-tree look is fixed (floating canopy cells) —
    // see EvictedLodCache::s_evictionFeedEnabled.
    Core::EvictedLodCache& evictedCache = chunkManager->getEvictedLodCache();
    const bool useEvictedCache =
        Core::EvictedLodCache::s_evictionFeedEnabled && evictedCache.chunkCount() > 0;
    if (!storage && !useEvictedCache) return 0;

    const glm::vec3 camPos = camera->getPosition();
    const glm::ivec3 camChunk = glm::ivec3(glm::floor(camPos / 32.0f));

    // Evict every frame: a chunk that has just become RESIDENT must stop being drawn here
    // immediately, or it double-draws against itself.
    m_farLod.erase(std::remove_if(m_farLod.begin(), m_farLod.end(),
        [&](const std::unique_ptr<FarLodChunk>& f) {
            if (!f) return true;
            if (chunkManager->getChunkAtCoord(f->chunkCoord) != nullptr) return true;
            const glm::vec3 c = glm::vec3(f->worldOrigin) + glm::vec3(16.0f);
            return glm::length(c - camPos) > chunkInclusionDistance;
        }), m_farLod.end());

    // Drive from the chunks that ACTUALLY have persisted geometry instead of probing a volume.
    // The old version scanned a (2*reach+1)^3 box of speculative coordinates; with reach derived
    // from chunkInclusionDistance that was 7.2M probes per frame (638 -> 2 FPS, measured), and
    // capping reach at 12 to fix that silently dropped chunks past ~384 units -- 5 of 5 became
    // 1 of 5 at one pose, and 5 of 7 at another. Iterating the stored set removes BOTH problems:
    // the work is proportional to what exists (a handful of rows), and there is no reach cap, so
    // coverage no longer depends on an arbitrary constant.
    // Also rescan when the evicted cache changes: chunks evict while the camera sits still
    // (streaming churn, teleports), and a crossing-keyed scan alone would never see them —
    // the same stationary blind spot FarTerrainManager's maxDistance change has.
    if (camChunk != m_farLodLastScanChunk || m_farLodScanIncomplete ||
        (useEvictedCache && evictedCache.revision() != m_farLodCacheRevision)) {
        m_farLodLastScanChunk = camChunk;
        m_farLodCacheRevision = evictedCache.revision();
        m_farLodCandidates = storage ? storage->getChunksWithLodBlobs()
                                     : std::vector<glm::ivec3>{};
        // Duplicates with the storage set are harmless: the build loop's already-built check
        // makes the second occurrence a no-op.
        if (useEvictedCache) evictedCache.appendCoords(m_farLodCandidates);
    }

    const auto view = Core::LodService::makeView(
        vulkanDevice ? static_cast<float>(vulkanDevice->getSwapChainExtent().height) : 900.0f,
        camera->getFovYDegrees());

    int built = 0;
    for (const glm::ivec3& coord : m_farLodCandidates) {
        if (built >= s_farLodBudgetPerFrame) break;
        if (chunkManager->getChunkAtCoord(coord)) continue;      // resident chunks own themselves
        bool already = false;
        for (const auto& f : m_farLod) if (f && f->chunkCoord == coord) { already = true; break; }
        if (already) continue;

        const glm::vec3 centre = glm::vec3(coord * 32) + glm::vec3(16.0f);
        const float dist = glm::length(centre - camPos);
        if (dist > chunkInclusionDistance) continue;
        const int level = std::max(1, Core::LodService::levelForDistance(
            1.0f, dist, s_lodTargetPixels, view, Core::LodPyramidService::kMaxLevel));

        // Memory first: an entry stashed at eviction is at least as fresh as the DB row (the
        // dirty-save runs before the eviction callback), and it costs no SQLite round-trip.
        std::vector<InstanceData> faces;
        if (!(useEvictedCache && evictedCache.facesForLevel(coord, level, faces)) &&
            !(storage && Core::LodPyramidService::facesFromStorage(*storage, coord, level, faces)))
            continue;
        if (faces.empty()) continue;

        auto entry = std::make_unique<FarLodChunk>();
        entry->chunkCoord = coord;
        entry->worldOrigin = coord * 32;
        entry->level = level;
        entry->instanceCount = static_cast<uint32_t>(faces.size());
        entry->buffer = std::make_unique<ChunkRenderBuffer>(vulkanDevice->getDevice(),
                                                            vulkanDevice->getPhysicalDevice());
        entry->buffer->createBuffer(faces);
        m_farLod.push_back(std::move(entry));
        ++built;
    }
    m_farLodScanIncomplete = (built >= s_farLodBudgetPerFrame);
    return built;
}

void RenderCoordinator::drawFarLodChunks(uint32_t currentFrame) {
    if (!s_farLodChunks || m_farLod.empty() || !camera || !vulkanDevice) return;
    for (const auto& f : m_farLod) {
        if (!f || !f->buffer || f->instanceCount == 0 ||
            f->buffer->getBuffer() == VK_NULL_HANDLE) continue;
        VkBuffer bufs[] = {f->buffer->getBuffer()};
        VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(vulkanDevice->getCommandBuffer(currentFrame), 1, 1, bufs, offs);
        const glm::vec3 rel = camera->relativeTo(glm::dvec3(f->worldOrigin));
        const glm::vec3 abs = glm::vec3(f->worldOrigin);
        vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), rel, abs);
        // The faces are ordinary scaleLevel==3 LOD cells, identical to what a resident chunk
        // emits, so they use the SAME pipeline -- no second shader path to keep in sync.
        vulkanDevice->drawIndexed(currentFrame, vulkanDevice->chunkIndexCount(), f->instanceCount);
    }
}

void RenderCoordinator::drawFrame() {
    // C1 (docs/ContinuousLodPlan.md): refresh the shared screen-space LOD scale ONCE, before
    // any consumer runs. It previously lived in buildCharacterFrameData, which runs after
    // renderGrass/renderFoliage in the scene pass — those would have read a one-frame-stale
    // scale (and the default 1.0 on frame 0). Exactly 1.0 at the reference config either way.
    if (vulkanDevice) {
        // Pass the camera's REAL vertical FOV rather than letting it default to the reference
        // constant — otherwise the correction is resolution-only and silently stops tracking
        // FOV the moment it becomes configurable.
        updateLodView(static_cast<float>(vulkanDevice->getSwapChainExtent().height),
                      camera ? camera->getFovYDegrees() : Camera::kFovYDegrees);
    }

    // C5: pick each chunk's LOD level from the shared metric before culling/drawing.
    updateChunkLod();
    updateFarLodChunks();   // C3.3: serve non-resident chunks from the persisted pyramid

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

    // WATER LAYER (P1): (re)bind the hydrology level grid when the world's bake appears or
    // changes (world switch). Rare — a descriptor rewrite on a possibly in-flight set, so it
    // idles the device first (a once-per-world-load hitch, hidden by the load itself).
    // Body-aware look: the upload is RGBA — R = level, G = per-body wave ENERGY from body size
    // (tangible-water F: ocean 1, lakes by log-area, floor 0.15, so a mountain tarn shows a ripple
    // where the ocean shows swell), B = turbidity and A = roughness (Water Appearance v4 W1 —
    // NEUTRAL until W2/W3 derive them; docs/Water.md).
    //
    // The packing moved into Phyxel::buildHydroUpload so it is unit-testable: as an inline loop
    // here it could only ever be checked by looking at the screen.
    if (waterPipeline && chunkManager) {
        const auto* gen = chunkManager->getStreamingGenerator();
        const auto* hydro = gen ? gen->hydrology() : nullptr;
        if (static_cast<const void*>(hydro) != m_lastHydroUploaded) {
            vkDeviceWaitIdle(vulkanDevice->getDevice());
            VkCommandBuffer oneShot = vulkanDevice->beginSingleTimeCommands();
            if (hydro) {
                Phyxel::WaterLookOverride ovr;
                ovr.active    = m_waterLookActive;
                ovr.turbidity = m_waterLookTurbidity;
                ovr.roughness = m_waterLookRoughness;
                // v4 W3: the LIVE wind, so fetch-limited energy and Cox-Munk roughness reflect the
                // actual sea state rather than a default. Direction comes from the same value the
                // vertex shader rotates the swell by, so the CPU and GPU cannot disagree on heading.
                Phyxel::WaterWind wind;
                wind.speedMs    = waterPipeline->windSpeed();
                wind.dirRadians = waterPipeline->windDirection();
                std::vector<float> rgba;
                Phyxel::buildHydroUpload(*hydro, gen->waterBodies(), ovr, rgba, wind);
                waterPipeline->recordHydrologyUpload(oneShot, rgba.data(),
                                                     hydro->cellsX(), hydro->cellsZ(),
                                                     hydro->originX(), hydro->originZ(),
                                                     hydro->cellSize());
            } else {
                waterPipeline->recordHydrologyUpload(oneShot, nullptr, 0, 0, 0.0f, 0.0f, 0.0f);
            }
            vulkanDevice->endSingleTimeCommands(oneShot);
            m_lastHydroUploaded = hydro;
        }
    }

    // THE SANE BASELINE: span-derived water placement for baked worlds (overrides the bake
    // upload above once chunks are resident; see the function).
    updateSpanWaterGrid();

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
            // No camera: still must produce the REVERSE-Z convention the pipelines test against
            // (graphics/DepthConvention.h). A forward-Z fallback here would make the no-camera
            // path fail the GREATER depth test and render nothing — a bug that would only appear
            // in whatever edge case leaves `camera` null.
            cachedProjectionMatrix = DepthConvention::infiniteReverseZPerspective(
                glm::radians(Camera::kFovYDegrees), aspect, 0.1f);
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
        // Shadow draw distance — how far the MID cascade renders (must be <= view distance).
        // Capped INDEPENDENTLY of the render distance: raising the view distance must not
        // stretch one map over a huge volume. The fit itself lives in fitShadowVolume()
        // (extracted 2026-08-05 so the near cascade reuses the identical math).
        const float kMaxShadowDist = std::min(maxChunkRenderDistance, s_shadowDistance);
        const ShadowFit fit = fitShadowVolume(
            kMaxShadowDist, float(shadowMap->getWidth() > 0 ? shadowMap->getWidth() : 8192),
            camWorld, sunDirection);
        lightSpaceMatrix = fit.lightSpaceMatrix;
        shadowCullCenter = fit.cullCenterAbs;
        shadowCullRadius = fit.cullRadius;
        m_shadowTexelWorld = fit.texelWorld;
        vulkanDevice->setShadowDepthRange(fit.depthRange);

        // Near cascade: same fit, tight distance, its own map (docs/NearShadowCascade.md).
        if (shadowMapNear && s_nearShadowEnabled) {
            const float nearDist = std::min(s_nearShadowDistance, kMaxShadowDist);
            const ShadowFit fitN = fitShadowVolume(
                nearDist,
                float(shadowMapNear->getWidth() > 0 ? shadowMapNear->getWidth() : 4096),
                camWorld, sunDirection);
            m_nearLightSpaceMatrix = fitN.lightSpaceMatrix;
            m_nearShadowCullCenter = fitN.cullCenterAbs;
            m_nearShadowCullRadius = fitN.cullRadius;
            m_nearShadowTexelWorld = fitN.texelWorld;
            vulkanDevice->setNearShadowCascade(m_nearLightSpaceMatrix, nearDist,
                                               fitN.depthRange);
        } else {
            vulkanDevice->setNearShadowCascade(glm::mat4(1.0f), 0.0f, 1.0f);
        }

        // Far cascade fit — EVERY frame (microseconds); only the caster PASS is cadenced
        // (renderShadowPass call site). This is safe with a persistent map because the fit
        // is texel-snapped in the WORLD-anchored light frame: a given world point keeps its
        // light-space UV across frames, so 1-3 frame old depth sampled through a fresh
        // matrix is off by at most the camera's travel quantized to 0.9 u texels —
        // invisible at LOD-band distances.
        if (shadowMapFar && s_farShadowEnabled) {
            const float farDist = std::min(s_farShadowDistance, maxChunkRenderDistance);
            const ShadowFit fitF = fitShadowVolume(
                farDist,
                float(shadowMapFar->getWidth() > 0 ? shadowMapFar->getWidth() : 4096),
                camWorld, sunDirection);
            m_farLightSpaceMatrix = fitF.lightSpaceMatrix;
            m_farShadowCullCenter = fitF.cullCenterAbs;
            m_farShadowCullRadius = fitF.cullRadius;
            vulkanDevice->setFarShadowCascade(m_farLightSpaceMatrix, farDist, fitF.depthRange);
        } else {
            vulkanDevice->setFarShadowCascade(glm::mat4(1.0f), 0.0f, 1.0f);
        }
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
    // Grass casts into the NEAR cascade now, so its blade-width shadow clamp must use the
    // NEAR map's texel (0.0195 u) — clamping against the mid texel (0.1125 u) is what forced
    // the fat mushy blade smudge.
    if (grassPipeline)
        grassPipeline->setShadowTexelWorld(
            (shadowMapNear && s_nearShadowEnabled) ? m_nearShadowTexelWorld
                                                   : m_shadowTexelWorld);
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
        // Mid cascade (the original single map), then the near cascade (tight map whose
        // texels resolve blade-scale casters — docs/NearShadowCascade.md).
        renderShadowPass(cmd, *shadowMap, lightSpaceMatrix, shadowCullCenter,
                         shadowCullRadius, kCascadeMid);
        if (shadowMapNear && s_nearShadowEnabled)
            renderShadowPass(cmd, *shadowMapNear, m_nearLightSpaceMatrix,
                             m_nearShadowCullCenter, m_nearShadowCullRadius, kCascadeNear);
        // FAR cascade on a cadence: skipping a frame skips the CLEAR too, so the map
        // simply persists — coarse texels a kilometre out cannot show the staleness.
        if (shadowMapFar && s_farShadowEnabled && --m_farShadowFrameCounter <= 0) {
            m_farShadowFrameCounter = std::max(1, s_farShadowCadence);
            renderShadowPass(cmd, *shadowMapFar, m_farLightSpaceMatrix,
                             m_farShadowCullCenter, m_farShadowCullRadius, kCascadeFar);
        }
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
    lastFrameStats.shadowMultidrawCalls = m_shadowMultidrawCalls;
    lastFrameStats.shadowGrassChunks = m_shadowGrassChunks;
    lastFrameStats.shadowFoliageChunks = m_shadowFoliageChunks;

    hasMirrorVoxels = scanForMirrorVoxels();
    LOG_DEBUG("RenderCoordinator", "Frame: visibleChunks={} hasMirrorVoxels={}", visibleChunkIndices.size(), hasMirrorVoxels);

    // Mirror reflection pass: render scene from reflected camera before the main scene pass.
    if (hasMirrorVoxels && renderPipeline->getMirrorPipeline() != VK_NULL_HANDLE) {
        GPU_PROFILE_SCOPE(gpuProfiler.get(), cmd, "Reflection Pass");  // D0: was untimed
        renderReflectionPass(currentFrame);
    }

    // Water reflection (Water Appearance v4 W4): this flag no longer means "planar reflection is
    // available" — that branch is gone. It now enables SCREEN-SPACE reflection in the water shader,
    // which marches the depth buffer the water pass already binds and falls back to the procedural
    // sky on a miss. Planar was rejected on two counts: it assumes a flat mirror plane (this sea is
    // Gerstner-displaced) and the shared mirror pass is broken (wrong winding/projection).
    // Runtime-toggleable via POST /api/debug/water_ssr — that toggle is the A/B control for every
    // before/after capture and the escape hatch if SSR misbehaves.
    m_waterReflectionActive = m_waterSsrEnabled;

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
    // docs/Water.md Phase 1.

    // Mirror surface pass (inside scene render pass, after all opaque/entity geometry)
    if (hasMirrorVoxels && renderPipeline->getMirrorPipeline() != VK_NULL_HANDLE) {
        renderMirrorGeometry(currentFrame);
    }

    // Game HUD / custom UI moved to the post-scene OVERLAY pass below, so water (which now
    // also draws after the scene pass) cannot paint over it. See docs/Water.md Phase 1.

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
        // Ripple heightfield upload (small-scale plan Phase 3) — also outside any render pass.
        // Skips the copy on unchanged frames; always refreshes the window push params.
        if (drawWaterCells)
            waterCellPipeline->updateRipple(vulkanDevice->getCommandBuffer(currentFrame),
                                            static_cast<uint32_t>(currentFrame),
                                            m_waterManager->ripple());
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
                // v4 W2: which water are we IN? Same query the surface shading's texture was
                // packed from (Phyxel::waterProfileAt), so the two cannot disagree — and the
                // water_look override applies here too, or the positive control would show a
                // murky surface over a clear underwater view.
                float uwTurbidity = 0.0f;
                if (m_waterLookActive) {
                    uwTurbidity = m_waterLookTurbidity;
                } else if (chunkManager) {
                    const auto* gen = chunkManager->getStreamingGenerator();
                    const auto* hyd = gen ? gen->hydrology() : nullptr;
                    const glm::vec3 eye = camera->getPosition();
                    if (hyd) {
                        Phyxel::WaterWind w;
                        w.speedMs    = waterPipeline->windSpeed();
                        w.dirRadians = waterPipeline->windDirection();
                        uwTurbidity = Phyxel::waterProfileAt(gen->waterBodies(), eye.x, eye.z,
                                                             hyd->cellSize(), w).turbidity;
                    }
                }
                waterPipeline->renderUnderwater(
                    vulkanDevice->getCommandBuffer(currentFrame),
                    vulkanDevice->getDescriptorSet(currentFrame),
                    *camera, cachedProjectionMatrix,
                    submergence, depthBelow,
                    vulkanDevice->getSwapChainExtent(), uwTurbidity);
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
    cameraFrustum.extractFromMatrix(cameraViewProj, Utils::Frustum::ClipConvention::ReverseZeroToOne);
    lightFrustum.extractFromMatrix(lightSpaceMatrix, Utils::Frustum::ClipConvention::ForwardZeroToOne);

    // C1: refresh the screen-space correction from the live swapchain, then use the
    // corrected cull distance. At 1600x900 this is identical to the legacy value.
    const float effCull = effectiveCharacterCullDistance();
    const float cullDistSq = effCull * effCull;

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
