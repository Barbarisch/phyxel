#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace Phyxel {
namespace Graphics {

/// REVERSE-Z DEPTH — the single source of truth for the SCENE pass's depth convention.
///
/// WHY. `maxChunkRenderDistance` doubles as the projection far plane, so "render distance" was a
/// hard geometric clip: raise it and you lose depth precision, lower it and the horizon vanishes.
/// Reverse-Z removes the trade entirely. With a floating-point depth buffer, mapping the NEAR
/// plane to 1.0 and infinity to 0.0 lines float's exponent density up with the region that needs
/// precision, so an INFINITE far plane is not merely tolerable — it is more accurate at every
/// distance than the finite forward-Z setup it replaces. There is then no far plane to configure.
///
/// WHAT IT REPLACES. The old setup was `glm::perspective(...)`, which without
/// GLM_FORCE_DEPTH_ZERO_TO_ONE emits **OpenGL [-1,1]** clip depth into a Vulkan **[0,1]** pipeline.
/// Vulkan clips z_clip < 0, so everything between the near plane and ~2x near was silently thrown
/// away, and half the depth range went unused. That is why the old comment in Camera claimed
/// perspective "tolerates" [-1,1] — it did, by discarding a sliver at the lens and wasting half the
/// buffer. Both defects go away here.
///
/// REQUIREMENTS, all satisfied in this engine:
///   - a FLOAT depth buffer. `VulkanDevice::findDepthFormat()` prefers VK_FORMAT_D32_SFLOAT.
///     With a UNORM depth buffer reverse-Z is merely neutral, not better — it is not harmful.
///   - depth CLEAR to 0.0 (the far value now), not 1.0  -> kSceneDepthClear
///   - depth COMPARE greater instead of less                -> sceneDepthCompareOp()
///
/// SCOPE — READ THIS BEFORE EDITING A PIPELINE. Reverse-Z applies to the **scene** pass only.
/// The SHADOW pass keeps forward-Z: it renders into its own depth attachment with its own
/// `glm::orthoRH_ZO` light matrix (already correct Vulkan [0,1]), and `voxel.frag` compares
/// shadow-map depth directly. Flipping the shadow pipelines
/// (`ShadowMap::createPipeline`, `buildDepthOnlyPipelineState`) or the shadow clear would invert
/// the shadow test and is NOT part of this change. Their depth-bias tuning (constant 1.25 /
/// slope 1.75) is likewise forward-Z tuning and must stay put.
namespace DepthConvention {

/// Compile-time, not a live toggle: pipelines bake the compare op at creation and the projection
/// must agree with every already-recorded depth value, so this cannot be flipped mid-frame the way
/// s_quadDraw or s_fineGreedyMerge can. Flip it and rebuild to A/B.
inline constexpr bool kReverseZ = true;

/// Depth value the scene depth attachment clears to. Reverse-Z clears to the FAR value, 0.0.
inline constexpr float kSceneDepthClear = kReverseZ ? 0.0f : 1.0f;

/// Depth test for scene geometry: nearer fragments have LARGER depth under reverse-Z.
inline constexpr VkCompareOp sceneDepthCompareOp() {
    return kReverseZ ? VK_COMPARE_OP_GREATER : VK_COMPARE_OP_LESS;
}

/// The `_OR_EQUAL` variant, for passes that re-draw coplanar geometry (debug lines, OIT, mirror)
/// and must not z-fight themselves away.
inline constexpr VkCompareOp sceneDepthCompareOpEqual() {
    return kReverseZ ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
}

/// Infinite reverse-Z perspective, right-handed (-Z forward), Vulkan [0,1] clip depth, with the
/// Vulkan Y-flip already applied to match this renderer's convention.
///
/// Derivation (so nobody has to reverse-engineer the literals): we want
///   z_ndc = near / (-z_view)   ->  1 at the near plane, approaching 0 as z_view -> -infinity.
/// Writing clip.z = near and clip.w = -z_view gives exactly that, i.e. the third column is all
/// zeros except w = -1, and the fourth column carries `near` in z. There is deliberately NO far
/// term anywhere in the matrix — that is the whole point.
inline glm::mat4 infiniteReverseZPerspective(float fovYRadians, float aspect, float nearP) {
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    glm::mat4 m(0.0f);
    m[0][0] =  f / aspect;
    m[1][1] =  f;
    m[2][3] = -1.0f;    // clip.w = -z_view
    m[3][2] =  nearP;   // clip.z = near
    m[1][1] *= -1.0f;   // Vulkan flips Y vs OpenGL
    return m;
}

} // namespace DepthConvention
} // namespace Graphics
} // namespace Phyxel
