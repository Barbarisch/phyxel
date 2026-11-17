#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <chrono>

namespace Phyxel {
namespace Graphics {

class Camera;

// The underwater fog overlay and the sea state (wind, swell, the wave clock).
//
// This was the old flat sea SHEET's pipeline (docs/Water.md Phase 0: a camera-following clipmap quad at a sea level, with
// a per-column water-layer grid). The sheet was DELETED in WaterCore §55.2 (W6.1b): the new water draws every sea, lake
// and river (the record tiles near, the far skin to the far terrain's reach - §55.1), and the sheet drew nothing the new
// water did not already cover (§55.0, §55.2) while its per-column grid stalled the device on every rebuild. What stays:
// the fullscreen underwater fog, and the wind / swell / clock that the shore band (ShoreBand, SeaSwell) and the
// per-body look profile (WaterProfile) read.
class WaterRenderPipeline {
public:
    WaterRenderPipeline();
    ~WaterRenderPipeline();

    // `uboLayout` is the shared scene descriptor-set layout (VulkanDevice::getDescriptorSetLayout), bound at SET 0 so the
    // fog tracks the live sun/ambient and can linearize the depth buffer. SET 1 is this pipeline's own taps (refraction,
    // depth, reflection).
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D swapChainExtent,
                    VkDescriptorSetLayout uboLayout);
    void cleanup();

    // Point the reflection sampler at the planar-reflection texture. Call after initialize() and again after any swapchain
    // resize (the texture is recreated). Must not be called mid-frame (updates a descriptor set).
    void setReflectionTexture(VkImageView reflectionView, VkSampler reflectionSampler);

    // Point set 1 at the post-scene taps: the half-res scene-colour copy and the scene depth buffer. Call after
    // initialize() and again after every swapchain resize. Not mid-frame.
    void setSceneTextures(VkImageView refractionView, VkSampler refractionSampler,
                          VkImageView sceneDepthView, VkSampler sceneDepthSampler);

    // Fullscreen underwater fog overlay (WaterSystemV3 Phase 1 item 5). Draw LAST in the water pass, after the surfaces,
    // when the camera is submerged: it must fog the sky and the underside of the surface too, so it runs with depth test
    // OFF.
    //   submergence — 0 = at/above the surface (draw skipped), 1 = fully under. The caller fades this over a short band so
    //                 breaking the surface doesn't pop.
    //   depthBelow  — how far under the surface the camera is (world units); darkens/blues the fog.
    //   turbidity   — the profile of the body the camera is INSIDE (v4 W2): a fullscreen pass has no per-pixel body, so
    //                 it must be told which water it is in or a murky lake reads clear from below. 0 = clear.
    void renderUnderwater(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet,
                          const Camera& camera, const glm::mat4& projectionMatrix,
                          float submergence, float depthBelow, VkExtent2D screenExtent,
                          float turbidity = 0.0f, float clarity = 0.0f);   ///< clarity (G3): the body's Secchi depth, m; 0 = derived

    // Gerstner swell controls (WaterSystemV3 Phase 2) - read by the shore band's ocean ring (SeaSwell) and the water_waves
    // route. amplitude 0 = a flat sea.
    void setWaves(float amplitude, float wavelength, float windDirectionRadians) {
        m_waveAmplitude = amplitude; m_waveLength = wavelength; m_windDirection = windDirectionRadians;
    }
    float waveAmplitude() const { return m_waveAmplitude; }
    /// The sea's clock (s since construction). The shore band prescribes its ocean ring at THIS time (Phase G).
    float waveTime() const { return std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_startTime).count(); }
    float waveLength() const { return m_waveLength; }
    float windDirection() const { return m_windDirection; }

    // WIND SPEED (v4 W3): drives the per-body profile (fetch-limited wave energy + Cox-Munk ripple roughness). Kept here
    // with the rest of the sea state, so "the wind" is one object rather than two that can disagree.
    void  setWindSpeed(float metresPerSecond) { m_windSpeed = metresPerSecond; }
    float windSpeed() const { return m_windSpeed; }

    void recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

private:
    void createDescriptorSetLayout(VkDescriptorSetLayout uboLayout);
    void createDescriptorPool();
    void createUnderwaterPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

    VkDevice         m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_underwaterPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE; // set 1 (scene taps + reflection)
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet = VK_NULL_HANDLE;
    bool                  m_sceneBound = false;      // refraction + depth written
    bool                  m_reflectionBound = false; // reflection written

    // ⚑GROUND: 0.45-voxel amplitude on a 14-voxel wavelength — a ~0.9 m swell on a 14 m period, a moderate breeze
    // (Beaufort 4). Runtime-settable via the water_waves debug command.
    float m_waveAmplitude = 0.45f;
    float m_waveLength = 14.0f;
    float m_windDirection = 0.6f;   // radians; the dominant swell heading
    // ⚑GROUND: 6.7 m/s = the mid-point of Beaufort force 4 (11-16 kn), the sea state the amplitude/wavelength above
    // describe (Cox-Munk roughness comes out at exactly 1.0).
    float m_windSpeed = 6.7f;

    std::chrono::high_resolution_clock::time_point m_startTime;
};

} // namespace Graphics
} // namespace Phyxel
