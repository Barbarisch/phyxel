// gi_field.glsl — THE ambient term for every receiver (docs/LightingPipeline.md §2).
//
// One answer to "how much indirect light arrives here": the probe field (gi_probe.comp, SSBO
// binding 13, 48x24x48 probes around the viewer, spacing ubo.giProbeGrid.w, refreshed 1/8 per
// frame). Each probe stores an AMBIENT CUBE: six cosine-weighted irradiance lobes (+X -X +Y -Y
// +Z -Z), each the sky-plus-one-bounce light arriving over that axis's hemisphere, traced with 18
// directions through the same micro-resolution occupancy the mesher builds. A surface facing N
// receives  sum_axis N_axis^2 * lobe(sign(N_axis), axis)  -- the standard ambient-cube evaluation,
// so a floor reads its up-lobe, a wall its horizontal lobe, a ceiling its down-lobe, with no
// separate "how much sky" scalar and no squaring of anything.
//
// What this REPLACED (2026-09-17, Ravenmere G-141): a per-fragment 5-ray trace around the surface
// NORMAL (phxSkyVisibility). Its rays hug the horizon, so an exterior wall facing a neighbour 13 u
// away read 0.39 sky access, squared into a 5.6x darker ambient, i.e. black; the same estimator
// produced the G-135 direct-sun defect. The trace is deleted from GLSL; the probe pass bounces off
// this field instead (multi-bounce by iteration). No receiver may reintroduce a sky trace, and no
// receiver may call phxAmbientAtmos with anything but full sky -- tools/lighting_doc_check.py
// enforces both.
//
// Fallback when the field is unavailable or the point lies outside the grid: the unoccluded
// hemisphere (phxAmbientAtmos with sky access 1.0), blended in over the outer band of the grid so
// there is no seam. Inside the grid with no reachable probe: the ambient floor (see phxAmbient).
// Never a second occlusion model. The visible cost is a logged gap: interiors more than ~48 u from
// the viewer read as open sky until a far probe cascade exists (LightingPipeline.md §8).
//
// Requires: lighting.glsl (phxAmbientAtmos, kAmbientFloorAtmos). Takes occBox / grid / skyColor as
// PARAMETERS so it can live in a vertex stage (grass) as well as fragment stages -- the same
// contract occupancy.glsl uses: no implicit uniform reads.
#ifndef PHYXEL_GI_FIELD_GLSL
#define PHYXEL_GI_FIELD_GLSL

#include "lighting.glsl"
#include "occupancy.glsl"   // probe-to-surface visibility (phxDdaHitsSolid)

// PHX_GI_LOBES vec4 per probe, lobe order +X -X +Y -Y +Z -Z. Alphas: lobes 0,1 = validity (1 = the
// probe sits in air); lobes 2,3,4 = the probe's world lattice coordinate L.xyz (the scrolling tag);
// lobe 5 = 0. Layout owned by gi_probe.comp / GiProbeField.cpp.
// The probe pass defines PHX_GI_WRITER: it reads the field (last refresh) to bounce light off it
// and writes its own slice; everything else is a reader.
#ifdef PHX_GI_WRITER
layout(std430, set = 0, binding = 13) buffer GiProbes {
    vec4 probes[];
} giField;
#else
layout(std430, set = 0, binding = 13) readonly buffer GiProbes {
    vec4 probes[];
} giField;
#endif

const int PHX_GI_DIM_X = 48;
const int PHX_GI_DIM_Y = 24;
const int PHX_GI_DIM_Z = 48;
const int PHX_GI_LOBES = 6;
const float PHX_GI_EDGE_FADE_PROBES = 4.0;   // outer band, in probes, blended into the fallback

