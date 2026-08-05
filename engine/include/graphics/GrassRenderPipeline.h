#pragma once

#include "graphics/WindSystem.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Phyxel {
namespace Graphics {

/// Renders the lightweight grass-blade layer on top of grass-topped terrain voxels.
///
/// Strategy (see the grass plan):
///   - One GPU instance per grass voxel (GrassInstanceData, 8 bytes), stored in each chunk's own
///     grassBuffer (parallel to its face buffer, built at remesh). No per-frame CPU work.
///   - The vertex shader (grass.vert) procedurally fans each instance into `bladesPerVoxel` blades
///     via gl_VertexIndex — 6 vertices/blade, no vertex buffer. Wind + sprout-in growth read
///     UBO.elapsedTime; distance fade reads UBO.cameraPosition.
///   - Cutout (alpha-tested via discard in grass.frag), so it renders in the opaque scene pass with
///     NO transparency/OIT sort cost. Cull OFF (blades are double-sided).
///   - Reuses the existing set-0 descriptor (UBO + texture arrays) — colour is sampled from the
///     voxel's own grass-top texture, so biome variants (forest/savanna) tint automatically.
class GrassRenderPipeline {
public:
    GrassRenderPipeline();
    ~GrassRenderPipeline();

    /// @param uboDescriptorSetLayout  Existing set-0 layout (UBO + atlas arrays + shadow + lights)
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D extent,
                    VkDescriptorSetLayout uboDescriptorSetLayout);

    void cleanup();

    /// Runtime-tunable knobs (Phase 3). Density and radius are affordable at these values only
    /// because of the per-chunk density LOD below — do not raise `radius` without it.
    struct Params {
        bool     enabled        = true;
        float    radius         = 224.0f; ///< world units: grass drawn only within this of camera
        /// Deliberately large (a third of the radius): the meadow must THIN OUT over a long band,
        /// not stop. A short fade reads as a circular mowing line tracking the camera.
        float    fadeRange      = 80.0f;  ///< world units of fade-to-zero height before the radius edge
        /// Raised 0.32 -> 0.44 (user: "should be taller mostly"). The meadow field multiplies this
        /// by 0.45-1.85, so the lush end now reaches ~0.81 units against a 1-unit voxel.
        float    bladeHeight    = 0.44f;  ///< full-grown blade height (world units; character ≈ 1.75)
        /// Runtime multiplier on the authored blade WIDTH (1.0 = as authored, ~0.040 u).
        /// Width — not height — is what decides whether a blade exceeds a shadow-map texel
        /// (~0.125 u at the default 420 u shadow distance), so it must be sweepable to test
        /// shadow casting at all. Rides the previously-unused `widthScale` push constant, so
        /// no push-constant layout change. POST /api/debug/grass {"bladeWidth": N}.
        float    bladeWidthScale = 1.0f;
        float    windStrength   = 0.13f;  ///< master wind amplitude (scaled by blade height in-shader)
        float    growDuration   = 6.0f;   ///< seconds for the sprout-in ramp
        /// 126 = 18 clumps/voxel. Raised from 70 (user: "grass should be far more dense"). The
        /// continuous per-blade density falloff in grass.vert is what keeps this affordable —
        /// only the near band ever pays the full count.
        uint32_t bladesPerVoxel = 140;    ///< procedural blades/voxel, grouped into tufts (7/clump)
        uint32_t bladeStyle     = 1;      ///< 1 = boxy rectangle blades (default), 0 = smooth tapered ribbon
        float    pushStrength   = 0.55f;  ///< character-displacer bend amplitude (0 = interaction off)
        /// Shared wind-field state — overwritten every frame by RenderCoordinator from the
        /// single WindSystem (grass and foliage always see identical wind). Not user-tunable
        /// here; tune via /api/debug/wind.
        WindSystem::State wind;
    };

    /// Blades per clump in grass.vert. The density LOD drops WHOLE clumps by shortening the draw,
    /// so every count it picks must be a multiple of this.
    static constexpr uint32_t kBladesPerClump = 7;

    /// `k` in the density curve 1/(1 + k*t^2), t = dist/radius. MUST match the literal in
    /// grass.vert's densityFrac — the CPU bound and the shader's own test have to agree or the
    /// bound can clip blades the shader wanted, which is what the per-chunk seam looked like.
    /// FULL density inside kDensityNearBand (a fraction of the radius), then the falloff over the
    /// remapped remainder. The flat near band stops the meadow reading as "dense at my feet,
    /// thinner just ahead"; the falloff past it is a cost constraint, not a look choice.
    static constexpr float kDensityNearBand = 0.15f;
    static constexpr float kDensityFalloff  = 140.0f;

