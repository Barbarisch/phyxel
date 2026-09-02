#pragma once

#include "core/AudioSystem.h"
#include "core/SoundRegistry.h"

#include <glm/glm.hpp>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel {
namespace Core {

// ============================================================================
// AmbienceDirector — biome soundscapes (docs/SoundSystemV2.md §4.2).
//
// A soundscape = a looping BED (2D, per-context) + randomized one-shot SCATTER
// (3D, placed around the listener). Context is derived per tick from data the
// engine already computes — biome at the listener column, and depth below the
// surface — through injected callbacks, so this class never touches ChunkManager
// or DayNightCycle directly (decoupled for offline-PCM unit tests).
//
// Chunk-invisibility rule: the context is a pure function of LISTENER WORLD
// POSITION via sampleSurface (itself order-independent); nothing here reads
// per-chunk state, so the soundscape cannot seam at chunk borders.
//
// Config: resources/sounds/ambience.json, keyed by biome NAME — deliberately
// NOT a biomes.json block, so worldgen's Biome struct and the generation
// pipeline stay untouched (FeatureDesignKeys: don't disturb other stages).
//
// Transitions: candidate context must hold for hysteresisSec (default 3 s)
// before the beds crossfade (default 3 s, equal-power via the AudioSystem loop
// fade API) — strafing a biome border must NOT thrash beds; the test pins
// exactly-one-crossfade per genuine transition.
// ============================================================================
class AmbienceDirector {
public:
    struct BiomeSample {
        std::string biome;    ///< biome name (matches ambience.json key)
        int surfaceY = 0;     ///< world Y of the surface at this column
    };
    /// Return false when the world can't answer (e.g. chunk not resident) —
    /// the director then holds its current context rather than guessing.
    using BiomeSampler = std::function<bool(float worldX, float worldZ, BiomeSample& out)>;
    /// Hour of day in [0,24); absent = treat as noon (day).
    using TimeProvider = std::function<float()>;

    struct ScatterSpec {
        std::string event;                 ///< SoundRegistry event name
        float intervalMin = 5.0f, intervalMax = 20.0f;  ///< seconds between one-shots
        std::string when = "always";       ///< "day" | "night" | "always"
        float radiusMin = 8.0f, radiusMax = 20.0f;      ///< horizontal ring around listener (m)
        float yMin = 0.0f, yMax = 0.0f;    ///< vertical offset range (m; birds up, insects ground)
    };
    struct ContextSpec {
        std::string bedDay;                ///< looping bed file (resolved path); may be empty
        std::string bedNight;              ///< night variant; empty = use bedDay
        std::vector<ScatterSpec> scatter;
    };

    bool load(const std::string& ambienceJsonPath);

    void setAudioSystem(AudioSystem* audio)   { m_audio = audio; }
    void setSoundRegistry(SoundRegistry* reg) { m_registry = reg; }
    void setBiomeSampler(BiomeSampler s)      { m_sampler = std::move(s); }
    void setTimeProvider(TimeProvider t)      { m_time = std::move(t); }

    /// Depth below the surface at which the surface soundscape yields to the
    /// "cave" context. Grounded on the deepest surface feature the generator
    /// carves: order-6 rivers cut ~8 voxels (TerrainGenerationV2 hydraulic
    /// geometry), so 12 m guarantees standing in any carved riverbed or
    /// walking a valley floor still counts as SURFACE, while genuine caves
    /// (bounded only by world depth) exceed it almost immediately.
    void setUndergroundDepth(float d) { m_undergroundDepth = d; }
    void setHysteresisSec(float s)    { m_hysteresisSec = s; }
    void setCrossfadeMs(unsigned ms)  { m_crossfadeMs = ms; }

    /// Seconds between biome-sampler calls (default 0.25 s). The sampler goes
    /// through the streaming WorldGenerator, whose per-column query CONTENDS
    /// with chunk-generation workers — calling it every frame collapsed a
    /// streaming hydrology world from ~140 FPS to ~7 (measured A/B, WaterTest
    /// 2026-09-02, empty-contexts bisect: the call alone, no audio playing).
    /// Context decisions sit behind 3 s hysteresis, so 4 Hz is semantically
    /// identical to per-frame.
    void setSampleIntervalSec(float s) { m_sampleIntervalSec = s; }

    /// Per-frame driver. Samples context at the listener, applies hysteresis,
    /// crossfades beds on a genuine transition, fires due scatter one-shots.
    void update(float dt, const glm::vec3& listenerPos);

    /// Stop the bed + pending scatter (scene unload).
    void stopAll(unsigned fadeMs = 0);

    // --- Observability (get_audio_state + tests) ---
    const std::string& activeContext() const { return m_activeContext; }
    int  crossfadeCount() const { return m_crossfadeCount; }
    bool hasContext(const std::string& name) const { return m_contexts.count(name) > 0; }

private:
    const ContextSpec* specFor(const std::string& context) const;
    bool isDay() const;
    void enterContext(const std::string& context);

    AudioSystem*   m_audio    = nullptr;
    SoundRegistry* m_registry = nullptr;
    BiomeSampler   m_sampler;
    TimeProvider   m_time;

    std::unordered_map<std::string, ContextSpec> m_contexts;  ///< biome name or "cave"

    std::string m_activeContext;     ///< empty until the first successful sample
    std::string m_pendingContext;
    float m_pendingSec = 0.0f;
    float m_hysteresisSec = 3.0f;    ///< SoundSystemV2 §4.2: ~3 s before switching
    unsigned m_crossfadeMs = 3000;   ///< §4.2: 2–4 s equal-power bed crossfade
    float m_undergroundDepth = 12.0f;
    float m_sampleIntervalSec = 0.25f;
    float m_sinceSample = 1e9f;      ///< huge so the first update samples immediately
    int  m_activeBedLoop = -1;       ///< AudioSystem loop id, -1 = none
    bool m_activeBedIsDay = true;    ///< which variant the current bed is
    int  m_crossfadeCount = 0;

    struct ScatterState { float countdown = 0.0f; };
    std::vector<ScatterState> m_scatterState;  ///< parallel to active spec's scatter

    std::mt19937 m_rng{std::random_device{}()};
};

} // namespace Core
} // namespace Phyxel
