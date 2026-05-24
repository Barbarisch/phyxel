#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

class KinematicVoxelManager;

/// Phase C — Object animation driver.
///
/// Drives the world transform of KinematicVoxelObjects emitted by the
/// composite-parts spawn path (Phase C0b). Each registered part owns a
/// rotation track (angle around a fixed local axis) and a translation
/// track (offset along a fixed local axis) so the same component covers
/// pivot-hinge parts (doors, lids, hatches) AND linear-slide parts
/// (drawers, sliding doors) without subclassing.
///
/// Update model:
///   * Each frame, advance currentAngle / currentOffset toward their
///     targets at the configured speed (radians/sec, meters/sec).
///   * Easing is applied to the *progress* fraction rather than to time,
///     so callers can interrupt mid-animation without snapping.
///   * After integration, recompute the world transform and push it to
///     KinematicVoxelManager::setTransform().
///
/// The component is intentionally engine-agnostic: it does not touch the
/// Bullet world, the NavGrid, or any callback registry. Higher-level
/// systems (DoorManager today, future ContainerManager / SlideManager)
/// observe `settled` and rebuild auxiliary structures on transitions.
class KinematicAnimator {
public:
    enum class Axis { X = 0, Y = 1, Z = 2 };
    enum class Easing { Linear, EaseInOut };

    /// Configuration captured at registration time. Targets/speeds are
    /// mutated in flight via setTargetAngle / setTargetOffset.
    struct PartConfig {
        std::string kinematicId;          ///< Object in KinematicVoxelManager.
        glm::vec3   hingeWorld{0.0f};    ///< Pivot point in world space.
        Axis        rotationAxis = Axis::Y;
        float       baseRotationRad = 0.0f;  ///< Yaw applied before track rotation.

        /// Optional slide direction in *local* (pre-rotation) space. For pure
        /// pivots leave at zero — translation track stays clamped to (0,0,0).
        glm::vec3   slideDirLocal{0.0f};
        Easing      easing = Easing::EaseInOut;
    };

    /// Register a kinematic object as an animatable part.
    /// Idempotent on kinematicId — re-registering overwrites the entry and
    /// resets currentAngle/currentOffset to zero.
    void registerPart(const PartConfig& cfg);

    /// Stop animating and forget the part. Safe to call for unknown IDs.
    void unregisterPart(const std::string& kinematicId);

    /// Drive the rotation track toward `angleRad` at `speedRadPerSec`.
    /// speed <= 0 snaps instantly.
    void setTargetAngle(const std::string& kinematicId,
                        float angleRad,
                        float speedRadPerSec);

    /// Drive the translation track toward `offsetMeters` (signed length
    /// along slideDirLocal) at `speedMetersPerSec`. speed <= 0 snaps.
    void setTargetOffset(const std::string& kinematicId,
                         float offsetMeters,
                         float speedMetersPerSec);

    /// Advance all tracks by dt and push transforms into the voxel manager.
    /// No-op when m_voxelManager is null.
    void update(float dt);

    /// True when both tracks have reached their target within epsilon.
    bool isSettled(const std::string& kinematicId) const;

    /// Current values (useful for tests and persistence).
    float currentAngle(const std::string& kinematicId) const;
    float currentOffset(const std::string& kinematicId) const;

    /// Compose the world transform for `cfg` at the given track values.
    /// Exposed for tests + callers that need the matrix without animating.
    static glm::mat4 composeTransform(const PartConfig& cfg,
                                       float angleRad,
                                       float offsetMeters);

    void setKinematicVoxelManager(KinematicVoxelManager* mgr) { m_voxelManager = mgr; }
    KinematicVoxelManager* getKinematicVoxelManager() const { return m_voxelManager; }

    /// Settle callback: fires once when a part transitions to settled.
    /// `kinematicId` and the just-reached angle are passed.
    using SettleCallback = std::function<void(const std::string&, float angleRad, float offsetMeters)>;
    void setSettleCallback(SettleCallback cb) { m_onSettle = std::move(cb); }

    size_t count() const { return m_parts.size(); }
    bool   has(const std::string& kinematicId) const { return m_parts.count(kinematicId) > 0; }

private:
    struct PartState {
        PartConfig cfg;
        float currentAngle = 0.0f;
        float targetAngle  = 0.0f;
        float angleSpeed   = 0.0f;     ///< radians/sec. <= 0 means snap.

        float currentOffset = 0.0f;
        float targetOffset  = 0.0f;
        float offsetSpeed   = 0.0f;    ///< meters/sec. <= 0 means snap.

        bool settled = true;
    };

    /// Integrate one scalar toward target at speed. Returns true when settled.
    /// Easing is applied to the progress fraction so resumes feel smooth.
    static bool integrate(float& current, float target, float speed, float dt, Easing easing);

    std::unordered_map<std::string, PartState> m_parts;
    KinematicVoxelManager* m_voxelManager = nullptr;
    SettleCallback m_onSettle;
};

} // namespace Core
} // namespace Phyxel
