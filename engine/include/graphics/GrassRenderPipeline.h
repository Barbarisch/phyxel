#pragma once

#include "graphics/WindSystem.h"
#include "graphics/GrassSiteOrder.h"   // kGrassGrid / kGrassSiteOrder / kGrassSeqSep (generated)

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
        /// Master wind amplitude. Raised 0.50 -> 1.0 on 2026-08-21 (user: "wind is way too
        /// slight"; target = lively meadow). At the WindSystem defaults this lands a ~15 degree
        /// average lean with gusts bowing blades to ~43 degrees — computed from
        /// leanSin ≈ 2*(base + gustAmp*gust)*windStrength, verified in the wind debug view.
        /// The lean cap (64 degrees) makes raising this safe.
        float    windStrength   = 1.0f;   ///< master wind amplitude (scaled by blade height in-shader)
        float    growDuration   = 6.0f;   ///< seconds for the sprout-in ramp
        /// Blades on a FULLY grassy voxel. Raised 30 -> 55 on 2026-08-21 (user: "noticeably
        /// denser"), together with the patch-field coverage (mean keep ~83%, so ~46 visible in
        /// an average cell). BUDGET (recompute before touching this, radius or the falloff —
        /// the total is quadratic in radius): disc integral of the per-blade density curve at
        /// R=224 gives ~17.4k blades per count unit -> 55 blades * 24 verts (4 segments) ≈ 23M
        /// verts/frame, at the historically-accepted 22.8M operating point (was 9.4M at 30*18).
        /// Spacing at 55 is 0.0957 u against a 0.042 u blade, so the packing clamp (0.0432)
        /// never trims a near-field blade.
        /// NOT required to be a multiple of kBladesPerClump any more — there are no clumps.
        uint32_t bladesPerVoxel = 55;     ///< procedural blades/voxel, one per lattice cell
        /// 0 = smooth ribbon tapering to a true point (DEFAULT since 2026-08-21, user call:
        /// "go back to pointy grass" — supersedes the 2026-07-11 boxy default), 1 = boxy
        /// rectangle (rest height quantized to microcube steps; the quantization is also why
        /// boxy made the voxel grid readable in grass height).
        uint32_t bladeStyle     = 0;      ///< 0 = smooth tapered ribbon (default), 1 = boxy rectangle
        float    pushStrength   = 0.55f;  ///< character-displacer bend amplitude (0 = interaction off)

        // ── MEADOW HEIGHT FIELD — the plain-scale height modifier ─────────────────────────
        // Two octaves of value noise in ABSOLUTE world space, so the field is identical either
        // side of any voxel or chunk boundary by construction (appearance is a pure function of
        // world position — docs/FeatureDesignKeys.md). Periods are WORLD UNITS.
        /// Dominant octave period. This is the knob that decides how large a "plain" reads as:
        /// the point is height varying across a FIELD you walk over, not per-voxel roughness.
        /// Anything shorter than the detail octave reads as noise and undoes the smoothness.
        /// ⚠ Periods MUST DIVIDE 2048 (the shader's hash-domain wrap, which passes through the
        /// world origin) or the meadow seams at every wrap line — the field noise is lattice-
        /// periodic (vnoise2p) and only divisor periods tile exactly. 72/26 -> 64/32 on
        /// 2026-08-21 for this reason (GrassMeadowSeamTest pins the continuity).
        float    meadowScale        = 64.0f;
        float    meadowDetailScale  = 32.0f;   ///< detail octave period, world units (divisor of 2048)
        float    meadowDetailWeight = 0.30f;   ///< 0..1; dominant octave takes the remainder
        float    heightMin          = 0.45f;   ///< height multiplier in cropped zones
        float    heightMax          = 1.85f;   ///< height multiplier in lush zones

        // ── EDGE TAPER — grass shortens toward the edge of a plain ────────────────────────
        /// Height multiplier at a fully-exposed edge. Deliberately well above 0: a bare edge
        /// voxel should read as SHORT grass, not as missing grass.
        float    edgeTaperFloor     = 0.40f;
        /// Reshapes the ramp without moving its endpoints. >1 holds full height further in then
        /// falls fast; <1 starts falling early and eases out. 1.0 = the original linear ramp.
        float    edgeTaperCurve     = 1.0f;
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

    // ── BLADE PLACEMENT — CPU mirror of grass.vert ────────────────────────────────────────────
    // Pure + static so tests enumerate the REAL blade roots without a Vulkan device, per the
    // GrassDensityLodTest convention (a test that re-derives the formula locally passes even when
    // the feature is disabled entirely). tests/graphics/GrassBladePackingTest.cpp measures the
    // non-overlap guarantee on these.

    /// Voxel-local [0,1]^2 root of `blade` on the grass voxel at ABSOLUTE world cell (cx,cy,cz).
    /// Absolute, not chunk-relative: grass.vert hashes `mod(absoluteCell, 2048)` precisely so a
    /// blade does not re-roll as the camera moves, and so two voxels either side of a chunk border
    /// agree. Passing a chunk-relative cell here would measure a different world than the shader
    /// draws.
    static glm::vec2 bladeRootLocal(int cx, int cy, int cz, uint32_t blade,
                                    uint32_t bladesPerVoxel);

    /// Blade width in WORLD UNITS as the shader computes it, including the density compensation
    /// and the sub-pixel floor. Excludes shadowWidthScale: the guarantee is about visible blades
    /// (shadow proxies may overlap; they union harmlessly in the depth buffer).
    static float bladeWidthAt(float dist, float radius, uint32_t bladesPerVoxel,
                              float widthScale, bool boxy);

    /// Per-blade density fraction, mirroring grass.vert's `densityFrac`.
    static float densityFracAt(float dist, float radius);

    /// Meadow height multiplier exactly as grass.vert computes it for a blade whose root sits at
    /// voxel-local `rootLocal` on the grass voxel at ABSOLUTE world cell (cx, ·, cz) — the value
    /// that multiplies Params::bladeHeight. CPU mirror, pure + static, same convention as
    /// bladeRootLocal above. tests/graphics/GrassMeadowSeamTest.cpp asserts on it that blade
    /// height is CONTINUOUS across voxel/chunk borders and across the 2048-unit hash-domain wrap
    /// — the "you can see the chunk grid in the grass" defect class.
    static float meadowHeightMulAt(int cx, int cz, glm::vec2 rootLocal, const Params& p);

    /// Jitter as a fraction of the guaranteed spacing, and the safety margin on the width
    /// clamp. MUST MATCH kJitterFrac / kPackMargin in grass.vert — they define the packing
    /// budget jointly: jitter takes 2*kJitterFrac of the spacing and the blade may occupy at
    /// most kPackMargin of what is left. Deriving jitter from the width instead would be
    /// circular (width <- distance <- root <- jitter).
    static constexpr float kJitterFrac = 0.25f;
    static constexpr float kPackMargin = 0.95f;

    /// Guaranteed minimum spacing for `blades` blades — the CONTINUOUS envelope Cseq/sqrt(N).
    /// Never use the per-N staircase: adjacent voxels can keep different N and the staircase
    /// lets the sparser one assume a separation its neighbour does not honour.
    static float sepGuaranteed(uint32_t blades);

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
