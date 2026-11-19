// docs/VoxelFoundationPlan.md §6.7 "Drawing", F5.3 -- see BodyDraw.h.
#include "core/voxfound/BodyDraw.h"

#include <atomic>
#include <cmath>

#include "core/voxfound/BodySim.h"
#include "core/voxfound/Mesher.h"

namespace Phyxel::Core::VoxFound {

namespace {

std::atomic<size_t> g_builds{0};

void appendBody(BodyDrawSet& set, const VoxelGrid& g, uint32_t index) {
    set.first.push_back(uint32_t(set.faces.size()));
    if (g.dim.x > kMaxChunkVoxels || g.dim.y > kMaxChunkVoxels || g.dim.z > kMaxChunkVoxels) {
        ++set.refused;   // the record's 8-bit positions cannot hold it (counted, never silent)
        set.count.push_back(0);
        return;
    }
    const std::vector<Quad> quads = meshRuns(g, Outside::Air);
    for (const Quad& q : quads) {
        const PackedFace f = packFace(q);
        set.faces.push_back(BodyFace{f.a, f.b, index});
    }
    set.count.push_back(uint32_t(quads.size()));
}

}  // namespace

uint64_t BodyDrawSet::hash() const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint32_t v) {
        for (int k = 0; k < 4; ++k) h = (h ^ ((v >> (8 * k)) & 0xFFu)) * 1099511628211ull;
    };
    for (const BodyFace& f : faces) {
        mix(f.a);
        mix(f.b);
        mix(f.body);
    }
    return h;
}

BodyDrawSet buildBodyFaces(const std::vector<Body>& bodies) {
    ++g_builds;
    BodyDrawSet set;
    for (size_t i = 0; i < bodies.size(); ++i) appendBody(set, bodies[i].grid, uint32_t(i));
    return set;
}

BodyDrawSet buildBodyFaces(const std::vector<SimBody>& bodies) {
    ++g_builds;
    BodyDrawSet set;
    for (size_t i = 0; i < bodies.size(); ++i) appendBody(set, bodies[i].src.grid, uint32_t(i));
    return set;
}

size_t bodyFaceBuildCount() { return g_builds.load(); }

BodyWorldPose worldPoseOf(const SimBody& b, glm::dvec3 areaOriginM) {
    const double vm = b.src.grid.voxelM;
    const glm::dvec3 comLocal = (b.src.com - glm::dvec3(b.src.origin)) * vm;   // grid corner -> centre, body frame
    BodyWorldPose w;
    w.q = b.quat;
    w.t = areaOriginM + (b.pos - b.quat * comLocal);   // where grid corner (0,0,0) is now: pos - R comLocal
    w.voxelM = vm;
    w.uvOrigin = b.src.origin;
    w.areaAbs = areaOriginM;
    return w;
}

BodyPose relativePose(const BodyWorldPose& w, glm::dvec3 camera) {
    BodyPose p;
    p.q = glm::vec4(float(w.q.x), float(w.q.y), float(w.q.z), float(w.q.w));
    p.t = glm::vec4(glm::vec3(w.t - camera), float(w.voxelM));   // relative in double, then float
    p.uvOrigin = glm::ivec4(w.uvOrigin, 0);
    p.areaAbs = glm::vec4(glm::vec3(w.areaAbs), 0.0f);
    return p;
}

// voxfound_face.vert's corner table (faceID 0 +Z 1 -Z 2 +X 3 -X 4 +Y 5 -Y), integers
glm::ivec3 faceCornerVoxel(PackedFace f, uint32_t corner) {
    const uint32_t axis = (f.a >> 24) & 3u;
    const bool pos = ((f.a >> 26) & 1u) != 0u;
    const glm::ivec3 o(int(f.a & 0xFFu), int((f.a >> 8) & 0xFFu), int((f.a >> 16) & 0xFFu));
    const int du = int((f.b & 0xFFu) + 1u), dv = int(((f.b >> 8) & 0xFFu) + 1u);
    const uint32_t faceID = axis == 2u ? (pos ? 0u : 1u) : axis == 0u ? (pos ? 2u : 3u) : (pos ? 4u : 5u);
    const int b0 = int(corner & 1u), b1 = int((corner >> 1) & 1u);
    glm::ivec3 off;
    if (faceID == 0u)      off = {b0, b1, 1};
    else if (faceID == 1u) off = {1 - b0, b1, 0};
    else if (faceID == 2u) off = {1, b1, 1 - b0};
    else if (faceID == 3u) off = {0, b1, b0};
    else if (faceID == 4u) off = {b0, 1, 1 - b1};
    else                   off = {b0, 0, b1};
    glm::ivec3 size(1);
    size[(axis + 1u) % 3u] = du;
    size[(axis + 2u) % 3u] = dv;
    return o + off * size;
}

// GLSL: v + 2 cross(q.xyz, cross(q.xyz, v) + q.w v)
glm::vec3 rotateByQuat(glm::vec4 q, glm::vec3 v) {
    const glm::vec3 u(q.x, q.y, q.z);
    return v + 2.0f * glm::cross(u, glm::cross(u, v) + q.w * v);
}

glm::vec3 bodyCornerRel(const BodyPose& p, PackedFace f, uint32_t corner) {
    const glm::vec3 local = glm::vec3(faceCornerVoxel(f, corner)) * p.t.w;
    return glm::vec3(p.t) + rotateByQuat(p.q, local);
}

glm::vec3 bodyNormal(const BodyPose& p, PackedFace f) {
    const uint32_t axis = (f.a >> 24) & 3u;
    glm::vec3 n(0.0f);
    n[axis] = ((f.a >> 26) & 1u) ? 1.0f : -1.0f;
    return rotateByQuat(p.q, n);
}

}  // namespace Phyxel::Core::VoxFound

namespace Phyxel::Core::VoxFound {

// F5.5b per-body cull: the grid box [0, 2 halfM] at the pose -> its centre t + R halfM and its axis-aligned half-extent
// |R| halfM (R's columns are the rotated axes), grown by a relative 1e-5 + 1 mm against float rounding in the shader.
glm::vec3 bodyHalfM(const SimBody& b) { return glm::vec3(b.src.grid.dim) * (b.src.grid.voxelM * 0.5f); }
void bodyBoxRel(const BodyPose& p, glm::vec3 halfM, glm::vec3* lo, glm::vec3* hi) {
    const glm::vec3 ax = rotateByQuat(p.q, glm::vec3(halfM.x, 0.0f, 0.0f));
    const glm::vec3 ay = rotateByQuat(p.q, glm::vec3(0.0f, halfM.y, 0.0f));
    const glm::vec3 az = rotateByQuat(p.q, glm::vec3(0.0f, 0.0f, halfM.z));
    const glm::vec3 c = glm::vec3(p.t) + ax + ay + az;
    const glm::vec3 e = glm::abs(ax) + glm::abs(ay) + glm::abs(az);
    const glm::vec3 g = e + (glm::abs(c) + e) * 1e-5f + glm::vec3(1e-3f);
    *lo = c - g;
    *hi = c + g;
}
bool bodyKept(const BodyPose& p, glm::vec3 halfM, const glm::vec4 planes[6]) {
    glm::vec3 lo, hi;
    bodyBoxRel(p, halfM, &lo, &hi);
    return boxInFrustum(planes, lo, hi);
}

}  // namespace Phyxel::Core::VoxFound
