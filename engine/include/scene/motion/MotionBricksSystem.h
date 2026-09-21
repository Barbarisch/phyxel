#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Phyxel::Scene { class AnimatedVoxelCharacter; }

namespace Phyxel::Scene::Motion {

class MotionBricksRuntime;
class MotionBricksSource;

/// Process-wide opt-in runtime and a bounded active-agent gate. Environment
/// configuration keeps model downloads and shipping defaults strictly off.
class MotionBricksSystem {
public:
    static MotionBricksSystem& instance();

    /// Returns false without error when PHYXEL_MOTION_PROVIDER is not
    /// "motionbricks". Otherwise lazily loads the pinned ABI runtime and
    /// attaches a per-character source, or reports a degradable error.
    bool tryAttachFromEnvironment(AnimatedVoxelCharacter& character,
                                  std::string& error);
    std::size_t activeAgents() const;
    void shutdown();

private:
    MotionBricksSystem() = default;
    mutable std::mutex m_mutex;
    bool m_attempted = false;
    std::string m_error;
    std::size_t m_maxAgents = 1;
    std::shared_ptr<MotionBricksRuntime> m_runtime;
    std::vector<std::weak_ptr<MotionBricksSource>> m_sources;
};

} // namespace Phyxel::Scene::Motion
