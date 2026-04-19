#pragma once

#include <bitset>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Physics {

// A world-space box returned from occupancy grid queries.
struct OccupiedBox {
    glm::vec3 center;
    glm::vec3 halfExtents;
};

// Per-chunk terrain collision data.
// Maintains three-level voxel hierarchy (cube / subcube / microcube) as compact
// bit structures. Dynamic rigid bodies query this instead of btCompoundShape.
//
// Coordinate convention: localPos is always (0-31, 0-31, 0-31) within the chunk.
// subPos is (0-2, 0-2, 0-2) within a parent cube.
// microPos is (0-2, 0-2, 0-2) within a parent subcube.
class VoxelOccupancyGrid {
public:
    static constexpr int   CHUNK_SIZE      = 32;
    static constexpr float CUBE_HALF       = 0.5f;
    static constexpr float SUBCUBE_HALF    = 1.0f / 6.0f;
    static constexpr float MICROCUBE_HALF  = 1.0f / 18.0f;
    static constexpr float SUBCUBE_SCALE   = 1.0f / 3.0f;
    static constexpr float MICROCUBE_SCALE = 1.0f / 9.0f;

    VoxelOccupancyGrid() = default;

    void setChunkOrigin(const glm::ivec3& origin) { m_origin = origin; }
    const glm::ivec3& getChunkOrigin() const { return m_origin; }

    // ---- Cube level ----
    void setCube(const glm::ivec3& localPos, bool filled);
    bool isCubeFilled(const glm::ivec3& localPos) const;

    // Mark a cube position as subdivided (individual subcube bits take over).
    // When subdivided, this position is NOT treated as a solid cube during queries.
    void markSubdivided(const glm::ivec3& localPos, bool subdivided);
    bool isSubdivided(const glm::ivec3& localPos) const;

    // ---- Subcube level ----
    void setSubcube(const glm::ivec3& localPos, const glm::ivec3& subPos, bool filled);
    bool isSubcubeFilled(const glm::ivec3& localPos, const glm::ivec3& subPos) const;

    void markSubcubeSubdivided(const glm::ivec3& localPos, const glm::ivec3& subPos, bool subdivided);
    bool isSubcubeSubdivided(const glm::ivec3& localPos, const glm::ivec3& subPos) const;

    // ---- Microcube level ----
    void setMicrocube(const glm::ivec3& localPos, const glm::ivec3& subPos,
                      const glm::ivec3& microPos, bool filled);
    bool isMicrocubeFilled(const glm::ivec3& localPos, const glm::ivec3& subPos,
                           const glm::ivec3& microPos) const;

    // ---- Spatial query ----
    // Appends all occupied boxes whose world-space AABBs overlap [aabbMin, aabbMax].
    // Does NOT clear results — caller may pre-populate or aggregate across chunks.
    void queryAABB(const glm::vec3& aabbMin, const glm::vec3& aabbMax,
                   std::vector<OccupiedBox>& results) const;

    void clear();

    // Broad chunk-level AABB for fast pre-rejection
    glm::vec3 chunkWorldMin() const { return glm::vec3(m_origin); }
    glm::vec3 chunkWorldMax() const { return glm::vec3(m_origin) + glm::vec3(CHUNK_SIZE); }

private:
    glm::ivec3 m_origin{0};

    // Dense cube-level bitsets: 32³ = 32768 bits = 4 KB each
    std::bitset<32*32*32> m_cubes;
    std::bitset<32*32*32> m_cubeSubdiv;

    // Sparse subcube maps: key = cube linear index (fits in uint16_t)
    // Value: 27-bit mask (one bit per subcube position, encoded as subIdx)
    std::unordered_map<uint16_t, uint32_t> m_subcubeFilled;
    std::unordered_map<uint16_t, uint32_t> m_subcubeSubdiv;

    // Sparse microcube map: key = (cubeIdx << 5) | subIdx, value = 27-bit mask
    std::unordered_map<uint32_t, uint32_t> m_microcubeFilled;

    // Index helpers
    static int cubeIdx(const glm::ivec3& p) {
        return p.z + p.y * 32 + p.x * 1024;
    }
    static int subIdx(const glm::ivec3& p) {
        return p.z + p.y * 3 + p.x * 9;
    }
    static bool validLocal(const glm::ivec3& p) {
        return p.x >= 0 && p.x < 32 && p.y >= 0 && p.y < 32 && p.z >= 0 && p.z < 32;
    }
    static bool validSub(const glm::ivec3& p) {
        return p.x >= 0 && p.x < 3 && p.y >= 0 && p.y < 3 && p.z >= 0 && p.z < 3;
    }

    glm::vec3 cubeWorldCenter(const glm::ivec3& lp) const;
    glm::vec3 subcubeWorldCenter(const glm::ivec3& lp, const glm::ivec3& sp) const;
    glm::vec3 microcubeWorldCenter(const glm::ivec3& lp, const glm::ivec3& sp,
                                   const glm::ivec3& mp) const;
};

} // namespace Physics
} // namespace Phyxel
