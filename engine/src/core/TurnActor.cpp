#include "core/TurnActor.h"

#include <cmath>

namespace Phyxel {
namespace Core {

void TurnActor::begin(ITurnActorBody* body, ActionBudget* budget) {
    m_body            = body;
    m_budget          = budget;
    m_activity        = Activity::Idle;
    m_pendingFeet     = 0.0f;
    m_attackSawActive = false;
    m_attackTimer     = 0.0f;
}

void TurnActor::end() {
    if (m_body) m_body->stop();
    m_body     = nullptr;
    m_budget   = nullptr;
    m_activity = Activity::Idle;
}

float TurnActor::horizontalDist(const glm::vec3& a, const glm::vec3& b) const {
    float dx = a.x - b.x;
    float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

void TurnActor::stopAndIdle() {
    if (m_body) m_body->stop();
    m_activity    = Activity::Idle;
    m_pendingFeet = 0.0f;
}

bool TurnActor::inReach(const glm::vec3& targetPos, float reachFeet) const {
    if (!m_body) return false;
    return horizontalDist(m_body->position(), targetPos) <= feetToUnits(reachFeet) + 1e-4f;
}

bool TurnActor::requestMove(const glm::vec3& target) {
    if (!isBound() || isBusy()) return false;
    if (!m_budget->canMove())   return false;

    m_moveTarget  = target;
    m_pendingFeet = 0.0f;
    m_activity    = Activity::Moving;
    return true;
}

bool TurnActor::requestAttack(const glm::vec3& targetPos, float reachFeet) {
    if (!isBound() || isBusy()) return false;
    if (!m_budget->canAct())    return false;
    if (!inReach(targetPos, reachFeet)) return false;

    m_budget->spendAction();
    m_body->beginAttack(targetPos);
    m_activity        = Activity::Attacking;
    m_attackSawActive = false;
    m_attackTimer     = 0.0f;
    return true;
}

void TurnActor::tick(float dt) {
    if (!isBound()) return;

    switch (m_activity) {
        case Activity::Idle:
            break;

        case Activity::Moving: {
            // Arrived?
            if (horizontalDist(m_body->position(), m_moveTarget) <= kArriveEpsUnits) {
                stopAndIdle();
                break;
            }
            // Out of movement budget?
            if (!m_budget->canMove()) {
                stopAndIdle();
                break;
            }
            // Advance and debit budget by the distance actually travelled.
            float movedUnits = m_body->stepToward(m_moveTarget, dt);
            m_pendingFeet += unitsToFeet(movedUnits);
            int wholeFeet = static_cast<int>(std::floor(m_pendingFeet));
            if (wholeFeet > 0) {
                int spent = m_budget->spendMovement(wholeFeet);
                m_pendingFeet -= static_cast<float>(spent);
                if (spent < wholeFeet) {   // budget hit zero mid-step
                    stopAndIdle();
                }
            }
            break;
        }

        case Activity::Attacking: {
            m_attackTimer += dt;
            if (m_body->isAttacking()) {
                m_attackSawActive = true;
            } else if (m_attackSawActive || m_attackTimer >= kAttackTimeoutSec) {
                // The swing has started and finished (or we waited too long for
                // it to register) — the attack is done, turn may advance.
                m_activity = Activity::Idle;
            }
            break;
        }
    }
}

} // namespace Core
} // namespace Phyxel
