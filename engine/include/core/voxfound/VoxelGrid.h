#pragma once
// docs/VoxelFoundationPlan.md — a plain voxel grid for the foundation labs (F0 skeleton).
//
// Headless, no engine dependencies beyond glm. Used by the fracture lab (§6, §8) as its world and by
// the storage lab (§3, §8) as the DENSE REFERENCE every new store must equal voxel for voxel.
//
// Layout follows the engine's convention (CLAUDE.md "Coordinate System"): Z-minor,
// index = z + dim.z * (y + dim.y * x).
//
// Channels (§3.1): material (0 = air), part id (0 = none; which brick / plank / beam), fixed (1 = held
// by the world: ground, a frame; anything bonded to a fixed voxel is anchored, §6.5).

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Phyxel::Core::VoxFound {

struct VoxelGrid {
    glm::ivec3 dim{0};
    std::vector<uint8_t> mat;
    std::vector<uint16_t> part;
    std::vector<uint8_t> fixed;

    void resize(glm::ivec3 d) {
        dim = d;
        const size_t n = size();
        mat.assign(n, 0);
        part.assign(n, 0);
        fixed.assign(n, 0);
    }
    size_t size() const { return size_t(dim.x) * size_t(dim.y) * size_t(dim.z); }
    bool in(glm::ivec3 p) const {
        return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < dim.x && p.y < dim.y && p.z < dim.z;
    }
    size_t idx(glm::ivec3 p) const {
        return size_t(p.z) + size_t(dim.z) * (size_t(p.y) + size_t(dim.y) * size_t(p.x));
    }
    glm::ivec3 pos(size_t i) const {
        const int z = int(i % size_t(dim.z));
        const size_t r = i / size_t(dim.z);
        return {int(r / size_t(dim.y)), int(r % size_t(dim.y)), z};
    }
    void set(glm::ivec3 p, uint8_t m, uint16_t partId = 0, bool isFixed = false) {
        const size_t i = idx(p);
        mat[i] = m;
        part[i] = partId;
        fixed[i] = isFixed ? 1 : 0;
    }
    uint8_t at(glm::ivec3 p) const { return in(p) ? mat[idx(p)] : 0; }
    size_t count(uint8_t m) const {
        size_t n = 0;
        for (uint8_t v : mat) n += (v == m);
        return n;
    }
    size_t solidCount() const {
        size_t n = 0;
        for (uint8_t v : mat) n += (v != 0);
        return n;
    }
};

}  // namespace Phyxel::Core::VoxFound
