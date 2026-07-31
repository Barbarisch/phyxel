// Shared LOD metric — GLSL mirror of Phyxel::Core::LodService (engine/include/core/LodService.h).
// C1 of docs/ContinuousLodPlan.md.
//
// ⛔ STATUS: NOT YET INCLUDED BY ANY SHADER. `grep '#include.*lod' shaders/*.vert *.frag *.comp`
// returns nothing. This file is STAGED for the C1 continuation (fading far terrain / grass /
// foliage / water onto the shared metric); it is NOT integration that has happened. Do not cite
// its existence as evidence that shaders share the metric — they do not yet.
// It is also a HAND-MAINTAINED duplicate of LodService.h with no test on the GLSL side, so a
// future edit to the C++ can diverge from it silently.
//
// This is the ONLY place a shader may compute a distance cutoff or a fade. Before C1 the
// engine had at least ten independent hardcoded radii (far terrain, grass, foliage, water,
// character LOD/cull, chunk render distance, occlusion bound, shadow cull, chunk streaming)
// with no shared metric, all in WORLD UNITS — so none of them responded to FOV or resolution.
//
// ⚠️ glslc does NOT track #include dependencies (CLAUDE.md). After editing this file you MUST
// manually recompile every shader that includes it, not just run build_shaders.bat.
//
// Keep in lockstep with LodService.h — the C++ side is unit-tested (LodServiceTest); this
// mirror is not, so any divergence is silent.

#ifndef PHYXEL_LOD_GLSL
#define PHYXEL_LOD_GLSL

// Pixels per world unit at unit distance.
//   frustum slice height at distance d = 2*d*tan(fovY/2)
//   pixels per world unit              = viewportH / (2*d*tan(fovY/2))
float lodPixelScale(float viewportHeight, float tanHalfFovY) {
    return viewportHeight * 0.5 / max(tanHalfFovY, 1e-6);
}

// Screen height in pixels of a `worldSize` object at `distance`.
float lodProjectedPixels(float worldSize, float distance, float viewportHeight, float tanHalfFovY) {
    return worldSize * lodPixelScale(viewportHeight, tanHalfFovY) / max(distance, 1e-4);
}

// Distance at which `worldSize` projects to `targetPixels`.
float lodDistanceForPixels(float worldSize, float targetPixels, float viewportHeight, float tanHalfFovY) {
    return worldSize * lodPixelScale(viewportHeight, tanHalfFovY) / max(targetPixels, 1e-4);
}

// Fade weight across a transition band: 1 = fully near, 0 = fully faded.
// Every subsystem fades on this curve so they stay consistent with each other.
float lodFadeWeight(float projectedPx, float fadeOutPx, float fadeInPx) {
    if (fadeInPx <= fadeOutPx) return projectedPx > fadeOutPx ? 1.0 : 0.0;
    return clamp((projectedPx - fadeOutPx) / (fadeInPx - fadeOutPx), 0.0, 1.0);
}

// Convenience: fade by world distance for an object of known size.
float lodFadeByDistance(float worldSize, float distance, float fadeOutPx, float fadeInPx,
                        float viewportHeight, float tanHalfFovY) {
    return lodFadeWeight(lodProjectedPixels(worldSize, distance, viewportHeight, tanHalfFovY),
                         fadeOutPx, fadeInPx);
}

#endif // PHYXEL_LOD_GLSL
