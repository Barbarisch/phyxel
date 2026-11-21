// docs/VoxelFoundationPlan.md §6.7, F5.2b -- the body solver on the GPU (BodySimGpu). The same tick as BodySimRef
// (engine/src/core/voxfound/BodySim.cpp), pass by pass, in float. Every rule here has its reason written next to the
// same rule in BodySim.cpp; this file only notes where the GPU differs.
//
// Passes (push constant `pass`):
//   0 INTEGRATE  1 AABB  2 WORLD contacts  3 PAIR contacts  4 PREFIX (1 thread)  5 SCATTER  6 SORT lists by key
//   7 COLOUR_INIT  8 COLOUR_ROUND(arg)  9 COLOUR_FINAL  10 PRIMAL(arg = colour)  11 DUAL  12 HARD_BODY
//   13 HARD_APPLY  14 FINISH  15 WARM_CLEAR(next half)  16 WARM_SAVE(next half)  17 FUSED colour (7-9)  18 FUSED primal (10 x colours)
//   F5.5a2, the narrowphase in parallel: 19 AWAKE_LIST  20 BROAD(arg 0: contact pairs, 1: hard pairs)  21 NARROW
//   (workgroup per pair)  22 HARD_NARROW (workgroup per pair)  23 HARD_BODY from the stored pair results
//   F5.5a3: 24 WORLD and 25 HARD_APPLY one workgroup per awake body
//   F5.5a4: 26 COLOUR_COUNT  27 COLOUR_PREFIX  28 COLOUR_SCATTER; in voxfound_bodies_primal.comp (VF_PRIMAL, 256):
//   29 PRIMAL (indirect per colour, a 32-thread group per body)  30 PRIMAL (every colour, one workgroup)
// GPU differences: candidates go to a per-worker scratch (worker = body i; its pairs j > i reuse it); a world surface
// corner is taken once by a canonical-owner rule (the CPU uses a set); constraint lists are sorted by key so every sum
// is order-independent (determinism); colours are Jones-Plassmann rounds (hashed priorities) instead of greedy;
// hard-contact body-body pushes are computed per body from the pre-push poses and summed (the CPU applies them pair
// by pair).

// F5.5a: two pipelines are compiled from this file. voxfound_bodies.comp (local size 64) runs one pass per dispatch;
// voxfound_bodies_fused.comp (VF_FUSED, local size 1024) runs pass 17 (every colour round) and pass 18 (one
// iteration's primal over every colour) each in ONE workgroup, with workgroup barriers between the steps. The per-pass code is shared
// (colourInit / colourRound / colourFinal / primal / dual below), so the two compute the same numbers.
#ifdef VF_FUSED
#define VF_GROUP 1024
#define VF_COH coherent
#elif defined(VF_PRIMAL)
#define VF_GROUP 256
#define VF_COH coherent
#else
#define VF_GROUP 64
#define VF_COH
#endif
layout(local_size_x = VF_GROUP) in;

struct Body {
    vec4 pos;           // w: invMass
    vec4 quat;          // x y z w
    vec4 vel;
    vec4 angVel;
    vec4 initial;
    vec4 initialQuat;
    vec4 inertial;
    vec4 inertialQuat;
    vec4 cumAng;
    vec4 comLocal;      // voxel coords of the centre in the body grid; w: radius (m)
    vec4 fixedProps;    // mass, friction, vm, asleepTicks (F5.5b: an exact float count)
    vec4 I0, I1, I2;    // body-frame inertia rows (kg m^2)
    uvec4 grid;         // voxel offset, dim x, y, z
    uvec4 meta;         // corner offset, corner count, asleep, slow ticks
    vec4 lo, hi;        // world AABB
};

struct Con {
    vec4 n;             // w: mu
    vec4 t1;            // w: cInitN
    vec4 t2;            // w: cInitT1
    vec4 rA;            // w: cInitT2
    vec4 rB;            // w: lambdaN
    vec4 lam;           // lambdaT1, lambdaT2, penN, penT1
    vec4 misc;          // penT2, stick, 0, 0
    vec4 anchor;
    uvec4 ids;          // a, b (STATIC = world), key lo, key hi
};

struct Cand {
    vec4 n;             // w: pen
    vec4 rA;            // w: mu
    vec4 rB;
    vec4 point;
    uvec4 f;            // key lo, key hi, bucket, flags (bit 0 warm, bit 1 used)
};

struct Warm {
    uvec4 key;          // lo, hi, used, stick
    vec4 lam;           // lambdaN, lambdaT1, lambdaT2, penN
    vec4 pen;           // penT1, penT2
    vec4 anchor;
};

layout(std430, binding = 0) readonly buffer Params { uint prm[]; };
layout(std430, binding = 1) VF_COH buffer Bodies { Body bodies[]; };
layout(std430, binding = 2) readonly buffer BodyVox { uint bodyVox[]; };
layout(std430, binding = 3) readonly buffer Corners { vec4 corners[]; };
layout(std430, binding = 4) readonly buffer World { uint worldVox[]; };
layout(std430, binding = 5) VF_COH buffer Cons { Con cons[]; };
// coherent: F5.5a2's workgroup per pair writes candidates in one thread and caps them in another
layout(std430, binding = 6) coherent buffer Scratch { Cand scratch[]; };
layout(std430, binding = 7) VF_COH buffer State { uint state[]; };
layout(std430, binding = 8) buffer Lists { uint lists[]; };
layout(std430, binding = 9) buffer WarmT { Warm warm[]; };
layout(std430, binding = 10) buffer Push { vec4 pushes[]; };
// F5.5a2: [0, nb) awake list | pairs (a, b) x MAXP | hard pairs x MAXP | hash (key, pair) x 2 MAXP | flags x nb
layout(std430, binding = 11) buffer Aux { uint aux[]; };
layout(std430, binding = 12) buffer HardRes { vec4 hres[]; };   // per hard pair: deepest normal, depth

layout(push_constant) uniform PC {
    uint pass;
    uint arg;
    uint parity;        // the warm half read this tick (the other is written)
    uint n;             // threads that do work
} pc;

// params (BodySimGpu.cpp P_*)
const uint P_NB = 0, P_WX = 1, P_WY = 2, P_WZ = 3, P_WVM = 4, P_GROUND = 5, P_DT = 6, P_GX = 7, P_GY = 8, P_GZ = 9,
           P_ALPHA = 10, P_BETA = 11, P_GAMMA = 12, P_PENMIN = 13, P_PENMAX = 14, P_MARGIN = 15, P_CAP = 16,
           P_REST = 17, P_SLEEPV = 18, P_SLEEPT = 19, P_WAKEV = 20, P_COLD = 21, P_COLDBODY = 22, P_IMPACT = 23,
           P_HARD = 24, P_MAXCONS = 25, P_NMATS = 26, P_GROUNDMU = 27, P_ITERS = 28, P_SCRATCH = 29, P_WARMCAP = 30,
           P_MAXPAIRS = 31,
           P_MATS = 32;
// state (BodySimGpu.cpp S_*): 0..15 summed over a step() call, 16.. per tick, per-body arrays from S_BODY
const uint S_CAND = 0, S_CAPPED = 1, S_DEEP = 2, S_PAIRS = 3, S_HARD = 4, S_OVF_SCRATCH = 5, S_OVF_CONS = 6,
           S_UNCOL = 7, S_MAXCOL = 8, S_MAXCONS = 9, S_AWAKE = 10, S_TICKCOL = 11, S_NPAIRS = 12,
           S_NHPAIRS = 13, S_NAWAKE = 14, S_OVF_PAIRS = 15, S_NCONS = 16,
           S_DG_MAXCAND = 17, S_DG_MAXITEMS = 18, S_DG_OVERCAP = 19, S_DG_SUMCAND = 20, S_REDO = 21, S_SPREAD_FB = 22,   // F5.5f: the split's w_count
           S_BODY = 32;
const uint STATIC = 0xFFFFFFFFu, UNC = 0xFFFFFFFFu, NOCOL = 0xFFFFFFFEu;
const float INF = 1e30;
const float kAlignSlackVox = 0.05, kInsetVox = 1e-3, kWarmDepthBonus = 0.001, kWarmSpreadBonus = 0.02;

float pf(uint i) { return uintBitsToFloat(prm[i]); }
uint NB() { return prm[P_NB]; }
uint sCount(uint i) { return S_BODY + i; }
uint sOffset(uint i) { return S_BODY + NB() + i; }
uint sFill(uint i) { return S_BODY + 2u * NB() + i; }
uint sColour(uint i) { return S_BODY + 3u * NB() + i; }
uint sWake(uint i) { return S_BODY + 4u * NB() + i; }
// F5.5h: the world spread -- each awake slot's first chunk (na + 1 words), its fallback flag, each chunk's count
uint sUnit(uint i) { return S_BODY + 5u * NB() + i; }
uint sSpFlag(uint i) { return S_BODY + 6u * NB() + 1u + i; }
uint sChunkCnt(uint u) { return S_BODY + 7u * NB() + 1u + u; }   // 16 NB words (BodySimGpu::kMaxChunks)

