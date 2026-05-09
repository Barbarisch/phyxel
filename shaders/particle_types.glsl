// particle_types.glsl — shared definitions for all particle compute shaders
// Include with: #include "particle_types.glsl"
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

// flags bitmask constants
const uint PARTICLE_ACTIVE   = 1u;
const uint PARTICLE_SLEEPING = 2u;
// type bits [3:2]
const uint PARTICLE_TYPE_CUBE    = 0u << 2;
const uint PARTICLE_TYPE_SUBCUBE = 1u << 2;
const uint PARTICLE_TYPE_MICRO   = 2u << 2;
const uint PARTICLE_TYPE_MASK    = 3u << 2;

// Spawn age: bits [23:16] — 8-bit counter (0–255) incremented each physics tick.
// Used to implement a grace period: freshly spawned particles skip character
// collision and sleep checks until they've had time to separate from the player
// and build up gravity-based velocity.
const uint SPAWN_AGE_SHIFT        = 16u;
const uint SPAWN_AGE_MASK         = 0x00FF0000u;
const uint CHAR_COLLISION_GRACE   = 5u;   // skip character push for 5 ticks (~0.08s)
const uint SLEEP_GRACE            = 30u;  // skip sleep check for 30 ticks (~0.5s)

// Character push grace: bits [31:24] — 8-bit countdown (0–255).
// When the player character pushes a particle, this counter is set to
// CHAR_PUSH_GRACE_TICKS. While > 0, surface friction is bypassed so the
// particle slides smoothly instead of decelerating and getting re-pushed
// every 2-3 ticks (which causes visible snapping/choppiness).
const uint CHAR_PUSH_GRACE_SHIFT = 24u;
const uint CHAR_PUSH_GRACE_MASK  = 0xFF000000u;
const uint CHAR_PUSH_GRACE_TICKS = 10u;  // ~0.17s grace at 60Hz

// Collision skin: small gap between particle surface and voxel boundary.
// Prevents sample points from landing exactly on integer cell boundaries,
// which caused adjacent ground cells to register as walls (pit problem).
const float COLLISION_SKIN = 0.01;

// Bounce velocity threshold: impacts with approach speed below this are
// absorbed (velocity zeroed) instead of bounced. Prevents micro-bounce
// jitter when particles rest on surfaces under gravity.
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
    float pad0;
    float pad1;
};

// Workgroup size used by all particle compute shaders
#define WORKGROUP_SIZE 256
