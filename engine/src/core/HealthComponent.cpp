#include "core/HealthComponent.h"
#include <algorithm>

namespace Phyxel {
namespace Core {

HealthComponent::HealthComponent(float maxHealth)
    : m_health(maxHealth)
    , m_maxHealth(maxHealth)
    , m_alive(true)
    , m_invulnerable(false) {}

float HealthComponent::getHealthPercent() const {
    if (m_maxHealth <= 0.0f) return 0.0f;
    return m_health / m_maxHealth;
}

float HealthComponent::takeDamage(float amount, const std::string& /*sourceId*/) {
    if (!m_alive || m_invulnerable || amount <= 0.0f) return 0.0f;

    float actual = std::min(amount, m_health);
    m_health -= actual;

    if (m_health <= 0.0f) {
        m_health = 0.0f;
        m_alive = false;
        if (m_onDeath) m_onDeath();
    }

    return actual;
}

float HealthComponent::heal(float amount) {
    if (!m_alive || amount <= 0.0f) return 0.0f;

    float actual = std::min(amount, m_maxHealth - m_health);
    m_health += actual;
    return actual;
}

void HealthComponent::setHealth(float health) {
    m_health = std::clamp(health, 0.0f, m_maxHealth);
    m_alive = m_health > 0.0f;
}

void HealthComponent::setMaxHealth(float maxHealth) {
    m_maxHealth = std::max(0.0f, maxHealth);
    m_health = std::min(m_health, m_maxHealth);
    if (m_health <= 0.0f) {
        m_alive = false;
    }
}

void HealthComponent::kill() {
    m_health = 0.0f;
    m_alive = false;
    if (m_onDeath) m_onDeath();
}

void HealthComponent::revive(float healthPercent) {
    healthPercent = std::clamp(healthPercent, 0.01f, 1.0f);
    m_health = m_maxHealth * healthPercent;
    m_alive = true;
}

nlohmann::json HealthComponent::toJson() const {
    return {
        {"health", m_health},
        {"maxHealth", m_maxHealth},
        {"alive", m_alive},
        {"invulnerable", m_invulnerable},
        {"healthPercent", getHealthPercent()}
    };
}

void HealthComponent::fromJson(const nlohmann::json& j) {
    if (j.contains("maxHealth")) m_maxHealth = j["maxHealth"].get<float>();
    if (j.contains("health")) m_health = std::clamp(j["health"].get<float>(), 0.0f, m_maxHealth);
    if (j.contains("alive")) m_alive = j["alive"].get<bool>();
    if (j.contains("invulnerable")) m_invulnerable = j["invulnerable"].get<bool>();
}

} // namespace Core
} // namespace Phyxel
