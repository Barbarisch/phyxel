#include "physics/VoxelOccupancyGrid.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Physics {

// ---- World-center helpers ----

glm::vec3 VoxelOccupancyGrid::cubeWorldCenter(const glm::ivec3& lp) const {
    return glm::vec3(m_origin) + glm::vec3(lp) + glm::vec3(0.5f);
}

glm::vec3 VoxelOccupancyGrid::subcubeWorldCenter(const glm::ivec3& lp,
                                                   const glm::ivec3& sp) const {
    glm::vec3 parent = cubeWorldCenter(lp);
    // sp is 0-2; offset from cube center: (sp - 1) * (1/3)
    glm::vec3 off = (glm::vec3(sp) - glm::vec3(1.0f)) * SUBCUBE_SCALE;
    return parent + off;
}

glm::vec3 VoxelOccupancyGrid::microcubeWorldCenter(const glm::ivec3& lp,
                                                     const glm::ivec3& sp,
                                                     const glm::ivec3& mp) const {
    glm::vec3 subCenter = subcubeWorldCenter(lp, sp);
    glm::vec3 off = (glm::vec3(mp) - glm::vec3(1.0f)) * MICROCUBE_SCALE;
    return subCenter + off;
}

// ---- Cube level ----

void VoxelOccupancyGrid::setCube(const glm::ivec3& lp, bool filled) {
    if (!validLocal(lp)) return;
    int idx = cubeIdx(lp);
    filled ? m_cubes.set(idx) : m_cubes.reset(idx);
}

bool VoxelOccupancyGrid::isCubeFilled(const glm::ivec3& lp) const {
    if (!validLocal(lp)) return false;
    return m_cubes.test(cubeIdx(lp));
}

void VoxelOccupancyGrid::markSubdivided(const glm::ivec3& lp, bool subdivided) {
    if (!validLocal(lp)) return;
    int idx = cubeIdx(lp);
    subdivided ? m_cubeSubdiv.set(idx) : m_cubeSubdiv.reset(idx);
    if (!subdivided) {
        // Remove any stored subcube data for this position
        m_subcubeFilled.erase(static_cast<uint16_t>(idx));
        m_subcubeSubdiv.erase(static_cast<uint16_t>(idx));
    }
}

bool VoxelOccupancyGrid::isSubdivided(const glm::ivec3& lp) const {
    if (!validLocal(lp)) return false;
    return m_cubeSubdiv.test(cubeIdx(lp));
}

// ---- Subcube level ----

void VoxelOccupancyGrid::setSubcube(const glm::ivec3& lp, const glm::ivec3& sp, bool filled) {
    if (!validLocal(lp) || !validSub(sp)) return;
    auto key = static_cast<uint16_t>(cubeIdx(lp));
    int  bit = subIdx(sp);
    if (filled)
        m_subcubeFilled[key] |= (1u << bit);
    else
        m_subcubeFilled[key] &= ~(1u << bit);
}

bool VoxelOccupancyGrid::isSubcubeFilled(const glm::ivec3& lp, const glm::ivec3& sp) const {
    if (!validLocal(lp) || !validSub(sp)) return false;
    auto key = static_cast<uint16_t>(cubeIdx(lp));
    auto it  = m_subcubeFilled.find(key);
    if (it == m_subcubeFilled.end()) return false;
    return (it->second >> subIdx(sp)) & 1u;
}

void VoxelOccupancyGrid::markSubcubeSubdivided(const glm::ivec3& lp, const glm::ivec3& sp,
                                                bool subdivided) {
    if (!validLocal(lp) || !validSub(sp)) return;
    auto key = static_cast<uint16_t>(cubeIdx(lp));
    int  bit = subIdx(sp);
    if (subdivided)
        m_subcubeSubdiv[key] |= (1u << bit);
    else
        m_subcubeSubdiv[key] &= ~(1u << bit);
}

bool VoxelOccupancyGrid::isSubcubeSubdivided(const glm::ivec3& lp, const glm::ivec3& sp) const {
    if (!validLocal(lp) || !validSub(sp)) return false;
    auto key = static_cast<uint16_t>(cubeIdx(lp));
    auto it  = m_subcubeSubdiv.find(key);
    if (it == m_subcubeSubdiv.end()) return false;
    return (it->second >> subIdx(sp)) & 1u;
}

// ---- Microcube level ----

void VoxelOccupancyGrid::setMicrocube(const glm::ivec3& lp, const glm::ivec3& sp,
                                       const glm::ivec3& mp, bool filled) {
    if (!validLocal(lp) || !validSub(sp) || !validSub(mp)) return;
    uint32_t key = (static_cast<uint32_t>(cubeIdx(lp)) << 5) | static_cast<uint32_t>(subIdx(sp));
    int      bit = subIdx(mp);
    if (filled)
        m_microcubeFilled[key] |= (1u << bit);
    else
        m_microcubeFilled[key] &= ~(1u << bit);
}

bool VoxelOccupancyGrid::isMicrocubeFilled(const glm::ivec3& lp, const glm::ivec3& sp,
                                            const glm::ivec3& mp) const {
    if (!validLocal(lp) || !validSub(sp) || !validSub(mp)) return false;
    uint32_t key = (static_cast<uint32_t>(cubeIdx(lp)) << 5) | static_cast<uint32_t>(subIdx(sp));
    auto it = m_microcubeFilled.find(key);
    if (it == m_microcubeFilled.end()) return false;
    return (it->second >> subIdx(mp)) & 1u;
}

