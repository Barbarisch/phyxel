// AmbienceDirectorTest — offline-PCM verification of biome soundscapes.
//
// Instrument: each fake biome's bed is a pure sine at a DISTINCT frequency
// (440 / 880 / 220 Hz), written by the test itself; Goertzel energy on the
// rendered mix identifies which bed is actually audible. Assertions are on the
// REAL AudioSystem output, driven through the REAL director — the sampler and
// clock are the only fakes.
//
// Pinned behaviors (docs/SoundSystemV2.md §4.2):
//  - hysteresis: strafing a biome border must not thrash beds
//  - a genuine transition crossfades exactly once
//  - underground overrides the surface biome ("cave")
//  - scatter one-shots fire on their intervals, spatialized
//  - unknown biome = silence, no crash

#include <gtest/gtest.h>
#include "core/AmbienceDirector.h"
#include "core/AudioSystem.h"
#include "core/SoundRegistry.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Phyxel::Core;

namespace {

constexpr uint32_t kSR = 48000;

// ---- tiny WAV writer (mono 16-bit PCM) ----
void writeWavMono16(const std::string& path, const std::vector<float>& samples, uint32_t sr) {
    std::ofstream f(path, std::ios::binary);
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    f.write("RIFF", 4); w32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(1); w16(1); w32(sr); w32(sr * 2); w16(2); w16(16);
    f.write("data", 4); w32(dataBytes);
    for (float s : samples) {
        int16_t v = static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, s)) * 32767.0f);
        f.write(reinterpret_cast<const char*>(&v), 2);
    }
}

void writeSineWav(const std::string& path, float freq, float seconds, float gain = 0.5f) {
    size_t n = static_cast<size_t>(seconds * kSR);
    std::vector<float> s(n);
    for (size_t i = 0; i < n; ++i)
        s[i] = gain * std::sin(2.0f * 3.14159265f * freq * float(i) / float(kSR));
    writeWavMono16(path, s, kSR);
}

// ---- Goertzel: energy at one frequency in an interleaved stereo buffer ----
double goertzel(const std::vector<float>& stereo, size_t frames, float freq) {
    double k = 2.0 * std::cos(2.0 * 3.141592653589793 * freq / double(kSR));
    double s0 = 0, s1 = 0, s2 = 0;
    for (size_t i = 0; i < frames; ++i) {
        double x = 0.5 * (double(stereo[i * 2]) + double(stereo[i * 2 + 1]));
        s0 = x + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}

class AmbienceDirectorPcmTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = (std::filesystem::temp_directory_path() / "phyxel_ambience_test").string();
        std::filesystem::create_directories(dir + "/beds");

        // Distinct-frequency tone beds: the instrument that makes "which bed
        // is playing" measurable. 2 s files, looped by the engine.
        writeSineWav(dir + "/beds/forest.wav", 440.0f, 2.0f);
        writeSineWav(dir + "/beds/desert.wav", 880.0f, 2.0f);
        writeSineWav(dir + "/beds/cave.wav",   220.0f, 2.0f);
        writeSineWav(dir + "/beds/drip.wav",  1760.0f, 0.2f);

        {
            std::ofstream f(dir + "/sounds.json");
            f << R"({"events":{"test.drip":{"files":["beds/drip.wav"],"channel":"Ambience","spatial":true}}})";
        }
        {
            std::ofstream f(dir + "/ambience.json");
            f << R"({"contexts":{
                "Forest": {"bed": "beds/forest.wav"},
                "Desert": {"bed": "beds/desert.wav"},
                "cave":   {"bed": "beds/cave.wav",
                           "scatter": [{"event": "test.drip", "interval": [0.05, 0.1],
                                        "radius": [2, 5], "yOffset": [0, 1]}]}
            }})";
        }

        AudioSystemConfig cfg;
        cfg.deviceless = true;
        cfg.sampleRate = kSR;
        cfg.channels   = 2;
        ASSERT_TRUE(audio.initialize(cfg));
        audio.update(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));

        registry.setAudioSystem(&audio);
        ASSERT_TRUE(registry.load(dir + "/sounds.json"));

        amb.setAudioSystem(&audio);
        amb.setSoundRegistry(&registry);
        ASSERT_TRUE(amb.load(dir + "/ambience.json"));
        amb.setCrossfadeMs(100);   // fast fades so tests settle quickly
        amb.setHysteresisSec(3.0f);

        // Default sampler: Forest everywhere, surface at y=100.
        biome = "Forest";
        surfaceY = 100;
        amb.setBiomeSampler([this](float, float, AmbienceDirector::BiomeSample& out) {
            out.biome = biome;
            out.surfaceY = surfaceY;
            return true;
        });
    }

    void TearDown() override {
        amb.stopAll();
        audio.shutdown();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    // Advance simulated time: director ticks of `step` seconds, rendering the
    // matching PCM between ticks so fades/loops actually progress. Returns the
    // LAST rendered block for spectral checks.
    std::vector<float> run(float seconds, const glm::vec3& listener, float step = 0.1f) {
        std::vector<float> block;
        int ticks = std::max(1, int(seconds / step));
        for (int i = 0; i < ticks; ++i) {
            audio.update(listener, glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
            amb.update(step, listener);
            size_t frames = size_t(step * kSR);
            block.assign(frames * 2, 0.0f);
            size_t got = audio.renderFrames(block.data(), frames);
            lastFrames = got;
        }
        return block;
    }

    std::string dir;
    AudioSystem audio;
    SoundRegistry registry;
    AmbienceDirector amb;
    std::string biome;
    int surfaceY = 100;
    size_t lastFrames = 0;
};

} // namespace