// SCROLLING (world-stable) ADDRESSING. The grid follows the viewer by re-snapping its origin to
// the 2 u lattice, so probes are addressed by their WORLD lattice coordinate L (world / spacing,
// an integer) wrapped into the slot array: slot = L mod dims. A probe therefore keeps its slot --
// and its blended history -- for as long as it stays inside the grid; only the probes that ENTER
// the grid get a slot whose old contents belong to a probe 96 u away, and those are recognised by
// the lattice tag stored with the probe (lobes 2..4 .a = L.xyz) and treated as fresh/invalid until
// the probe pass rewrites them. Without this (the first G-141 build) every 2 u of camera travel
// shifted the whole buffer under every surface, and the temporal blend then took ~2 s to fade the
// wrong light out -- "textures go dark and come back while moving" (user, 2026-09-19).
ivec3 phxProbeLatticeOrigin(vec4 grid) {
    return ivec3(floor(grid.xyz / max(grid.w, 1e-3) + 0.5));   // origin is a lattice multiple
}
/// True modulo (floor division), never GLSL's %, whose result is undefined for negative operands.
ivec3 phxWrapSlot(ivec3 L) {
    ivec3 d = ivec3(PHX_GI_DIM_X, PHX_GI_DIM_Y, PHX_GI_DIM_Z);
    return L - d * ivec3(floor(vec3(L) / vec3(d)));
}
int phxProbeSlotBase(ivec3 L, ivec3 o) {
    ivec3 sl = phxWrapSlot(L);
    return (sl.x + sl.y * PHX_GI_DIM_X + sl.z * PHX_GI_DIM_X * PHX_GI_DIM_Y) * PHX_GI_LOBES;
}
/// Does the slot for lattice coordinate L currently hold THAT probe (and is it in air)?
float phxProbeValidAt(ivec3 L, ivec3 o) {
    int base = phxProbeSlotBase(L, o);
    vec4 l0 = giField.probes[base];
    if (l0.a < 0.5) return 0.0;
    if (giField.probes[base + 2].a != float(L.x) ||
        giField.probes[base + 3].a != float(L.y) ||
        giField.probes[base + 4].a != float(L.z)) return 0.0;   // stale slot: a probe from elsewhere
    return 1.0;
}

/// Ambient-cube evaluation of ONE probe (by world lattice coordinate) for a surface facing N.
/// Returns rgb; .a = validity (in air AND the slot holds this probe).
vec4 phxProbeIrradianceFor(ivec3 L, ivec3 o, vec3 N) {
    float valid = phxProbeValidAt(L, o);
    int base = phxProbeSlotBase(L, o);
    vec4 lx = giField.probes[base + (N.x >= 0.0 ? 0 : 1)];
    vec4 ly = giField.probes[base + (N.y >= 0.0 ? 2 : 3)];
    vec4 lz = giField.probes[base + (N.z >= 0.0 ? 4 : 5)];
    vec3 n2 = N * N;
    return vec4(lx.rgb * n2.x + ly.rgb * n2.y + lz.rgb * n2.z, valid);
}