// ---- quaternions (x y z w), as solver_types.glsl ----
vec3 qrot(vec4 q, vec3 v) {
    vec3 u = q.xyz;
    return 2.0 * dot(u, v) * u + (q.w * q.w - dot(u, u)) * v + 2.0 * q.w * cross(u, v);
}
vec4 qmul(vec4 a, vec4 b) {
    return vec4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
vec4 qinv(vec4 q) { return vec4(-q.xyz, q.w); }
vec4 qintegrate(vec4 q, vec3 v) { return normalize(q + 0.5 * qmul(vec4(v, 0.0), q)); }
vec3 angDisp(vec4 q, vec4 q0) {
    vec4 d = qmul(q, qinv(q0));
    if (d.w < 0.0) d = -d;
    return 2.0 * d.xyz;
}
mat3 qmat(vec4 q) { return mat3(qrot(q, vec3(1, 0, 0)), qrot(q, vec3(0, 1, 0)), qrot(q, vec3(0, 0, 1))); }

void tangentBasis(vec3 n, out vec3 t1, out vec3 t2) {
    t1 = abs(n.x) < 0.9 ? normalize(cross(n, vec3(1, 0, 0))) : normalize(cross(n, vec3(0, 1, 0)));
    t2 = cross(n, t1);
}

uint hash32(uint x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
uvec2 keyOf(uint a, uint b, uint kind, uint idx, uint dir) {
    uint h1 = hash32(a * 0x9E3779B1u ^ hash32(b + 0x632BE5ABu));
    h1 = hash32(h1 ^ hash32(kind * 0x85EBCA77u ^ idx));
    h1 = hash32(h1 ^ (dir * 0xC2B2AE3Du));
    uint h2 = hash32(h1 ^ 0x27D4EB2Fu ^ hash32(idx * 0x165667B1u + dir + (kind << 8)));
    return uvec2(h1, h2 ^ hash32(a + 7u * b + 1u));
}

bool dynamicBody(uint i) { return i != STATIC && bodies[i].meta.z == 0u && bodies[i].pos.w > 0.0; }

// ---- grids ----
bool worldSolid(ivec3 c) {
    ivec3 d = ivec3(prm[P_WX], prm[P_WY], prm[P_WZ]);
    if (all(greaterThanEqual(c, ivec3(0))) && all(lessThan(c, d)))
        return worldVox[uint(c.z) + uint(d.z) * (uint(c.y) + uint(d.y) * uint(c.x))] != 0u;
    return prm[P_GROUND] != 0u && c.y < 0;
}
uint worldMat(ivec3 c) {
    ivec3 d = ivec3(prm[P_WX], prm[P_WY], prm[P_WZ]);
    if (all(greaterThanEqual(c, ivec3(0))) && all(lessThan(c, d)))
        return worldVox[uint(c.z) + uint(d.z) * (uint(c.y) + uint(d.y) * uint(c.x))];
    return 0u;
}
bool bodySolid(uint bi, ivec3 c) {
    uvec4 g = bodies[bi].grid;
    if (any(lessThan(c, ivec3(0))) || any(greaterThanEqual(c, ivec3(g.yzw)))) return false;
    return bodyVox[g.x + uint(c.z) + g.w * (uint(c.y) + g.z * uint(c.x))] != 0u;
}
bool solidAt(bool isWorld, uint bi, ivec3 c) { return isWorld ? worldSolid(c) : bodySolid(bi, c); }
float muOfWorld(ivec3 c) {
    uint m = worldMat(c);
    return m != 0u ? pf(P_MATS + m) : pf(P_GROUNDMU);
}

// ---- the escape (BodySim.cpp escape) ----
struct Esc { int status; int dir; vec3 n; float pen; ivec3 cell; };

Esc escapeAt(bool isWorld, uint bi, vec3 u, float marginVox, vec3 prefer, float vm) {
    Esc e;
    e.status = 0; e.dir = -1; e.n = vec3(0.0); e.pen = 0.0; e.cell = ivec3(0);
    ivec3 c = ivec3(floor(u));
    vec3 f = u - vec3(c);
    bool inside = solidAt(isWorld, bi, c);
    float bestA = INF, bestAny = INF;
    int kA = -1;
    vec3 nA = vec3(0.0);
    ivec3 oA = ivec3(0);
    int k = 0;
    for (int i3 = 0; i3 < 27; ++i3) {
        if (i3 == 13) continue;
        ivec3 o = ivec3(i3 / 9 - 1, (i3 / 3) % 3 - 1, i3 % 3 - 1);
        int kk = k++;
        if (!inside && abs(o.x) + abs(o.y) + abs(o.z) != 1) continue;
        if (solidAt(isWorld, bi, c + o) == inside) continue;
        vec3 v = vec3(0.0);
        for (int a = 0; a < 3; ++a)
            if (o[a] != 0) v[a] = o[a] > 0 ? 1.0 - f[a] : -f[a];
        float dist = length(v);
        if (!inside && dist > marginVox) continue;
        vec3 nn = dist > 1e-12 ? v / dist : normalize(vec3(o));
        if (!inside) nn = -nn;
        bestAny = min(bestAny, dist);
        if (dot(nn, prefer) >= 0.5 && dist < bestA) { bestA = dist; kA = kk; nA = nn; oA = o; }
    }
    if (bestAny >= INF) {
        if (inside) e.status = 2;
        return e;
    }
    if (kA < 0 || bestA > bestAny + kAlignSlackVox) return e;
    e.status = 1;
    e.dir = kA;
    e.n = nA;
    e.pen = inside ? bestA * vm : -bestA * vm;
    e.cell = inside ? c : c + oA;
    return e;
}

// ---- F5.5 tunnelling fix (BodySim.cpp sweep): a fast corner's path from its tick-start position a to b (world voxel
// coordinates), 3D DDA with ties to the lowest axis; the first solid cell entered gives the contact ----
Esc sweepWorld(vec3 a, vec3 b, float vm) {
    Esc e;
    e.status = 0; e.dir = -1; e.n = vec3(0.0); e.pen = 0.0; e.cell = ivec3(0);
    ivec3 c = ivec3(floor(a));
    if (worldSolid(c)) return e;
    vec3 d = b - a;
    ivec3 stp = ivec3(0);
    vec3 tMax = vec3(INF), tDelta = vec3(INF);
    for (int k = 0; k < 3; ++k) {
        if (d[k] > 0.0) { stp[k] = 1; tMax[k] = (float(c[k]) + 1.0 - a[k]) / d[k]; tDelta[k] = 1.0 / d[k]; }
        else if (d[k] < 0.0) { stp[k] = -1; tMax[k] = (a[k] - float(c[k])) / -d[k]; tDelta[k] = 1.0 / -d[k]; }
    }
    for (int it = 0; it < 1024; ++it) {
        int ax = 0;
        if (tMax[1] < tMax[ax]) ax = 1;
        if (tMax[2] < tMax[ax]) ax = 2;
        if (tMax[ax] > 1.0) return e;
        c[ax] += stp[ax];
        if (worldSolid(c)) {
            ivec3 o = ivec3(0);
            o[ax] = -stp[ax];
            float plane = stp[ax] > 0 ? float(c[ax]) : float(c[ax]) + 1.0;
            e.status = 1;
            e.n = vec3(o);
            e.pen = (plane - b[ax]) * float(o[ax]) * vm;
            e.cell = c;
            int i3 = (o.x + 1) * 9 + (o.y + 1) * 3 + (o.z + 1);
            e.dir = i3 < 13 ? i3 : i3 - 1;   // escapeAt's neighbour index
            return e;
        }
        tMax[ax] += tDelta[ax];
    }
    return e;
}

// ---- candidate sink: mode 0 = scratch, mode 1 = deepest only, 2 = count only, 3 = staged (F5.5g) ----
uint gMode = 0u, gBase = 0u, gCount = 0u;
bool gQuiet = false;   // F5.5f: a diagnostic copy of the world pass -- no counter side effects
bool gNoDeep = false;  // F5.5g: a fallback re-test (its deep points were counted when staged)
const uint STAGE = 48u;   // F5.5g: staged candidates per world-pass thread (BodySimGpu::kStage)
// the deep-point counter: counted once per item -- in the write (mode 0) or the staging (mode 3)
bool gDeepDone = false;   // F5.5h: a spread body's fallback (its deep points were counted by its chunks)
bool countDeep() { return !gQuiet && !gDeepDone && ((gMode == 0u && !gNoDeep) || gMode == 3u); }
float gDeepPen = 0.0;
vec3 gDeepN = vec3(0.0);

void consider(vec3 n, float pen, vec3 rA, vec3 rB, vec3 point, float mu, uint bucket, uvec2 key) {
    if (gMode == 1u) {
        if (pen > gDeepPen) { gDeepPen = pen; gDeepN = n; }
        return;
    }
    if (gMode == 2u) { ++gCount; return; }   // F5.5a3: count only
    if (gMode == 0u && gCount >= prm[P_SCRATCH]) { if (!gQuiet) atomicAdd(state[S_OVF_SCRATCH], 1u); return; }
    if (gMode == 3u && gCount >= STAGE) { ++gCount; return; }   // F5.5g: past the stage -- this thread re-tests
    Cand c;
    c.n = vec4(n, pen);
    c.rA = vec4(rA, mu);
    c.rB = vec4(rB, 0.0);
    c.point = vec4(point, 0.0);
    c.f = uvec4(key, bucket, 0u);
    scratch[gBase + gCount] = c;
    ++gCount;
}

vec3 dirVec(uint d) {
    vec3 v = vec3(0.0);
    v[d / 2u] = (d & 1u) == 0u ? 1.0 : -1.0;
    return v;
}

// F5.5a3: the world test as items -- A's corners first (item k < corner count), then the world cells in A's box in the
// serial loop's order (x, then y, then z) -- so one thread can loop them all and a workgroup can split them in order.
void worldCorner(uint ai, Body A, mat3 R, uint k) {
    float wvm = pf(P_WVM);
    float mVox = pf(P_MARGIN) / wvm;
    vec4 cr = corners[A.meta.x + k];
    vec3 w = A.pos.xyz + R * cr.xyz;
    vec3 prefer = -(R * dirVec(uint(cr.w)));
    Esc e = escapeAt(true, 0u, w / wvm, mVox, prefer, wvm);
    {   // F5.5: a fast corner's contact comes from its path (BodySim.cpp: the same rule)
        vec3 w0 = A.initial.xyz + qrot(A.initialQuat, cr.xyz);
        vec3 d = w - w0;
        if (length(d) >= wvm) {
            Esc sw;
            sw.status = 0;
            vec3 u0 = w0 / wvm;
            if (!worldSolid(ivec3(floor(u0)))) {
                sw = sweepWorld(u0, w / wvm, wvm);
            } else {   // resting a hair inside a surface at the tick start: that surface, deepened by the move
                Esc e0 = escapeAt(true, 0u, u0, mVox, prefer, wvm);
                if (e0.status == 1 && e0.pen > 0.0 && dot(d, e0.n) < 0.0) {
                    sw = e0;
                    sw.pen = e0.pen - dot(d, e0.n);
                }
            }
            if (sw.status == 1) e = sw;
        }
    }
    if (e.status == 2) { if (countDeep()) atomicAdd(state[S_DEEP], 1u); return; }
    if (e.status != 1) return;
    float mu = 0.5 * (A.fixedProps.y + muOfWorld(e.cell));
    consider(e.n, e.pen, cr.xyz, w, w, mu, uint(e.dir), keyOf(ai, STATIC, 0u, k, uint(e.dir)));
}
// the world's surface corners of one solid cell near A into A's grid; each (lattice point, face) once: its owner cell
void worldCell(uint ai, Body A, mat3 R, mat3 Rt, ivec3 cell, vec3 lo, vec3 hi) {
    float wvm = pf(P_WVM), avm = A.fixedProps.z;
    if (!worldSolid(cell)) return;
    for (uint d = 0u; d < 6u; ++d) {
        ivec3 D = ivec3(dirVec(d));
        if (worldSolid(cell + D)) continue;
        int a = int(d / 2u);
        int base = D[a] > 0 ? cell[a] + 1 : cell[a];
        int u = (a + 1) % 3, ww = (a + 2) % 3;
        for (int du = 0; du < 2; ++du)
            for (int dw = 0; dw < 2; ++dw) {
                ivec3 L;
                L[a] = base;
                L[u] = cell[u] + du;
                L[ww] = cell[ww] + dw;
                // canonical owner: the first of the (up to 4) cells sharing L on face d that is solid
                // with face d exposed, in order (u - 1, w - 1), (u - 1, w), (u, w - 1), (u, w)
                bool mine = false;
                for (int ou = 1; ou >= 0 && !mine; --ou) {
                    bool stop = false;
                    for (int ow = 1; ow >= 0; --ow) {
                        ivec3 c2 = cell;
                        c2[u] = L[u] - ou;
                        c2[ww] = L[ww] - ow;
                        if (worldSolid(c2) && !worldSolid(c2 + D)) {
                            mine = c2 == cell;
                            stop = true;
                            break;
                        }
                    }
                    if (stop) break;
                }
                if (!mine) continue;
                // inset into the world's solid (BodySim.cpp insetCorner)
                vec3 sum = vec3(0.0), anyd = vec3(0.0);
                for (int ex = -1; ex <= 0; ++ex)
                    for (int ey = -1; ey <= 0; ++ey)
                        for (int ez = -1; ez <= 0; ++ez) {
                            if (!worldSolid(L + ivec3(ex, ey, ez))) continue;
                            vec3 dd = vec3(ex, ey, ez) + 0.5;
                            sum += dd;
                            anyd = dd;
                        }
                if (dot(sum, sum) < 1e-12) sum = anyd;
                vec3 w = (vec3(L) + kInsetVox * normalize(sum)) * wvm;
                if (any(lessThan(w, lo)) || any(greaterThan(w, hi))) continue;
                vec3 local = Rt * (w - A.pos.xyz);
                vec3 prefer = Rt * -vec3(D);
                Esc e = escapeAt(false, ai, local / avm + A.comLocal.xyz, pf(P_MARGIN) / avm, prefer, avm);
                if (e.status == 2) { if (countDeep()) atomicAdd(state[S_DEEP], 1u); continue; }
                if (e.status != 1) continue;
                float mu = 0.5 * (A.fixedProps.y + muOfWorld(cell));
                uint lk = hash32(uint(L.x) * 0x9E3779B1u ^ hash32(uint(L.y) * 0x85EBCA77u ^
                                                                   hash32(uint(L.z) + 0x27D4EB2Fu)));
                consider(-(R * e.n), e.pen, local, w, w, mu, 26u + uint(e.dir),
                         keyOf(ai, STATIC, 1u + d, lk, uint(e.dir)));
            }
    }
}
// A's box of world cells: returns the cell count (0 below the ground band), with the box's first cell, extent and bounds
uint worldBox(Body A, out ivec3 cl, out ivec3 nc, out vec3 lo, out vec3 hi) {
    float wvm = pf(P_WVM);
    lo = A.lo.xyz - pf(P_MARGIN);
    hi = A.hi.xyz + pf(P_MARGIN);
    cl = ivec3(int(floor(lo.x / wvm)) - 1, max(int(floor(lo.y / wvm)) - 1, -1), int(floor(lo.z / wvm)) - 1);
    ivec3 ch = ivec3(floor(hi / wvm)) + 1;
    nc = ivec3(0);
    if (ch.y < -1) return 0u;
    nc = ch - cl + 1;
    return uint(nc.x * nc.y * nc.z);
}
void worldItem(uint ai, Body A, mat3 R, mat3 Rt, uint q, ivec3 cl, ivec3 nc, vec3 lo, vec3 hi) {
    if (q < A.meta.y) { worldCorner(ai, A, R, q); return; }
    int c = int(q - A.meta.y);
    int x = c / (nc.y * nc.z), y = (c / nc.z) % nc.y, z = c % nc.z;
    worldCell(ai, A, R, Rt, cl + ivec3(x, y, z), lo, hi);
}
void againstWorld(uint ai) {
    Body A = bodies[ai];
    mat3 R = qmat(A.quat), Rt = transpose(R);
    ivec3 cl, nc;
    vec3 lo, hi;
    uint n = A.meta.y + worldBox(A, cl, nc, lo, hi);
    for (uint q = 0u; q < n; ++q) worldItem(ai, A, R, Rt, q, cl, nc, lo, hi);
}

void againstBody(uint ai, uint bi) {
    Body A = bodies[ai], B = bodies[bi];
    mat3 RA = qmat(A.quat), RB = qmat(B.quat);
    float mu = 0.5 * (A.fixedProps.y + B.fixedProps.y);
    for (uint side = 0u; side < 2u; ++side) {
        uint xi = side == 0u ? ai : bi, yi = side == 0u ? bi : ai;
        Body X = side == 0u ? A : B;
        Body Y = side == 0u ? B : A;
        mat3 RX = side == 0u ? RA : RB, RY = side == 0u ? RB : RA;
        mat3 RYt = transpose(RY);
        float yvm = Y.fixedProps.z;
        for (uint k = 0u; k < X.meta.y; ++k) {
            vec4 cr = corners[X.meta.x + k];
            vec3 w = X.pos.xyz + RX * cr.xyz;
            vec3 local = RYt * (w - Y.pos.xyz);
            vec3 prefer = RYt * -(RX * dirVec(uint(cr.w)));
            Esc e = escapeAt(false, yi, local / yvm + Y.comLocal.xyz, pf(P_MARGIN) / yvm, prefer, yvm);
            if (e.status == 2) { if (gMode == 0u) atomicAdd(state[S_DEEP], 1u); continue; }
            if (e.status != 1) continue;
            vec3 nOut = RY * e.n;
            consider(side == 0u ? nOut : -nOut, e.pen, side == 0u ? cr.xyz : local, side == 0u ? local : cr.xyz, w,
                     mu, side * 26u + uint(e.dir), keyOf(ai, bi, 2u + side * 7u, k, uint(e.dir)));
        }
    }
}

// ---- warm start table (two halves) ----
uint warmSlot(uvec2 key) { return hash32(key.x ^ hash32(key.y)) & (prm[P_WARMCAP] - 1u); }
int findWarm(uvec2 key, uint halfIdx) {
    uint base = halfIdx * prm[P_WARMCAP];
    uint s = warmSlot(key);
    for (uint p = 0u; p < 64u; ++p) {
        uint idx = base + ((s + p) & (prm[P_WARMCAP] - 1u));
        uvec4 k = warm[idx].key;
        if (k.z == 0u) return -1;
        if (k.x == key.x && k.y == key.y) return int(idx);
    }
    return -1;
}

// one kept candidate -> a constraint (F5.5a2: shared by the serial cap and the workgroup cap)
void emitOne(uint a, uint b, Cand c) {
    Body A = bodies[a];
    mat3 RA = qmat(A.quat);
    float dt = pf(P_DT), dt2 = dt * dt;
    Con k;
    vec3 n = c.n.xyz;
    vec3 t1, t2;
    tangentBasis(n, t1, t2);
    vec3 rAW = RA * c.rA.xyz;
    float jdq = dot(n, A.pos.xyz - A.initial.xyz) + dot(cross(rAW, n), A.cumAng.xyz);
    vec3 rBW = vec3(0.0);
    if (b != STATIC) {
        Body B = bodies[b];
        rBW = qrot(B.quat, c.rB.xyz);
        jdq += -dot(n, B.pos.xyz - B.initial.xyz) + dot(cross(rBW, -n), B.cumAng.xyz);
    }
    float cInitN = c.n.w + jdq;
    int wi = findWarm(c.f.xy, pc.parity);
    float coldPen = pf(P_COLD) * A.fixedProps.x / dt2;
    if (b != STATIC && bodies[b].meta.z == 0u) {
        Body B = bodies[b];
        vec3 vRel = (A.vel.xyz + cross(A.angVel.xyz, rAW)) - (B.vel.xyz + cross(B.angVel.xyz, rBW));
        if (-dot(vRel, n) > pf(P_IMPACT)) {
            float mB = B.fixedProps.x;
            coldPen = pf(P_COLDBODY) * (A.fixedProps.x * mB / (A.fixedProps.x + mB)) / dt2;
        }
    }
    float penMin = pf(P_PENMIN), penMax = pf(P_PENMAX), alpha = pf(P_ALPHA), gamma = pf(P_GAMMA);
    float lN = 0.0, lT1 = 0.0, lT2 = 0.0, pN, pT1, pT2;
    uint stick = 0u;
    vec3 anchorW = vec3(0.0);
    bool cold = wi < 0;
    if (cold) {
        pN = clamp(coldPen, penMin, penMax);
        pT1 = pN;
        pT2 = pN;
    } else {
        Warm w = warm[wi];
        lN = w.lam.x * alpha; lT1 = w.lam.y * alpha; lT2 = w.lam.z * alpha;
        pN = clamp(w.lam.w * gamma, penMin, penMax);
        pT1 = clamp(w.pen.x * gamma, penMin, penMax);
        pT2 = clamp(w.pen.y * gamma, penMin, penMax);
        stick = w.key.w;
        anchorW = w.anchor.xyz;
    }
    float cT1 = 0.0, cT2 = 0.0;
    vec3 anchor = vec3(0.0);
    if (b == STATIC) {
        vec3 ptStart = A.initial.xyz + qrot(A.initialQuat, c.rA.xyz);
        anchor = (!cold && stick != 0u) ? anchorW : ptStart;
        cT1 = dot(t1, ptStart - anchor);
        cT2 = dot(t2, ptStart - anchor);
    }
    k.n = vec4(n, c.rA.w);
    k.t1 = vec4(t1, cInitN);
    k.t2 = vec4(t2, cT1);
    k.rA = vec4(c.rA.xyz, cT2);
    k.rB = vec4(c.rB.xyz, lN);
    k.lam = vec4(lT1, lT2, pN, pT1);
    k.misc = vec4(pT2, float(stick), 0.0, 0.0);
    k.anchor = vec4(anchor, 0.0);
    k.ids = uvec4(a, b, c.f.x, c.f.y);
    uint slot = atomicAdd(state[S_NCONS], 1u);
    if (slot >= prm[P_MAXCONS]) { atomicAdd(state[S_OVF_CONS], 1u); return; }
    cons[slot] = k;
    atomicAdd(state[sCount(a)], 1u);
    if (b != STATIC) atomicAdd(state[sCount(b)], 1u);
}

// ---- the cap (BodySim.cpp capContacts) + emission, for candidates in scratch [gBase, gBase + gCount) ----
void emitPair(uint a, uint b) {
    uint cnt = gCount;
    if (cnt == 0u) return;
    atomicAdd(state[S_CAND], cnt);
    uint cap = prm[P_CAP];
    for (uint i = 0u; i < cnt; ++i) {
        uvec4 f = scratch[gBase + i].f;
        scratch[gBase + i].f.w = findWarm(f.xy, pc.parity) >= 0 ? 1u : 0u;
    }
    uint keep[16];
    uint nk = 0u;
    if (cnt <= cap) {
        for (uint i = 0u; i < cnt; ++i) keep[nk++] = i;
    } else {
        // winners: the best per bucket (depth + warm bonus), first index on ties
        float wScore[52];
        uint wIdx[52];
        for (int b2 = 0; b2 < 52; ++b2) { wScore[b2] = -INF; wIdx[b2] = 0xFFFFFFFFu; }
        for (uint i = 0u; i < cnt; ++i) {
            Cand c = scratch[gBase + i];
            float s = c.n.w + ((c.f.w & 1u) != 0u ? kWarmDepthBonus : 0.0);
            uint bk = c.f.z;
            if (s > wScore[bk]) { wScore[bk] = s; wIdx[bk] = i; }
        }
        uint win[52];
        uint nw = 0u;
        for (int b2 = 0; b2 < 52; ++b2)
            if (wIdx[b2] != 0xFFFFFFFFu) win[nw++] = wIdx[b2];
        // stable sort by depth, deepest first
        for (uint i = 1u; i < nw; ++i) {
            uint v = win[i];
            float pv = scratch[gBase + v].n.w;
            int j = int(i) - 1;
            while (j >= 0 && scratch[gBase + win[j]].n.w < pv) { win[j + 1] = win[j]; --j; }
            win[j + 1] = v;
        }
        for (uint i = 0u; i < nw && nk < cap; ++i) {
            keep[nk++] = win[i];
            scratch[gBase + win[i]].f.w |= 2u;
        }
        while (nk < cap) {
            float bestScore = -1.0;
            uint best = 0xFFFFFFFFu;
            for (uint i = 0u; i < cnt; ++i) {
                Cand c = scratch[gBase + i];
                if ((c.f.w & 2u) != 0u) continue;
                float dmin = INF;
                for (uint q = 0u; q < nk; ++q) dmin = min(dmin, length(c.point.xyz - scratch[gBase + keep[q]].point.xyz));
                float s = dmin + ((c.f.w & 1u) != 0u ? kWarmSpreadBonus : 0.0);
                if (s > bestScore) { bestScore = s; best = i; }
            }
            if (best == 0xFFFFFFFFu) break;
            keep[nk++] = best;
            scratch[gBase + best].f.w |= 2u;
        }
    }
    atomicAdd(state[S_CAPPED], cnt - nk);

    for (uint q = 0u; q < nk; ++q) emitOne(a, b, scratch[gBase + keep[q]]);
}

// ---- the constraint value at the current iterate (BodySim.cpp evalC) ----
void evalC(Con k, out float Cn, out float Ct1, out float Ct2, out vec3 rAW, out vec3 rBW) {
    Body A = bodies[k.ids.x];
    rAW = qrot(A.quat, k.rA.xyz);
    vec3 dB = vec3(0.0), aB = vec3(0.0);
    rBW = vec3(0.0);
    if (k.ids.y != STATIC) {
        Body B = bodies[k.ids.y];
        dB = B.pos.xyz - B.initial.xyz;
        aB = B.cumAng.xyz;
        rBW = qrot(B.quat, k.rB.xyz);
    }
    vec3 dA = A.pos.xyz - A.initial.xyz;
    float alpha = pf(P_ALPHA);
    float c0 = k.t1.w > 0.0 ? (1.0 - alpha) * k.t1.w : k.t1.w;
    vec3 n = k.n.xyz, t1 = k.t1.xyz, t2 = k.t2.xyz;
    Cn = -c0 + dot(n, dA - dB) + dot(cross(rAW, n), A.cumAng.xyz) + dot(cross(rBW, -n), aB);
    Ct1 = (1.0 - alpha) * k.t2.w + dot(t1, dA - dB) + dot(cross(rAW, t1), A.cumAng.xyz) + dot(cross(rBW, -t1), aB);
    Ct2 = (1.0 - alpha) * k.rA.w + dot(t2, dA - dB) + dot(cross(rAW, t2), A.cumAng.xyz) + dot(cross(rBW, -t2), aB);
}

void solve6(float a[36], float b[6], out float x[6]) {
    float L[36];
    float D[6];
    for (int i = 0; i < 36; ++i) L[i] = 0.0;
    for (int j = 0; j < 6; ++j) {
        float d = a[j * 6 + j];
        for (int k = 0; k < j; ++k) d -= L[j * 6 + k] * L[j * 6 + k] * D[k];
        D[j] = max(d, 1e-30);
        L[j * 6 + j] = 1.0;
        for (int i = j + 1; i < 6; ++i) {
            float s = a[i * 6 + j];
            for (int k = 0; k < j; ++k) s -= L[i * 6 + k] * L[j * 6 + k] * D[k];
            L[i * 6 + j] = s / D[j];
        }
    }
    float y[6];
    for (int i = 0; i < 6; ++i) {
        y[i] = b[i];
        for (int k = 0; k < i; ++k) y[i] -= L[i * 6 + k] * y[k];
    }
    for (int i = 0; i < 6; ++i) y[i] /= D[i];
    for (int i = 5; i >= 0; --i) {
        x[i] = y[i];
        for (int k = i + 1; k < 6; ++k) x[i] -= L[k * 6 + i] * x[k];
    }
}

void pushOut(uint i, vec3 n, float d) {
    vec3 disp = bodies[i].pos.xyz - bodies[i].initial.xyz;
    float vn = dot(disp, n);
    if (vn < 0.0) disp -= n * vn;
    bodies[i].pos.xyz += n * d;
    bodies[i].initial.xyz = bodies[i].pos.xyz - disp;
}

bool aabbOverlap(uint i, uint j, float m) {
    return all(lessThanEqual(bodies[i].lo.xyz, bodies[j].hi.xyz + m)) &&
           all(lessThanEqual(bodies[j].lo.xyz, bodies[i].hi.xyz + m));
}

// ---- F5.5a2: the narrowphase in parallel ----
uint MAXP() { return prm[P_MAXPAIRS]; }
uint auxPairs(uint which) { return NB() + which * 2u * MAXP(); }   // which 0: contact pairs, 1: hard pairs
uint auxHash() { return NB() + 4u * MAXP(); }                       // 2 MAXP entries (power of two) of (key, pair)
uint auxFlag() { return NB() + 8u * MAXP(); }                       // per body: in a hard pair deeper than P_HARD
uint pairKey(uint i, uint j) { return min(i, j) * NB() + max(i, j) + 1u; }

// corner q of the pair's list (side 0: A's corners into B, then side 1: B's into A), exactly as againstBody's loop
bool pairCorner(uint ai, uint bi, Body A, Body B, mat3 RA, mat3 RB, float mu, uint q, bool countDeep, out Cand c,
                out vec3 nSide) {
    uint side = q < A.meta.y ? 0u : 1u;
    uint k = side == 0u ? q : q - A.meta.y;
    uint yi = side == 0u ? bi : ai;
    Body X = side == 0u ? A : B;
    Body Y = side == 0u ? B : A;
    mat3 RX = side == 0u ? RA : RB, RY = side == 0u ? RB : RA;
    mat3 RYt = transpose(RY);
    float yvm = Y.fixedProps.z;
    vec4 cr = corners[X.meta.x + k];
    vec3 w = X.pos.xyz + RX * cr.xyz;
    vec3 local = RYt * (w - Y.pos.xyz);
    vec3 prefer = RYt * -(RX * dirVec(uint(cr.w)));
    Esc e = escapeAt(false, yi, local / yvm + Y.comLocal.xyz, pf(P_MARGIN) / yvm, prefer, yvm);
    if (e.status == 2) { if (countDeep) atomicAdd(state[S_DEEP], 1u); return false; }
    if (e.status != 1) return false;
    vec3 nOut = RY * e.n;
    nSide = side == 0u ? nOut : -nOut;
    c.n = vec4(nSide, e.pen);
    c.rA = vec4(side == 0u ? cr.xyz : local, mu);
    c.rB = vec4(side == 0u ? local : cr.xyz, 0.0);
    c.point = vec4(w, 0.0);
    c.f = uvec4(keyOf(ai, bi, 2u + side * 7u, k, uint(e.dir)), side * 26u + uint(e.dir), 0u);
    return true;
}

shared uint gwCnt[64], gwOff[64], gwTotal, gwNk, gwStop, gwKeep[16], gwWIdx[52];
shared float gwBest[64];
shared uint gwBestI[64];
shared vec4 gwDeep[64];

// the cap of emitPair, one workgroup: same winners, same depth order, same spread picks (first index on ties)
// F5.5f: stop 0 = the whole cap and the emit; 3 = return after the bucket winners; 4 = after the spread picks
void coopEmit(uint a, uint b, uint base, uint cnt, uint stop) {
    uint lid = gl_LocalInvocationID.x;
    for (uint i = lid; i < cnt; i += 64u) {
        uvec4 f = scratch[base + i].f;
        scratch[base + i].f.w = findWarm(f.xy, pc.parity) >= 0 ? 1u : 0u;
    }
    memoryBarrierBuffer();
    barrier();
    uint cap = prm[P_CAP];
    if (cnt <= cap) {
        if (lid == 0u) {
            for (uint i = 0u; i < cnt; ++i) gwKeep[i] = i;
            gwNk = cnt;
        }
    } else {
        if (lid < 52u) {   // the best per bucket (depth + warm bonus), first index on ties
            float best = -INF;
            uint bi = 0xFFFFFFFFu;
            for (uint i = 0u; i < cnt; ++i) {
                Cand c = scratch[base + i];
                if (c.f.z != lid) continue;
                float sc = c.n.w + ((c.f.w & 1u) != 0u ? kWarmDepthBonus : 0.0);
                if (sc > best) { best = sc; bi = i; }
            }
            gwWIdx[lid] = bi;
        }
        memoryBarrierShared();
        barrier();
        if (lid == 0u) {
            uint win[52];
            uint nw = 0u;
            for (int b2 = 0; b2 < 52; ++b2)
                if (gwWIdx[b2] != 0xFFFFFFFFu) win[nw++] = gwWIdx[b2];
            for (uint i = 1u; i < nw; ++i) {   // stable sort by depth, deepest first
                uint v = win[i];
                float pv = scratch[base + v].n.w;
                int j = int(i) - 1;
                while (j >= 0 && scratch[base + win[j]].n.w < pv) { win[j + 1] = win[j]; --j; }
                win[j + 1] = v;
            }
            uint nk = 0u;
            for (uint i = 0u; i < nw && nk < cap; ++i) {
                gwKeep[nk++] = win[i];
                scratch[base + win[i]].f.w |= 2u;
            }
            gwNk = nk;
            gwStop = 0u;
        }
        memoryBarrierBuffer();
        barrier();
        if (stop == 3u) return;   // F5.5f (uniform: a push constant)
        uint per = (cnt + 63u) / 64u, i0 = min(cnt, lid * per), i1 = min(cnt, i0 + per);
        while (gwNk < cap && gwStop == 0u) {
            uint nk = gwNk;
            float best = -1.0;
            uint bi = 0xFFFFFFFFu;
            for (uint i = i0; i < i1; ++i) {
                Cand c = scratch[base + i];
                if ((c.f.w & 2u) != 0u) continue;
                float dmin = INF;
                for (uint q = 0u; q < nk; ++q) dmin = min(dmin, length(c.point.xyz - scratch[base + gwKeep[q]].point.xyz));
                float sc = dmin + ((c.f.w & 1u) != 0u ? kWarmSpreadBonus : 0.0);
                if (sc > best) { best = sc; bi = i; }
            }
            gwBest[lid] = best;
            gwBestI[lid] = bi;
            memoryBarrierShared();
            barrier();
            if (lid == 0u) {
                float gb = -1.0;
                uint gi = 0xFFFFFFFFu;
                for (uint t2 = 0u; t2 < 64u; ++t2)
                    if (gwBestI[t2] != 0xFFFFFFFFu && gwBest[t2] > gb) { gb = gwBest[t2]; gi = gwBestI[t2]; }
                if (gi == 0xFFFFFFFFu) gwStop = 1u;
                else {
                    gwKeep[nk] = gi;
                    gwNk = nk + 1u;
                    scratch[base + gi].f.w |= 2u;
                }
            }
            memoryBarrierBuffer();
            barrier();
        }
    }
    memoryBarrierShared();
    barrier();
    if (stop != 0u) return;   // F5.5f: 3 (cnt <= cap: nothing to pick) or 4
    uint nk = gwNk;
    if (lid == 0u) atomicAdd(state[S_CAPPED], cnt - nk);
    if (lid < nk) emitOne(a, b, scratch[base + gwKeep[lid]]);
}

// pass 21: one workgroup per contact pair (persistent groups); scratch region = the group's
void narrowPairs() {
    uint lid = gl_LocalInvocationID.x, g = gl_WorkGroupID.x, G = gl_NumWorkGroups.x;
    uint np = min(state[S_NPAIRS], MAXP());
    uint base = g * prm[P_SCRATCH];
    for (uint p = g; p < np; p += G) {
        uint a = aux[auxPairs(0u) + 2u * p], b = aux[auxPairs(0u) + 2u * p + 1u];
        Body A = bodies[a], B = bodies[b];
        mat3 RA = qmat(A.quat), RB = qmat(B.quat);
        float mu = 0.5 * (A.fixedProps.y + B.fixedProps.y);
        uint L = A.meta.y + B.meta.y, per = (L + 63u) / 64u, q0 = min(L, lid * per), q1 = min(L, q0 + per);
        Cand c;
        vec3 ns;
        uint n = 0u;
        for (uint q = q0; q < q1; ++q) n += pairCorner(a, b, A, B, RA, RB, mu, q, true, c, ns) ? 1u : 0u;
        gwCnt[lid] = n;
        memoryBarrierShared();
        barrier();
        if (lid == 0u) {
            uint off = 0u;
            for (uint t2 = 0u; t2 < 64u; ++t2) { gwOff[t2] = off; off += gwCnt[t2]; }
            gwTotal = off;
        }
        memoryBarrierShared();
        barrier();
        uint off = gwOff[lid], scr = prm[P_SCRATCH];
        for (uint q = q0; q < q1; ++q) {
            if (!pairCorner(a, b, A, B, RA, RB, mu, q, false, c, ns)) continue;
            if (off < scr) scratch[base + off] = c;
            ++off;
        }
        uint total = gwTotal, cnt = min(total, scr);
        if (lid == 0u && total > scr) atomicAdd(state[S_OVF_SCRATCH], total - scr);
        memoryBarrierBuffer();
        barrier();
        if (cnt > 0u) {
            if (lid == 0u) {
                atomicAdd(state[S_CAND], cnt);
                if (bodies[b].meta.z != 0u && length(bodies[a].vel.xyz) > pf(P_WAKEV)) state[sWake(b)] = 1u;
            }
            coopEmit(a, b, base, cnt, 0u);
        }
        memoryBarrierBuffer();
        barrier();
    }
}

// pass 22: one workgroup per hard pair: the deepest point (first in the serial order on ties), stored + hashed
void narrowHard() {
    uint lid = gl_LocalInvocationID.x, g = gl_WorkGroupID.x, G = gl_NumWorkGroups.x;
    uint np = min(state[S_NHPAIRS], MAXP());
    float hc = pf(P_HARD);
    for (uint p = g; p < np; p += G) {
        uint a = aux[auxPairs(1u) + 2u * p], b = aux[auxPairs(1u) + 2u * p + 1u];
        Body A = bodies[a], B = bodies[b];
        mat3 RA = qmat(A.quat), RB = qmat(B.quat);
        float mu = 0.5 * (A.fixedProps.y + B.fixedProps.y);
        uint L = A.meta.y + B.meta.y, per = (L + 63u) / 64u, q0 = min(L, lid * per), q1 = min(L, q0 + per);
        float best = 0.0;
        vec3 bn = vec3(0.0);
        Cand c;
        vec3 ns;
        for (uint q = q0; q < q1; ++q)
            if (pairCorner(a, b, A, B, RA, RB, mu, q, false, c, ns) && c.n.w > best) { best = c.n.w; bn = ns; }
        gwDeep[lid] = vec4(bn, best);
        memoryBarrierShared();
        barrier();
        if (lid == 0u) {
            vec4 d = vec4(0.0);
            for (uint t2 = 0u; t2 < 64u; ++t2)
                if (gwDeep[t2].w > d.w) d = gwDeep[t2];
            hres[p] = d;
            uint key = pairKey(a, b), cap2 = 2u * MAXP(), h = hash32(key) & (cap2 - 1u);
            for (uint k = 0u; k < cap2; ++k) {
                uint slot = auxHash() + 2u * ((h + k) & (cap2 - 1u));
                if (atomicCompSwap(aux[slot], 0u, key) == 0u) { aux[slot + 1u] = p; break; }
            }
            if (d.w > hc) { aux[auxFlag() + a] = 1u; aux[auxFlag() + b] = 1u; }
        }
        memoryBarrierShared();
        barrier();
    }
}

// ---- F5.5a3: the world test in parallel, one workgroup per awake body ----
shared Body gwA;
// pass 24: K_WORLD for the awake bodies: count, scan, write (the serial candidate order), then the cap (coopEmit)
// F5.5f: pc.arg = where a diagnostic copy stops (0 = the real pass; 1 = the count (+ the per-body maxima), 2 = + the
// write, 3 = + the bucket winners, 4 = + the spread picks); pc.n bit 0 = quiet (no counter side effects in the copy)
void worldBody(uint ai, uint base, uint stBase, uint stop, bool once, bool scramble);
void worldPar() {
    uint lid = gl_LocalInvocationID.x, g = gl_WorkGroupID.x, G = gl_NumWorkGroups.x;
    uint na = state[S_NAWAKE], scr = prm[P_SCRATCH], base = g * scr;
    uint stop = pc.arg;
    gQuiet = stop != 0u && (pc.n & 1u) != 0u;
    // F5.5g: pc.n bit 1 = once (each item tested once: staged, then copied), bit 2 = the scramble (tests only)
    bool once = (pc.n & 2u) != 0u, scramble = (pc.n & 4u) != 0u;
    uint stBase = prm[P_NB] * scr + (g * 64u + lid) * STAGE;   // this thread's stage, past the bodies' scratch
    for (uint idx = g; idx < na; idx += G) worldBody(aux[idx], base, stBase, stop, once, scramble);
}
// one awake body's world pass on this workgroup (the body of F5.5a3's loop; F5.5h's fallback runs it too)
void worldBody(uint ai, uint base, uint stBase, uint stop, bool once, bool scramble) {
    uint lid = gl_LocalInvocationID.x, scr = prm[P_SCRATCH];
    {
        Body A = bodies[ai];
        mat3 R = qmat(A.quat), Rt = transpose(R);
        ivec3 cl, nc;
        vec3 lo, hi;
        uint L = A.meta.y + worldBox(A, cl, nc, lo, hi);
        uint per = (L + 63u) / 64u, q0 = min(L, lid * per), q1 = min(L, q0 + per);
        gMode = once ? 3u : 2u; gBase = stBase; gCount = 0u;
        for (uint q = q0; q < q1; ++q) worldItem(ai, A, R, Rt, q, cl, nc, lo, hi);
        gwCnt[lid] = gCount;
        memoryBarrierShared();
        barrier();
        if (lid == 0u) {
            uint off = 0u;
            for (uint t2 = 0u; t2 < 64u; ++t2) { gwOff[t2] = off; off += gwCnt[t2]; }
            gwTotal = off;
            if (stop == 1u) {   // F5.5f: the per-body maxima (this pass's own counter words, zeroed per tick)
                atomicMax(state[S_DG_MAXCAND], off);
                atomicMax(state[S_DG_MAXITEMS], L);
                if (off > prm[P_CAP]) atomicAdd(state[S_DG_OVERCAP], 1u);
                atomicAdd(state[S_DG_SUMCAND], off);
            }
        }
        memoryBarrierShared();
        barrier();
        if (stop == 1u) return;
        uint mine = gwCnt[lid];
        if (once && mine <= STAGE) {   // F5.5g: the staged candidates to the serial offset -- the write's slots + drops
            uint off = gwOff[lid], ovf = 0u;
            for (uint i = 0u; i < mine; ++i) {
                uint dst = scramble ? gwTotal - 1u - (off + i) : off + i;   // scramble: the body's order reversed
                if (dst < scr) scratch[base + dst] = scratch[stBase + i];
                else ++ovf;
            }
            if (ovf > 0u && !gQuiet) atomicAdd(state[S_OVF_SCRATCH], ovf);
        } else {   // the write: re-test this thread's items (all threads in the two-test pass; past the stage in once)
            if (once && stop == 0u) atomicAdd(state[S_REDO], 1u);
            gNoDeep = once;
            gMode = 0u; gBase = base; gCount = gwOff[lid];   // consider() writes scratch[base + offset], drops past scr
            for (uint q = q0; q < q1; ++q) worldItem(ai, A, R, Rt, q, cl, nc, lo, hi);
            gNoDeep = false;
        }
        uint cnt = min(gwTotal, scr);
        memoryBarrierBuffer();
        barrier();
        if (stop == 2u) return;
        if (cnt > 0u) {
            if (lid == 0u && (stop == 0u || !gQuiet)) atomicAdd(state[S_CAND], cnt);   // F5.5f: loud = the red
            coopEmit(ai, STATIC, base, cnt, stop);
        }
        memoryBarrierBuffer();
        barrier();
    }
}

// ---- F5.5h: a big body's world items over more than one workgroup ----
const uint CH_ITEMS = 256u, CH_MAX = 16u, CH_SLOT = 256u;   // BodySimGpu::kChunkItems / kMaxChunks; a chunk's slot
// a body's chunk size and count from its item count L (both passes agree): >= 256 items, at most 16 chunks
uint chunkSize(uint L) { return max(CH_ITEMS, (L + CH_MAX - 1u) / CH_MAX); }
uint chunkCount(uint L) { uint cs = chunkSize(L); return (L + cs - 1u) / cs; }
uint bodyItems(Body A) { ivec3 cl, nc; vec3 lo, hi; return A.meta.y + worldBox(A, cl, nc, lo, hi); }
// pass 31 (one workgroup): each awake slot's chunk count -> the first chunk of each (a prefix); the flags cleared
void worldUnits() {
    uint lid = gl_LocalInvocationID.x, na = state[S_NAWAKE];
    for (uint idx = lid; idx < na; idx += 64u) {
        state[sUnit(idx + 1u)] = chunkCount(bodyItems(bodies[aux[idx]]));
        state[sSpFlag(idx)] = 0u;
    }
    memoryBarrierBuffer();
    barrier();
    if (lid == 0u) {
        uint s = 0u;
        state[sUnit(0u)] = 0u;
        for (uint idx = 0u; idx < na; ++idx) { s += state[sUnit(idx + 1u)]; state[sUnit(idx + 1u)] = s; }
    }
}
shared uint gwBad;
// pass 32 (persistent workgroups over the chunks): one chunk's items tested once (staged), written in order to the
// body's own scratch region (its awake slot) at the chunk's slot; the count stored; a chunk that does not fit (a
// thread past its stage, or the chunk past its slot) flags its body for the fallback
void worldChunks() {
    uint lid = gl_LocalInvocationID.x, g = gl_WorkGroupID.x, G = gl_NumWorkGroups.x;
    uint na = state[S_NAWAKE], scr = prm[P_SCRATCH], U = state[sUnit(na)];
    uint stBase = prm[P_NB] * scr + (g * 64u + lid) * STAGE;
    for (uint u = g; u < U; u += G) {
        uint lo2 = 0u, hi2 = na;   // the last awake slot whose first chunk <= u
        while (hi2 - lo2 > 1u) {
            uint mid = (lo2 + hi2) / 2u;
            if (state[sUnit(mid)] <= u) lo2 = mid;
            else hi2 = mid;
        }
        uint idx = lo2, k = u - state[sUnit(idx)], ai = aux[idx];
        Body A = bodies[ai];
        mat3 R = qmat(A.quat), Rt = transpose(R);
        ivec3 cl, nc;
        vec3 lo, hi;
        uint L = A.meta.y + worldBox(A, cl, nc, lo, hi), cs = chunkSize(L);
        uint c0 = k * cs, c1 = min(L, c0 + cs), n = c1 - c0;
        uint per = (n + 63u) / 64u, q0 = min(c1, c0 + lid * per), q1 = min(c1, q0 + per);
        gMode = 3u; gBase = stBase; gCount = 0u;
        for (uint q = q0; q < q1; ++q) worldItem(ai, A, R, Rt, q, cl, nc, lo, hi);
        gwCnt[lid] = gCount;
        memoryBarrierShared();
        barrier();
        if (lid == 0u) {
            uint off = 0u, bad = 0u;
            for (uint t2 = 0u; t2 < 64u; ++t2) { gwOff[t2] = off; off += gwCnt[t2]; if (gwCnt[t2] > STAGE) bad = 1u; }
            gwTotal = off;
            gwBad = (bad != 0u || off > CH_SLOT) ? 1u : 0u;
        }
        memoryBarrierShared();
        barrier();
        if (gwBad == 0u) {
            uint dst = idx * scr + k * CH_SLOT + gwOff[lid];
            for (uint i = 0u; i < gwCnt[lid]; ++i) scratch[dst + i] = scratch[stBase + i];
        }
        if (lid == 0u) {
            state[sChunkCnt(u)] = gwTotal;
            if (gwBad != 0u) state[sSpFlag(idx)] = 1u;
        }
        memoryBarrierShared();
        barrier();
    }
}
// pass 33 (persistent workgroups over the awake slots): the chunks moved left in place to the serial layout (each read
// whole into registers before it is written: its target never reaches a later chunk's slot), then the cap + emit; a
// flagged body runs F5.5g's whole per-body pass here instead. pc.n bit 2 = the scramble (each chunk's order reversed)
void worldFinish() {
    uint lid = gl_LocalInvocationID.x, g = gl_WorkGroupID.x, G = gl_NumWorkGroups.x;
    uint na = state[S_NAWAKE], scr = prm[P_SCRATCH];
    bool scramble = (pc.n & 4u) != 0u;
    uint stBase = prm[P_NB] * scr + (g * 64u + lid) * STAGE;
    for (uint idx = g; idx < na; idx += G) {
        uint ai = aux[idx], base = idx * scr, u0 = state[sUnit(idx)], nch = state[sUnit(idx + 1u)] - u0;
        if (state[sSpFlag(idx)] != 0u) {
            if (lid == 0u) atomicAdd(state[S_SPREAD_FB], 1u);
            gDeepDone = true;
            worldBody(ai, base, stBase, 0u, true, false);
            gDeepDone = false;
            continue;
        }
        if (lid == 0u) {
            uint off = 0u;
            for (uint c = 0u; c < nch; ++c) { gwOff[c] = off; off += state[sChunkCnt(u0 + c)]; }
            gwTotal = off;
        }
        memoryBarrierShared();
        barrier();
        for (uint c = scramble ? 0u : 1u; c < nch; ++c) {   // chunk 0 is in place (unless scrambled)
            uint n = state[sChunkCnt(u0 + c)], src = base + c * CH_SLOT, dst = base + gwOff[c];
            Cand t[4];
            for (uint j = 0u; j < 4u; ++j) {
                uint i = lid + 64u * j;
                if (i < n) t[j] = scratch[src + i];
            }
            memoryBarrierBuffer();
            barrier();
            for (uint j = 0u; j < 4u; ++j) {
                uint i = lid + 64u * j;
                if (i < n) scratch[dst + (scramble ? n - 1u - i : i)] = t[j];
            }
            memoryBarrierBuffer();
            barrier();
        }
        uint cnt = min(gwTotal, scr);
        if (cnt > 0u) {
            if (lid == 0u) atomicAdd(state[S_CAND], cnt);
            coopEmit(ai, STATIC, base, cnt, 0u);
        }
        memoryBarrierBuffer();
        barrier();
    }
}
// pass 25: HARD_APPLY for the awake bodies: the summed body-body push (one thread), then the deepest world point
// (reduced in serial order, first on ties) at the pushed pose
void hardApplyPar() {
    uint lid = gl_LocalInvocationID.x, g = gl_WorkGroupID.x, G = gl_NumWorkGroups.x;
    uint na = state[S_NAWAKE];
    for (uint idx = g; idx < na; idx += G) {
        uint ai = aux[idx];
        if (lid == 0u) {
            vec3 push = pushes[ai].xyz;
            float pl = length(push);
            if (pl > 0.0) pushOut(ai, push / pl, pl);
            gwA = bodies[ai];
        }
        memoryBarrierShared();
        barrier();
        Body A = gwA;
        mat3 R = qmat(A.quat), Rt = transpose(R);
        ivec3 cl, nc;
        vec3 lo, hi;
        uint L = A.meta.y + worldBox(A, cl, nc, lo, hi);
        uint per = (L + 63u) / 64u, q0 = min(L, lid * per), q1 = min(L, q0 + per);
        gMode = 1u; gDeepPen = 0.0; gDeepN = vec3(0.0);
        for (uint q = q0; q < q1; ++q) worldItem(ai, A, R, Rt, q, cl, nc, lo, hi);
        gwDeep[lid] = vec4(gDeepN, gDeepPen);
        memoryBarrierShared();
        barrier();
        if (lid == 0u) {
            vec4 d = vec4(0.0);
            for (uint t2 = 0u; t2 < 64u; ++t2)
                if (gwDeep[t2].w > d.w) d = gwDeep[t2];
            if (d.w > pf(P_HARD)) {
                pushOut(ai, d.xyz, d.w);
                atomicAdd(state[S_HARD], 1u);
            }
        }
        memoryBarrierShared();
        barrier();
    }
}

// ---- F5.5a4: per-colour body lists (counting sort) and indirect dispatches ----
// aux after the flags: count[32] | offset[32] | fill[32] | args[32 x 3] | list[nb]
uint auxCol() { return 2u * NB() + 8u * MAXP(); }

int findHard(uint i, uint j) {
    uint key = pairKey(i, j), cap2 = 2u * MAXP(), h = hash32(key) & (cap2 - 1u);
    for (uint k = 0u; k < cap2; ++k) {
        uint slot = auxHash() + 2u * ((h + k) & (cap2 - 1u));
        uint v = aux[slot];
        if (v == key) return int(aux[slot + 1u]);
        if (v == 0u) return -1;
    }
    return -1;
}

uint prio(uint i) { return hash32(i * 0x9E3779B9u + 0x7F4A7C15u); }

// ---- the solve: shared by both pipelines (F5.5a) ----
void colourInit(uint i) {
    state[sColour(i)] = dynamicBody(i) ? UNC : NOCOL;
}
// an uncoloured body that beats every competing neighbour takes colour r
void colourRound(uint i, uint r) {
    if (state[sColour(i)] != UNC) return;
    uint pi = prio(i);
    uint o = state[sOffset(i)], c = state[sCount(i)];
    for (uint p = 0u; p < c; ++p) {
        uvec4 ids = cons[lists[o + p]].ids;
        uint j = ids.x == i ? ids.y : ids.x;
        if (j == STATIC || j == i) continue;
        uint cj = state[sColour(j)];
        if (cj != UNC && cj != r) continue;   // coloured in an earlier round, or not dynamic
        uint pj = prio(j);
        if (pj > pi || (pj == pi && j > i)) return;
    }
    state[sColour(i)] = r;
    atomicMax(state[S_MAXCOL], r + 1u);
}
void colourFinal(uint i) {
    if (state[sColour(i)] == UNC) atomicAdd(state[S_UNCOL], 1u);
}
void primal(uint i, uint c) {
    if (state[sColour(i)] != c) return;
    Body b = bodies[i];
    float dt = pf(P_DT);
    float dt2 = dt * dt;
    mat3 R = qmat(b.quat);
    mat3 Ib = mat3(b.I0.xyz, b.I1.xyz, b.I2.xyz);   // symmetric: rows == columns
    mat3 Iw = R * Ib * transpose(R) / dt2;
    float mdt2 = b.fixedProps.x / dt2;
    float lhs[36];
    float rhs[6];
    for (int q = 0; q < 36; ++q) lhs[q] = 0.0;
    for (int a = 0; a < 3; ++a) lhs[a * 6 + a] = mdt2;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) lhs[(3 + r) * 6 + 3 + c] = Iw[c][r];
    vec3 dp = b.pos.xyz - b.inertial.xyz;
    vec3 da = Iw * angDisp(b.quat, b.inertialQuat);
    for (int a = 0; a < 3; ++a) { rhs[a] = mdt2 * dp[a]; rhs[3 + a] = da[a]; }
    uint o = state[sOffset(i)], cnt = state[sCount(i)];
    for (uint p = 0u; p < cnt; ++p) {
        Con k = cons[lists[o + p]];
        float Cn, Ct1, Ct2;
        vec3 rAW, rBW;
        evalC(k, Cn, Ct1, Ct2, rAW, rBW);
        float fn = min(k.lam.z * Cn + k.rB.w, 0.0);
        float ft1 = k.lam.w * Ct1 + k.lam.x, ft2 = k.misc.x * Ct2 + k.lam.y;
        float bound = abs(fn) * k.n.w, fs = sqrt(ft1 * ft1 + ft2 * ft2);
        if (fs > bound && fs > 0.0) { ft1 *= bound / fs; ft2 *= bound / fs; }
        bool isA = k.ids.x == i;
        float sJ = isA ? 1.0 : -1.0;
        vec3 arm = isA ? rAW : rBW;
        vec3 dirs[3] = vec3[3](k.n.xyz, k.t1.xyz, k.t2.xyz);
        float F[3] = float[3](fn, ft1, ft2);
        float K[3] = float[3](k.lam.z, k.lam.w, k.misc.x);
        for (int r = 0; r < 3; ++r) {
            vec3 jl = dirs[r] * sJ, ja = cross(arm, jl);
            float J[6] = float[6](jl.x, jl.y, jl.z, ja.x, ja.y, ja.z);
            for (int pp = 0; pp < 6; ++pp) {
                rhs[pp] += J[pp] * F[r];
                for (int qq = 0; qq < 6; ++qq) lhs[pp * 6 + qq] += K[r] * J[pp] * J[qq];
            }
        }
    }
    for (int q = 0; q < 6; ++q) rhs[q] = -rhs[q];
    float d[6];
    solve6(lhs, rhs, d);
    vec3 dAng = vec3(d[3], d[4], d[5]);
    bodies[i].pos.xyz = b.pos.xyz + vec3(d[0], d[1], d[2]);
    bodies[i].quat = qintegrate(b.quat, dAng);
    bodies[i].cumAng.xyz = b.cumAng.xyz + dAng;
}
#ifdef VF_PRIMAL
// F5.5a4: one body's PRIMAL on a 32-thread group (eight per workgroup): the lanes take its constraints strided and sum
// partial terms; a fixed tree in shared memory adds the partials; lane 0 adds the inertial terms and solves. Every
// thread of the workgroup calls it (it has barriers); `has` says whether this group has a body. Both dispatch forms
// (29 indirect per colour, 30 one workgroup for every colour) call this, so they compute the same numbers.
// Deterministic, but NOT bit-equal to primal() (the sum is reassociated).
shared float gpRed[256 * 42];
void primalVG(uint i, bool has) {
    uint lid = gl_LocalInvocationID.x, l = lid & 31u;
    uint r = lid * 42u;
    if (has) {
        float lhs[36];
        float rhs[6];
        for (int q = 0; q < 36; ++q) lhs[q] = 0.0;
        for (int q = 0; q < 6; ++q) rhs[q] = 0.0;
        uint o = state[sOffset(i)], cnt = state[sCount(i)];
        for (uint p = l; p < cnt; p += 32u) {
            Con k = cons[lists[o + p]];
            float Cn, Ct1, Ct2;
            vec3 rAW, rBW;
            evalC(k, Cn, Ct1, Ct2, rAW, rBW);
            float fn = min(k.lam.z * Cn + k.rB.w, 0.0);
            float ft1 = k.lam.w * Ct1 + k.lam.x, ft2 = k.misc.x * Ct2 + k.lam.y;
            float bound = abs(fn) * k.n.w, fs = sqrt(ft1 * ft1 + ft2 * ft2);
            if (fs > bound && fs > 0.0) { ft1 *= bound / fs; ft2 *= bound / fs; }
            bool isA = k.ids.x == i;
            float sJ = isA ? 1.0 : -1.0;
            vec3 arm = isA ? rAW : rBW;
            vec3 dirs[3] = vec3[3](k.n.xyz, k.t1.xyz, k.t2.xyz);
            float F[3] = float[3](fn, ft1, ft2);
            float K[3] = float[3](k.lam.z, k.lam.w, k.misc.x);
            for (int r = 0; r < 3; ++r) {
                vec3 jl = dirs[r] * sJ, ja = cross(arm, jl);
                float J[6] = float[6](jl.x, jl.y, jl.z, ja.x, ja.y, ja.z);
                for (int pp = 0; pp < 6; ++pp) {
                    rhs[pp] += J[pp] * F[r];
                    for (int qq = 0; qq < 6; ++qq) lhs[pp * 6 + qq] += K[r] * J[pp] * J[qq];
                }
            }
        }
        for (int q = 0; q < 36; ++q) gpRed[r + uint(q)] = lhs[q];
        for (int q = 0; q < 6; ++q) gpRed[r + 36u + uint(q)] = rhs[q];
    }
    memoryBarrierShared();
    barrier();
    for (uint off = 16u; off > 0u; off >>= 1u) {
        if (has && l < off)
            for (uint q = 0u; q < 42u; ++q) gpRed[r + q] += gpRed[r + off * 42u + q];
        memoryBarrierShared();
        barrier();
    }
    if (has && l == 0u) {
        uint r0 = r;
        Body b = bodies[i];
        float dt = pf(P_DT);
        float dt2 = dt * dt;
        mat3 R = qmat(b.quat);
        mat3 Ib = mat3(b.I0.xyz, b.I1.xyz, b.I2.xyz);   // symmetric: rows == columns
        mat3 Iw = R * Ib * transpose(R) / dt2;
        float mdt2 = b.fixedProps.x / dt2;
        float lhs[36];
        float rhs[6];
        for (int q = 0; q < 36; ++q) lhs[q] = 0.0;
        for (int a = 0; a < 3; ++a) lhs[a * 6 + a] = mdt2;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) lhs[(3 + r) * 6 + 3 + c] = Iw[c][r];
        vec3 dp = b.pos.xyz - b.inertial.xyz;
        vec3 da = Iw * angDisp(b.quat, b.inertialQuat);
        for (int a = 0; a < 3; ++a) { rhs[a] = mdt2 * dp[a]; rhs[3 + a] = da[a]; }
        for (int q = 0; q < 36; ++q) lhs[q] += gpRed[r0 + uint(q)];
        for (int q = 0; q < 6; ++q) rhs[q] += gpRed[r0 + 36u + uint(q)];
        for (int q = 0; q < 6; ++q) rhs[q] = -rhs[q];
        float d[6];
        solve6(lhs, rhs, d);
        vec3 dAng = vec3(d[3], d[4], d[5]);
        bodies[i].pos.xyz = b.pos.xyz + vec3(d[0], d[1], d[2]);
        bodies[i].quat = qintegrate(b.quat, dAng);
        bodies[i].cumAng.xyz = b.cumAng.xyz + dAng;
    }
}
#endif