// CONTROL: entering the first context plays its bed, identifiable by frequency.
TEST_F(AmbienceDirectorPcmTest, FirstContextPlaysItsBed) {
    glm::vec3 listener(0.0f, 100.0f, 0.0f);
    auto block = run(1.0f, listener);

    ASSERT_GT(lastFrames, 0u);
    EXPECT_EQ(amb.activeContext(), "Forest");
    EXPECT_EQ(amb.crossfadeCount(), 1);   // the initial entry
    double e440 = goertzel(block, lastFrames, 440.0f);
    double e880 = goertzel(block, lastFrames, 880.0f);
    EXPECT_GT(e440, e880 * 10.0) << "forest bed (440 Hz) not dominant";
}

// A genuine border crossing: exactly ONE crossfade, new bed audible after.
TEST_F(AmbienceDirectorPcmTest, BorderCrossingCrossfadesExactlyOnce) {
    glm::vec3 listener(0.0f, 100.0f, 0.0f);
    run(1.0f, listener);
    ASSERT_EQ(amb.activeContext(), "Forest");

    biome = "Desert";                    // step across the border
    run(1.0f, listener);                 // < 3 s hysteresis: no switch yet
    EXPECT_EQ(amb.activeContext(), "Forest") << "switched before hysteresis elapsed";
    EXPECT_EQ(amb.crossfadeCount(), 1);

    auto block = run(4.0f, listener);    // past hysteresis + fade
    EXPECT_EQ(amb.activeContext(), "Desert");
    EXPECT_EQ(amb.crossfadeCount(), 2) << "border crossing must crossfade exactly once";
    double e440 = goertzel(block, lastFrames, 440.0f);
    double e880 = goertzel(block, lastFrames, 880.0f);
    EXPECT_GT(e880, e440 * 10.0) << "desert bed (880 Hz) not dominant after the fade";
}

