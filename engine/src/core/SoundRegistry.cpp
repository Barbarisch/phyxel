#include "core/SoundRegistry.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace Phyxel {
namespace Core {

namespace {

AudioChannel channelFromString(const std::string& s) {
    if (s == "Master")   return AudioChannel::Master;
    if (s == "Music")    return AudioChannel::Music;
    if (s == "Voice")    return AudioChannel::Voice;
    if (s == "Ambience") return AudioChannel::Ambience;
    return AudioChannel::SFX;
}

// Parse a "[min, max]" range; a bare number means a fixed value.
void parseRange(const nlohmann::json& j, float& outMin, float& outMax) {
    if (j.is_array() && j.size() == 2) {
        outMin = j[0].get<float>();
        outMax = j[1].get<float>();
    } else if (j.is_number()) {
        outMin = outMax = j.get<float>();
    }
}

} // namespace

bool SoundRegistry::load(const std::string& catalogPath) {
    std::ifstream f(catalogPath);
    if (!f) {
        LOG_WARN("SoundRegistry", "Catalog not found: {}", catalogPath);
        return false;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        LOG_ERROR("SoundRegistry", "Catalog parse failure in {}: {}", catalogPath, e.what());
        return false;
    }

    const fs::path baseDir = fs::path(catalogPath).parent_path();
    std::unordered_map<std::string, Event> events;

    // NOTE: bound to a named object, NOT `j.value(...).items()` inline — value()
    // returns a temporary that dies at the end of the full expression while the
    // items() proxy still points at it. That dangling iteration crashed the
    // probe boot (found by the --audio-selftest bisect, 2026-09-01).
    const nlohmann::json eventsJson = j.value("events", nlohmann::json::object());
    for (const auto& [name, ev] : eventsJson.items()) {
        Event e;
        for (const auto& file : ev.value("files", nlohmann::json::array())) {
            e.files.push_back((baseDir / file.get<std::string>()).generic_string());
        }
        if (e.files.empty()) {
            LOG_WARN("SoundRegistry", "Event '{}' has no files — skipped", name);
            continue;
        }
        if (ev.contains("volume")) parseRange(ev["volume"], e.volumeMin, e.volumeMax);
        if (ev.contains("pitch"))  parseRange(ev["pitch"],  e.pitchMin,  e.pitchMax);
        e.channel = channelFromString(ev.value("channel", std::string("SFX")));
        e.spatial = ev.value("spatial", false);
        e.minDistance = ev.value("minDistance", 1.0f);
        events.emplace(name, std::move(e));
    }

    m_events = std::move(events);
    m_warned.clear();
    LOG_INFO("SoundRegistry", "Loaded {} sound events from {}", m_events.size(), catalogPath);
    return true;
}

const SoundRegistry::Event* SoundRegistry::getEvent(const std::string& name) const {
    auto it = m_events.find(name);
    return it == m_events.end() ? nullptr : &it->second;
}

void SoundRegistry::playEvent(const std::string& name, const std::optional<glm::vec3>& position,
                              float volumeScale) {
    if (!m_audio) return;

    auto it = m_events.find(name);
    if (it == m_events.end()) {
        // Loud once, silent after — an unknown event is a content bug the
        // catalog test should have caught, not a per-frame log flood.
        if (!m_warned[name]) {
            m_warned[name] = true;
            LOG_WARN("SoundRegistry", "Unknown sound event '{}' — playing nothing", name);
        }
        return;
    }
    const Event& e = it->second;

    // Random variant + jitter within catalog ranges.
    const std::string& file =
        e.files[e.files.size() == 1
                    ? 0
                    : std::uniform_int_distribution<size_t>(0, e.files.size() - 1)(m_rng)];
    auto jitter = [this](float lo, float hi) {
        return lo >= hi ? lo : std::uniform_real_distribution<float>(lo, hi)(m_rng);
    };
    const float volume = jitter(e.volumeMin, e.volumeMax) * volumeScale;
    const float pitch  = jitter(e.pitchMin, e.pitchMax);

    if (e.spatial && position.has_value()) {
        m_audio->playSound3D(file, *position, e.channel, volume, glm::vec3(0.0f), pitch,
                             e.minDistance);
    } else {
        m_audio->playSound(file, e.channel, volume, pitch);
    }
}

} // namespace Core
} // namespace Phyxel
