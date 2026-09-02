#include "core/AmbienceDirector.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace Phyxel {
namespace Core {

bool AmbienceDirector::load(const std::string& ambienceJsonPath) {
    std::ifstream f(ambienceJsonPath);
    if (!f) {
        LOG_WARN("Ambience", "Config not found: {}", ambienceJsonPath);
        return false;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        LOG_ERROR("Ambience", "Config parse failure in {}: {}", ambienceJsonPath, e.what());
        return false;
    }

    const fs::path baseDir = fs::path(ambienceJsonPath).parent_path();
    std::unordered_map<std::string, ContextSpec> contexts;

    // Named object, NOT j.value(...).items() inline — the value() temporary
    // dies under the items() proxy (the SoundRegistry boot-crash lesson).
    const nlohmann::json contextsJson = j.value("contexts", nlohmann::json::object());
    for (const auto& [name, cj] : contextsJson.items()) {
        ContextSpec spec;
        if (cj.contains("bed")) {
            const auto& bed = cj["bed"];
            if (bed.is_string()) {
                spec.bedDay = (baseDir / bed.get<std::string>()).generic_string();
            } else if (bed.is_object()) {
                if (bed.contains("day"))
                    spec.bedDay = (baseDir / bed["day"].get<std::string>()).generic_string();
                if (bed.contains("night"))
                    spec.bedNight = (baseDir / bed["night"].get<std::string>()).generic_string();
            }
        }
        const nlohmann::json scatterJson = cj.value("scatter", nlohmann::json::array());
        for (const auto& sj : scatterJson) {
            ScatterSpec s;
            s.event = sj.value("event", std::string());
            if (s.event.empty()) continue;
            if (sj.contains("interval") && sj["interval"].is_array() && sj["interval"].size() == 2) {
                s.intervalMin = sj["interval"][0].get<float>();
                s.intervalMax = sj["interval"][1].get<float>();
            }
            s.when = sj.value("when", std::string("always"));
            if (sj.contains("radius") && sj["radius"].is_array() && sj["radius"].size() == 2) {
                s.radiusMin = sj["radius"][0].get<float>();
                s.radiusMax = sj["radius"][1].get<float>();
            }
            if (sj.contains("yOffset") && sj["yOffset"].is_array() && sj["yOffset"].size() == 2) {
                s.yMin = sj["yOffset"][0].get<float>();
                s.yMax = sj["yOffset"][1].get<float>();
            }
            spec.scatter.push_back(std::move(s));
        }
        contexts.emplace(name, std::move(spec));
    }

    m_contexts = std::move(contexts);
    LOG_INFO("Ambience", "Loaded {} ambience contexts from {}", m_contexts.size(), ambienceJsonPath);
    return true;
}

const AmbienceDirector::ContextSpec* AmbienceDirector::specFor(const std::string& context) const {
    auto it = m_contexts.find(context);
    return it == m_contexts.end() ? nullptr : &it->second;
}

bool AmbienceDirector::isDay() const {
    if (!m_time) return true;               // no clock wired = treat as day
    float h = m_time();
    return h >= 6.0f && h < 20.0f;          // dawn 06:00, dusk 20:00
}

void AmbienceDirector::enterContext(const std::string& context) {
    const ContextSpec* spec = specFor(context);
    const bool day = isDay();
    std::string newBed;
    if (spec) {
        newBed = (!day && !spec->bedNight.empty()) ? spec->bedNight : spec->bedDay;
    }

    // Which bed is currently up? (An unknown context = no spec = silence.)
    const ContextSpec* oldSpec = specFor(m_activeContext);
    std::string oldBed;
    if (oldSpec) {
        oldBed = (!m_activeBedIsDay && !oldSpec->bedNight.empty()) ? oldSpec->bedNight
                                                                   : oldSpec->bedDay;
    }

    m_activeContext = context;
    m_activeBedIsDay = day;

    // Reset scatter timers for the new context.
    m_scatterState.clear();
    if (spec) {
        m_scatterState.resize(spec->scatter.size());
        for (size_t i = 0; i < spec->scatter.size(); ++i) {
            const auto& s = spec->scatter[i];
            m_scatterState[i].countdown =
                std::uniform_real_distribution<float>(s.intervalMin, s.intervalMax)(m_rng);
        }
    }

    // Same bed file (e.g. two forest biomes sharing one bed): keep it running —
    // a crossfade to the identical file would be an audible dip for nothing.
    if (newBed == oldBed) return;

    if (m_audio) {
        if (m_activeBedLoop >= 0) m_audio->stopLoop(m_activeBedLoop, m_crossfadeMs);
        m_activeBedLoop =
            newBed.empty() ? -1
                           : m_audio->playLoop(newBed, AudioChannel::Ambience, 1.0f, m_crossfadeMs);
    }
    ++m_crossfadeCount;
    LOG_INFO("Ambience", "Context -> '{}' (bed: {})", context,
             newBed.empty() ? "<silence>" : newBed);
}

void AmbienceDirector::update(float dt, const glm::vec3& listenerPos) {
    if (!m_audio || !m_sampler) return;

    // THROTTLED biome sampling — never every frame. The sampler queries the
    // streaming WorldGenerator, which contends with chunk-generation workers;
    // per-frame calls measured 140 FPS -> 7 FPS on a streaming hydrology world
    // (WaterTest A/B + empty-contexts bisect, 2026-09-02). With 3 s context
    // hysteresis, 4 Hz sampling is behaviorally identical. `elapsed` (the real
    // time between samples) feeds the hysteresis accumulator so its timing is
    // unchanged by the throttle.
    m_sinceSample += dt;
    if (m_sinceSample >= m_sampleIntervalSec) {
        const float elapsed = std::min(m_sinceSample, 1.0f);  // clamp huge first-call value
        m_sinceSample = 0.0f;

        BiomeSample sample;
        if (m_sampler(listenerPos.x, listenerPos.z, sample)) {
            // Underground: depth below the sampled surface trumps the surface biome.
            std::string context =
                (listenerPos.y < float(sample.surfaceY) - m_undergroundDepth) ? "cave"
                                                                              : sample.biome;

            if (m_activeContext.empty()) {
                // First successful sample: enter immediately — a 3 s hysteresis
                // hold at boot would just be silence for no reason.
                enterContext(context);
            } else if (context == m_activeContext) {
                m_pendingContext.clear();
                m_pendingSec = 0.0f;
            } else if (context == m_pendingContext) {
                m_pendingSec += elapsed;
                if (m_pendingSec >= m_hysteresisSec) enterContext(context);
            } else {
                m_pendingContext = context;
                m_pendingSec = 0.0f;
            }
        }
        // Sampler failure (chunk not resident): hold the current context.
    }

    // Day/night bed variant swap within the active context.
    if (!m_activeContext.empty() && isDay() != m_activeBedIsDay) {
        const ContextSpec* spec = specFor(m_activeContext);
        if (spec && !spec->bedNight.empty()) {
            enterContext(m_activeContext);  // re-enter picks the other variant
        } else {
            m_activeBedIsDay = isDay();     // no variant — just track the flag
        }
    }

    // Scatter one-shots around the listener.
    const ContextSpec* spec = specFor(m_activeContext);
    if (spec && m_registry) {
        const bool day = isDay();
        for (size_t i = 0; i < spec->scatter.size() && i < m_scatterState.size(); ++i) {
            const ScatterSpec& s = spec->scatter[i];
            ScatterState& st = m_scatterState[i];
            st.countdown -= dt;
            if (st.countdown > 0.0f) continue;
            st.countdown =
                std::uniform_real_distribution<float>(s.intervalMin, s.intervalMax)(m_rng);
            if (s.when == "day" && !day) continue;
            if (s.when == "night" && day) continue;

            // Random point on a ring around the listener; birds get positive
            // yOffset ranges, ground insects ~0 (all data-driven).
            float angle  = std::uniform_real_distribution<float>(0.0f, 6.2831853f)(m_rng);
            float radius = std::uniform_real_distribution<float>(s.radiusMin, s.radiusMax)(m_rng);
            float yOff   = std::uniform_real_distribution<float>(s.yMin, s.yMax)(m_rng);
            glm::vec3 pos = listenerPos +
                            glm::vec3(std::cos(angle) * radius, yOff, std::sin(angle) * radius);
            m_registry->playEvent(s.event, pos);
        }
    }
}

void AmbienceDirector::stopAll(unsigned fadeMs) {
    if (m_audio && m_activeBedLoop >= 0) {
        m_audio->stopLoop(m_activeBedLoop, fadeMs);
        m_activeBedLoop = -1;
    }
    m_activeContext.clear();
    m_pendingContext.clear();
    m_pendingSec = 0.0f;
    m_scatterState.clear();
}

} // namespace Core
} // namespace Phyxel
