#pragma once

#include "core/TurnActor.h"
#include "scene/AnimatedVoxelCharacter.h"

#include <cmath>

namespace Phyxel {
namespace Scene {

/// Adapts a live AnimatedVoxelCharacter to Core::ITurnActorBody so a TurnActor
/// can drive it through its normal movement/attack FSM during a turn-based
/// turn. The character integrates its own locomotion in the main update loop;
/// this adapter only feeds control inputs and measures how far it travelled so
/// the TurnActor can debit the movement budget. See docs/TurnBasedCombat.md S4.
///
/// One adapter per character; cheap to keep persistent. Movement tracking is
/// self-resetting (stop() drops the last-position baseline), so it is correct
/// across multiple turns and multiple moves within a turn.
class CharacterTurnBody : public Core::ITurnActorBody {
public:
    explicit CharacterTurnBody(AnimatedVoxelCharacter* character)
        : m_char(character) {}

    void setCharacter(AnimatedVoxelCharacter* c) { m_char = c; m_haveLast = false; }
    AnimatedVoxelCharacter* character() const { return m_char; }

    /// Movement input magnitude fed to the FSM while approaching (run = 1.0).
    void setMoveInput(float m) { m_moveInput = m; }

    glm::vec3 position() const override {
        return m_char ? m_char->getPosition() : glm::vec3(0.0f);
    }

    float stepToward(const glm::vec3& target, float dt) override {
        (void)dt;
        if (!m_char) return 0.0f;

        glm::vec3 pos = m_char->getPosition();

        // Distance actually travelled since the previous drive call (XZ).
        float moved = 0.0f;
        if (m_haveLast) {
            float dx = pos.x - m_lastPos.x;
            float dz = pos.z - m_lastPos.z;
            moved = std::sqrt(dx * dx + dz * dz);
        }
        m_lastPos  = pos;
        m_haveLast = true;

        // Face the target and feed forward input (model fronts +Z; negative
        // forward drives toward the facing direction — same convention as
        // CombatBehavior).
        glm::vec3 to = target - pos; to.y = 0.0f;
        float dist = std::sqrt(to.x * to.x + to.z * to.z);
        if (dist > 1e-4f) {
            m_char->setFacingYaw(std::atan2(to.x / dist, to.z / dist));
            m_char->setControlInput(-m_moveInput, 0.0f, 0.0f);
        }
        return moved;
    }

    void stop() override {
        if (m_char) m_char->setControlInput(0.0f, 0.0f, 0.0f);
        m_haveLast = false;   // drop the baseline so the next move starts fresh
    }

    void beginAttack(const glm::vec3& targetPos) override {
        if (!m_char) return;
        glm::vec3 to = targetPos - m_char->getPosition(); to.y = 0.0f;
        float dist = std::sqrt(to.x * to.x + to.z * to.z);
        if (dist > 1e-4f)
            m_char->setFacingYaw(std::atan2(to.x / dist, to.z / dist));
        m_char->lightAttack();
    }

    bool isAttacking() const override {
        return m_char && m_char->getAnimationState() == AnimatedCharacterState::Attack;
    }

private:
    AnimatedVoxelCharacter* m_char = nullptr;
    glm::vec3 m_lastPos{0.0f};
    bool      m_haveLast  = false;
    float     m_moveInput = 1.0f;
};

} // namespace Scene
} // namespace Phyxel
