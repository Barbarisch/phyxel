#include "core/SpellVfxMapper.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {

namespace {

// Set the primary emissive color on every emission (per-spell palette).
void recolor(VfxComposition& c, const glm::vec3& color) {
    for (auto& e : c.emissions) {
        switch (e.archetype) {
            case VfxArchetypeKind::Burst:      e.burst.color = color; break;
            case VfxArchetypeKind::Projectile: e.projectile.color = color; break;
            case VfxArchetypeKind::Beam:       e.beam.color = color; break;
            case VfxArchetypeKind::Field:      e.field.color = color; break;
        }
    }
}

// Build a single-burst composition (used for touch/heal-style spells).
VfxComposition oneBurst(const glm::vec3& color, int count, float speed, float upBias,
                        float lifetime, float size, VfxAnchorKind anchor) {
    VfxComposition c;
    VfxEmissionSpec e;
    e.archetype = VfxArchetypeKind::Burst;
    e.originAnchor = anchor;
    e.burst.color = color; e.burst.count = count; e.burst.speed = speed;
    e.burst.upBias = upBias; e.burst.lifetime = lifetime; e.burst.size = size;
    e.burst.intensity = 1.7f;
    c.emissions.push_back(e);
    return c;
}

// Brighten a color toward white for higher tiers (more "refined"/mastered look).
glm::vec3 tierTint(const glm::vec3& base, int tier) {
    float t = std::min(0.4f, 0.08f * static_cast<float>(std::max(0, tier)));
    return base * (1.0f - t) + glm::vec3(1.0f) * t;
}

// Scale a composition's params by the gameplay modifiers (the heart of Layer 3).
void applyModifiers(VfxComposition& c, const VfxSpellModifiers& mods) {
    float amt        = std::max(0.25f, mods.power) * (mods.crit ? 1.4f : 1.0f);
    float sizeScale  = std::sqrt(amt);                 // size/radius grow sublinearly
    float countScale = std::max(0.5f, amt);            // amount grows ~linearly
    int   tierBonus  = std::max(0, mods.tier);

    for (auto& e : c.emissions) {
        switch (e.archetype) {
            case VfxArchetypeKind::Burst: {
                int n = static_cast<int>(std::lround(e.burst.count * countScale)) + tierBonus * 8;
                e.burst.count     = std::clamp(n, 4, 400);   // cap to bound GPU overdraw
                e.burst.size     *= sizeScale;
                e.burst.speed    *= (0.85f + 0.15f * amt);
                e.burst.intensity = std::min(3.0f, e.burst.intensity * (1.0f + 0.08f * tierBonus) * (mods.crit ? 1.2f : 1.0f));
                e.burst.color     = tierTint(e.burst.color, tierBonus);
                break;
            }
            case VfxArchetypeKind::Projectile: {
                if (mods.rangeUnits > 0.0f) e.projectile.maxDistance = mods.rangeUnits;
                e.projectile.coreSize       *= sizeScale;
                e.projectile.lightRadius    *= (0.8f + 0.2f * sizeScale);
                e.projectile.lightIntensity  = std::min(3.5f, e.projectile.lightIntensity * (mods.crit ? 1.25f : 1.0f));
                e.projectile.trailRate      *= (0.8f + 0.2f * amt);
                e.projectile.color           = tierTint(e.projectile.color, tierBonus);
                break;
            }
            case VfxArchetypeKind::Beam: {
                e.beam.thickness *= sizeScale;
                e.beam.density   *= (0.9f + 0.1f * amt);
                e.beam.intensity  = std::min(3.0f, e.beam.intensity * (1.0f + 0.06f * tierBonus) * (mods.crit ? 1.2f : 1.0f));
                e.beam.color      = tierTint(e.beam.color, tierBonus);
                break;
            }
            case VfxArchetypeKind::Field: {
                e.field.radius   *= (0.85f + 0.15f * sizeScale);
                e.field.density   = std::min(220.0f, e.field.density * countScale);
                e.field.intensity = std::min(3.0f, e.field.intensity * (1.0f + 0.06f * tierBonus));
                e.field.color     = tierTint(e.field.color, tierBonus);
                break;
            }
        }
    }
}

} // namespace

