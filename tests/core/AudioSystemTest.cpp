// AudioSystemTest — offline-PCM measurement of the REAL AudioSystem.
//
// Audio has no visual diagnostic, so these tests render the engine's actual
// mixed output via AudioSystem::renderFrames (deviceless miniaudio) and assert
// on per-channel RMS. No speaker, no ears, no vibes.
//
// Instrument discipline (FeatureDesignKeys: one variable, a prediction, a
// control): PanControl_* are the CONTROL — a known-good hard-left/hard-right
// pan. If the instrument itself doesn't spatialize (engine not started,
// buffers silently empty, async load race), the control fails and every other
// result here is void.

#include <gtest/gtest.h>
#include "core/AudioSystem.h"
#include "core/SoundRegistry.h"

#include <nlohmann/json.hpp>
#include <fstream>

#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using Phyxel::Core::AudioChannel;
using Phyxel::Core::AudioSystem;
using Phyxel::Core::AudioSystemConfig;

namespace {

// Tests may run from repo root, build/, or build/tests/<Config>/ — probe the
// same prefix ladder other resource-reading tests use (ApothecaryTypologyTest).
std::string soundPath(const std::string& name) {
    for (const char* prefix : {"resources/sounds/", "../resources/sounds/",
                               "../../resources/sounds/", "../../../resources/sounds/"}) {
        std::string p = std::string(prefix) + name;
        if (std::filesystem::exists(p)) return p;
    }
    return "";
}

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels   = 2;

struct StereoRms {
    double left  = 0.0;
    double right = 0.0;
    double total() const { return std::sqrt((left * left + right * right) / 2.0); }
};

// Render `seconds` of output and measure per-channel RMS.
StereoRms renderAndMeasure(AudioSystem& audio, double seconds) {
    const size_t frames = static_cast<size_t>(seconds * kSampleRate);
    std::vector<float> buf(frames * kChannels, 0.0f);

    // Render in device-callback-sized chunks — one giant read is a code path a
    // real device never exercises.
    const size_t chunk = 480;  // 10 ms
    size_t done = 0;
    while (done < frames) {
        size_t n = std::min(chunk, frames - done);
        size_t got = audio.renderFrames(buf.data() + done * kChannels, n);
        if (got == 0) break;  // engine refused; RMS 0 will fail the control test
        done += got;
    }

    StereoRms rms;
    double sumL = 0.0, sumR = 0.0;
    for (size_t i = 0; i < done; ++i) {
        sumL += double(buf[i * 2]) * double(buf[i * 2]);
        sumR += double(buf[i * 2 + 1]) * double(buf[i * 2 + 1]);
    }
    if (done > 0) {
        rms.left  = std::sqrt(sumL / double(done));
        rms.right = std::sqrt(sumR / double(done));
    }
    return rms;
}

class AudioSystemPcmTest : public ::testing::Test {
protected:
    void SetUp() override {
        AudioSystemConfig cfg;
        cfg.deviceless = true;
        cfg.sampleRate = kSampleRate;
        cfg.channels   = kChannels;
        ASSERT_TRUE(audio.initialize(cfg)) << "deviceless ma_engine failed to init";

        wav = soundPath("whoosh.wav");  // 1.0 s of shaped noise — steady content
        ASSERT_FALSE(wav.empty()) << "resources/sounds/whoosh.wav not found from test cwd";

        // Listener at origin, facing -Z, +Y up. miniaudio is right-handed
        // (ma_handedness_right default), so right = forward x up = +X.
        audio.update(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void TearDown() override { audio.shutdown(); }

    AudioSystem audio;
    std::string wav;
};

} // namespace

// ============================================================================
// CONTROL — the instrument itself. A sound hard to the listener's LEFT must
// measure louder in the left channel, and mirrored for RIGHT. If these fail,
// nothing else in this file means anything.
// ============================================================================

TEST_F(AudioSystemPcmTest, PanControl_SourceLeftIsLouderLeft) {
    audio.playSound3D(wav, glm::vec3(-10.0f, 0.0f, 0.0f), AudioChannel::SFX, 1.0f);
    StereoRms rms = renderAndMeasure(audio, 0.5);

    ASSERT_GT(rms.total(), 1e-4) << "no signal rendered at all — instrument broken";
    // minSpatializationChannelGain=0.2 keeps ~20% in the far channel, so the
    // honest expectation is a strong ratio, not silence on the right.
    EXPECT_GT(rms.left, rms.right * 2.0)
        << "L=" << rms.left << " R=" << rms.right;
}

TEST_F(AudioSystemPcmTest, PanControl_SourceRightIsLouderRight) {
    audio.playSound3D(wav, glm::vec3(10.0f, 0.0f, 0.0f), AudioChannel::SFX, 1.0f);
    StereoRms rms = renderAndMeasure(audio, 0.5);

    ASSERT_GT(rms.total(), 1e-4) << "no signal rendered at all — instrument broken";
    EXPECT_GT(rms.right, rms.left * 2.0)
        << "L=" << rms.left << " R=" << rms.right;
}

// A 2D sound must ignore listener orientation entirely: equal power both sides.
TEST_F(AudioSystemPcmTest, Sound2DIsNotSpatialized) {
    audio.playSound(wav, AudioChannel::SFX, 1.0f);
    StereoRms rms = renderAndMeasure(audio, 0.5);

    ASSERT_GT(rms.total(), 1e-4);
    EXPECT_NEAR(rms.left / rms.right, 1.0, 0.05)
        << "L=" << rms.left << " R=" << rms.right;
}

// Distance attenuates: the same sound at 30 units must be quieter than at 2.
// (Model tuning is Inc 2's job; this pins that attenuation exists at all.)
TEST_F(AudioSystemPcmTest, DistanceAttenuates) {
    audio.playSound3D(wav, glm::vec3(0.0f, 0.0f, -2.0f), AudioChannel::SFX, 1.0f);
    StereoRms nearRms = renderAndMeasure(audio, 0.5);

    // Fresh system per condition — one variable.
    audio.shutdown();
    AudioSystemConfig cfg;
    cfg.deviceless = true; cfg.sampleRate = kSampleRate; cfg.channels = kChannels;
    ASSERT_TRUE(audio.initialize(cfg));
    audio.update(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    audio.playSound3D(wav, glm::vec3(0.0f, 0.0f, -30.0f), AudioChannel::SFX, 1.0f);
    StereoRms farRms = renderAndMeasure(audio, 0.5);

    ASSERT_GT(nearRms.total(), 1e-4);
    ASSERT_GT(farRms.total(), 0.0);
    EXPECT_GT(nearRms.total(), farRms.total() * 2.0)
        << "near=" << nearRms.total() << " far=" << farRms.total();
}

// ============================================================================
// RED — Master bus. getGroup() maps AudioChannel::Master onto the SFX group,
// so "master volume 0" does not silence Music. This is the AudioSystem.cpp
// defect (getGroup has no Master case; default: returns &groupSFX) and it is
// why a shipped game's master-volume slider silently does nothing to music.
// EXPECTED TO FAIL until Inc 2 introduces a real master bus.
// ============================================================================

TEST_F(AudioSystemPcmTest, MasterVolumeZeroSilencesMusicChannel) {
    audio.setChannelVolume(AudioChannel::Master, 0.0f);
    audio.playSound(wav, AudioChannel::Music, 1.0f);
    StereoRms rms = renderAndMeasure(audio, 0.5);

    EXPECT_LT(rms.total(), 1e-6)
        << "Master=0 but the Music channel still renders at RMS " << rms.total()
        << " — AudioChannel::Master is aliased onto the SFX group";
}

TEST_F(AudioSystemPcmTest, MasterVolumeZeroSilencesSfxChannel) {
    audio.setChannelVolume(AudioChannel::Master, 0.0f);
    audio.playSound(wav, AudioChannel::SFX, 1.0f);
    StereoRms rms = renderAndMeasure(audio, 0.5);

    // This one PASSES today — but only because of the aliasing bug (Master IS
    // the SFX group). It must keep passing after the real master bus lands,
    // which is what makes it worth pinning now.
    EXPECT_LT(rms.total(), 1e-6);
}

// ============================================================================
// Recycling — update() is what returns finished sounds to the pool. These pin
// the mechanics that EngineRuntime::endFrame() must drive every frame; without
// that call the "accumulates" shape below is exactly the shipped-game leak.
// ============================================================================

TEST_F(AudioSystemPcmTest, UpdateRecyclesFinishedSoundsToPool) {
    audio.playSound(wav, AudioChannel::SFX, 1.0f);
    EXPECT_EQ(audio.activeSoundCount(), 1u);
    EXPECT_EQ(audio.pooledSoundCount(), 0u);

    // Still playing — update() must NOT steal a live sound.
    renderAndMeasure(audio, 0.1);
    audio.update(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(audio.activeSoundCount(), 1u);

    // Drive the 1.0 s whoosh past its end, then recycle.
    renderAndMeasure(audio, 1.1);
    audio.update(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(audio.activeSoundCount(), 0u);
    EXPECT_EQ(audio.pooledSoundCount(), 1u);

    // Replaying the same file must reuse the pooled decoder, not create one.
    audio.playSound(wav, AudioChannel::SFX, 1.0f);
    EXPECT_EQ(audio.activeSoundCount(), 1u);
    EXPECT_EQ(audio.pooledSoundCount(), 0u);
}

TEST_F(AudioSystemPcmTest, WithoutUpdateFinishedSoundsAccumulate) {
    // Documents the leak mechanism: no update() call, so finished sounds are
    // never reaped. This is what every packaged game did before the
    // EngineRuntime::endFrame() wiring — one live decoder per playSound, forever.
    for (int i = 0; i < 3; ++i) {
        audio.playSound(wav, AudioChannel::SFX, 1.0f);
        renderAndMeasure(audio, 1.1);  // each sound plays fully to the end
    }
    EXPECT_EQ(audio.activeSoundCount(), 3u);  // all finished, none reclaimed
    EXPECT_EQ(audio.pooledSoundCount(), 0u);
}

// ============================================================================
// RED — no audibility cull. maxDistance is the miniaudio default (FLT_MAX) and
// nothing culls at play time, so a sound 10 km away still claims a decoder and
// mixes forever at ~-80 dB. In a large streamed world every distant NPC/emitter
// event burns a voice for nothing. EXPECTED TO FAIL until Inc 2 adds a play-time
// audibility cull. (1 voxel = 1 m — docs/FineVoxelItems.md fine grid: 81 cells
// x 1.23 cm ≈ 1 m per cube.)
// ============================================================================

TEST_F(AudioSystemPcmTest, InaudiblyDistantSoundDoesNotClaimAVoice) {
    audio.playSound3D(wav, glm::vec3(0.0f, 0.0f, -10000.0f), AudioChannel::SFX, 1.0f);
    EXPECT_EQ(audio.activeSoundCount(), 0u)
        << "a sound 10 km from the listener was started anyway — it will mix "
           "inaudibly and hold a decoder until it ends";
}

// ============================================================================
// Music fades. stopMusic() was a hard ma_sound_uninit — a pop on every
// transition. With fadeMs the outgoing track must still be audible early in
// the fade window and gone after it.
// ============================================================================

TEST_F(AudioSystemPcmTest, StopMusicWithFadeDecaysInsteadOfCutting) {
    audio.playMusic(wav, 1.0f);
    renderAndMeasure(audio, 0.1);  // establish playback
    audio.stopMusic(/*fadeMs=*/200);

    // First 100 ms of the fade: must still carry signal (a hard cut = silence).
    StereoRms early = renderAndMeasure(audio, 0.1);
    EXPECT_GT(early.total(), 1e-4)
        << "fade-out was an instant cut — the transition pops";

    // Well past the 200 ms fade: silent, and the faded track reaped by update().
    renderAndMeasure(audio, 0.3);
    StereoRms late = renderAndMeasure(audio, 0.1);
    EXPECT_LT(late.total(), 1e-5) << "fade never completed";

    audio.update(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FALSE(audio.isMusicPlaying());
}

// ============================================================================
// RED — dead asset reference. VoxelInteractionSystem.cpp:573 plays hit.wav on
// furniture activation; the file does not exist, so every activation logs an
// error and plays silence. EXPECTED TO FAIL until the file ships (Inc 3's
// catalog test generalizes this to every referenced asset).
// ============================================================================

// ============================================================================
// Catalog + provenance validation (docs/SoundSystemV2.md §4.1). This is the
// generalization of the hit.wav lesson: every file the catalog references must
// exist, and every shipped audio file must carry a provenance row.
// ============================================================================

namespace {
std::string soundsDirPath() {
    std::string p = soundPath("sounds.json");
    return p.empty() ? "" : std::filesystem::path(p).parent_path().generic_string();
}
} // namespace

TEST(AudioCatalogTest, EveryCatalogFileExistsOnDisk) {
    std::string catalog = soundPath("sounds.json");
    ASSERT_FALSE(catalog.empty()) << "resources/sounds/sounds.json missing";
    nlohmann::json j;
    std::ifstream(catalog) >> j;
    ASSERT_TRUE(j.contains("events"));
    const auto baseDir = std::filesystem::path(catalog).parent_path();

    int fileCount = 0;
    for (const auto& [name, ev] : j["events"].items()) {
        for (const auto& file : ev.value("files", nlohmann::json::array())) {
            ++fileCount;
            EXPECT_TRUE(std::filesystem::exists(baseDir / file.get<std::string>()))
                << "event '" << name << "' references missing file " << file;
        }
    }
    EXPECT_GT(fileCount, 0) << "catalog has no file references at all";
}

TEST(AudioCatalogTest, EveryDiskAudioFileHasProvenance) {
    std::string dir = soundsDirPath();
    ASSERT_FALSE(dir.empty());
    std::string sourcesPath = dir + "/SOURCES.json";
    ASSERT_TRUE(std::filesystem::exists(sourcesPath)) << "SOURCES.json missing";
    nlohmann::json sources;
    std::ifstream(sourcesPath) >> sources;
    ASSERT_TRUE(sources.contains("files"));

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext != ".wav" && ext != ".ogg" && ext != ".mp3" && ext != ".flac") continue;
        std::string rel = std::filesystem::relative(entry.path(), dir).generic_string();
        EXPECT_TRUE(sources["files"].contains(rel))
            << rel << " is shipped without a SOURCES.json provenance row";
    }
}

TEST(AudioCatalogTest, LegacyGameplayFilesAreCoveredByEvents) {
    // The migration contract: every filename the pre-catalog C++ call sites
    // played must be reachable through a catalog event, so switching those
    // sites to playEvent() cannot silently drop a sound.
    std::string catalog = soundPath("sounds.json");
    ASSERT_FALSE(catalog.empty());
    nlohmann::json j;
    std::ifstream(catalog) >> j;

    for (const char* file : {"place.wav", "hit.wav", "axe_chop.wav", "whoosh.wav"}) {
        bool covered = false;
        for (const auto& [name, ev] : j["events"].items()) {
            for (const auto& f : ev.value("files", nlohmann::json::array())) {
                if (f.get<std::string>() == file) { covered = true; break; }
            }
        }
        EXPECT_TRUE(covered) << file << " (played by legacy C++ call sites) has no catalog event";
    }
}

// End-to-end: the registry loads the SHIPPED catalog and renders real audio
// through the real AudioSystem — not a mock of either.
TEST(AudioCatalogTest, RegistryPlaysShippedEventsAudibly) {
    Phyxel::Core::AudioSystem audio;
    Phyxel::Core::AudioSystemConfig cfg;
    cfg.deviceless = true; cfg.sampleRate = kSampleRate; cfg.channels = kChannels;
    ASSERT_TRUE(audio.initialize(cfg));
    audio.update(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Phyxel::Core::SoundRegistry registry;
    registry.setAudioSystem(&audio);
    ASSERT_TRUE(registry.load(soundPath("sounds.json")));
    EXPECT_GE(registry.eventCount(), 5u);

    // Spatial event with a position: must produce signal, panned by position.
    registry.playEvent("chop.impact.trunk", glm::vec3(-5.0f, 0.0f, 0.0f));
    StereoRms rms = renderAndMeasure(audio, 0.3);
    EXPECT_GT(rms.total(), 1e-4) << "catalog event produced no audio";
    EXPECT_GT(rms.left, rms.right) << "spatial catalog event ignored its position";

    // Unknown event: warns, no crash, no signal.
    registry.playEvent("no.such.event", glm::vec3(0.0f));
    audio.shutdown();
}

// Fetched CC0 recordings are mp3 previews (Freesound token auth) — the first
// mp3s in the repo, so pin that miniaudio's mp3 decode path actually renders
// them, spatialized. A file that exists but doesn't decode is the hit.wav
// failure with extra steps.
TEST(AudioCatalogTest, FetchedNatureEventsRenderAudibly) {
    Phyxel::Core::AudioSystem audio;
    Phyxel::Core::AudioSystemConfig cfg;
    cfg.deviceless = true; cfg.sampleRate = kSampleRate; cfg.channels = kChannels;
    ASSERT_TRUE(audio.initialize(cfg));
    audio.update(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Phyxel::Core::SoundRegistry registry;
    registry.setAudioSystem(&audio);
    ASSERT_TRUE(registry.load(soundPath("sounds.json")));

    // Every FILE in every nature pool, individually — playEvent picks pool
    // variants at random, so an event-level check can pass while one variant
    // is broken. Measure the whole clip: field recordings often lead with a
    // second of near-silence, so a first-window RMS check flakes.
    for (const char* ev : {"amb.bird.songbird", "amb.bird.blackbird", "amb.bird.crow",
                           "amb.bird.owl", "amb.insect.cricket", "amb.insect.cicada",
                           "amb.animal.frog"}) {
        const auto* spec = registry.getEvent(ev);
        if (!spec) continue;  // pools shrink if a fetch is re-curated
        for (const std::string& file : spec->files) {
            audio.playSound(file, Phyxel::Core::AudioChannel::Ambience, 1.0f);
            StereoRms rms = renderAndMeasure(audio, 12.0);
            EXPECT_GT(rms.total(), 1e-6)
                << ev << " variant decoded to silence: " << file;
            audio.update(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }
    audio.shutdown();
}

TEST(AudioAssetTest, GameplayReferencedSoundsExistOnDisk) {
    // Files named by C++ gameplay call sites (grep playSound/playSound3D):
    //   place.wav    VoxelInteractionSystem.cpp placeVoxel/Subcube/Microcube
    //   hit.wav      VoxelInteractionSystem.cpp furniture activation
    //   axe_chop.wav Application.cpp chop impact
    //   whoosh.wav   Application.cpp leaf swipe
    for (const char* name : {"place.wav", "hit.wav", "axe_chop.wav", "whoosh.wav"}) {
        EXPECT_FALSE(soundPath(name).empty())
            << "gameplay code references resources/sounds/" << name
            << " but it does not exist — that call site plays silence";
    }
}