    /// Density LOD: blades actually drawn for a chunk whose center sits `dist` from the camera,
    /// as a fraction of `bladesPerVoxel`. Pure + static so tests can call it without a device.
    ///
    /// Why shortening the draw is pop-free: grass.vert derives every blade's clump, seeds and
    /// height from its own `gl_VertexIndex` and the FULL `bladesPerVoxel` (pushed unchanged), so
    /// drawing the first N blades leaves the survivors BIT-IDENTICAL and simply omits the
    /// highest-indexed clumps. Re-deriving the shader's hashes from a reduced count instead would
    /// re-roll every blade at each tier boundary — a visible shimmer across the whole band.
    static uint32_t bladesForDistance(uint32_t bladesPerVoxel, float dist, float radius);

    /// Area-conserving width compensation for a reduced blade count: fewer, wider blades cover the
    /// same ground, so a tier change does not read as the meadow thinning out.
    static float widthCompensation(uint32_t bladesDrawn, uint32_t bladesPerVoxel);

    /// One draw record per visible grass chunk (assembled by RenderCoordinator from the chunks that
    /// survived static-geometry culling AND lie within Params::radius of the camera).
    struct ChunkDraw {
        VkBuffer  buffer;   ///< chunk's grass instance buffer (arena: the region block)
        uint32_t  count;    ///< grass instance count (voxels)
        glm::vec3 origin;   ///< chunk world origin (push-constant base offset)
        VkDeviceSize bindOffset = 0;  ///< 4.3 A2: arena span byte offset (0 legacy)
        float     centerDist = 0.0f;  ///< chunk-center distance to camera (drives the density LOD)
    };

    /// Record grass draws. Call inside the scene render pass, after static geometry.
    /// @param uboSet  Per-frame set 0 (view/proj/elapsedTime/cameraPosition/atlas).
    void render(VkCommandBuffer cmd, VkDescriptorSet uboSet, const std::vector<ChunkDraw>& chunks);

    /// Create the shadow-caster variant (depth-only, same procedural blade cutout) against
    /// the shadow map's render pass. Call AFTER initialize(); safe to skip — grass then
    /// simply casts no shadows.
    bool initializeShadow(VkRenderPass shadowRenderPass, VkExtent2D shadowExtent);

    /// Record grass SHADOW draws. Call inside the shadow render pass. Reuses the per-frame
    /// descriptor set; grass_shadow.vert reads only the UBO.
    void renderShadow(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                      const std::vector<ChunkDraw>& chunks);

    /// Grass shadow CASTING on/off (POST /api/debug/grass {"castShadows":bool}). Exists so
    /// "do blades cast?" is answered by an exact A/B — render the same frame with it on and
    /// off and diff the pixels — instead of by eyeballing hairline features in a screenshot,
    /// which produced five wrong conclusions in a row on 2026-08-03.
    static bool s_castShadows;

    /// How much wider than the real blade the SHADOW proxy is drawn — 1.0 = identical width,
    /// 2.0 = double (POST /api/debug/grass {"shadowWidthScale": N}).
    /// This replaced a shadow-TEXEL clamp, which sized the shadow to the shadow map rather than
    /// to the blade and so grew with shadow distance: 0.50u of shadow for a ~0.10u blade at 420.
    /// Tying it to the blade keeps the shadow proportional; the price is that a blade under one
    /// texel stops casting instead of smearing, which a near cascade would fix.
    static float s_shadowWidthScale;

    /// Recreate pipeline after swapchain resize (caller must re-run initialize()).
    void recreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

    Params&       params()       { return m_params; }
    const Params& params() const { return m_params; }

    /// Camera-relative rendering: true camera world position, set per frame before draws
    /// (chunk offsets are pushed as world - camera; hash seeds stay absolute).
    void setCameraWorld(const glm::vec3& camPos) { m_cameraWorld = camPos; }

    /// World size of ONE shadow-map texel this frame (2 * fittedRadius / mapWidth). The shadow
    /// pass clamps blade width to a few of these so a blade always covers a texel centre and
    /// actually rasterizes. MEASURED on the single-blade rig: a 0.04 u blade writes NOTHING at
    /// a 0.024 u texel; 0.08 u casts. Texel size scales with shadow distance, so this has to be
    /// per-frame data — a fixed widening is correct at exactly one distance and wrong elsewhere.
    void setShadowTexelWorld(float t) { m_shadowTexelWorld = t; }

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout uboLayout);

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;
    VkPipeline       m_shadowPipeline = VK_NULL_HANDLE;  ///< depth-only caster (may be null)
    Params           m_params;
    glm::vec3        m_cameraWorld{0.0f};   // per-frame camera position (camera-relative rendering)
    float            m_shadowTexelWorld = 0.0f;  ///< see setShadowTexelWorld
};

} // namespace Graphics
} // namespace Phyxel
