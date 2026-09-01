#include "scene/behaviors/CombatBehavior.h"
#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "core/EntityRegistry.h"
#include "core/CombatSystem.h"
#include "core/HealthComponent.h"
#include "core/MeleeAnimMapper.h"
#include "core/ItemRegistry.h"
#include "core/RpgItem.h"
#include "utils/Logger.h"

#include <cmath>
#include <random>
#include <cstdlib>

namespace Phyxel {
namespace Scene {

// Resolve the AnimatedVoxelCharacter behind any fighter entity — an NPCEntity
// wraps one; the player is registered as the character directly.
static AnimatedVoxelCharacter* charOf(Entity* e) {
    if (auto* n = dynamic_cast<NPCEntity*>(e)) return n->getAnimatedCharacter();
    return dynamic_cast<AnimatedVoxelCharacter*>(e);
}

const char* CombatBehavior::getStateName() const {
    switch (m_state) {
        case State::Seek:     return "Seek";
        case State::Approach: return "Approach";
        case State::Strafe:   return "Strafe";
        case State::Attack:   return "Attack";
        case State::Recover:  return "Recover";
    }
    return "?";
}

void CombatBehavior::ensureWired(NPCContext& ctx, AnimatedVoxelCharacter* character) {
    if (m_wired || !character) return;
    m_wired = true;

    // Publish our allegiance on the entity so OTHER combatants can see it
    // when they pick targets (Entity::hostileTo). Without this the tag would
    // only be visible to ourselves, which is useless for choosing sides.
    if (ctx.self && !m_faction.empty()) ctx.self->setFaction(m_faction);

    // Resolve the moveset from the held weapon via the same mapper the player
    // uses (unarmed = boxing/elbow/kick when no weapon). Lets enemies wield
    // swords/spears/etc. by setting a weapon id.
    auto& mapper = Core::MeleeAnimMapper::instance();
    if (!mapper.isLoaded())
        mapper.loadConfig("resources/rpg_items/anim/melee_anim_families.json");
    auto& rpgReg = Core::RpgItemRegistry::instance();
    if (rpgReg.count() == 0) rpgReg.loadFromDirectory("resources/rpg_items");
    const Core::ItemDefinition* weaponDef =
        m_weaponId.empty() ? nullptr : Core::ItemRegistry::instance().getItem(m_weaponId);
    const Core::MeleeMovesetDef md = mapper.resolveMovesetDef(weaponDef);
    AnimatedVoxelCharacter::MeleeMoveset ms;
    ms.lightChain      = md.lightChain;
    ms.heavy           = md.heavy;
    ms.block           = md.block;
    ms.attackRate      = md.attackRate;
    ms.chainWindowFrac = md.chainWindowFrac;
    ms.blockHoldFrac   = md.blockHoldFrac;
    character->setMoveset(std::move(ms));
    LOG_DEBUG("CombatAI", "{} moveset family '{}' (weapon '{}')",
              ctx.selfId, md.family, m_weaponId.empty() ? "(none)" : m_weaponId);
    // The visible held weapon is managed by the host (Application::updateNpcHeldItems),
    // which uses the same item template + grip orientation as the player's held item.

    // NPC swings land damage at the clip's hit frame, exactly like the player
    // (Application::createAnimatedCharacter). attackerEntity is the registered
    // NPCEntity so CombatSystem excludes the attacker from its own cone; the
    // target's dodge i-frames (isDodgeInvulnerable) make the hit whiff.
    Core::CombatSystem*  combat = ctx.combatSystem;
    Core::EntityRegistry* reg   = ctx.entityRegistry;
    Scene::Entity*        self  = ctx.self;
    const std::string     selfId = ctx.selfId;
    const float dmg = m_attackDamage, reach = m_attackReach;
    if (combat && reg && self) {
        character->setOnHitFrame([combat, reg, character, self, selfId, dmg, reach]() {
            Core::CombatSystem::AttackParams p;
            p.attackerId     = selfId;
            p.attackerEntity = self;
            // Originate the hit at the swinging hand so it connects on visual contact.
            p.attackerPos    = character->getAttackOrigin();
            const float yaw  = character->getYaw();   // visual front is +Z at yaw 0
            p.attackerForward = glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
            p.damage = dmg;
            p.reach  = reach;
            p.coneAngleDeg = 150.0f;  // origin already at the hand
            auto events = combat->performAttack(p, *reg);
            for (const auto& ev : events) {
                // Plain {} only — the logger prints printf specs literally and
                // shifts args (5th instance of this bug in the codebase).
                LOG_INFO("CombatAI", "{} hit {} for {} dmg{}",
                         selfId, ev.targetId, ev.actualDamage, ev.killed ? " (killed)" : "");
            }
        });
    }
}

// Uniform [0,1). Thread-local so 400 combatants rolling every tick don't
// contend on a shared generator.
static float frand01() {
    static thread_local std::mt19937 rng(std::random_device{}());
    static thread_local std::uniform_real_distribution<float> d(0.0f, 1.0f);
    return d(rng);
}

std::string CombatBehavior::acquireTarget(NPCContext& ctx, const glm::vec3& selfPos) const {
    if (!ctx.entityRegistry) return "";
    // Nearest LIVE opponent that isn't us — the player ("animated") OR another
    // combat NPC ("npc"), so two AI fighters will duel each other.
    std::string best;
    float bestD2 = m_aggroRange * m_aggroRange;
    auto consider = [&](const char* type) {
        for (const auto& [id, e] : ctx.entityRegistry->getEntitiesByType(type)) {
            if (!e || e == ctx.self) continue;          // skip self
            const auto* hp = e->getHealthComponent();
            if (!hp || !hp->isAlive()) continue;         // skip dead / healthless
            // FACTION: never target an ally. The header always promised this
            // ("combat NPCs only target entities whose faction differs") but
            // the check was missing, so any group fight was a free-for-all —
            // a 20v20 had everyone hitting their nearest neighbour regardless
            // of side. Allegiance lives on the ENTITY so we can read theirs.
            if (!ctx.self->hostileTo(*e)) continue;
            const glm::vec3 d = e->getPosition() - selfPos;
            float d2 = d.x * d.x + d.z * d.z;
            // INTELLIGENCE shapes target choice: a bright fighter discounts
            // distance for wounded enemies and finishes them, a dull one just
            // swings at whatever is closest. (Scoring in squared-distance
            // space, so a badly hurt foe "feels" nearer than it is.)
            if (m_intelligence >= 12) {
                if (const auto* hp = e->getHealthComponent()) {
                    if (hp->getMaxHealth() > 0.0f) {
                        const float frac = hp->getHealth() / hp->getMaxHealth();
                        d2 *= (0.35f + 0.65f * frac);   // hurt => more attractive
                    }
                }
            }
            if (d2 < bestD2) { bestD2 = d2; best = id; }
        }
    };
    consider("animated");
    consider("npc");
    return best;
}

void CombatBehavior::update(float dt, NPCContext& ctx) {
    if (!ctx.self) return;
    auto* npc = dynamic_cast<NPCEntity*>(ctx.self);
    AnimatedVoxelCharacter* character = npc ? npc->getAnimatedCharacter() : nullptr;
    ensureWired(ctx, character);

    // Dead or knocked out — stop fighting; the death/KO state owns the character.
    if (character && character->isIncapacitated()) return;

    if (m_cooldownTimer > 0.0f) m_cooldownTimer -= dt;

    const glm::vec3 selfPos = ctx.self->getPosition();

    // Validate / re-acquire — drop a missing OR dead target (so killing the
    // opponent counts as losing the target, i.e. victory).
    Scene::Entity* cur = (ctx.entityRegistry && !m_targetId.empty())
                             ? ctx.entityRegistry->getEntity(m_targetId) : nullptr;
    const bool curDead = cur && (!cur->getHealthComponent() ||
                                 !cur->getHealthComponent()->isAlive());
    if (!cur || curDead) m_targetId = acquireTarget(ctx, selfPos);

    Scene::Entity* target = (ctx.entityRegistry && !m_targetId.empty())
                                ? ctx.entityRegistry->getEntity(m_targetId) : nullptr;

    // Helper: feed the character control inputs (drives the full FSM, unlike
    // setMoveVelocity which bypasses updateStateMachine). forward<0 = move
    // toward facing (W), forward>0 = backward (S), strafe = sidestep.
    auto drive = [&](float forward, float strafe) {
        if (character) character->setControlInput(forward, 0.0f, strafe);
        else           ctx.self->setMoveVelocity(glm::vec3(0.0f));
    };

    if (!target) {                       // --- no live enemy ---
        // If we'd been fighting and the last foe is gone, we won — celebrate.
        if (m_hadTarget && character && !character->isCelebrating())
            character->celebrate("taunt");
        if (character && character->isCelebrating()) return;  // keep celebrating
        m_state = State::Seek;
        drive(0.0f, 0.0f);
        return;
    }
    m_hadTarget = true;

    glm::vec3 toTarget = target->getPosition() - selfPos;
    toTarget.y = 0.0f;
    const float dist = glm::length(toTarget);
    const glm::vec3 dir = dist > 1e-4f ? toTarget / dist : glm::vec3(0.0f, 0.0f, 1.0f);

    // ── TACTICAL LAYER ──────────────────────────────────────────
    // Re-evaluated on an INTELLIGENCE-scaled cadence: a dull soldier keeps
    // doing whatever it was doing for over a second, a sharp one reconsiders
    // several times a second. Between evaluations the previous intent stands,
    // which is what makes low-INT troops look committed and stupid.
    m_thinkTimer -= dt;
    if (m_coverCooldown > 0.0f) m_coverCooldown -= dt;

    if (m_thinkTimer <= 0.0f) {
        m_thinkTimer = reactionDelay();

        const auto* hc = ctx.self->getHealthComponent();
        const float hpFrac = (hc && hc->getMaxHealth() > 0.0f)
                                 ? hc->getHealth() / hc->getMaxHealth() : 1.0f;

        // 1) Squad order — obeyed only as far as intelligence allows. A dim
        //    trooper hears "fall back" and charges anyway.
        AI::CommandStructure::Order order = AI::CommandStructure::Order::Advance;
        bool obeying = false;
        if (m_command) {
            order = m_command->orderFor(ctx.selfId);
            obeying = (frand01() < obedience());
            if (order != AI::CommandStructure::Order::Advance) {
                if (obeying) ++m_ordersObeyed; else ++m_ordersIgnored;
            }
        }

        // 2) Cover: worth taking when hurt, or when ordered to hold, and only
        //    if this NPC is disciplined enough to think of it.
        const bool wantsCover =
            m_chunks && (hpFrac < 0.6f ||
                         (obeying && order == AI::CommandStructure::Order::Hold));
        if (wantsCover && m_coverCooldown <= 0.0f && frand01() < coverDiscipline()) {
            m_coverCooldown = 2.5f;
            auto spot = AI::TacticalSpace::findCover(*m_chunks, selfPos,
                                                     target->getPosition(), 14.0f);
            if (spot.found) {
                m_takingCover = true;
                m_coverPos = spot.position;
                m_intent = "cover";
                ++m_coverTaken;
            } else {
                // Wanted cover, the ground had none. Worth counting separately:
                // it is the difference between "the layer never fires" and
                // "the layer fires and the battlefield is open".
                ++m_coverDenied;
            }
        }

        // 3) Orders that override the default charge.
        if (obeying && !m_takingCover) {
            switch (order) {
                case AI::CommandStructure::Order::FallBack:
                    m_intent = "fall_back";
                    break;
                case AI::CommandStructure::Order::Flank:
                    m_intent = "flank";
                    break;
                case AI::CommandStructure::Order::Hold:
                    // Anchor on the ground we are standing on the moment the
                    // order lands. Without an anchor "hold" was a label with no
                    // behaviour — the soldier reported holding while charging.
                    if (!m_holding) { m_holdAnchor = selfPos; m_holding = true; }
                    m_intent = "hold";
                    break;
                default:
                    m_intent = "engage";
                    break;
            }
            if (order != AI::CommandStructure::Order::Hold) m_holding = false;
        } else if (!m_takingCover) {
            m_intent = "engage";   // free-lancing: charge the nearest foe
            m_holding = false;
        }
    }

    // Drop cover once healthy again, or once we have arrived and the threat
    // can no longer see us (job done — resume the fight from here).
    if (m_takingCover) {
        glm::vec3 toCover = m_coverPos - selfPos; toCover.y = 0.0f;
        const float coverDist = glm::length(toCover);
        if (coverDist < 1.2f) {
            // Arrived. Hold the cover we just paid to reach — without anchoring
            // here, the very next approach branch would charge straight back
            // out of it and the whole move would be wasted.
            m_takingCover = false;
            m_holdAnchor  = selfPos;
            m_holding     = true;
            m_intent      = "hold";
        } else {
            const glm::vec3 cdir = toCover / std::max(coverDist, 1e-4f);
            if (character) {
                character->setFacingYaw(std::atan2(cdir.x, cdir.z));
                character->setControlInput(-m_moveSpeed, 0.0f, 0.0f);
            }
            return;   // moving to cover is this frame's whole job
        }
    }

    // FALL BACK: run for the rally point instead of fighting.
    if (std::string(m_intent) == "fall_back" && m_command) {
        if (const auto* sq = m_command->squadOf(ctx.selfId)) {
            glm::vec3 toRally = sq->rally - selfPos; toRally.y = 0.0f;
            const float d = glm::length(toRally);
            if (d > 2.0f && character) {
                const glm::vec3 rdir = toRally / d;
                character->setFacingYaw(std::atan2(rdir.x, rdir.z));
                character->setControlInput(-m_moveSpeed, 0.0f, 0.0f);
                return;
            }
        }
    }

    // FLANK: approach on an arc rather than straight down the enemy's front.
    if (std::string(m_intent) == "flank" && dist > m_attackRange * 2.0f && character) {
        const glm::vec3 side(-dir.z, 0.0f, dir.x);
        const glm::vec3 arc = glm::normalize(dir * 0.6f + side * m_strafeSign * 0.8f);
        character->setFacingYaw(std::atan2(arc.x, arc.z));
        character->setControlInput(-m_moveSpeed, 0.0f, 0.0f);
        return;
    }

    // Lose the target once it leaves aggro range (+hysteresis).
    if (dist > m_aggroRange * 1.3f) {
        m_targetId.clear();
        m_state = State::Seek;
        drive(0.0f, 0.0f);
        return;
    }

    // Always face the target (model fronts +Z, so yaw = atan2(x, z)). This also
    // aims the hit-frame cone. During movement the input branch keeps this yaw
    // (only the external-velocity path would override it, which we don't use).
    if (character) character->setFacingYaw(std::atan2(dir.x, dir.z));

    // Already mid-roll? Let it finish — the dodge owns movement + i-frames.
    if (character && character->isDodging()) return;

    // --- Evade: roll clear when the target is swinging at us within reach ---
    // Gated by a dodge cooldown + reaction chance + one-per-swing so fights
    // actually resolve instead of becoming a perfect-dodge stalemate.
    if (m_dodgeCdTimer > 0.0f) m_dodgeCdTimer -= dt;
    AnimatedVoxelCharacter* targetChar = charOf(target);
    const bool targetSwinging = targetChar &&
        targetChar->getAnimationState() == AnimatedCharacterState::Attack;
    if (!targetSwinging) {
        m_evadedThisSwing = false;            // reset for the next swing
    } else if (character && dist < m_evadeRange && m_dodgeCdTimer <= 0.0f &&
               !m_evadedThisSwing &&
               (std::rand() / static_cast<float>(RAND_MAX)) < m_evadeChance) {
        glm::vec2 perp(-dir.z, dir.x);        // sidestep-roll around the attacker
        if (std::rand() % 2) perp = -perp;
        character->dodge(perp);
        m_dodgeCdTimer  = m_dodgeCooldown;
        m_evadedThisSwing = true;
        return;
    }

    const bool attacking = character &&
        character->getAnimationState() == AnimatedCharacterState::Attack;

    // Brief back-off after a swing (spacing — the souls "hit and reposition").
    if (m_recoverTimer > 0.0f && !attacking) {
        m_recoverTimer -= dt;
        m_state = State::Recover;
        drive(0.6f, 0.0f);   // forward>0 = step backward, away from the target
        return;
    }

    // ── APPROACH ROUTING ────────────────────────────────────────
    // Walk straight when the straight line is actually WALKABLE; ask the shared
    // async pathfinder only when it is not. Before this, approach was always a
    // straight line and any wall defeated it outright: the Redoubt horde walked
    // into the outside of a fort, stacked against the face and was shot down
    // without ever finding the open gate.
    //
    // The gate is deliberately a WALKABILITY test, not line-of-sight. A
    // defender on a parapet is plainly visible while the ground route to them
    // is blocked, so a sight check would keep reporting "just go straight".
    if (dist > m_attackRange && m_chunks) {
        // Take the path service from the CONTEXT, not from a pointer cached at
        // spawn. NPCManager::buildNavGrid() is what creates and starts the
        // service, and it necessarily runs AFTER the scene (and its NPCs) load —
        // so anything captured at spawn time is null forever. That is precisely
        // how two fighters ended up standing at a wall face for 100 s without
        // once stepping sideways toward the gap.
        if (!m_pathService && ctx.pathService) m_pathService = ctx.pathService;

        if (m_repathTimer > 0.0f) m_repathTimer -= dt;

        const glm::vec3 goal = target->getPosition();
        m_routeBlocked = !AI::TacticalSpace::directRouteWalkable(*m_chunks, selfPos, goal);

        // The goal moved far from what the current path was built for: that
        // path leads somewhere the target has left.
        if (!m_pathWaypoints.empty() && glm::length(goal - m_pathGoal) > 6.0f) {
            m_pathWaypoints.clear();
            m_pathIdx = 0;
        }

        // Collect a finished async result.
        if (m_pathHandle != 0 && m_pathService) {
            Core::NavGraph::PathResult res;
            if (m_pathService->tryGetResult(m_pathHandle, res)) {
                m_pathHandle = 0;
                if (res.found && res.waypoints.size() > 1) {
                    m_pathWaypoints = std::move(res.waypoints);
                    m_pathIdx = 1;            // [0] is where we already stand
                    m_pathGoal = goal;
                }
            }
        }

        if (!m_routeBlocked) {
            // Open ground: drop any path and charge. Costs nothing, and keeps
            // 200 fighters in a field from generating a single A* query.
            m_pathWaypoints.clear();
            m_pathIdx = 0;
        } else {
            if (m_pathIdx < m_pathWaypoints.size()) {
                // Follow the route. Advance past waypoints we have reached.
                glm::vec3 wp = m_pathWaypoints[m_pathIdx];
                glm::vec3 toWp = wp - selfPos; toWp.y = 0.0f;
                while (glm::length(toWp) < 1.2f && m_pathIdx + 1 < m_pathWaypoints.size()) {
                    ++m_pathIdx;
                    wp = m_pathWaypoints[m_pathIdx];
                    toWp = wp - selfPos; toWp.y = 0.0f;
                }
                const float wpDist = glm::length(toWp);
                if (wpDist > 0.35f && character) {
                    const glm::vec3 wdir = toWp / wpDist;
                    character->setFacingYaw(std::atan2(wdir.x, wdir.z));
                    character->setControlInput(-m_moveSpeed, 0.0f, 0.0f);
                    m_state = State::Approach;
                    return;                    // routing owns this frame
                }
                if (m_pathIdx + 1 >= m_pathWaypoints.size()) {
                    m_pathWaypoints.clear();   // arrived at the end of the route
                    m_pathIdx = 0;
                }
            } else if (m_pathHandle == 0 && m_pathService && m_repathTimer <= 0.0f) {
                // Blocked with no route: ask for one. Nearer fighters win the
                // queue — they are the ones the player is watching, and a
                // 400-body battle must not starve them behind distant ranks.
                Core::NavAgentProfile prof;   // default: 2 tall, 1 step, 4 drop
                const int priority = static_cast<int>(1000.0f - std::min(dist, 999.0f));
                m_pathHandle = m_pathService->requestPath(prof, selfPos, goal, priority);
                m_repathTimer = 1.5f;          // don't hammer the queue
            }
        }
    }

    if (dist > m_attackRange) {           // --- Approach ---
        // HOLD means hold: a squad told to stand its ground gives up at most a
        // couple of paces of the ground it was ordered to keep, and makes the
        // enemy come to it. Chasing would turn every "hold the ridge" into the
        // same general charge.
        if (m_holding) {
            glm::vec3 drift = selfPos - m_holdAnchor; drift.y = 0.0f;
            if (glm::length(drift) > 2.5f) {
                m_state = State::Strafe;
                drive(0.0f, 0.0f);        // stand fast, already facing the target
                return;
            }
        }
        m_state = State::Approach;
        drive(-m_moveSpeed, 0.0f);        // run toward the target to close fast
        return;
    }

    // In range.
    if (!attacking && m_cooldownTimer <= 0.0f && character) {   // --- Attack ---
        m_state = State::Attack;
        const float roll = std::rand() / static_cast<float>(RAND_MAX);
        if (roll < 0.30f) {
            character->heavyAttack();       // committed strong attack
            m_comboTimer = 0.0f;
        } else {
            character->lightAttack();       // start a light combo...
            // ...and keep feeding inputs for a bit so it CHAINS through the
            // moveset (boxing->elbow->kick / sword light1->2->3) instead of
            // replaying the first link every cooldown.
            m_comboTimer = 0.45f + (std::rand() / static_cast<float>(RAND_MAX)) * 0.85f;
        }
        m_cooldownTimer = m_attackCooldown;
        m_recoverTimer  = m_recoverTime;
        drive(0.0f, 0.0f);
    } else if (attacking) {               // mid-swing: hold ground (don't cancel)
        if (m_comboTimer > 0.0f) {        // continue the light chain
            m_comboTimer -= dt;
            character->lightAttack();      // buffered; FSM advances at the chain window
        }
        drive(0.0f, 0.0f);
    } else {                              // --- in range, waiting on cooldown ---
        // Hold position and face the target. (Continuous circle-strafing made
        // two AIs orbit-drift across the map and shifted the target out of reach
        // mid-swing — repositioning is a later polish item; a stable square-up
        // gives reliable trade-blows-and-dodge exchanges.)
        m_state = State::Strafe;
        drive(0.0f, 0.0f);
    }
}

} // namespace Scene
} // namespace Phyxel
