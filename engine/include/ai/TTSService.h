#pragma once

#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <functional>

namespace Phyxel {
namespace Core { class AudioSystem; }
namespace AI {

/// Per-character voice descriptor resolved at speak time. Lets a character's
/// VoiceProfile (gender/age/pitch/rate/gruffness) drive Piper speaker + rate
/// selection without TTSService depending on the story layer.
struct TTSVoiceHint {
    bool  valid     = false;      ///< Set true by the resolver when a profile was found.
    int   speakerId = -1;         ///< Explicit Piper speaker override (-1 = derive).
    std::string gender = "neutral";
    std::string age    = "adult";
    float pitch     = 0.5f;
    float rate      = 0.5f;       ///< 0 slow .. 1 fast (maps to lengthScale).
    float gruffness = 0.5f;
};

/// Configuration for local Piper text-to-speech.
/// All paths are resolved against the engine resource root at initialize().
struct TTSConfig {
    std::string piperExe;     ///< Path to piper(.exe). Empty = auto-detect under external/piper.
    std::string modelPath;    ///< Path to the .onnx voice model. Empty = auto-detect.
    std::string cacheDir;     ///< Directory for synthesized WAVs. Empty = "cache/tts".
    int   numSpeakers   = 0;  ///< Speakers in the model (0 = read from .onnx.json).
    float lengthScaleMin = 0.92f; ///< Per-voice speaking-rate jitter range (lower = faster).
    float lengthScaleMax = 1.12f;
    float volume        = 1.0f;   ///< Voice playback volume (0..1).
    bool  enabled       = true;
};

// ============================================================================
// TTSService — local Piper TTS for NPC dialogue.
//
// Procedural per-NPC voice: a stable voiceKey (the NPC id/name) is hashed to a
// speaker id within a multi-speaker model and a deterministic speaking-rate,
// so every NPC sounds distinct and consistent across lines.
//
// Synthesis runs on a worker thread (CPU-bound). Results are cached to WAV
// keyed by (text, speaker, rate, model) and played through the AudioSystem
// Voice channel — spatialized when a position is given. When the Piper binary
// or model is absent, the service no-ops gracefully so the engine still runs.
// ============================================================================
class TTSService {
public:
    TTSService();
    ~TTSService();

    TTSService(const TTSService&) = delete;
    TTSService& operator=(const TTSService&) = delete;

    /// Resolve assets, read model metadata, start the worker thread.
    /// Returns true when Piper is available; false (but safe) otherwise.
    bool initialize(Core::AudioSystem* audio, const TTSConfig& config,
                    const std::string& resourceRoot = "");

    /// Stop the worker thread and drain the queue.
    void shutdown();

    /// True when a usable Piper binary + voice model were found.
    bool isAvailable() const { return m_available; }

    /// Queue an NPC line for synthesis + spatialized playback at `position`.
    /// Returns immediately; work happens on the worker thread.
    void speak(const std::string& voiceKey, const std::string& text,
               const glm::vec3& position);

    /// Queue an NPC line for non-spatialized (2D) playback.
    void speak2D(const std::string& voiceKey, const std::string& text);

    /// Resolver that maps a voiceKey (NPC/character id) to a TTSVoiceHint.
    /// Return true and fill `out` when a voice profile exists; false to fall back
    /// to the deterministic name-hash voice. Wired where the story engine is available.
    using VoiceHintResolver = std::function<bool(const std::string& voiceKey, TTSVoiceHint& out)>;
    void setVoiceHintResolver(VoiceHintResolver resolver) { m_voiceResolver = std::move(resolver); }

private:
    struct Job {
        std::string text;
        int   speakerId;
        float lengthScale;
        glm::vec3 position;
        bool  spatial;
    };

    int   speakerIdFor(const std::string& voiceKey, const TTSVoiceHint& hint) const;
    float lengthScaleFor(const std::string& voiceKey, const TTSVoiceHint& hint) const;
    std::string cachePathFor(const std::string& text, int speakerId, float lengthScale) const;
    bool  synthesize(const Job& job, const std::string& outWav);
    void  workerLoop();
    void  enqueue(const std::string& voiceKey, const std::string& text,
                  const glm::vec3& position, bool spatial);

    Core::AudioSystem* m_audio = nullptr;
    TTSConfig   m_config;
    VoiceHintResolver m_voiceResolver;  ///< Optional: maps voiceKey -> per-character voice.
    std::string m_modelName;   ///< Bare model filename, part of the cache key.
    bool        m_available = false;

    // Worker thread
    std::thread             m_worker;
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::deque<Job>         m_queue;
    std::atomic<bool>       m_running{false};
};

} // namespace AI
} // namespace Phyxel