void dual(uint ci) {
    if (ci >= min(state[S_NCONS], prm[P_MAXCONS])) return;
    Con k = cons[ci];
    float Cn, Ct1, Ct2;
    vec3 rAW, rBW;
    evalC(k, Cn, Ct1, Ct2, rAW, rBW);
    float beta = pf(P_BETA), penMax = pf(P_PENMAX);
    float ln = min(k.lam.z * Cn + k.rB.w, 0.0);
    if (ln < 0.0) k.lam.z = min(k.lam.z + beta * abs(Cn), penMax);
    float bound = abs(ln) * k.n.w;
    float ft1 = k.lam.w * Ct1 + k.lam.x, ft2 = k.misc.x * Ct2 + k.lam.y;
    float fs = sqrt(ft1 * ft1 + ft2 * ft2);
    if (fs > bound && fs > 0.0) { ft1 *= bound / fs; ft2 *= bound / fs; }
    if (fs <= bound) {
        if (abs(Ct1) > 1e-5) k.lam.w = min(k.lam.w + beta * abs(Ct1), penMax);
        if (abs(Ct2) > 1e-5) k.misc.x = min(k.misc.x + beta * abs(Ct2), penMax);
    }
    k.rB.w = ln;
    k.lam.x = ft1;
    k.lam.y = ft2;
    k.misc.y = fs <= bound ? 1.0 : 0.0;
    cons[ci] = k;
}

