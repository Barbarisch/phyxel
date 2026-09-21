#pragma once

#include "scene/motion/MotionTypes.h"

namespace Phyxel::Scene::Motion {

class IMotionSource {
public:
    virtual ~IMotionSource() = default;

    virtual void submitIntent(const MotionIntent& intent) = 0;
    virtual bool sample(double timeSeconds, LocalPoseFrame& output) = 0;
    virtual void reset() = 0;
    virtual MotionSourceStatus status() const = 0;
};

/// Dependency-free deterministic source used to prove routing, validation,
/// fallback, and retargeting before a neural runtime is present.
class DeterministicMotionSource final : public IMotionSource {
public:
    DeterministicMotionSource(std::vector<std::string> jointNames,
                              std::vector<glm::quat> localRotations);

    void submitIntent(const MotionIntent& intent) override;
    bool sample(double timeSeconds, LocalPoseFrame& output) override;
    void reset() override;
    MotionSourceStatus status() const override;

    void setAvailable(bool available) { m_available = available; }
    const MotionIntent& lastIntent() const { return m_intent; }

private:
    std::vector<std::string> m_jointNames;
    std::vector<glm::quat> m_localRotations;
    MotionIntent m_intent;
    bool m_available = true;
};

/// Return a provider frame when it is valid, otherwise preserve the exact
/// caller-owned clip pose and mark only its provenance as fallback.
LocalPoseFrame providerOrFallback(const LocalPoseFrame* providerFrame,
                                  const LocalPoseFrame& clipFrame);

} // namespace Phyxel::Scene::Motion

