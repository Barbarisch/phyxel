#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <chrono>

namespace Phyxel {
namespace Graphics {

class Camera;

// Phase 0 water surface (see docs/WaterSystem.md).
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
    void renderUnderwater(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet,
                          const Camera& camera, const glm::mat4& projectionMatrix,
                          float submergence, float depthBelow, VkExtent2D screenExtent);

    // Gerstner swell controls (WaterSystemV3 Phase 2). amplitude 0 disables the displacement
    // entirely, which restores the pre-Phase-2 flat sheet — that is the A/B used to prove the
    // waves are what changed, and the escape hatch if they ever misbehave.
    void setWaves(float amplitude, float wavelength, float windDirectionRadians) {
        m_waveAmplitude = amplitude; m_waveLength = wavelength; m_windDirection = windDirectionRadians;
    }
    float waveAmplitude() const { return m_waveAmplitude; }
    float waveLength() const { return m_waveLength; }
    float windDirection() const { return m_windDirection; }

    // Size the wave zone so its taper falls OUTSIDE the far plane. If the zone ends within view,
    // its edge is a ring of flattening water centred on the camera that follows the viewer around —
    // seen from above as a vortex, and read from any angle as "the waves come from where I stand".
    // Rebuilds the mesh only when the radius changes materially. Safe to call on world load.
    void setWaveRadius(float radius);
    float waveRadius() const { return m_waveRadius; }

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
    VkBuffer       m_indexBuffer = VK_NULL_HANDLE;        // radial sea mesh (Phase 2)
    VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;
    uint32_t       m_indexCount = 0;
    float          m_waveRadius = 700.0f;   // world units; set from the render distance
    int            m_waveRings = 0;         // derived from the radius at build time

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

    std::chrono::high_resolution_clock::time_point m_startTime;
};

} // namespace Graphics
} // namespace Phyxel