VfxComposition resolveSpellVfx(const std::string& spellId, const VfxSpellModifiers& mods) {
    VfxComposition c;

    // ---- Per-spell base composition (Layer 2: spell -> archetype(s) + palette) ----
    // Most reuse a generic archetype structure (buildTestSpell) then recolor.
    if (spellId == "fireball") {
        c = buildTestSpell("t_firebolt");          // projectile -> explosive burst
        recolor(c, glm::vec3(1.0f, 0.4f, 0.08f));
    } else if (spellId == "fire_bolt") {
        c = buildTestSpell("t_firebolt");
        recolor(c, glm::vec3(1.0f, 0.55f, 0.15f));
    } else if (spellId == "magic_missile") {
        c = buildTestSpell("t_magicmissile");
        recolor(c, glm::vec3(0.5f, 0.25f, 1.0f));
    } else if (spellId == "eldritch_blast") {
        c = buildTestSpell("t_lightningbolt");     // beam
        recolor(c, glm::vec3(0.3f, 0.95f, 0.55f));
    } else if (spellId == "lightning_bolt") {
        c = buildTestSpell("t_lightningbolt");
        recolor(c, glm::vec3(0.8f, 0.9f, 1.0f));
    } else if (spellId == "burning_hands") {
        c = buildTestSpell("t_conecold");          // cone shape
        recolor(c, glm::vec3(1.0f, 0.45f, 0.1f));
    } else if (spellId == "cone_of_cold") {
        c = buildTestSpell("t_conecold");
        recolor(c, glm::vec3(0.6f, 0.85f, 1.0f));
    } else if (spellId == "thunderwave") {
        c = buildTestSpell("t_thunderwave");
        recolor(c, glm::vec3(0.8f, 0.85f, 1.0f));
    } else if (spellId == "moonbeam") {
        c = buildTestSpell("t_moonbeam");
        recolor(c, glm::vec3(0.7f, 0.8f, 1.0f));
    } else if (spellId == "spirit_guardians") {
        c = buildTestSpell("t_spiritguardians");
        recolor(c, glm::vec3(0.9f, 0.85f, 0.5f));
    } else if (spellId == "wall_of_fire") {
        c = buildTestSpell("t_walloffire");
        recolor(c, glm::vec3(1.0f, 0.4f, 0.08f));
    } else if (spellId == "chain_lightning") {
        c = buildTestSpell("t_chainlightning");
        recolor(c, glm::vec3(0.7f, 0.85f, 1.0f));
    } else if (spellId == "cure_wounds" || spellId == "healing_word") {
        // Rising golden motes at the target.
        c = oneBurst(glm::vec3(1.0f, 0.95f, 0.4f), 55, 2.5f, 0.8f, 1.3f, 0.13f, VfxAnchorKind::Target);
    } else if (spellId == "shield") {
        // Brief protective blue bubble at the caster.
        VfxEmissionSpec e;
        e.archetype = VfxArchetypeKind::Field;
        e.originAnchor = VfxAnchorKind::Caster;
        e.field.shape = VfxShape::Sphere; e.field.radius = 2.0f; e.field.duration = 1.2f;
        e.field.density = 80.0f; e.field.thickness = 0.13f; e.field.intensity = 1.7f;
        e.field.color = glm::vec3(0.3f, 0.6f, 1.0f); e.field.light = true;
        c.emissions.push_back(e);
    } else {
        // Unknown spell: a generic sphere burst at the target.
        c = oneBurst(glm::vec3(0.8f, 0.8f, 0.9f), 50, 6.0f, 0.2f, 0.8f, 0.18f, VfxAnchorKind::Target);
    }
    c.name = spellId;

    // ---- Layer 3 scaling ----
    applyModifiers(c, mods);
    return c;
}

} // namespace Phyxel