#ifdef VF_FUSED
shared uint gsLeft, gsUsed;
void syncAll() { memoryBarrierBuffer(); barrier(); }
// pass 17: COLOUR_INIT, the rounds until none is left uncoloured, COLOUR_FINAL; this tick's colour count -> S_TICKCOL.
void fusedColour() {
    uint lid = gl_LocalInvocationID.x, nb = NB();
    for (uint i = lid; i < nb; i += VF_GROUP) colourInit(i);
    if (lid == 0u) gsUsed = 0u;
    syncAll();
    for (uint r = 0u; r < 32u; ++r) {
        if (lid == 0u) gsLeft = 0u;
        syncAll();
        for (uint i = lid; i < nb; i += VF_GROUP) {
            if (state[sColour(i)] != UNC) continue;
            colourRound(i, r);
            if (state[sColour(i)] == UNC) atomicAdd(gsLeft, 1u);
            else atomicMax(gsUsed, r + 1u);
        }
        syncAll();
        if (gsLeft == 0u) break;
        barrier();   // every thread has read gsLeft before thread 0 clears it
    }
    for (uint i = lid; i < nb; i += VF_GROUP) colourFinal(i);
    if (lid == 0u) state[S_TICKCOL] = gsUsed;
}
// pass 18: one iteration's PRIMAL for every colour used, in colour order (the DUAL stays a wide dispatch: run 1 put it
// here too and one workgroup was slower than the per-colour path at 1,000 bodies).
void fusedPrimal() {
    uint lid = gl_LocalInvocationID.x, nb = NB(), used = state[S_TICKCOL];
    for (uint c = 0u; c < used; ++c) {
        for (uint i = lid; i < nb; i += VF_GROUP)
            if (state[sColour(i)] == c) primal(i, c);
        syncAll();
    }
}
#endif


