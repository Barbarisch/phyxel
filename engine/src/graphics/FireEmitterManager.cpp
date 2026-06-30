#include "graphics/FireEmitterManager.h"
#include "core/VfxSystem.h"

#include <cmath>

namespace Phyxel {
namespace Graphics {

uint64_t FireEmitterManager::voxelKey(const glm::vec3& p) {
    // Quantise to the microcube grid (1/9) so a given voxel maps to a stable key
    // frame to frame. 21 bits/axis covers +/-~116k microcubes (~13k world units).
    auto q = [](float v) -> uint64_t {
        int64_t i = static_cast<int64_t>(std::llround(v * 9.0));
        return static_cast<uint64_t>(i) & 0x1FFFFFull;
    };
    return (q(p.x) << 42) | (q(p.y) << 21) | q(p.z);
}

uint64_t FireEmitterManager::columnKey(const glm::vec3& p) {
    // Horizontal cell at ~1/3-unit resolution: groups nearby embers so only the
    // highest one in each spot becomes a flame tongue (a few peaks, not a wall).
    auto q = [](float v) -> uint64_t {
        int64_t i = static_cast<int64_t>(std::llround(v * 3.0));
        return static_cast<uint64_t>(i) & 0xFFFFFFFFull;
    };
    return (q(p.x) << 32) | q(p.z);
}

void FireEmitterManager::sync(const std::vector<glm::vec3>& flamingPositions) {
    if (!m_vfx) return;

    // 1) Reduce to peak voxels: highest flaming voxel per horizontal cell.
    std::unordered_map<uint64_t, glm::vec3> peaks;
    for (const auto& p : flamingPositions) {
        uint64_t ck = columnKey(p);
        auto it = peaks.find(ck);
        if (it == peaks.end() || p.y > it->second.y) peaks[ck] = p;
    }

    // 2) Index the desired tongues by stable voxel key.
    std::unordered_map<uint64_t, glm::vec3> desired;
    for (const auto& kv : peaks) desired[voxelKey(kv.second)] = kv.second;

    // 3) Dismiss tongues whose ember is gone.
    for (auto it = m_tongues.begin(); it != m_tongues.end();) {
        if (desired.find(it->first) == desired.end()) {
            m_vfx->dismiss(it->second);
            it = m_tongues.erase(it);
        } else {
            ++it;
        }
    }

    // 4) Spawn a small tapering tongue for each new ember peak.
    for (const auto& kv : desired) {
        if (m_tongues.find(kv.first) != m_tongues.end()) continue;

        // Aim at the voxel's top-centre. getWorldPosition() is the voxel's min
        // corner; a microcube edge is 1/9, so +~1/18 centres X/Z and +1/9 sits
        // the emitter at the top face where flame should lick up.
        glm::vec3 center = kv.second + glm::vec3(0.055f, 0.11f, 0.055f);

        VfxFieldParams p;
        p.shape           = VfxShape::Fountain;
        p.untilDismissed  = true;
        p.radius          = 0.05f;   // thin column -> a tongue, not a cloud
        p.thickness       = 0.07f;   // small particle cubes
        p.density         = 2.0f;    // few particles/frame -> no additive blow-out
        p.intensity       = 0.5f;    // dim enough to stay orange, not white
        p.particleLifetime= 0.34f;   // short -> fades before piling at the apex
        p.pulseSpeed      = 10.0f;   // lively flicker
        p.color           = glm::vec3(1.0f, 0.5f, 0.13f); // warm flame orange
        p.light           = true;
        p.lightRadius     = 5.0f;
        p.lightIntensity  = 1.4f;

        std::string id = m_vfx->spawnField(center, p);
        if (!id.empty()) m_tongues[kv.first] = id;
    }
}

void FireEmitterManager::clear() {
    if (m_vfx) {
        for (auto& kv : m_tongues) m_vfx->dismiss(kv.second);
    }
    m_tongues.clear();
}

} // namespace Graphics
} // namespace Phyxel
