// solver_types.glsl — shared types for the constraint-based particle physics pipeline
//
// SolverBody  : per-particle state (treated as sphere rigid body)
// GPUConstraint: one contact point (normal + 2 friction DOFs)

// ---- SolverBody (128 bytes, std430, all groups vec4-aligned) ----
struct SolverBody {
    vec3  pos;          float invMass;      // 0  .. 15
    vec3  vel;          float pad0;         // 16 .. 31   (m/s)
    vec3  angVel;       float invInertia;   // 32 .. 47   (rad/s, scalar sphere)
    vec4  quat;                             // 48 .. 63   (x y z w)
    vec3  prevVel;      float radius;       // 64 .. 79   (pre-integrate vel, sphere radius)
    vec3  halfExt;      float pad1;         // 80 .. 95   (AABB half-extents)
    uint  particleIdx;  uint flags;
    float friction;     float restitution;  // 96 .. 111
    float pad2;         float pad3;
    float pad4;         float pad5;         // 112 .. 127
};
// 128 bytes

// ---- GPUConstraint (80 bytes, std430) ----
// bodyB == SOLVER_STATIC means body B is the static world (infinite mass, zero velocity).
struct GPUConstraint {
    vec3  n;    float depth;       // 0  .. 15  normal B→A, penetration (> 0 = overlapping)
    vec3  rA;   float lambdaN;     // 16 .. 31  contact arm on A, normal lambda (warmstart)
    vec3  rB;   float lambdaF1;    // 32 .. 47  contact arm on B, friction lambda 1
    vec3  t1;   float lambdaF2;    // 48 .. 63  tangent 1, friction lambda 2
    uint  bodyA; uint bodyB; float mu; float pad0; // 64 .. 79
};
// 80 bytes

const uint SOLVER_STATIC = 0xFFFFFFFFu;

// Solver state buffer slot indices
const uint SS_CONSTRAINT_COUNT = 0u;  // atomic constraint counter
const uint SOLVER_STATE_SIZE   = 4u;

// Baumgarte stabilization strength and slop
const float BAUMGARTE        = 0.2;
const float SLOP             = 0.01;   // > gravity_penetration_per_frame (g*dt^2 ~0.003) + COLLISION_MARGIN
const float COLLISION_MARGIN = 0.005;

#define WORKGROUP_SOLVER 256

// Build orthogonal tangent pair from normal n
void tangentBasis(vec3 n, out vec3 t1, out vec3 t2) {
    t1 = (abs(n.x) < 0.9) ? normalize(cross(n, vec3(1,0,0)))
                           : normalize(cross(n, vec3(0,1,0)));
    t2 = cross(n, t1);
}

// Rotate vector v by quaternion q (xyzw)
vec3 quatRotate(vec4 q, vec3 v) {
    vec3 u = q.xyz;
    return 2.0*dot(u,v)*u + (q.w*q.w - dot(u,u))*v + 2.0*q.w*cross(u,v);
}

// Effective mass denominator for an impulse along direction d at arm r on body with invM, invI
float effectiveMass(float invM, float invI, vec3 r, vec3 d) {
    vec3 rxd = cross(r, d);
    return invM + invI * dot(rxd, rxd);
}
