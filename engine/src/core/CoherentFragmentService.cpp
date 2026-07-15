#include "core/CoherentFragmentService.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

std::vector<FragmentBox> CoherentFragmentService::mergeVoxelsToBoxes(
    const std::vector<KinematicVoxel>& voxels,
    const std::function<float(const KinematicVoxel&)>& voxelMass)
{
    if (voxels.empty()) return {};

    // Cell size = the finest voxel present, so every voxel maps to a whole number
    // of cells (cube over a subcube grid spans 3 cells/axis, etc.).
    float minScale = 1.0f;
    for (const auto& v : voxels) minScale = std::min(minScale, v.scale.x);
    const float cellSize = minScale;
    const float invCell  = 1.0f / cellSize;

    glm::vec3 boundsMin(1e9f), boundsMax(-1e9f);
    for (const auto& v : voxels) {
        boundsMin = glm::min(boundsMin, v.localPos - v.scale * 0.5f);
        boundsMax = glm::max(boundsMax, v.localPos + v.scale * 0.5f);
    }

    glm::ivec3 gridSize = glm::ivec3(glm::ceil((boundsMax - boundsMin) * invCell)) + glm::ivec3(1);

    // Guard: an object whose bounds span too many fine cells would allocate a huge
    // grid. Fall back to one box per voxel (no merging). Mass model is unchanged
    // (the caller-supplied per-voxel mass).
    constexpr int MAX_GRID_DIM = 64;
    if (gridSize.x > MAX_GRID_DIM || gridSize.y > MAX_GRID_DIM || gridSize.z > MAX_GRID_DIM) {
        std::vector<FragmentBox> result;
        result.reserve(voxels.size());
        for (const auto& v : voxels) {
            result.push_back({v.localPos, v.scale * 0.5f, voxelMass(v)});
        }
        return result;
    }

    const int gx = gridSize.x, gy = gridSize.y, gz = gridSize.z;
    std::vector<uint8_t> grid(static_cast<size_t>(gx) * gy * gz, 0);
    std::vector<float>   cellMass(static_cast<size_t>(gx) * gy * gz, 0.0f);

    auto gridIdx = [&](int x, int y, int z) -> int {
        return z + y * gz + x * (gy * gz);
    };

    // Rasterize each voxel into the cells it covers; spread its total mass evenly
    // across those cells so a later merged box re-sums to conserve mass.
    for (const auto& v : voxels) {
        glm::vec3 vmin = v.localPos - v.scale * 0.5f;
        glm::vec3 vmax = v.localPos + v.scale * 0.5f;
        glm::ivec3 gmin = glm::ivec3(glm::floor((vmin - boundsMin) * invCell));
        glm::ivec3 gmax = glm::ivec3(glm::floor((vmax - boundsMin) * invCell - glm::vec3(0.001f)));
        gmin = glm::clamp(gmin, glm::ivec3(0), gridSize - glm::ivec3(1));
        gmax = glm::clamp(gmax, glm::ivec3(0), gridSize - glm::ivec3(1));

        int cellCount = (gmax.x - gmin.x + 1) * (gmax.y - gmin.y + 1) * (gmax.z - gmin.z + 1);
        float massPerCell = (cellCount > 0) ? voxelMass(v) / static_cast<float>(cellCount) : 0.0f;

        for (int x = gmin.x; x <= gmax.x; ++x)
            for (int y = gmin.y; y <= gmax.y; ++y)
                for (int z = gmin.z; z <= gmax.z; ++z) {
                    int idx = gridIdx(x, y, z);
                    grid[idx] = 1;
                    cellMass[idx] += massPerCell;
                }
    }

    // Greedy merge: grow a box in +z, then +y, then +x while all cells are filled.
    std::vector<FragmentBox> result;
    for (int x = 0; x < gx; ++x) {
        for (int y = 0; y < gy; ++y) {
            for (int z = 0; z < gz; ++z) {
                if (grid[gridIdx(x, y, z)] != 1) continue;

                int zEnd = z;
                while (zEnd + 1 < gz && grid[gridIdx(x, y, zEnd + 1)] == 1) ++zEnd;

                int yEnd = y;
                bool canExpandY = true;
                while (canExpandY && yEnd + 1 < gy) {
                    for (int zz = z; zz <= zEnd; ++zz) {
                        if (grid[gridIdx(x, yEnd + 1, zz)] != 1) { canExpandY = false; break; }
                    }
                    if (canExpandY) ++yEnd;
                }

                int xEnd = x;
                bool canExpandX = true;
                while (canExpandX && xEnd + 1 < gx) {
                    for (int yy = y; yy <= yEnd && canExpandX; ++yy) {
                        for (int zz = z; zz <= zEnd; ++zz) {
                            if (grid[gridIdx(xEnd + 1, yy, zz)] != 1) { canExpandX = false; break; }
                        }
                    }
                    if (canExpandX) ++xEnd;
                }

                float boxMass = 0.0f;
                for (int xx = x; xx <= xEnd; ++xx)
                    for (int yy = y; yy <= yEnd; ++yy)
                        for (int zz = z; zz <= zEnd; ++zz) {
                            int idx = gridIdx(xx, yy, zz);
                            grid[idx] = 2;   // consumed
                            boxMass += cellMass[idx];
                        }

                glm::vec3 boxMin = boundsMin + glm::vec3(x, y, z) * cellSize;
                glm::vec3 boxMax = boundsMin + glm::vec3(xEnd + 1, yEnd + 1, zEnd + 1) * cellSize;
                result.push_back({ (boxMin + boxMax) * 0.5f, (boxMax - boxMin) * 0.5f, boxMass });
            }
        }
    }

    return result;
}