// Strafing the border: context alternates every tick — beds must NOT thrash.
TEST_F(AmbienceDirectorPcmTest, BorderStrafingDoesNotThrashBeds) {
    glm::vec3 listener(0.0f, 100.0f, 0.0f);
    run(1.0f, listener);
    ASSERT_EQ(amb.crossfadeCount(), 1);

    for (int i = 0; i < 600; ++i) {      // 60 simulated seconds of flapping
        biome = (i % 2 == 0) ? "Desert" : "Forest";
        audio.update(listener, glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        amb.update(0.1f, listener);
    }
    EXPECT_EQ(amb.crossfadeCount(), 1)
        << "bed flapping while strafing a biome border — hysteresis broken";
    EXPECT_EQ(amb.activeContext(), "Forest");
}

// Deep below the surface the biome no longer matters: cave takes over.
TEST_F(AmbienceDirectorPcmTest, UndergroundOverridesSurfaceBiome) {
    glm::vec3 surface(0.0f, 100.0f, 0.0f);
    run(1.0f, surface);
    ASSERT_EQ(amb.activeContext(), "Forest");

    glm::vec3 deep(0.0f, 50.0f, 0.0f);   // 50 m below surfaceY=100
    auto block = run(4.0f, deep);        // hysteresis + fade
    EXPECT_EQ(amb.activeContext(), "cave");
    double e220 = goertzel(block, lastFrames, 220.0f);
    double e440 = goertzel(block, lastFrames, 440.0f);
    EXPECT_GT(e220, e440 * 10.0) << "cave bed (220 Hz) not dominant underground";
}

// Scatter one-shots fire on their catalog intervals (cave drips at 1760 Hz).
TEST_F(AmbienceDirectorPcmTest, ScatterOneShotsFire) {
    glm::vec3 deep(0.0f, 50.0f, 0.0f);
    run(4.0f, deep);                     // settle into cave
    ASSERT_EQ(amb.activeContext(), "cave");

    // Intervals are 0.05-0.1 s; across 2 s dozens fire. Sum drip-band energy
    // over all blocks (drips are 0.2 s one-shots — a single unlucky last
    // block could miss one).
    double dripEnergy = 0.0, controlEnergy = 0.0;
    for (int i = 0; i < 20; ++i) {
        auto block = run(0.1f, deep);
        dripEnergy += goertzel(block, lastFrames, 1760.0f);
        controlEnergy += goertzel(block, lastFrames, 3123.0f);  // control: empty band
    }
    EXPECT_GT(dripEnergy, controlEnergy * 10.0) << "no drip one-shots audible in the cave";
}

// Day/night gating: a "night"-only scatter event must be mute at noon and
// firing at 23:00. (The L4 counters can show events firing in both periods but
// cannot prove WHICH were suppressed — this is the layer that pins the filter.)
TEST_F(AmbienceDirectorPcmTest, NightScatterIsMuteByDay) {
    // Rewrite the cave context's drip to night-only, reload.
    {
        std::ofstream f(dir + "/ambience.json");
        f << R"({"contexts":{
            "Forest": {"bed": "beds/forest.wav"},
            "cave":   {"bed": "beds/cave.wav",
                       "scatter": [{"event": "test.drip", "interval": [0.05, 0.1],
                                    "when": "night", "radius": [2, 5], "yOffset": [0, 1]}]}
        }})";
    }
    ASSERT_TRUE(amb.load(dir + "/ambience.json"));

    float hour = 12.0f;  // noon
    amb.setTimeProvider([&hour]() { return hour; });

    glm::vec3 deep(0.0f, 50.0f, 0.0f);
    run(4.0f, deep);  // settle into cave
    ASSERT_EQ(amb.activeContext(), "cave");

    // Noon: the night-only drip must NOT fire.
    double dayDrip = 0.0;
    for (int i = 0; i < 20; ++i) {
        auto block = run(0.1f, deep);
        dayDrip += goertzel(block, lastFrames, 1760.0f);
    }

    // 23:00: same context, same intervals — now it must fire.
    hour = 23.0f;
    run(0.5f, deep);  // let countdowns expire under the new clock
    double nightDrip = 0.0;
    for (int i = 0; i < 20; ++i) {
        auto block = run(0.1f, deep);
        nightDrip += goertzel(block, lastFrames, 1760.0f);
    }

    EXPECT_GT(nightDrip, dayDrip * 10.0)
        << "night-only scatter fired equally by day (day=" << dayDrip
        << " night=" << nightDrip << ") — the 'when' filter is not gating";
}

// The biome sampler must be THROTTLED, never per-frame: it queries the
// streaming WorldGenerator, which contends with chunk-gen workers — per-frame
// calls measured 140 FPS -> 7 FPS on a streaming hydrology world (WaterTest
// A/B + empty-contexts bisect, 2026-09-02). This pin fails on per-frame
// sampling (300 calls) and holds the 4 Hz contract.
TEST_F(AmbienceDirectorPcmTest, BiomeSamplerIsThrottledNotPerFrame) {
    int calls = 0;
    amb.setBiomeSampler([&calls](float, float, AmbienceDirector::BiomeSample& out) {
        ++calls;
        out.biome = "Forest";
        out.surfaceY = 100;
        return true;
    });

    // 300 frames at 10 ms = 3.0 s simulated at 100 FPS.
    for (int i = 0; i < 300; ++i) {
        amb.update(0.01f, glm::vec3(0.0f, 100.0f, 0.0f));
    }
    // 0.25 s interval over 3 s = ~12 samples (+1 immediate first call).
    EXPECT_LE(calls, 20) << "sampler called " << calls
                         << "x in 3 s — per-frame sampling collapses streaming-world FPS";
    EXPECT_GE(calls, 8) << "sampler starved (" << calls << " calls) — context would go stale";
}

// A biome with no ambience entry: silence (bed stops), no crash.
TEST_F(AmbienceDirectorPcmTest, UnknownBiomeFadesToSilence) {
    glm::vec3 listener(0.0f, 100.0f, 0.0f);
    run(1.0f, listener);
    ASSERT_EQ(amb.activeContext(), "Forest");

    biome = "Moonscape";                 // not in ambience.json
    auto block = run(5.0f, listener);
    EXPECT_EQ(amb.activeContext(), "Moonscape");
    double e440 = goertzel(block, lastFrames, 440.0f);
    // The 440 bed must be gone (faded out); allow numerical dust.
    double rms = 0.0;
    for (size_t i = 0; i < lastFrames * 2; ++i) rms += double(block[i]) * double(block[i]);
    rms = std::sqrt(rms / double(lastFrames * 2));
    EXPECT_LT(rms, 1e-4) << "unknown biome still renders a bed (RMS " << rms
                         << ", 440Hz energy " << e440 << ")";
}
