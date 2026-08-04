#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <chrono>

namespace Phyxel {
namespace Graphics {

class Camera;

// Phase 0 water surface (see docs/Water.md).
//
// Draws a single large translucent quad locked to a world sea level and centered on
// the camera in XZ — an "infinite ocean" plane. The scene depth buffer occludes it,
// so the surface only appears where open space meets sea level (the implicit-ocean
// model). Procedural Fresnel + ripples in the shader; no simulation, reflection, or
// refraction yet (those are later phases). Self-contained, modeled on
// VfxRenderPipeline: its own quad vertex buffer, pipeline, and push constants.
class WaterRenderPipeline {
public:
    WaterRenderPipeline();
    ~WaterRenderPipeline();

    // `uboLayout` is the shared scene descriptor-set layout (VulkanDevice::getDescriptorSetLayout),
    // bound at SET 0 so the sea tracks the live sun/ambient and can linearize the depth buffer
    // (WaterSystemV3 Phase 1). SET 1 is this pipeline's own taps (refraction, depth, reflection).
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D swapChainExtent,
                    VkDescriptorSetLayout uboLayout);
    void cleanup();

    // Point the reflection sampler at the planar-reflection texture. Call after
    // initialize() and again after any swapchain resize (the texture is recreated).
    // Must not be called mid-frame (updates a descriptor set).
    void setReflectionTexture(VkImageView reflectionView, VkSampler reflectionSampler);

    // Point set 1 at the post-scene taps: the half-res scene-colour copy (refraction) and the
    // scene depth buffer (seabed distance → absorption + soft shorelines). Call after
    // initialize() and again after every swapchain resize. Not mid-frame.
    void setSceneTextures(VkImageView refractionView, VkSampler refractionSampler,
                          VkImageView sceneDepthView, VkSampler sceneDepthSampler);

    // Draw the sea-level surface. `size` is the side length of the camera-following
    // quad (world units). When `reflectionEnabled`, the fragment shader samples the
    // reflection texture (set via setReflectionTexture). Call inside the WATER render pass
    // (after the scene pass), not the scene pass.
    void render(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet, const Camera& camera,
                const glm::mat4& projectionMatrix, float seaLevel, float size,
                VkExtent2D screenExtent, bool reflectionEnabled);

    // Fullscreen underwater fog overlay (WaterSystemV3 Phase 1 item 5). Draw LAST in the water
    // pass, after the surfaces, when the camera is submerged: it must fog the sky and the
    // underside of the surface too, so it runs with depth test OFF.
    //   submergence — 0 = at/above the surface (draw skipped), 1 = fully under. The caller fades
    //                 this over a short band so breaking the surface doesn't pop.
    //   depthBelow  — how far under the surface the camera is (world units); darkens/blues the fog.
    //   turbidity   — the profile of the body the camera is INSIDE (v4 W2). The surface reads its
    //                 profile per pixel from the hydrology texture; this fullscreen pass has no
    //                 per-pixel body, so it must be told which water it is in or a murky lake will
    //                 read clear from below and breaking the surface will pop. 0 = clear.
    void renderUnderwater(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet,
                          const Camera& camera, const glm::mat4& projectionMatrix,
                          float submergence, float depthBelow, VkExtent2D screenExtent,
                          float turbidity = 0.0f);

    // Gerstner swell controls (WaterSystemV3 Phase 2). amplitude 0 disables the displacement
    // entirely, which restores the pre-Phase-2 flat sheet — that is the A/B used to prove the
    // waves are what changed, and the escape hatch if they ever misbehave.
    void setWaves(float amplitude, float wavelength, float windDirectionRadians) {
        m_waveAmplitude = amplitude; m_waveLength = wavelength; m_windDirection = windDirectionRadians;
    }
    float waveAmplitude() const { return m_waveAmplitude; }
    float waveLength() const { return m_waveLength; }
    float windDirection() const { return m_windDirection; }

    // WIND SPEED (v4 W3). Not used by this pipeline's own shading — it drives the CPU-side per-body
    // profile (fetch-limited wave energy + Cox-Munk ripple roughness), which reaches the shader
    // through the hydrology texture. Kept here because this is where the rest of the sea state
    // already lives, so "the wind" is one object rather than two that can disagree.
    void  setWindSpeed(float metresPerSecond) { m_windSpeed = metresPerSecond; }
    float windSpeed() const { return m_windSpeed; }

    // Size the wave zone so its taper falls OUTSIDE the far plane. If the zone ends within view,
    // its edge is a ring of flattening water centred on the camera that follows the viewer around —
    // seen from above as a vortex, and read from any angle as "the waves come from where I stand".
    // Rebuilds the mesh only when the radius changes materially. Safe to call on world load.
    void setWaveRadius(float radius);
    float waveRadius() const { return m_waveRadius; }

