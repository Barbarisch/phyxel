// particle_types.glsl — shared definitions for all particle compute shaders
// Include with: #include "particle_types.glsl"
// Constants and push-constant layouts shared with C++ live in solver_shared.h (1b).
#include "solver_shared.h"
//
// GpuParticle std430 layout (96 bytes, verified matches C++ GpuParticle struct):
//   offset  0: vec3  position      (12 bytes)
//   offset 12: float lifetime      (4 bytes)
//   offset 16: vec3  prevPosition  (12 bytes)
//   offset 28: float maxLifetime   (4 bytes)
//   offset 32: vec4  rotation      (16 bytes, quaternion x,y,z,w)
//   offset 48: vec3  angularVel    (12 bytes)
//   offset 60: uint  flags         (4 bytes)
//   offset 64: vec3  scale         (12 bytes)
//   offset 76: uint  materialIndex (4 bytes)
//   offset 80: vec4  color         (16 bytes)
//   total:     96 bytes

struct GpuParticle {
    vec3  position;
    float lifetime;
    vec3  prevPosition;
    float maxLifetime;
    vec4  rotation;
    vec3  angularVel;
    uint  flags;
    vec3  scale;
    uint  materialIndex;
    vec4  color;
};

// flags bits: PARTICLE_ACTIVE / PARTICLE_SLEEPING / PARTICLE_TYPE_* are in solver_shared.h.

// Spawn age: bits [23:16] — 8-bit counter (0–255) incremented each physics tick.
// Freshly spawned particles skip the sleep check until they've had time to separate.
const uint SPAWN_AGE_SHIFT        = 16u;
const uint SPAWN_AGE_MASK         = 0x00FF0000u;
const uint SLEEP_GRACE            = 30u;  // skip sleep check for 30 ticks (~0.5s)

// Bits [31:24] are FREE since 2026-10-04 (DebrisInteractionPlan D4 deleted the legacy
// XPBD pipeline, the only user of the old CHAR_PUSH_GRACE byte). Bits [15:8] are the
// sleep counter (solver_types.glsl SLEEP_CTR_*).

// Per-material physics properties (32 bytes, std430).
// Uploaded once at init from C++ MaterialProperties table.
// Index with materialIndex from GpuParticle.
struct MaterialPhysics {
    float mass;           // Gravity scaling (~0.2 cork .. 6.0 stone)
    float restitution;    // Bounciness (0.0 = dead stop, 1.0 = perfect bounce)
    float friction;       // Surface grip (0.0 = ice, 1.0 = rubber)
    float linearDamp;     // Air drag on velocity per frame (~0.98–0.999)
    float angularDamp;    // Air drag on spin per frame (~0.95–0.99)
    float breakForceScale;// Impulse multiplier at spawn
    float buoyancy;       // water density / material density (Phase 6c; > 1 floats)
    float pad1;
};

// Workgroup size used by all particle compute shaders (solver_shared.h)
#define WORKGROUP_SIZE PHX_WORKGROUP
