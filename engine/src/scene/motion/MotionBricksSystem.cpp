#include "scene/motion/MotionBricksSystem.h"

#include "scene/AnimatedVoxelCharacter.h"
#include "scene/motion/HumanoidRetargeter.h"
#include "scene/motion/MotionBricksSource.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace Phyxel::Scene::Motion {

namespace {
const char* environment(const char* key) {
    const char* value = std::getenv(key);
    return value && value[0] ? value : nullptr;
}
}

MotionBricksSystem& MotionBricksSystem::instance() {
    static MotionBricksSystem system;
    return system;
}

bool MotionBricksSystem::tryAttachFromEnvironment(
    AnimatedVoxelCharacter& character, std::string& error) {
    error.clear();
    const char* provider = environment("PHYXEL_MOTION_PROVIDER");
    if (!provider || std::string(provider) != "motionbricks") return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_attempted) {
        m_attempted = true;
        const char* library = environment("PHYXEL_MOTIONBRICKS_LIBRARY");
        const char* assets = environment("PHYXEL_MOTIONBRICKS_ASSETS");
        if (!library || !assets) {
            m_error = "PHYXEL_MOTIONBRICKS_LIBRARY and PHYXEL_MOTIONBRICKS_ASSETS are required";
        } else {
            MotionBricksConfig config;
            config.libraryPath = library;
            const std::filesystem::path assetRoot(assets);
            config.modelDirectory = (assetRoot / "g1-f32").string();
            const char* style = environment("PHYXEL_MOTIONBRICKS_STYLE");
            config.stylePath = (assetRoot / "styles" /
                (style ? std::string(style) : std::string("walk.mbstyle"))).string();
            const char* backend = environment("PHYXEL_MOTIONBRICKS_DEVICE");
            config.device = backend && std::string(backend) == "vulkan"
                ? MotionBricksDevice::Vulkan : MotionBricksDevice::Cpu;
            if (const char* limit = environment("PHYXEL_MOTIONBRICKS_MAX_AGENTS")) {
                try { m_maxAgents = std::max<std::size_t>(1, std::stoul(limit)); }
                catch (...) { m_error = "invalid PHYXEL_MOTIONBRICKS_MAX_AGENTS"; }
            }
            if (m_error.empty()) m_runtime = MotionBricksRuntime::create(config, m_error);
        }
    }
    if (!m_runtime) { error = m_error; return false; }

    m_sources.erase(std::remove_if(m_sources.begin(), m_sources.end(),
        [](const auto& source) { return source.expired(); }), m_sources.end());
    if (m_sources.size() >= m_maxAgents) {
        error = "MotionBricks active-agent budget exhausted; using clips";
        return false;
    }
    auto source = m_runtime->createSource(error);
    if (!source) return false;
    if (!character.setMotionSource(source, g1ToMixamoRetargetMap())) {
        error = "character rig is incompatible with the G1 humanoid retarget map";
        return false;
    }
    m_sources.emplace_back(source);
    return true;
}

std::size_t MotionBricksSystem::activeAgents() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<std::size_t>(std::count_if(m_sources.begin(), m_sources.end(),
        [](const auto& source) { return !source.expired(); }));
}

void MotionBricksSystem::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sources.clear();
    m_runtime.reset();
    m_error.clear();
    m_attempted = false;
}

} // namespace Phyxel::Scene::Motion