// ---- Spatial query ----

void VoxelOccupancyGrid::queryAABB(const glm::vec3& aabbMin, const glm::vec3& aabbMax,
                                    std::vector<OccupiedBox>& results) const {
    // Broad chunk-level rejection
    if (aabbMax.x < m_origin.x || aabbMin.x > m_origin.x + CHUNK_SIZE ||
        aabbMax.y < m_origin.y || aabbMin.y > m_origin.y + CHUNK_SIZE ||
        aabbMax.z < m_origin.z || aabbMin.z > m_origin.z + CHUNK_SIZE) {
        return;
    }

    // Compute the range of cube grid cells the AABB overlaps
    auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };

    int x0 = clamp(static_cast<int>(std::floor(aabbMin.x - m_origin.x)),     0, CHUNK_SIZE - 1);
    int y0 = clamp(static_cast<int>(std::floor(aabbMin.y - m_origin.y)),     0, CHUNK_SIZE - 1);
    int z0 = clamp(static_cast<int>(std::floor(aabbMin.z - m_origin.z)),     0, CHUNK_SIZE - 1);
    int x1 = clamp(static_cast<int>(std::floor(aabbMax.x - m_origin.x)),     0, CHUNK_SIZE - 1);
    int y1 = clamp(static_cast<int>(std::floor(aabbMax.y - m_origin.y)),     0, CHUNK_SIZE - 1);
    int z1 = clamp(static_cast<int>(std::floor(aabbMax.z - m_origin.z)),     0, CHUNK_SIZE - 1);

    const glm::vec3 cubeHE{CUBE_HALF, CUBE_HALF, CUBE_HALF};
    const glm::vec3 subHE{SUBCUBE_HALF, SUBCUBE_HALF, SUBCUBE_HALF};
    const glm::vec3 microHE{MICROCUBE_HALF, MICROCUBE_HALF, MICROCUBE_HALF};

    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                glm::ivec3 lp{x, y, z};
                int cidx = cubeIdx(lp);

                if (!m_cubes.test(cidx)) continue;

                if (!m_cubeSubdiv.test(cidx)) {
                    // Solid cube
                    results.push_back({cubeWorldCenter(lp), cubeHE});
                    continue;
                }

                // Subdivided — iterate subcubes
                auto sit = m_subcubeFilled.find(static_cast<uint16_t>(cidx));
                if (sit == m_subcubeFilled.end()) continue;
                uint32_t subMask = sit->second;

                auto sdivIt = m_subcubeSubdiv.find(static_cast<uint16_t>(cidx));
                uint32_t subdiv = sdivIt != m_subcubeSubdiv.end() ? sdivIt->second : 0u;

                for (int sx = 0; sx < 3; ++sx) {
                    for (int sy = 0; sy < 3; ++sy) {
                        for (int sz = 0; sz < 3; ++sz) {
                            glm::ivec3 sp{sx, sy, sz};
                            int sbit = subIdx(sp);

                            if (!((subMask >> sbit) & 1u)) continue;

                            if (!((subdiv >> sbit) & 1u)) {
                                // Solid subcube
                                glm::vec3 center = subcubeWorldCenter(lp, sp);
                                // Quick AABB overlap test before adding
                                if (center.x + SUBCUBE_HALF > aabbMin.x && center.x - SUBCUBE_HALF < aabbMax.x &&
                                    center.y + SUBCUBE_HALF > aabbMin.y && center.y - SUBCUBE_HALF < aabbMax.y &&
                                    center.z + SUBCUBE_HALF > aabbMin.z && center.z - SUBCUBE_HALF < aabbMax.z) {
                                    results.push_back({center, subHE});
                                }
                                continue;
                            }

                            // Subdivided subcube — iterate microcubes
                            uint32_t mkey = (static_cast<uint32_t>(cidx) << 5) | static_cast<uint32_t>(sbit);
                            auto mit = m_microcubeFilled.find(mkey);
                            if (mit == m_microcubeFilled.end()) continue;
                            uint32_t microMask = mit->second;

                            for (int mx = 0; mx < 3; ++mx) {
                                for (int my = 0; my < 3; ++my) {
                                    for (int mz = 0; mz < 3; ++mz) {
                                        glm::ivec3 mp{mx, my, mz};
                                        int mbit = subIdx(mp);
                                        if (!((microMask >> mbit) & 1u)) continue;

                                        glm::vec3 center = microcubeWorldCenter(lp, sp, mp);
                                        if (center.x + MICROCUBE_HALF > aabbMin.x && center.x - MICROCUBE_HALF < aabbMax.x &&
                                            center.y + MICROCUBE_HALF > aabbMin.y && center.y - MICROCUBE_HALF < aabbMax.y &&
                                            center.z + MICROCUBE_HALF > aabbMin.z && center.z - MICROCUBE_HALF < aabbMax.z) {
                                            results.push_back({center, microHE});
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void VoxelOccupancyGrid::clear() {
    m_cubes.reset();
    m_cubeSubdiv.reset();
    m_subcubeFilled.clear();
    m_subcubeSubdiv.clear();
    m_microcubeFilled.clear();
}

} // namespace Physics
} // namespace Phyxel
