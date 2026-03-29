#pragma once

#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

class HealthComponent {
public:
    explicit HealthComponent(float maxHealth = 100.0f);

    // Health queries
    float getHealth() const { return m_health; }
    float getMaxHealth() const { return m_maxHealth; }
    bool isAlive() const { return m_alive; }
    bool isInvulnerable() const { return m_invulnerable; }
    float getHealthPercent() const;

    // Health modification
    float takeDamage(float amount, const std::string& sourceId = "");
    float heal(float amount);
    void setHealth(float health);
    void setMaxHealth(float maxHealth);
    void kill();
    void revive(float healthPercent = 1.0f);

    // Invulnerability
    void setInvulnerable(bool invulnerable) { m_invulnerable = invulnerable; }

    // Death callback (fired once when health reaches 0)
    void setOnDeathCallback(std::function<void()> callback) { m_onDeath = std::move(callback); }

    // Serialization
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    float m_health;
    float m_maxHealth;
    bool m_alive;
    bool m_invulnerable;
    std::function<void()> m_onDeath;
};

} // namespace Core
} // namespace Phyxel
