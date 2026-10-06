// solver_shared.h — the ONE definition of every constant and push-constant layout shared by the
// GPU debris solver's C++ host (engine GpuParticlePhysics) and its compute shaders (GLSL).
// DebrisInteractionPlan Phase 1b, 2026-10-04.
//
// WHY: these used to be written twice — `static constexpr` in GpuParticlePhysics.h and `const`
// in particle_types.glsl / solver_types.glsl / voxel_contact.glsl / individual shaders — kept in
// sync by "must match" comments. The C++ push-constant structs were even declared twice in
// GpuParticlePhysics.cpp, and the two copies had already drifted (a `pad` in one was `flags` in
// the other). A value changed on one side compiled cleanly and corrupted the GPU silently.
//
// HOW: valid as both C++ and GLSL. GLSL (glslang, GL_GOOGLE_include_directive) sees
// `const uint X = ...;`; C++ sees `constexpr uint X = ...;` inside Phyxel::DebrisShared with
// `uint` = uint32_t. Push constants are FIELD macros: a shader writes
//     layout(push_constant) uniform PC { PHX_PC_INTEGRATE } pc;
// and C++ gets `struct IntegratePC { PHX_PC_INTEGRATE };` (below), so the layout exists once.
// Every C++ size is static_asserted at the bottom.
//
// Only values BOTH sides use live here. GLSL-only tuning (sleep tiers, penalty bounds, the
// sample pattern) stays in solver_types.glsl / voxel_contact.glsl.

#ifndef PHX_SOLVER_SHARED_H
#define PHX_SOLVER_SHARED_H

