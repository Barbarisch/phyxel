#include "scene/GaitController.h"
#include "scene/Limb.h"
#include "scene/RagdollCharacter.h"
#include <glm/gtx/norm.hpp>

namespace VulkanCube {
namespace Scene {

GaitController::GaitController(RagdollCharacter* owner) : owner(owner) {
}

void GaitController::addLimb(Limb* limb) {
    LimbState state;
    state.limb = limb;
    state.isSwinging = false;
    state.swingTimer = 0.0f;
    state.swingStartPos = limb->getEndEffectorPosition();
    state.swingTargetPos = state.swingStartPos;
    
    // Initialize target to current position
    limb->setTarget(state.swingStartPos);
    
    limbs.push_back(state);
}

glm::vec3 GaitController::calculateIdealFootPosition(Limb* limb, const glm::vec3& velocity) {
    glm::vec3 homeWorld = limb->getHomePositionWorld();
    
    // If not moving, ideal is just home
    if (glm::length2(velocity) < 0.001f) {
        return homeWorld;
    }
    
    // Lead the target based on velocity
    // We want to land the foot such that it spends equal time behind and in front of the home pos relative to body
    // Target = Home + Velocity * LeadTime
    float leadTime = 0.5f; 
    return homeWorld + velocity * leadTime;
}

void GaitController::update(float deltaTime, const glm::vec3& desiredVelocity) {
    timeSinceLastStep += deltaTime;

    // 1. Update Swinging Limbs
    for (auto& state : limbs) {
        if (state.isSwinging) {
            state.swingTimer += deltaTime;
            float t = state.swingTimer / stepDuration;
            
            if (t >= 1.0f) {
                // Finish swing
                state.isSwinging = false;
                state.limb->setTarget(state.swingTargetPos);
                state.limb->setGrounded(true);
            } else {
                // Parabolic arc
                glm::vec3 currentPos = glm::mix(state.swingStartPos, state.swingTargetPos, t);
                // Add height (parabola: 4 * h * t * (1-t))
                currentPos.y += stepHeight * 4.0f * t * (1.0f - t);
                state.limb->setTarget(currentPos);
            }
        }
        
        // Always update the limb's internal physics/IK
        state.limb->update(deltaTime);
    }

    // 2. Check for new steps
    if (timeSinceLastStep >= stepInterval && !limbs.empty()) {
        // Check the next limb in sequence
        int idx = nextLimbToStep;
        LimbState& state = limbs[idx];
        
        if (!state.isSwinging) {
            glm::vec3 currentPos = state.limb->getEndEffectorPosition();
            glm::vec3 idealPos = calculateIdealFootPosition(state.limb, desiredVelocity);
            
            float distSq = glm::distance2(currentPos, idealPos);
            float threshold = stepDistance * 0.5f;
            
            // If velocity is high, we might want to step sooner
            if (glm::length2(desiredVelocity) > 0.1f && distSq > threshold * threshold) {
                // Trigger step
                state.isSwinging = true;
                state.swingTimer = 0.0f;
                state.swingStartPos = currentPos; // Start from where we are
                state.swingTargetPos = idealPos;  // Go to where we want to be
                state.limb->setGrounded(false);
                
                timeSinceLastStep = 0.0f;
                nextLimbToStep = (nextLimbToStep + 1) % limbs.size();
            } else {
                // If this limb is fine, check if we should skip it and check the next one?
                // For now, just advance to check the next one next frame to keep the ripple going
                // But only if we didn't step.
                // Actually, if we don't step, we should probably check the next one immediately 
                // so we don't wait stepInterval just to find out the next leg is fine too.
                
                int checks = 0;
                while (checks < limbs.size()) {
                    int currIdx = (nextLimbToStep + checks) % limbs.size();
                    LimbState& s = limbs[currIdx];
                    
                    if (!s.isSwinging) {
                        glm::vec3 cPos = s.limb->getEndEffectorPosition();
                        glm::vec3 iPos = calculateIdealFootPosition(s.limb, desiredVelocity);
                        if (glm::distance2(cPos, iPos) > threshold * threshold) {
                            // Found one that needs to step
                            nextLimbToStep = currIdx; // Set this as the current one
                            // Trigger step
                            s.isSwinging = true;
                            s.swingTimer = 0.0f;
                            s.swingStartPos = cPos;
                            s.swingTargetPos = iPos;
                            s.limb->setGrounded(false);
                            
                            timeSinceLastStep = 0.0f;
                            nextLimbToStep = (nextLimbToStep + 1) % limbs.size();
                            break;
                        }
                    }
                    checks++;
                }
            }
        } else {
            // Currently swinging, move to next
            nextLimbToStep = (nextLimbToStep + 1) % limbs.size();
        }
    }
}

} // namespace Scene
} // namespace VulkanCube
