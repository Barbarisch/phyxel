#pragma once

#include "scene/motion/MotionSource.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Phyxel::Scene::Motion {

enum class MotionBricksDevice : std::uint32_t {
    Auto = 0,
    Cpu = 1,
    Vulkan = 2
};

struct MotionBricksConfig {
    std::string libraryPath;
    std::string modelDirectory;
    std::string stylePath;
    MotionBricksDevice device = MotionBricksDevice::Cpu;
    std::uint32_t threads = 0;
};

class MotionBricksSource;

/// Shared immutable native model/style owner. Keeping this separate from an
/// agent prevents N characters from loading N copies of the 0.73 GB model.
class MotionBricksRuntime : public std::enable_shared_from_this<MotionBricksRuntime> {
public:
    ~MotionBricksRuntime();

    static std::shared_ptr<MotionBricksRuntime> create(
        const MotionBricksConfig& config, std::string& error);

    /// Load the DLL, validate every required ABI-v1 symbol, then unload it.
    /// Does not require model assets and is used by installers/diagnostics.
    static bool probeLibrary(const std::string& libraryPath, std::string& error);

    std::shared_ptr<MotionBricksSource> createSource(std::string& error);
    const std::vector<std::string>& jointNames() const;
    std::uint32_t abiVersion() const;
    const MotionBricksConfig& config() const;

private:
    struct Impl;
    explicit MotionBricksRuntime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
    friend class MotionBricksSource;
};

/// Stateful per-character agent. Planning happens on a private worker and the
/// game thread only samples an immutable copied window.
class MotionBricksSource final : public IMotionSource {
public:
    ~MotionBricksSource() override;

    void submitIntent(const MotionIntent& intent) override;
    bool sample(double timeSeconds, LocalPoseFrame& output) override;
    void reset() override;
    MotionSourceStatus status() const override;

private:
    struct Impl;
    MotionBricksSource(std::shared_ptr<MotionBricksRuntime> runtime,
                       std::unique_ptr<Impl> impl);
    std::shared_ptr<MotionBricksRuntime> m_runtime;
    std::unique_ptr<Impl> m_impl;
    friend class MotionBricksRuntime;
};

} // namespace Phyxel::Scene::Motion
