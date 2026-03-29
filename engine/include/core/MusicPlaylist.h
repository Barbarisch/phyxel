#pragma once

#include "core/AudioSystem.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <random>
#include <algorithm>

namespace Phyxel {
namespace Core {

class MusicPlaylist {
public:
    enum class Mode { Sequential, Shuffle };

    void addTrack(const std::string& path) {
        m_tracks.push_back(path);
    }

    void removeTrack(const std::string& path) {
        m_tracks.erase(std::remove(m_tracks.begin(), m_tracks.end(), path), m_tracks.end());
        if (m_currentIndex >= static_cast<int>(m_tracks.size())) {
            m_currentIndex = 0;
        }
    }

    void clear() {
        m_tracks.clear();
        m_currentIndex = 0;
        m_playing = false;
    }

    void setMode(Mode mode) { m_mode = mode; }
    Mode getMode() const { return m_mode; }

    void setVolume(float vol) { m_volume = std::clamp(vol, 0.0f, 1.0f); }
    float getVolume() const { return m_volume; }

    void play(AudioSystem* audio) {
        if (m_tracks.empty()) return;
        m_playing = true;
        if (audio) playCurrentTrack(audio);
    }

    void stop(AudioSystem* audio) {
        m_playing = false;
        if (audio) audio->stopMusic();
    }

    void next(AudioSystem* audio) {
        if (m_tracks.empty()) return;
        advanceTrack();
        if (m_playing && audio) playCurrentTrack(audio);
    }

    void update(AudioSystem* audio) {
        if (!audio || !m_playing || m_tracks.empty()) return;
        if (!audio->isMusicPlaying()) {
            advanceTrack();
            playCurrentTrack(audio);
        }
    }

    const std::vector<std::string>& getTracks() const { return m_tracks; }
    int getCurrentIndex() const { return m_currentIndex; }
    bool isPlaying() const { return m_playing; }

    std::string getCurrentTrack() const {
        if (m_tracks.empty() || m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_tracks.size()))
            return "";
        return m_tracks[m_currentIndex];
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["playing"] = m_playing;
        j["mode"] = (m_mode == Mode::Sequential) ? "sequential" : "shuffle";
        j["volume"] = m_volume;
        j["currentIndex"] = m_currentIndex;
        j["currentTrack"] = getCurrentTrack();
        j["trackCount"] = m_tracks.size();
        j["tracks"] = m_tracks;
        return j;
    }

private:
    void playCurrentTrack(AudioSystem* audio) {
        if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_tracks.size())) {
            audio->playMusic(m_tracks[m_currentIndex], m_volume);
        }
    }

    void advanceTrack() {
        if (m_tracks.empty()) return;
        if (m_mode == Mode::Sequential) {
            m_currentIndex = (m_currentIndex + 1) % static_cast<int>(m_tracks.size());
        } else {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(0, static_cast<int>(m_tracks.size()) - 1);
            int next = dist(gen);
            if (m_tracks.size() > 1 && next == m_currentIndex) {
                next = (next + 1) % static_cast<int>(m_tracks.size());
            }
            m_currentIndex = next;
        }
    }

    std::vector<std::string> m_tracks;
    int m_currentIndex = 0;
    float m_volume = 0.5f;
    Mode m_mode = Mode::Sequential;
    bool m_playing = false;
};

} // namespace Core
} // namespace Phyxel
