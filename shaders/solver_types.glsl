// solver_types.glsl — shared types for the Shallot AVBD physics pipeline
//
// SolverBody    : per-particle state (OBB rigid body, 208 bytes)
// GPUConstraint : one contact point (normal + 2 friction DOFs, 128 bytes)
// WarmstartEntry: persisted lambda/penalty/stick from previous frame (64 bytes)
//
// Sign conventions (match Shallot):
//   n       = contact normal pointing from body B toward body A (outward from voxel)
//   depth   = positive penetration depth at constraint creation
//   C_init_n= positive initial depth (stored for AVBD alpha-blend reference)
//   In dual/primal shaders, gap = -depth (negative when penetrating, Shallot convention)
//
// rA/rB are stored in BODY-LOCAL space; dual/primal rotate by current quat each iter
// to get the world-space arm rAW for Jacobian assembly.

// ---- SolverBody (208 bytes, std430) ----
struct SolverBody {
    vec3  pos;          float invMass;      // 0  .. 15
    vec3  vel;          float pad0;         // 16 .. 31
    vec3  angVel;       float invInertia;   // 32 .. 47   (scalar sphere inertia)
    vec4  quat;                             // 48 .. 63   (x y z w)
    vec3  prevVel;      float radius;       // 64 .. 79
    vec3  halfExt;      float pad1;         // 80 .. 95
    uint  particleIdx;  uint flags;
    float friction;     float restitution;  // 96 .. 111
    float pad2;         float pad3;
    float pad4;         float pad5;         // 112 .. 127
    vec3  initial;      float pad6;         // 128 .. 143
    vec4  initialQuat;                      // 144 .. 159
    vec3  inertial;     float pad7;         // 160 .. 175
    vec4  inertialQuat;                     // 176 .. 191
    vec3  cumAng;       float pad8;         // 192 .. 207
};

// ---- GPUConstraint (128 bytes, std430) ----
//
// rA, rB are BODY-LOCAL contact arms (rotate per-iter to world via quatRotate(quat, rA)).
// featureKey discriminates hash collisions; stick is the friction in-cone flag.
struct GPUConstraint {
    vec3  n;    float depth;       // 0   .. 15  normal B→A, penetration depth
    vec3  rA;   float lambdaN;     // 16  .. 31  body-A LOCAL arm, normal lambda
    vec3  rB;   float lambdaF1;    // 32  .. 47  body-B LOCAL arm, friction lambda 1
    vec3  t1;   float lambdaF2;    // 48  .. 63  tangent 1 (world space), friction lambda 2
    uint  bodyA;     uint  bodyB;     float mu;          uint color;        // 64 .. 79
    float penalty_n; float C_init_n;  float penalty_t1;  float penalty_t2;  // 80 .. 95
    uint  featureKey; uint  wsKey;    uint  isNew;       uint  stick;       // 96 .. 111
    float C_init_t1; float C_init_t2; float pad0;        float pad1;        // 112 .. 127
};

// ---- WarmstartEntry (64 bytes, std430) ----
//
// One entry per occupied hash slot. Mirrors Shallot's WarmstartEntry layout.
struct WarmstartEntry {
    float lambdaN;    float penaltyN;
    float lambdaF1;   float penaltyF1;    // 0  .. 15
    float lambdaF2;   float penaltyF2;
    uint  stick;      uint  featureKey;   // 16 .. 31
    vec3  rA;         float pad0;          // 32 .. 47
    vec3  rB;         float pad1;          // 48 .. 63
};

const uint SOLVER_STATIC    = 0xFFFFFFFFu;
const uint UNCOLORED        = 0xFFFFFFFFu;
const uint MAX_COLORS       = 12u;
const uint HASH_EMPTY       = 0xFFFFFFFFu;
const uint FEATURE_KEY_NONE = 0xFFFFFFFFu;
const uint MAX_PROBE        = 128u;

// Solver state buffer layout:
//   [0]              SS_CONSTRAINT_COUNT
//   [1..3]           (counters: WARMSTART_HITS, WARMSTART_LOADED, WARMSTART_NAN)
//   [HASH_BASE..]    open-addressed hash table of wsKey (size = HASH_CAP, pow2)
const uint SS_CONSTRAINT_COUNT  = 0u;
const uint SS_WARMSTART_HITS    = 1u;
const uint SS_WARMSTART_LOADED  = 2u;
const uint SS_WARMSTART_NAN     = 3u;
const uint HASH_BASE            = 8u;
const uint HASH_CAP             = 131072u;  // 60000 * 2 rounded up to pow2
const uint HASH_MASK            = HASH_CAP - 1u;
const uint SOLVER_STATE_SIZE    = HASH_BASE + HASH_CAP;

// AVBD constants (match Shallot)
const float ALPHA            = 0.99;     // Shallot canonical. (1-ALPHA) is the fraction of penetration driven per iter; lower values inject too much energy → popcorn.
const float BETA             = 100000.0;
const float PENALTY_MIN      = 1.0;       // Shallot default; warm-start drives to M/dt² in 1-2 frames
const float PENALTY_MAX      = 1e10;
const float STICK_THRESH     = 1e-5;
const float COLLISION_MARGIN = 0.005;
const float GAMMA            = 0.999;

#define WORKGROUP_SOLVER 256

// ---- Quaternion helpers ----
vec3 quatRotate(vec4 q, vec3 v) {
    vec3 u = q.xyz;
    return 2.0*dot(u,v)*u + (q.w*q.w - dot(u,u))*v + 2.0*q.w*cross(u,v);
}
vec4 quatMul(vec4 a, vec4 b) {
    return vec4(
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    );
}
vec4 quatInv(vec4 q) { return vec4(-q.xyz, q.w); }
vec4 quatIntegrate(vec4 q, vec3 v) {
    vec4 dq = quatMul(vec4(v, 0.0), q);
    return normalize(q + dq * 0.5);
}
vec3 angDispFromInitial(vec4 q, vec4 qInit) {
    vec4 dq = quatMul(q, quatInv(qInit));
    return 2.0 * dq.xyz;
}

// Build orthogonal tangent pair from normal n
void tangentBasis(vec3 n, out vec3 t1, out vec3 t2) {
    t1 = (abs(n.x) < 0.9) ? normalize(cross(n, vec3(1,0,0)))
                           : normalize(cross(n, vec3(0,1,0)));
    t2 = cross(n, t1);
}

float effectiveMass(float invM, float invI, vec3 r, vec3 d) {
    vec3 rxd = cross(r, d);
    return invM + invI * dot(rxd, rxd);
}

// ---- Hash helpers (Shallot-style open-addressed table embedded in solverState) ----
uint hashKey(uint k) {
    uint h = k;
    h ^= h >> 16u;
    h *= 0x85ebca6bu;
    h ^= h >> 13u;
    h *= 0xc2b2ae35u;
    h ^= h >> 16u;
    return h;
}

// Symmetric in (a,b): dynamic-dynamic pairs hash the same regardless of discovery order.
uint packKey(uint a, uint b, uint slot) {
    uint lo = min(a, b);
    uint hi = max(a, b);
    uint h = lo * 0x9e3779b9u + hi;
    h ^= slot * 0x517cc1b7u;
    h ^= h >> 16u;
    h *= 0x85ebca6bu;
    h ^= h >> 13u;
    h *= 0xc2b2ae35u;
    h ^= h >> 16u;
    return (h == HASH_EMPTY) ? (h ^ 1u) : h;
}

bool isNanOrInf(float v) {
    return !(v == v) || abs(v) > 1e30;
}