    // ── WATER LAYER (terrain-gen stage output; water-layer P1) ────────────────────────────────
    // `levels` is RGBA, 4 floats per cell, row-major (Phyxel::buildHydroUpload packs it):
    // R = basin level, G = wave energy, B = turbidity, A = roughness. Widened from RG in
    // Water Appearance v4 W1 — B/A are inert until W2/W3 derive them.
    //
    // Record the per-column basin-level grid upload into `cmd` (one-shot command buffer, outside
    // any render pass) and point set-1 binding 3 at it. The clipmap then draws EVERY basin in
    // view at its own level — sea at sea level, each lake at its spill — with the dry-land gate
    // deriving shorelines per pixel. levels==nullptr uploads a 1×1 "no water layer" sentinel and
    // keeps flat-sea mode (per-column lookup disabled, pixel-identical to the pre-P1 look): call
    // that form once right after initialize() so the binding is always valid.
    // REPLACING a previously-uploaded grid re-writes the descriptor — the caller must ensure the
    // device is idle (vkDeviceWaitIdle) when swapping mid-session (world change).
    void recordHydrologyUpload(VkCommandBuffer cmd, const float* levels, int cellsX, int cellsZ,
                               float originX, float originZ, float cellSize);
    bool hydrologyBound() const { return m_hydroBound; }

    void recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

private:
    void createDescriptorSetLayout(VkDescriptorSetLayout uboLayout);
    void createDescriptorPool();
    void createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void createUnderwaterPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void createBuffers();

    VkDevice         m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline = VK_NULL_HANDLE;
    VkPipeline            m_underwaterPipeline = VK_NULL_HANDLE; // shares m_pipelineLayout
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE; // set 1 (scene taps + reflection)
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet = VK_NULL_HANDLE;
    bool                  m_sceneBound = false;      // refraction + depth written
    bool                  m_reflectionBound = false; // reflection written

    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer = VK_NULL_HANDLE;        // sea clipmap; see SeaMesh.h
    VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;
    uint32_t       m_indexCount = 0;

    // Water-layer level grid (P1): owned here like the cell pipeline owns its ripple texture.
    void destroyHydrologyResources();
    VkImage        m_hydroImage = VK_NULL_HANDLE;
    VkDeviceMemory m_hydroImageMemory = VK_NULL_HANDLE;
    VkImageView    m_hydroView = VK_NULL_HANDLE;
    VkSampler      m_hydroSampler = VK_NULL_HANDLE;      // NEAREST — basins are piecewise-constant
    VkBuffer       m_hydroStaging = VK_NULL_HANDLE;
    VkDeviceMemory m_hydroStagingMemory = VK_NULL_HANDLE;
    void*          m_hydroStagingMapped = nullptr;
    VkDeviceSize   m_hydroStagingBytes = 0;
    int            m_hydroCellsX = 0, m_hydroCellsZ = 0;
    bool           m_hydroBound = false;                 // no draw until binding 3 is valid
    glm::vec3      m_hydroParams{0.0f, 0.0f, 0.0f};      // originX, originZ, invCellSize (0 = flat)
    float          m_waveRadius = 700.0f;      // world units; set from the render distance
    float          m_seaOuterExtent = 0.0f;    // reach the clipmap actually achieved

    // ⚑GROUND: 0.45-voxel amplitude on a 14-voxel wavelength — a ~0.9 m swell on a 14 m period,
    // a moderate breeze (Beaufort 4).
    //
    // THESE WERE SHRUNK TO 0.30/9.5 AND PUT BACK. The complaint that prompted the shrink ("waves
    // look too big from far away") had a different cause: the ocean had no small-scale detail at
    // ANY distance, so the swell was the only thing to look at. Shrinking it did not add detail, it
    // just removed the majesty and left a busy chop that read worse. The actual fix was the fine
    // octaves + per-octave screen-space LOD; with those in place the larger swell is what makes it
    // read as an ocean rather than a pond. Runtime-settable via the water_waves debug command.
    float m_waveAmplitude = 0.45f;
    float m_waveLength = 14.0f;
    float m_windDirection = 0.6f;   // radians; the dominant swell heading
    // ⚑GROUND: 6.7 m/s = the mid-point of Beaufort force 4 (11-16 kn), which is the sea state the
    // amplitude/wavelength above were authored to describe. Defaulting here means the derived
    // profile reproduces today's look exactly (Cox-Munk roughness comes out at exactly 1.0).
    float m_windSpeed = 6.7f;

    std::chrono::high_resolution_clock::time_point m_startTime;
};

} // namespace Graphics
} // namespace Phyxel