void main() {
#ifdef VF_FUSED
    if (pc.pass == 17u) { fusedColour(); return; }
    if (pc.pass == 18u) { fusedPrimal(); return; }
#endif
    if (pc.pass == 21u) { narrowPairs(); return; }
    if (pc.pass == 22u) { narrowHard(); return; }
    if (pc.pass == 24u) { worldPar(); return; }
    if (pc.pass == 31u) { worldUnits(); return; }    // F5.5h
    if (pc.pass == 32u) { worldChunks(); return; }
    if (pc.pass == 33u) { worldFinish(); return; }
    if (pc.pass == 25u) { hardApplyPar(); return; }
#ifdef VF_PRIMAL
    if (pc.pass == 29u) {   // PRIMAL arg = colour (indirect): workgroup w's 32-thread group g takes body 8 w + g
        uint k = gl_WorkGroupID.x * 8u + (gl_LocalInvocationID.x >> 5u), n = aux[auxCol() + pc.arg];
        bool has = k < n;
        primalVG(has ? aux[auxCol() + 192u + aux[auxCol() + 32u + pc.arg] + k] : 0u, has);
        return;
    }
    if (pc.pass == 30u) {   // PRIMAL, every colour used in order, one workgroup (the form for few awake bodies)
        uint used = state[S_TICKCOL], g8 = gl_LocalInvocationID.x >> 5u;
        for (uint c = 0u; c < used; ++c) {
            uint n = aux[auxCol() + c], o = aux[auxCol() + 32u + c];
            for (uint b8 = 0u; b8 < n; b8 += 8u) {
                uint k = b8 + g8;
                bool has = k < n;
                primalVG(has ? aux[auxCol() + 192u + o + k] : 0u, has);
            }
            memoryBarrierBuffer();
            barrier();
        }
        return;
    }
#endif
    uint t = gl_GlobalInvocationID.x;
    if (t >= pc.n) return;
    float dt = pf(P_DT);

    if (pc.pass == 0u) {   // INTEGRATE
        uint i = t;
        state[sCount(i)] = 0u;
        Body b = bodies[i];
        b.initial = vec4(b.pos.xyz, 0.0);
        b.initialQuat = b.quat;
        b.cumAng = vec4(0.0);
        if (!dynamicBody(i)) {
            b.inertial = vec4(b.pos.xyz, 0.0);
            b.inertialQuat = b.quat;
        } else {
            float prevSpeed = length(b.vel.xyz) + length(b.angVel.xyz) * b.comLocal.w;
            b.vel.xyz += vec3(pf(P_GX), pf(P_GY), pf(P_GZ)) * dt;
            b.inertial = vec4(b.pos.xyz + b.vel.xyz * dt, 0.0);
            b.inertialQuat = qintegrate(b.quat, b.angVel.xyz * dt);
            if (prevSpeed >= pf(P_REST)) {
                b.pos.xyz = b.inertial.xyz;
                b.quat = b.inertialQuat;
                b.cumAng = vec4(angDisp(b.inertialQuat, b.initialQuat), 0.0);
            }
        }
        bodies[i] = b;
        return;
    }
    if (pc.pass == 1u) {   // AABB at the current pose
        uint i = t;
        Body b = bodies[i];
        mat3 R = qmat(b.quat);
        vec3 lo = vec3(INF), hi = vec3(-INF);
        for (uint k = 0u; k < b.meta.y; ++k) {
            vec3 w = b.pos.xyz + R * corners[b.meta.x + k].xyz;
            lo = min(lo, w);
            hi = max(hi, w);
        }
        bodies[i].lo = vec4(lo, 0.0);
        bodies[i].hi = vec4(hi, 0.0);
        return;
    }
    if (pc.pass == 2u) {   // WORLD contacts
        uint i = t;
        if (!dynamicBody(i)) return;
        gMode = 0u; gBase = i * prm[P_SCRATCH]; gCount = 0u;
        againstWorld(i);
        emitPair(i, STATIC);
        return;
    }
    if (pc.pass == 3u) {   // PAIR contacts: worker i, every j > i
        uint i = t;
        for (uint j = i + 1u; j < NB(); ++j) {
            bool di = dynamicBody(i), dj = dynamicBody(j);
            if (!di && !dj) continue;
            if (!aabbOverlap(i, j, pf(P_MARGIN))) continue;
            atomicAdd(state[S_PAIRS], 1u);
            uint a = di ? i : j, b = a == i ? j : i;
            gMode = 0u; gBase = i * prm[P_SCRATCH]; gCount = 0u;
            againstBody(a, b);
            if (gCount == 0u) continue;
            if (bodies[b].meta.z != 0u && length(bodies[a].vel.xyz) > pf(P_WAKEV)) state[sWake(b)] = 1u;
            emitPair(a, b);
        }
        return;
    }
    if (pc.pass == 4u) {   // PREFIX
        uint off = 0u;
        for (uint i = 0u; i < NB(); ++i) {
            state[sOffset(i)] = off;
            state[sFill(i)] = 0u;
            off += state[sCount(i)];
        }
        uint nc = min(state[S_NCONS], prm[P_MAXCONS]);
        state[S_MAXCONS] = max(state[S_MAXCONS], nc);
        return;
    }
    if (pc.pass == 5u) {   // SCATTER
        uint ci = t;
        if (ci >= min(state[S_NCONS], prm[P_MAXCONS])) return;
        uvec4 ids = cons[ci].ids;
        uint pa = atomicAdd(state[sFill(ids.x)], 1u);
        lists[state[sOffset(ids.x)] + pa] = ci;
        if (ids.y != STATIC) {
            uint pb = atomicAdd(state[sFill(ids.y)], 1u);
            lists[state[sOffset(ids.y)] + pb] = ci;
        }
        return;
    }
    if (pc.pass == 6u) {   // SORT each body's list by key (order-independent sums)
        uint i = t;
        uint o = state[sOffset(i)], c = state[sCount(i)];
        for (uint p = 1u; p < c; ++p) {
            uint v = lists[o + p];
            uvec2 kv = cons[v].ids.zw;
            int q = int(p) - 1;
            while (q >= 0) {
                uvec2 kq = cons[lists[o + uint(q)]].ids.zw;
                if (kq.x < kv.x || (kq.x == kv.x && kq.y <= kv.y)) break;
                lists[o + uint(q) + 1u] = lists[o + uint(q)];
                --q;
            }
            lists[o + uint(q + 1)] = v;
        }
        return;
    }
    if (pc.pass == 7u) { colourInit(t); return; }   // COLOUR_INIT
    if (pc.pass == 8u) { colourRound(t, pc.arg); return; }   // COLOUR_ROUND arg
    if (pc.pass == 9u) { colourFinal(t); return; }   // COLOUR_FINAL
    if (pc.pass == 10u) { primal(t, pc.arg); return; }   // PRIMAL for colour arg
    if (pc.pass == 11u) { dual(t); return; }   // DUAL
    if (pc.pass == 19u) {   // AWAKE_LIST (F5.5a2)
        if (dynamicBody(t)) aux[atomicAdd(state[S_NAWAKE], 1u)] = t;
        return;
    }
    if (pc.pass == 20u) {   // BROAD: awake body a against every j; each unordered pair once, roles as K_PAIRS
        if (t >= state[S_NAWAKE]) return;
        uint a = aux[t];
        float m = pc.arg == 0u ? pf(P_MARGIN) : 0.0;
        uint ctr = pc.arg == 0u ? S_NPAIRS : S_NHPAIRS;
        for (uint j = 0u; j < NB(); ++j) {
            if (j == a || (j < a && dynamicBody(j))) continue;   // a dynamic j < a finds this pair itself
            if (!aabbOverlap(a, j, m)) continue;
            if (pc.arg == 0u) atomicAdd(state[S_PAIRS], 1u);
            uint slot = atomicAdd(state[ctr], 1u);
            if (slot >= MAXP()) { atomicAdd(state[S_OVF_PAIRS], 1u); continue; }
            aux[auxPairs(pc.arg) + 2u * slot] = a;
            aux[auxPairs(pc.arg) + 2u * slot + 1u] = j;
        }
        return;
    }
    if (pc.pass == 23u) {   // HARD_BODY from the stored pair results: the same sum over j ascending as pass 12
        uint i = t;
        if (aux[auxFlag() + i] == 0u) { pushes[i] = vec4(0.0); return; }   // in no pair deeper than P_HARD: no push
        vec3 push = vec3(0.0);
        float hc = pf(P_HARD);
        for (uint j = 0u; j < NB(); ++j) {
            if (j == i) continue;
            uint lo = min(i, j), hi = max(i, j);
            bool dl = dynamicBody(lo), dh = dynamicBody(hi);
            if (!dl && !dh) continue;
            if (!aabbOverlap(i, j, 0.0)) continue;
            uint a = dl ? lo : hi, b = a == lo ? hi : lo;
            int hp = findHard(i, j);
            if (hp < 0) {   // the hard pair list overflowed (counted): the serial test
                gMode = 1u; gDeepPen = 0.0; gDeepN = vec3(0.0);
                againstBody(a, b);
            } else {
                gDeepPen = hres[hp].w;
                gDeepN = hres[hp].xyz;
            }
            if (gDeepPen <= hc) continue;
            float ia = bodies[a].pos.w, ib = dynamicBody(b) ? bodies[b].pos.w : 0.0;
            if (i == a) {
                push += gDeepN * (gDeepPen * ia / (ia + ib));
                atomicAdd(state[S_HARD], 1u);
            } else if (ib > 0.0) {
                push -= gDeepN * (gDeepPen * ib / (ia + ib));
            }
        }
        pushes[i] = vec4(push, 0.0);
        return;
    }
    if (pc.pass == 26u) {   // COLOUR_COUNT (F5.5a4)
        uint c = state[sColour(t)];
        if (c < 32u) atomicAdd(aux[auxCol() + c], 1u);
        return;
    }
    if (pc.pass == 27u) {   // COLOUR_PREFIX (1 thread): offsets and the indirect dispatch per colour
        uint off = 0u;
        for (uint c = 0u; c < 32u; ++c) {
            uint n = aux[auxCol() + c];
            aux[auxCol() + 32u + c] = off;
            aux[auxCol() + 64u + c] = 0u;
            aux[auxCol() + 96u + 3u * c] = (n + 7u) / 8u;   // eight bodies per primal workgroup
            aux[auxCol() + 97u + 3u * c] = 1u;
            aux[auxCol() + 98u + 3u * c] = 1u;
            off += n;
        }
        return;
    }
    if (pc.pass == 28u) {   // COLOUR_SCATTER
        uint c = state[sColour(t)];
        if (c >= 32u) return;
        uint slot = atomicAdd(aux[auxCol() + 64u + c], 1u);
        aux[auxCol() + 192u + aux[auxCol() + 32u + c] + slot] = t;
        return;
    }
    if (pc.pass == 12u) {   // HARD_BODY: this body's share of every body-body push, from the pre-push poses
        uint i = t;
        vec3 push = vec3(0.0);
        float hc = pf(P_HARD);
        for (uint j = 0u; j < NB(); ++j) {
            if (j == i) continue;
            uint lo = min(i, j), hi = max(i, j);
            bool dl = dynamicBody(lo), dh = dynamicBody(hi);
            if (!dl && !dh) continue;
            if (!aabbOverlap(i, j, 0.0)) continue;
            uint a = dl ? lo : hi, b = a == lo ? hi : lo;
            gMode = 1u; gDeepPen = 0.0; gDeepN = vec3(0.0);
            againstBody(a, b);
            if (gDeepPen <= hc) continue;
            float ia = bodies[a].pos.w, ib = dynamicBody(b) ? bodies[b].pos.w : 0.0;
            if (i == a) {
                push += gDeepN * (gDeepPen * ia / (ia + ib));
                atomicAdd(state[S_HARD], 1u);
            } else if (ib > 0.0) {
                push -= gDeepN * (gDeepPen * ib / (ia + ib));
            }
        }
        pushes[i] = vec4(push, 0.0);
        return;
    }
    if (pc.pass == 13u) {   // HARD_APPLY: the summed body-body push, then the world (last: it is authoritative)
        uint i = t;
        if (!dynamicBody(i)) return;
        vec3 push = pushes[i].xyz;
        float pl = length(push);
        if (pl > 0.0) pushOut(i, push / pl, pl);
        gMode = 1u; gDeepPen = 0.0; gDeepN = vec3(0.0);
        againstWorld(i);
        if (gDeepPen > pf(P_HARD)) {
            pushOut(i, gDeepN, gDeepPen);
            atomicAdd(state[S_HARD], 1u);
        }
        return;
    }
    if (pc.pass == 14u) {   // FINISH: velocities, wake, sleep
        uint i = t;
        Body b = bodies[i];
        bool dyn = dynamicBody(i);
        if (dyn) {
            b.vel.xyz = (b.pos.xyz - b.initial.xyz) / dt;
            b.angVel.xyz = angDisp(b.quat, b.initialQuat) / dt;
        }
        // asleepTicks (F5.5b, BodySim.cpp's rule): 0 on wake and on the tick a body sleeps, +1 each asleep tick
        if (state[sWake(i)] != 0u) { b.meta.z = 0u; b.meta.w = 0u; b.fixedProps.w = 0.0; state[sWake(i)] = 0u; }
        if (b.meta.z != 0u) b.fixedProps.w += 1.0;
        if (b.meta.z == 0u) {
            float sv = pf(P_SLEEPV);
            bool slow = length(b.vel.xyz) < sv && length(b.angVel.xyz) * b.comLocal.w < sv;
            b.meta.w = slow ? b.meta.w + 1u : 0u;
            if (b.meta.w >= prm[P_SLEEPT]) {
                b.meta.z = 1u;
                b.fixedProps.w = 0.0;
                b.vel = vec4(0.0);
                b.angVel = vec4(0.0);
            }
            atomicAdd(state[S_AWAKE], 1u);
        }
        bodies[i] = b;
        return;
    }
    if (pc.pass == 15u) {   // WARM_CLEAR the half written this tick
        warm[(pc.parity ^ 1u) * prm[P_WARMCAP] + t].key = uvec4(0u);
        return;
    }
    if (pc.pass == 16u) {   // WARM_SAVE into the other half
        uint ci = t;
        if (ci >= min(state[S_NCONS], prm[P_MAXCONS])) return;
        Con k = cons[ci];
        uvec2 key = k.ids.zw;
        uint base = (pc.parity ^ 1u) * prm[P_WARMCAP];
        uint s = warmSlot(key);
        for (uint p = 0u; p < 64u; ++p) {
            uint idx = base + ((s + p) & (prm[P_WARMCAP] - 1u));
            if (atomicCompSwap(warm[idx].key.z, 0u, 1u) == 0u) {
                warm[idx].key.x = key.x;
                warm[idx].key.y = key.y;
                warm[idx].key.w = k.misc.y > 0.5 ? 1u : 0u;
                warm[idx].lam = vec4(k.rB.w, k.lam.x, k.lam.y, k.lam.z);
                warm[idx].pen = vec4(k.lam.w, k.misc.x, 0.0, 0.0);
                warm[idx].anchor = vec4(k.anchor.xyz, 0.0);
                return;
            }
        }
        return;
    }
}
