#pragma once

#include "core/AudioSystem.h"

#include <glm/glm.hpp>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel {
namespace Core {

// ============================================================================
// SoundRegistry — the sound EVENT catalog (docs/SoundSystemV2.md §4.1).
//
// Gameplay code names events ("furniture.activate"), not files. The catalog
// (resources/sounds/sounds.json) maps each event to a variation pool plus
// volume/pitch jitter ranges, which kills repetition fatigue and lets audio
// assets be swapped/expanded without touching C++. This is the same
// data-driven philosophy as materials.json.
//
// The hit.wav incident is why this exists: a gameplay call site referenced a
// file that didn't exist and every activation silently played nothing for
// months. With the registry, (a) a missing file fails CatalogValidation in the
// unit suite, and (b) playEvent on an unknown event logs loudly once.
// ============================================================================
class SoundRegistry {
public:
    struct Event {
        std::vector<std::string> files;   ///< resolved absolute/relative paths, ready to play
        float volumeMin = 1.0f, volumeMax = 1.0f;
        float pitchMin  = 1.0f, pitchMax  = 1.0f;
        AudioChannel channel = AudioChannel::SFX;
        bool spatial = false;             ///< caller is expected to pass a position
    };

    /// Load the catalog. `catalogPath` is the sounds.json path; file entries
    /// inside are resolved against its directory. Returns false (and leaves the
    /// registry empty) on parse failure. Safe to call again to hot-reload.
    bool load(const std::string& catalogPath);

    /// Fire an event. 3D when `position` is given AND the event is spatial;
    /// 2D otherwise. Picks a random variant + jitters volume/pitch within the
    /// catalog ranges. Unknown events log a warning (once per name) and no-op.
    /// `volumeScale` multiplies the catalog volume (caller-side emphasis).
    void playEvent(const std::string& name, const std::optional<glm::vec3>& position = std::nullopt,
                   float volumeScale = 1.0f);

    bool  hasEvent(const std::string& name) const { return m_events.count(name) > 0; }
    size_t eventCount() const { return m_events.size(); }
    const Event* getEvent(const std::string& name) const;

    void setAudioSystem(AudioSystem* audio) { m_audio = audio; }

private:
    AudioSystem* m_audio = nullptr;
    std::unordered_map<std::string, Event> m_events;
    std::unordered_map<std::string, bool> m_warned;  ///< one warning per unknown event
    std::mt19937 m_rng{std::random_device{}()};      ///< gameplay jitter — no determinism contract
};

} // namespace Core
} // namespace Phyxel