#ifdef __cplusplus
#include <cstdint>
namespace Phyxel { namespace DebrisShared {
typedef uint32_t uint;
struct ivec4 { int32_t x, y, z, w; };   // GLSL ivec4 (16-byte aligned in push constants)
struct vec4  { float x, y, z, w; };     // GLSL vec4 (std430 storage layouts, e.g. PHX_KINEMATIC_BOX)
#define PHX_CONST constexpr
#else
#define PHX_CONST const
#endif

// ---- Dispatch ---------------------------------------------------------------------------
// A macro, not a const: GLSL layout(local_size_x = ...) wants a plain integer constant.
#define PHX_WORKGROUP 256          // local_size_x of every per-body / per-cell / per-constraint pass
#define PHX_SCAN_BLOCK 256         // cells scanned per workgroup by particle_scan_block.comp
PHX_CONST uint WORKGROUP  = PHX_WORKGROUP;

// ---- Capacities ---------------------------------------------------------------------------
PHX_CONST uint MAX_PARTICLES     = 10000u;
PHX_CONST uint MAX_CONSTRAINTS   = 60000u;
PHX_CONST uint MAX_COLORS        = 32u;   // was 12: packed piles need up to ~27 colours (26-neighbourhood); bodies past the cap were SKIPPED by primal (audit D1)
PHX_CONST uint MAX_KINEMATIC     = 512u;  // kinematic bodies (movers), Phase 2
// Kinematic bodies live at FIXED body indices [KINEMATIC_BASE, KINEMATIC_BASE + MAX_KINEMATIC):
// never inside the particle range, so count/grid/integrate/sync never see them (Phase 2).
PHX_CONST uint KINEMATIC_BASE    = MAX_PARTICLES;   // character collider boxes: 4 torso + 4 arm + 4 leg

// ---- Broadphase grid (cell = floor(position) wrapped to a 64³ torus) -----------------------
PHX_CONST int GRID_SIZE   = 64;
PHX_CONST int GRID_CELLS  = GRID_SIZE * GRID_SIZE * GRID_SIZE;   // 262,144
PHX_CONST int SCAN_BLOCK  = PHX_SCAN_BLOCK;
PHX_CONST int SCAN_BLOCKS = GRID_CELLS / SCAN_BLOCK;             // 1024

// ---- Solver state buffer: header counters, warm-start hash, wake bits ----------------------
PHX_CONST uint HASH_BASE  = 16u;       // header counters live in [0, HASH_BASE) (room to grow, 1e)
PHX_CONST uint HASH_CAP   = 131072u;   // power of two, >= 2 * MAX_CONSTRAINTS
PHX_CONST uint WAKE_WORDS = 320u;      // one bit per body: 320 * 32 = 10240 >= MAX_PARTICLES
// Header counter slots (the settle probe reads these back).
PHX_CONST uint SS_CONSTRAINT_COUNT     = 0u;
PHX_CONST uint SS_WARMSTART_HITS       = 1u;
PHX_CONST uint SS_WARMSTART_LOADED     = 2u;
PHX_CONST uint SS_WARMSTART_NAN        = 3u;
PHX_CONST uint SS_HARDCONTACT_FIRES    = 4u;   // bodies the post-solve push-out moved
PHX_CONST uint SS_HARDCONTACT_DEPTH_UM = 5u;   // deepest push-out this tick, micrometres (atomicMax)
PHX_CONST uint SS_WAKE_REQUESTS        = 6u;   // wake bits set this tick (impact + character)
PHX_CONST uint SS_FROZEN_UNKNOWN       = 7u;   // bodies HELD this tick: a contact sample needed occupancy the pool does not have (1c)
PHX_CONST uint SS_KINEMATIC_CONTACTS   = 8u;   // bodies a mover (character collider / scripted box) pushed this tick (1e)
PHX_CONST uint SS_IMPULSES_APPLIED     = 9u;   // bodies an impulse reached this tick (Phase 4; 0 until then)
PHX_CONST uint SS_KINEMATIC_DEPTH_UM   = 10u;  // deepest tick-start penetration into a kinematic body, micrometres (Phase 2)

// ---- GpuParticle.flags bits ------------------------------------------------------------------
PHX_CONST uint PARTICLE_ACTIVE       = 1u;
PHX_CONST uint PARTICLE_SLEEPING     = 2u;
PHX_CONST uint PARTICLE_TYPE_CUBE    = 0u << 2;   // type bits [3:2]
PHX_CONST uint PARTICLE_TYPE_SUBCUBE = 1u << 2;
PHX_CONST uint PARTICLE_TYPE_MICRO   = 2u << 2;
PHX_CONST uint PARTICLE_TYPE_MASK    = 3u << 2;
PHX_CONST uint PARTICLE_KINEMATIC    = 1u << 4;   // a mover body (invMass 0, moved by the CPU), Phase 2

// ---- GpuParticle.materialIndex packing (DebrisInteractionPlan 1d texture parity) -------------
// Bits 0-15: material index (~102 materials). Bits 16-25: the piece's micro position INSIDE its
// parent cube, mx*81 + my*9 + mz (0..728), so a broken subcube/microcube keeps showing the slice
// of the parent texture it showed while static. EVERY material reader masks with MATERIAL_MASK:
// particle_expand (texture), solver_sync_in + solver_integrate (physics), the C++ readbacks.
PHX_CONST uint MATERIAL_MASK = 0xFFFFu;
PHX_CONST uint SLICE_SHIFT   = 16u;
PHX_CONST uint SLICE_MASK    = 0x3FFu;

// ---- Solver switches (runtime A/B of the settling fixes; pushed every tick) ----------------
PHX_CONST uint SOLVER_FLAG_MASS_PENALTY    = 1u;   // cold contacts start at m/dt² stiffness, not 1
PHX_CONST uint SOLVER_FLAG_START_AT_REST   = 2u;   // slow bodies start the solve from x⁻ (three-avbd)
PHX_CONST uint SOLVER_FLAG_HC_NEUTRAL      = 4u;   // hard-contact push-out adds no velocity
PHX_CONST uint SOLVER_FLAG_POST_STAB       = 8u;   // alpha=1 solve + one alpha=0 pass; measured WORSE, off
PHX_CONST uint SOLVER_FLAG_STATIC_FRICTION = 16u;  // stiff cold friction rows + anchored static friction
PHX_CONST uint SOLVER_FLAG_KINEMATIC_CONTACTS = 32u; // movers are AVBD bodies (Phase 2); OFF = movers touch nothing (the D7 shove is deleted)
PHX_CONST uint SOLVER_FLAGS_DEFAULT        = 55u;  // all but POST_STAB; movers are AVBD bodies (Phase 2)
PHX_CONST float SOLVER_ALPHA               = 0.99f; // error-correction alpha (Shallot canonical)
PHX_CONST uint PRIMAL_STORE_VELOCITY       = 0xFFFFFFFEu;  // PrimalPC.targetColor sentinel

// ---- Push-constant layouts (fields only) -----------------------------------------------------
#define PHX_PC_COUNT       uint count;                                  /* grid_build, sort_scatter */
#define PHX_PC_CELLS       uint cellCount;                              /* grid_clear, scan_block, scan_add */
#define PHX_PC_BLOCKS      uint numBlocks;                              /* scan_blocksums */
#define PHX_PC_BODIES      uint bodyCount;                              /* body_color, prefix_sum */
#define PHX_PC_CONSTRAINTS uint maxConstraints;                         /* csr_count, csr_scatter, warmstart_save */
#define PHX_PC_CSR_CLEAR   uint bodyCount; uint maxConstraints;
#define PHX_PC_SYNC_IN     uint count; float dt;
#define PHX_PC_INTEGRATE   uint count; float dt; float gravity; uint flags;
#define PHX_PC_CONTACTS    uint count; uint maxConstraints; uint flags; float coldScale; ivec4 occBox;  /* narrowphase, voxel; coldScale x m/dt² = cold stiffness; occBox = occupancy box min chunk + bit0 ready (1c) */
#define PHX_PC_DUAL        uint maxConstraints; float dt; uint pad0; float alpha;         /* alpha: ALPHA, or 1 under post-stab */
#define PHX_PC_PRIMAL      uint bodyCount; float dt; uint targetColor; float alpha;       /* targetColor: a colour, or PRIMAL_STORE_VELOCITY */
#define PHX_PC_HARDCONTACT uint count; uint flags; float pad1; float pad2; ivec4 occBox;
#define PHX_PC_SYNC_OUT    uint count; float dt; float lifetimeDt; uint flags;
#define PHX_PC_EXPAND      uint count; uint maxFaceSlots; float interpAlpha;              /* interpAlpha: fraction of FIXED_DT since the last tick */
#define PHX_PC_KINEMATIC   uint boxCount; uint tick; float dt; uint pad0;                 /* kinematic_sync: tick = sub-step index in this frame */

// One kinematic box as the CPU uploads it (host-mapped, std430). The kinematic_sync pass turns
// it into SolverBody KINEMATIC_BASE + k each tick: initial = center + velocity*dt*tick.
#define PHX_KINEMATIC_BOX  vec4 center; vec4 halfExt; vec4 rotation; vec4 velocity;     /* .w unused; rotation = quat xyzw */

#ifdef __cplusplus
struct GridCountPC      { PHX_PC_COUNT };
struct GridCellsPC      { PHX_PC_CELLS };
struct ScanBlocksPC     { PHX_PC_BLOCKS };
struct BodiesPC         { PHX_PC_BODIES };
struct ConstraintsPC    { PHX_PC_CONSTRAINTS };
struct CsrClearPC       { PHX_PC_CSR_CLEAR };
struct SyncInPC         { PHX_PC_SYNC_IN };
struct IntegratePC      { PHX_PC_INTEGRATE };
struct ContactsPC       { PHX_PC_CONTACTS };
struct DualPC           { PHX_PC_DUAL };
struct PrimalPC         { PHX_PC_PRIMAL };
struct HardContactPC    { PHX_PC_HARDCONTACT };
struct SyncOutPC        { PHX_PC_SYNC_OUT };
struct ExpandPC         { PHX_PC_EXPAND };
struct KinematicPC      { PHX_PC_KINEMATIC };
struct KinematicBoxGpu  { PHX_KINEMATIC_BOX };

// Sizes as the shaders see them (std430 push-constant packing of 4-byte scalars).
static_assert(sizeof(GridCountPC)   == 4,  "PHX_PC_COUNT");
static_assert(sizeof(GridCellsPC)   == 4,  "PHX_PC_CELLS");
static_assert(sizeof(ScanBlocksPC)  == 4,  "PHX_PC_BLOCKS");
static_assert(sizeof(BodiesPC)      == 4,  "PHX_PC_BODIES");
static_assert(sizeof(ConstraintsPC) == 4,  "PHX_PC_CONSTRAINTS");
static_assert(sizeof(CsrClearPC)    == 8,  "PHX_PC_CSR_CLEAR");
static_assert(sizeof(SyncInPC)      == 8,  "PHX_PC_SYNC_IN");
static_assert(sizeof(IntegratePC)   == 16, "PHX_PC_INTEGRATE");
static_assert(sizeof(ContactsPC)    == 32, "PHX_PC_CONTACTS");
static_assert(sizeof(DualPC)        == 16, "PHX_PC_DUAL");
static_assert(sizeof(PrimalPC)      == 16, "PHX_PC_PRIMAL");
static_assert(sizeof(HardContactPC) == 32, "PHX_PC_HARDCONTACT");
static_assert(sizeof(SyncOutPC)     == 16, "PHX_PC_SYNC_OUT");
static_assert(sizeof(ExpandPC)      == 12, "PHX_PC_EXPAND");
static_assert(sizeof(KinematicPC)   == 16, "PHX_PC_KINEMATIC");
static_assert(sizeof(KinematicBoxGpu) == 64, "PHX_KINEMATIC_BOX");

// Invariants the shaders rely on.
static_assert((HASH_CAP & (HASH_CAP - 1u)) == 0u,      "HASH_CAP must be a power of two (HASH_MASK)");
static_assert(HASH_CAP >= 2u * MAX_CONSTRAINTS,        "warm-start hash load factor must stay <= 0.5");
static_assert(WAKE_WORDS * 32u >= MAX_PARTICLES,       "one wake bit per body");
static_assert(SS_KINEMATIC_DEPTH_UM < HASH_BASE,       "header counters must fit before the hash table");
static_assert((GRID_SIZE & (GRID_SIZE - 1)) == 0,      "GRID_SIZE must be a power of two (cells wrap with &)");
static_assert(GRID_CELLS % SCAN_BLOCK == 0,            "the parallel scan covers whole blocks");
static_assert(SCAN_BLOCK == PHX_WORKGROUP,             "scan_block uses one thread per cell");
static_assert(SOLVER_FLAGS_DEFAULT == (SOLVER_FLAG_MASS_PENALTY | SOLVER_FLAG_START_AT_REST |
                                       SOLVER_FLAG_HC_NEUTRAL | SOLVER_FLAG_STATIC_FRICTION |
                                       SOLVER_FLAG_KINEMATIC_CONTACTS),
              "default = every shipped fix incl. kinematic contacts, POST_STAB off");
}}  // namespace Phyxel::DebrisShared
#endif

#undef PHX_CONST
#endif  // PHX_SOLVER_SHARED_H
