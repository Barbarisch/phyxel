#pragma once

// The streaming FOCUS override (docs/PerfProgram2026-09.md section 16, I14; WorldForge M2): while
// held, the streaming pump anchors on this position instead of the player. Pure data, no engine
// dependencies, so its ownership rules are unit-tested directly (StreamingFocusOwnerTest).
//
// It has more than one user: the WorldForge build job and /api/worldforge/focus drive residency at a
// remote build site, and the camera-path benchmark (I12 stream_follow) drives it along a route. Each
// user names itself as the HOLDER; a different holder cannot take or clear a held focus. Without that,
// a WorldForge release cleared a route's focus mid-run and a route overwrote a build site's focus.
// Main-thread only, like ChunkManager::playerPosition.

#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace Phyxel {
namespace Core {

class StreamingFocus {
public:
    // How far the focus may move in ONE frame. The streaming pump loads around the focus; an instant
    // far jump is a teleport of the anchor, and characters standing on ground that just unloaded are
    // held (or were dropped, before the residency gate). WorldForge walks its focus toward a remote
    // site in steps of this size ("~2 chunks per poll -- a fast player, not a teleport") and re-polls
    // every frame, so this is a PER-FRAME bound. Shared by every holder.
    static constexpr float kMaxStepPerFrame = 64.0f;

    // Takes or moves the focus as `holder`. Refused (returns false, nothing changes) while a DIFFERENT
    // holder holds it. An empty holder name is refused.
    bool set(const glm::vec3& pos, const std::string& holder) {
        if (holder.empty()) return false;
        if (pos_ && holder_ != holder) return false;
        pos_ = pos;
        holder_ = holder;
        return true;
    }

    // Releases the focus if `holder` holds it. Returns false (nothing changes) otherwise.
    bool clear(const std::string& holder) {
        if (!pos_ || holder_ != holder) return false;
        pos_.reset();
        holder_.clear();
        return true;
    }

    bool held() const { return pos_.has_value(); }
    const std::string& holder() const { return holder_; }
    glm::vec3 anchor(const glm::vec3& player) const { return pos_.value_or(player); }

    // The next focus position when walking from `current` toward `target`: at most kMaxStepPerFrame.
    static glm::vec3 stepToward(const glm::vec3& current, const glm::vec3& target) {
        const glm::vec3 to = target - current;
        const float dist = glm::length(to);
        return dist <= kMaxStepPerFrame ? target : current + to * (kMaxStepPerFrame / dist);
    }

private:
    std::optional<glm::vec3> pos_;
    std::string holder_;
};

}  // namespace Core
}  // namespace Phyxel
