#pragma once

#include <glm/vec3.hpp>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Manages player death, respawn delay, and spawn point.
/// Wire into Application: on death → startDeathSequence(), in update() → update(dt),
/// in render → call renderDeathOverlay() when isPlayerDead().
class RespawnSystem {
public:
    void setSpawnPoint(const glm::vec3& pos) { m_spawnPoint = pos; }
    const glm::vec3& getSpawnPoint() const { return m_spawnPoint; }

    void setRespawnDelay(float seconds) { m_respawnDelay = seconds; }
    float getRespawnDelay() const { return m_respawnDelay; }

    bool isPlayerDead() const { return m_playerDead; }
    float getDeathTimer() const { return m_deathTimer; }
    int getDeathCount() const { return m_deathCount; }

    /// Called when the player dies. Starts the respawn countdown.
    void startDeathSequence() {
        m_playerDead = true;
        m_deathTimer = 0.0f;
        m_deathCount++;
    }

    /// Tick: advances timer. When timer >= delay, calls respawnCallback and resets.
    void update(float dt) {
        if (!m_playerDead) return;
        m_deathTimer += dt;
        if (m_deathTimer >= m_respawnDelay) {
            respawn();
        }
    }

    /// Force immediate respawn (e.g. from API/MCP).
    void respawn() {
        m_playerDead = false;
        m_deathTimer = 0.0f;
        if (m_onRespawn) m_onRespawn(m_spawnPoint);
    }

    /// Set callback for when respawn triggers. Receives spawn point.
    void setOnRespawnCallback(std::function<void(const glm::vec3&)> cb) {
        m_onRespawn = std::move(cb);
    }

    nlohmann::json toJson() const {
        return {
            {"playerDead", m_playerDead},
            {"deathTimer", m_deathTimer},
            {"respawnDelay", m_respawnDelay},
            {"deathCount", m_deathCount},
            {"spawnPoint", {{"x", m_spawnPoint.x}, {"y", m_spawnPoint.y}, {"z", m_spawnPoint.z}}}
        };
    }

private:
    glm::vec3 m_spawnPoint{16.0f, 25.0f, 16.0f};
    float m_respawnDelay = 3.0f;
    bool m_playerDead = false;
    float m_deathTimer = 0.0f;
    int m_deathCount = 0;
    std::function<void(const glm::vec3&)> m_onRespawn;
};

} // namespace Core
} // namespace Phyxel
