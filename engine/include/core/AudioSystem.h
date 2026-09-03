#pragma once

#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace Phyxel {
namespace Core {

enum class AudioChannel {
    Master,
    SFX,
    Music,
    Voice,
    Ambience   ///< looping environment beds — separate bus so SFX ducking never mutes the world
};

/// Engine setup. Defaults reproduce the historical behaviour — open the system
/// default playback device and let it pull the mix on its own thread.
struct AudioSystemConfig {
    /// Run with NO output device. Nothing is audible; the caller pulls the mixed
    /// PCM itself via renderFrames().
    ///
    /// This is the seam that makes audio *measurable*. Audio has no visual
    /// diagnostic, so without it every claim about bus routing or spatial
    /// panning is unfalsifiable ("it sounded right"). Rendering deterministic
    /// PCM offline lets a test assert on the ACTUAL output of this class —
    /// per-channel RMS for panning, total RMS for bus gain.
    bool     deviceless  = false;
    uint32_t sampleRate  = 48000;   ///< Deviceless only; a device supplies its own.
    uint32_t channels    = 2;       ///< Deviceless only; a device supplies its own.
};

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    bool initialize(const AudioSystemConfig& config = {});
    void shutdown();

    /// Deviceless mode only: mix `frameCount` frames into `out` (interleaved
    /// float, `config.channels` samples per frame). Returns frames written, or
    /// 0 when a real device owns the mix (it is pulling on its own thread).
    size_t renderFrames(float* out, size_t frameCount);
    
    // Update listener (player) position and velocity for 3D audio
    void update(const glm::vec3& listenerPos, const glm::vec3& listenerForward, const glm::vec3& listenerUp, const glm::vec3& listenerVelocity = glm::vec3(0.0f));

    // Play a sound file (fire and forget) - 2D. pitch is a playback-rate
    // multiplier (1.0 = as recorded); SoundRegistry jitters it per event.
    void playSound(const std::string& filePath, AudioChannel channel = AudioChannel::SFX, float volume = 1.0f, float pitch = 1.0f);

    // Play a 3D sound at a specific position. Sounds whose effective gain
    // lands below -60 dB at the current listener distance are culled (never
    // started) rather than mixed inaudibly forever.
    void playSound3D(const std::string& filePath, const glm::vec3& position, AudioChannel channel = AudioChannel::SFX, float volume = 1.0f, const glm::vec3& velocity = glm::vec3(0.0f), float pitch = 1.0f);

    // Background music (looping, one track at a time). fadeMs > 0 crossfades:
    // the outgoing track fades out while the new one fades in (equal duration).
    // fadeMs = 0 keeps the historical hard cut.
    void playMusic(const std::string& filePath, float volume = 1.0f, unsigned fadeMs = 0);
    void stopMusic(unsigned fadeMs = 0);
    bool isMusicPlaying() const;
    std::string getMusicTrack() const;

    // Set volume for a specific channel (0.0 to 1.0)
    void setChannelVolume(AudioChannel channel, float volume);

    // ------------------------------------------------------------------
    // Long-lived looping sources (ambience beds, fireplaces, waterfalls).
    // Unlike playSound* these are NOT pooled/recycled — they play until
    // stopLoop(), which fades them out and reaps them in update().
    // ------------------------------------------------------------------

    /// Start a looping source. 2D when `position` is null, 3D at *position
    /// otherwise. Streams from disk (beds are minutes long). Returns a handle
    /// id, or -1 on load failure.
    int  playLoop(const std::string& filePath, AudioChannel channel = AudioChannel::Ambience,
                  float volume = 1.0f, unsigned fadeInMs = 0, const glm::vec3* position = nullptr);

    /// Fade out (fadeOutMs = 0 → hard stop) and release a loop. Unknown ids no-op.
    void stopLoop(int id, unsigned fadeOutMs = 0);

    /// Loops currently playing (fading-out ones count until reaped).
    size_t activeLoopCount() const;

    // Preload a sound into memory (optional optimization)
    void preloadSound(const std::string& filePath);

    /// Listener position as miniaudio currently has it — read back from the
    /// engine, NOT a cached copy, so an API probe measures the real state.
    glm::vec3 getListenerPosition() const;

    /// Sounds currently playing (moved back to the pool by update() when done).
    /// If update() is never called — the shipped-game defect this exposes —
    /// this grows without bound, one live decoder per playSound call.
    size_t activeSoundCount() const;

    /// Finished sounds parked for reuse, across all file paths.
    size_t pooledSoundCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Core
} // namespace Phyxel