PhysicalizedFragment CoherentFragmentService::physicalize(
    Physics::VoxelDynamicsWorld* voxelWorld,
    KinematicVoxelManager* kinematic,
    const std::string& idHint,
    std::vector<KinematicVoxel> voxels,
    const glm::mat4& objectTransform,
    const glm::vec3& initialLinVel,
    const glm::vec3& initialAngVel,
    const std::function<float(const KinematicVoxel&)>& voxelMass,
    const std::function<float(float)>& finalizeTotalMass,
    float restitution, float friction, float linearDamp, float angularDamp)
{
    // Single-list form (furniture): the same voxels drive collision AND render.
    const std::vector<KinematicVoxel> collision = voxels;   // copy; render list is moved below
    return physicalize(voxelWorld, kinematic, idHint, std::move(voxels), collision,
                       objectTransform, initialLinVel, initialAngVel,
                       voxelMass, finalizeTotalMass,
                       restitution, friction, linearDamp, angularDamp);
}

PhysicalizedFragment CoherentFragmentService::physicalize(
    Physics::VoxelDynamicsWorld* voxelWorld,
    KinematicVoxelManager* kinematic,
    const std::string& idHint,
    std::vector<KinematicVoxel> renderVoxels,
    const std::vector<KinematicVoxel>& collisionVoxels,
    const glm::mat4& objectTransform,
    const glm::vec3& initialLinVel,
    const glm::vec3& initialAngVel,
    const std::function<float(const KinematicVoxel&)>& voxelMass,
    const std::function<float(float)>& finalizeTotalMass,
    float restitution, float friction, float linearDamp, float angularDamp)
{
    PhysicalizedFragment out;
    if (!voxelWorld || renderVoxels.empty() || collisionVoxels.empty()) return out;
    std::vector<KinematicVoxel> voxels = std::move(renderVoxels);   // render list

    auto boxes = mergeVoxelsToBoxes(collisionVoxels, voxelMass);
    if (boxes.empty()) return out;

    // Mass-weighted center of mass of the merged boxes.
    float rawMass = 0.0f;
    glm::vec3 com(0.0f);
    for (const auto& b : boxes) { rawMass += b.mass; com += b.center * b.mass; }
    if (rawMass > 0.0f) com /= rawMass;

    // Optional total-mass remap (furniture clamp; world identity), renormalized per box.
    float finalMass = finalizeTotalMass ? finalizeTotalMass(rawMass) : rawMass;
    float massScale = (rawMass > 1e-6f) ? (finalMass / rawMass) : 1.0f;

    std::vector<Physics::LocalBox> localBoxes;
    localBoxes.reserve(boxes.size());
    for (const auto& b : boxes) {
        Physics::LocalBox lb;
        lb.offset      = b.center - com;   // body frame is centered on the COM
        lb.halfExtents = b.halfExtents;
        lb.mass        = b.mass * massScale;
        localBoxes.push_back(lb);
    }

    // The body sits at the COM in world space; orientation comes from the object.
    glm::mat4 transform = glm::translate(objectTransform, com);
    glm::vec3 worldPos  = glm::vec3(transform[3]);
    glm::quat orient    = glm::quat_cast(objectTransform);

    Physics::VoxelRigidBody* body = voxelWorld->createBody(
        localBoxes, worldPos, orient, restitution, friction, linearDamp, angularDamp);
    if (!body) return out;

    body->linearVelocity  = initialLinVel;
    body->angularVelocity = initialAngVel;
    body->wake();

    // Re-center the render voxels on the COM so their shared transform aligns.
    for (auto& v : voxels) v.localPos -= com;

    std::string kinId;
    if (kinematic) {
        kinId = kinematic->add(idHint, std::move(voxels), transform, idHint, true);
    }

    out.body         = body;
    out.kineticObjId = kinId;
    out.transform    = transform;
    return out;
}

} // namespace Core
} // namespace Phyxel
