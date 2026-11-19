#pragma once
// docs/VoxelFoundationPlan.md §6.7 "Drawing", F5.3 -- bodies drawn with one transform.
//
// "A body is a small block grid with one transform. Faces are built once when the body is made, drawn with the body's
// transform; the record is the §4.2 face record + a body index. No per-voxel float transform."
//
//  * BodyFace = the 8 B PackedFace (Mesher.h, unchanged; positions relative to the BODY's grid) + the body index.
//    shaders/voxfound_body.vert reads it as-is (binding 1, stride 12). Faces are built once (buildBodyFaces), with
//    meshRuns, the CPU reference the GPU face builder equals (F2.1); after that only poses change.
//  * BodyPose (std430, 64 B): the body's one transform for this frame, CAMERA-RELATIVE (computed in double here, so
//    no large float is subtracted on the device). A corner of a face lands at  t.xyz + rotate(q, local * t.w).
//  * Texture: a pure function of where the voxel was in its source area when it broke off (§4.3), so a piece carries
//    the texture it had in the wall: uv from (uvOrigin + local) * voxel size.
// Predictions: docs/evidence/voxfound_f53_prediction.txt. Tests: VoxFoundBodyDrawTest.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/voxfound/Bodies.h"
#include "core/voxfound/Mesher.h"

namespace Phyxel::Core::VoxFound {

struct SimBody;   // BodySim.h

struct BodyFace {
    uint32_t a = 0, b = 0;   // PackedFace, body-grid-local
    uint32_t body = 0;       // index into the pose buffer
};
static_assert(sizeof(BodyFace) == 12, "voxfound_body.vert instance stride");

struct BodyDrawSet {
    std::vector<BodyFace> faces;             // body after body, ascending body index
    std::vector<uint32_t> first, count;      // per body: its face range
    size_t refused = 0;                      // bodies with a side > kMaxChunkVoxels (no faces; counted, never silent)
    uint64_t hash() const;                   // FNV-1a over the faces (the "built once" check)
};

/// Mesh every body's grid once (meshRuns, Outside::Air) into one face list.
BodyDrawSet buildBodyFaces(const std::vector<Body>& bodies);
BodyDrawSet buildBodyFaces(const std::vector<SimBody>& bodies);
/// How many times buildBodyFaces has run in this process (the F5.3 D2 check).
size_t bodyFaceBuildCount();

struct BodyPose {
    glm::vec4 q{0.0f, 0.0f, 0.0f, 1.0f};     // rotation (x, y, z, w)
    glm::vec4 t{0.0f};                       // xyz: body grid corner (0,0,0), camera-relative metres; w: voxel size m
    glm::ivec4 uvOrigin{0};                  // xyz: the body's grid origin in its source area (voxels)
    glm::vec4 areaAbs{0.0f};                 // xyz: the source area's origin, absolute metres (varied-hash seed)
};
static_assert(sizeof(BodyPose) == 64, "std430 pose stride");

/// A body's transform in absolute world metres (double): what the solver owns; the renderer makes it camera-relative.
struct BodyWorldPose {
    glm::dquat q{1.0, 0.0, 0.0, 0.0};
    glm::dvec3 t{0.0};                       // where body grid corner (0,0,0) is now, world metres
    double voxelM = 0.125;
    glm::ivec3 uvOrigin{0};                  // the body's grid origin in its source area (voxels)
    glm::dvec3 areaAbs{0.0};                 // the source area's origin, world metres
};
/// The pose of a simulated body: its sim positions are metres from the area origin (`areaOriginM`, world metres).
BodyWorldPose worldPoseOf(const SimBody& b, glm::dvec3 areaOriginM);
BodyPose relativePose(const BodyWorldPose& w, glm::dvec3 camera);
inline BodyPose poseOf(const SimBody& b, glm::dvec3 areaOriginM, glm::dvec3 camera) {
    return relativePose(worldPoseOf(b, areaOriginM), camera);
}

/// What the renderer draws (RenderCoordinator::setVoxFoundBodies): the faces (built once; facesRevision bumps when the
/// set of bodies changes) and one world pose per body (rewritten every tick). The renderer turns the poses
/// camera-relative once per frame, so one buffer serves the main pass and the shadow casters.
struct BodyDrawList {
    std::vector<BodyFace> faces;
    uint64_t facesRevision = 0;
    std::vector<BodyWorldPose> poses;
    bool shown = true;
    bool castShadows = true;
    bool mutate = false;                     // F5.3 gate mutation: a 0.5 degree turn on every body (main pass only)
    // F5.5b per-body cull (a cost knob: changes what is drawn, never how it looks). Per body, set with the faces:
    // its face range and its grid box half-size (metres). Without them (sizes differ from poses) nothing is culled.
    std::vector<uint32_t> first, count;
    std::vector<glm::vec3> halfM;
    bool cull = true;
};

/// F5.5b: body grid box half-size in metres (dim * voxelM / 2); the box spans [0, 2 halfM] in body-grid metres.
glm::vec3 bodyHalfM(const SimBody& b);
/// The body's grid box at `p` as an axis-aligned box in p's (camera-relative) frame -- conservative: contains every
/// face corner bodyCornerRel can produce.
void bodyBoxRel(const BodyPose& p, glm::vec3 halfM, glm::vec3* lo, glm::vec3* hi);
/// Kept = that box intersects the clip volume whose planes are `planes` (Mesher.h clipPlanesZeroToOne, same frame).
bool bodyKept(const BodyPose& p, glm::vec3 halfM, const glm::vec4 planes[6]);

/// CPU mirrors of voxfound_body.vert, in float, in the shader's order of operations.
glm::ivec3 faceCornerVoxel(PackedFace f, uint32_t corner);          // corner 0-3, body-grid voxels (integers)
glm::vec3 bodyCornerRel(const BodyPose& p, PackedFace f, uint32_t corner);
glm::vec3 bodyNormal(const BodyPose& p, PackedFace f);
glm::vec3 rotateByQuat(glm::vec4 q, glm::vec3 v);

}  // namespace Phyxel::Core::VoxFound