/// Trilinear sample of the probe field at an ABSOLUTE world position, for a surface facing N.
/// Returns false when the field is unavailable (bit 3 of occBox.w clear -- binding 13 then holds
/// the inert fallback buffer and must not be read) or the position is outside the grid; `inGrid`
/// tells the caller which. `edge` = 1 deep inside the grid, falling to 0 over the outer
/// PHX_GI_EDGE_FADE_PROBES band.
///
/// WEIGHTED interpolation, not plain trilinear. Each of the 8 lattice neighbours is kept only if
///   validity   -- it sits in air (a probe buried in solid describes a point no surface can see);
///   hemisphere -- it lies in FRONT of the surface (dot(toProbe, N) > 0): a probe behind the
///                 surface plane is on the other side of the wall by definition;
///   visibility -- the straight line from the surface to the probe crosses no solid matter
///                 (phxSegmentBlocked: cube-stepped, micro inside mixed cubes, <= 2*sqrt(3) u).
/// The measured M5.1 failure was light leaking through walls into sealed rooms: with only a soft
/// hemisphere weight, a probe 1 u outside a wall still contributed ~30% to the wall's inside face
/// (Lighting Lab, sealed room at 11% of the open-roof control instead of the 3% floor). The
/// visibility trace is what makes the field honour rule R8: matter blocks light regardless of the
/// voxel size that stores it.
bool phxGiIrradiance(vec3 worldPos, vec3 N, ivec4 occBox, vec4 grid,
                     out vec3 outIrradiance, out float edge, out bool inGrid) {
    outIrradiance = vec3(0.0);
    edge = 0.0;
    inGrid = false;
    if ((occBox.w & 8) == 0) return false;
    float spacing = max(grid.w, 1e-3);
    // NORMAL BIAS (the DDGI recipe): sample the lattice from a point pushed a quarter spacing
    // off the surface along N. A face that lies exactly ON a lattice plane (every wall face at an
    // even world coordinate, with 2 u spacing) otherwise has floor(rel) pick the cell BEHIND it:
    // four neighbours inside the wall (buried) and four behind the plane (rejected), so the face
    // fell to the floor-only fallback. Measured on the Lighting Lab door room: its far wall read
    // exactly the sealed value until this line existed.
    vec3 rel = (worldPos + N * (0.25 * spacing) - grid.xyz) / spacing;
    vec3 dims = vec3(PHX_GI_DIM_X - 1, PHX_GI_DIM_Y - 1, PHX_GI_DIM_Z - 1);
    if (any(lessThan(rel, vec3(0.0))) || any(greaterThan(rel, dims))) return false;
    inGrid = true;
    vec3 toEdge = min(rel, dims - rel);
    edge = clamp(min(toEdge.x, min(toEdge.y, toEdge.z)) / PHX_GI_EDGE_FADE_PROBES, 0.0, 1.0);

    // Cell [b, b+1] per axis; b is clamped so b+1 never leaves the grid (rel == dims exactly).
    ivec3 b = min(ivec3(floor(rel)), ivec3(PHX_GI_DIM_X - 2, PHX_GI_DIM_Y - 2, PHX_GI_DIM_Z - 2));
    vec3  f = rel - vec3(b);
    ivec3 o = phxProbeLatticeOrigin(grid);
    // Start the visibility rays a little off the surface, on its front side, so the surface's own
    // cell never counts as the blocker.
    vec3 from = worldPos + N * (1.5 / 9.0);

    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 8; ++i) {
        ivec3 off = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        vec4 pr = phxProbeIrradianceFor(o + b + off, o, N);
        if (pr.a < 0.5) continue;                       // buried probe, or a slot not yet rewritten for this position

        vec3 probeWorld = grid.xyz + vec3(b + off) * spacing;
        vec3 toProbe = probeWorld - worldPos;
        float len = length(toProbe);
        float facing = (len > 1e-4) ? dot(toProbe / len, N) : 1.0;
        if (facing <= 0.0) continue;                    // behind the surface plane
#ifndef PHX_GI_WRITER
        // Receivers pay for the leak guard. The probe pass reads the field only to weight a 0.30
        // bounce, second order, so it keeps the hemisphere + validity tests and skips the trace:
        // measured, the per-hit traces were the bulk of a 50 ms/frame probe pass on the laptop GPU.
        if (phxSegmentBlocked(from, probeWorld, occBox)) continue;   // a wall between
#endif

        vec3 tri = mix(1.0 - f, f, vec3(off));          // trilinear weight
        float w = tri.x * tri.y * tri.z * facing;
        sum  += pr.rgb * w;
        wsum += w;
    }
    if (wsum < 1e-4) return false;
    outIrradiance = sum / wsum;
    return true;
}

/// THE ambient fill for a surface at worldPos facing N. Rule R2 (LightingPipeline.md §0.3): every
/// receiver calls this and nothing else for ambient. The floor term is the same one the analytic
/// model carries, so a fully sealed room is very dark rather than a hole in the frame.
///
/// Fallbacks, and the direction each one errs in:
///   outside the grid / field off  -> the open-sky hemisphere (the old look, never invented dark);
///   inside the grid, no visible valid neighbour (a pocket narrower than the 2 u lattice) -> the
///   floor only. Inside a building that is the honest answer; outdoors it can only happen in a
///   slot no probe reaches, and a dark slot is the lesser error next to a bright sealed room.
vec3 phxAmbient(vec3 worldPos, vec3 N, ivec4 occBox, vec4 grid, vec3 skyColor) {
    vec3 open = phxAmbientAtmos(N, 1.0, skyColor);
    vec3 floorTerm = skyColor * kAmbientFloorAtmos;
    vec3 irr; float edge; bool inGrid;
    if (phxGiIrradiance(worldPos, N, occBox, grid, irr, edge, inGrid))
        return mix(open, irr + floorTerm, edge);
    return inGrid ? floorTerm : open;
}

/// A 0..1 "how open is this point" scalar DERIVED from the ambient just computed (its luminance
/// against the open-sky answer for the same normal). For the two consumers that need a gate rather
/// than a colour: unshadowed moonlight, and direct sun where the shadow maps have no coverage.
float phxSkyAccessOf(vec3 ambient, vec3 N, vec3 skyColor) {
    const vec3 kLum = vec3(0.2126, 0.7152, 0.0722);
    float open = max(dot(phxAmbientAtmos(N, 1.0, skyColor), kLum), 1e-5);
    return clamp(dot(ambient, kLum) / open, 0.0, 1.0);
}

#endif // PHYXEL_GI_FIELD_GLSL
