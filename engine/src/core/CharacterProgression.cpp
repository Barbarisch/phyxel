#include "core/CharacterProgression.h"
#include "core/ClassDefinition.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// XP tables (PHB)
// ---------------------------------------------------------------------------

// Index = level (0 unused, 1–20 used)
const int CharacterProgression::XP_THRESHOLDS[21] = {
    0,       // 0 — unused
    0,       // 1
    300,     // 2
    900,     // 3
    2700,    // 4
    6500,    // 5
    14000,   // 6
    23000,   // 7
    34000,   // 8
    48000,   // 9
    64000,   // 10
    85000,   // 11
    100000,  // 12
    120000,  // 13
    140000,  // 14
    165000,  // 15
    195000,  // 16
    225000,  // 17
    265000,  // 18
    305000,  // 19
    355000   // 20
};

// Per-character XP budget thresholds for encounter difficulty
const int CharacterProgression::ENCOUNTER_XP_EASY[20] = {
    25,50,75,125,250,300,350,450,550,600,
    800,1000,1100,1250,1400,1600,2000,2100,2400,2800
};
const int CharacterProgression::ENCOUNTER_XP_MEDIUM[20] = {
    50,100,150,250,500,600,750,900,1100,1200,
    1600,2000,2200,2500,2800,3200,3900,4200,4900,5700
};
const int CharacterProgression::ENCOUNTER_XP_HARD[20] = {
    75,150,225,375,750,900,1100,1400,1600,1900,
    2400,3000,3400,3800,4300,4800,5900,6300,7300,8500
};
const int CharacterProgression::ENCOUNTER_XP_DEADLY[20] = {
    100,200,400,500,1100,1400,1700,2100,2400,2800,
    3600,4500,5100,5700,6400,7200,8800,9500,10900,12700
};

// ---------------------------------------------------------------------------
// XP / level queries
// ---------------------------------------------------------------------------

int CharacterProgression::levelForXP(int xp) {
    for (int lvl = 20; lvl >= 1; --lvl) {
        if (xp >= XP_THRESHOLDS[lvl]) return lvl;
    }
    return 1;
}

int CharacterProgression::xpForLevel(int level) {
    level = std::clamp(level, 1, 20);
    return XP_THRESHOLDS[level];
}

int CharacterProgression::xpToNextLevel(int currentXP) {
    int currentLevel = levelForXP(currentXP);
    if (currentLevel >= 20) return 0;
    return XP_THRESHOLDS[currentLevel + 1] - currentXP;
}

// ---------------------------------------------------------------------------
// Level up
// ---------------------------------------------------------------------------

CharacterProgression::LevelUpResult CharacterProgression::levelUp(
    CharacterSheet& sheet,
    const std::string& classId,
    DiceSystem& dice,
    bool useAverageHP)
{
    LevelUpResult result;
    result.classId = classId;

    const auto* cls = ClassRegistry::instance().getClass(classId);
    if (!cls) {
        LOG_WARN("CharacterProgression", "levelUp: unknown class '{}'", classId);
        return result;
    }

    // Find or create ClassLevel entry
    ClassLevel* classLevel = nullptr;
    for (auto& cl : sheet.classes) {
        if (cl.classId == classId) { classLevel = &cl; break; }
    }
    if (!classLevel) {
        // First level in this class (multiclass entry)
        sheet.classes.push_back({classId, 0, ""});
        classLevel = &sheet.classes.back();
    }

    classLevel->level++;
    int newClassLvl = classLevel->level;
    result.newClassLevel = newClassLvl;
    result.newTotalLevel = sheet.totalLevel();

    // --- HP gain ---
    int conMod = sheet.attributes.constitution.modifier();
    int hpGain = 0;
    if (useAverageHP) {
        // PHB average: ceil((faces + 1) / 2) = (faces / 2) + 1
        hpGain = (cls->hitDieFaces / 2) + 1 + conMod;
    } else {
        auto roll = dice.roll(static_cast<DieType>(cls->hitDieFaces));
        hpGain = roll.dice[0] + conMod;
    }
    hpGain = std::max(1, hpGain);  // always gain at least 1 HP
    sheet.maxHP    += hpGain;
    sheet.currentHP += hpGain;
    result.hpGained = hpGain;

    // --- Hit dice pool ---
    bool poolFound = false;
    for (auto& pool : sheet.hitDicePools) {
        if (pool.classId == classId) {
            pool.total++;
            pool.remaining++;
            poolFound = true;
            break;
        }
    }
    if (!poolFound) {
        HitDicePool pool;
        pool.classId   = classId;
        pool.faces     = cls->hitDieFaces;
        pool.total     = 1;
        pool.remaining = 1;
        sheet.hitDicePools.push_back(pool);
    }

    // --- Class features at this level ---
    auto feats = cls->getFeaturesAtLevel(newClassLvl);
    for (const auto& feat : feats) {
        result.featuresGained.push_back(feat.id);
        sheet.earnedFeatureIds.push_back(feat.id);
    }

    // --- ASI check ---
    if (cls->hasASIAtLevel(newClassLvl)) {
        sheet.availableASIs++;
        result.grantsASI = true;
    }

    // --- Proficiencies (multiclass: only granted at level 1 in that class) ---
    if (newClassLvl == 1) {
        for (auto a : cls->savingThrowProficiencies)
            sheet.savingThrowProficiencies.insert(a);
        for (const auto& p : cls->armorProficiencies)  sheet.armorProficiencies.insert(p);
        for (const auto& p : cls->weaponProficiencies) sheet.weaponProficiencies.insert(p);
        for (const auto& p : cls->toolProficiencies)   sheet.toolProficiencies.insert(p);
    }

    sheet.recalculate();
    result.success = true;
    return result;
}

// ---------------------------------------------------------------------------
// XP awarding
// ---------------------------------------------------------------------------

bool CharacterProgression::awardXP(CharacterSheet& sheet, int xp, DiceSystem& dice,
                                     bool autoLevel, bool useAverageHP) {
    sheet.experiencePoints += xp;

    if (!autoLevel || sheet.classes.empty()) return false;

    int currentLevel  = sheet.totalLevel();
    int targetLevel   = levelForXP(sheet.experiencePoints);
    if (targetLevel <= currentLevel || currentLevel >= 20) return false;

    // Auto-level in first class
    const std::string& primaryClass = sheet.classes[0].classId;
    levelUp(sheet, primaryClass, dice, useAverageHP);
    return true;
}

// ---------------------------------------------------------------------------
// ASI
// ---------------------------------------------------------------------------

bool CharacterProgression::applyASI(CharacterSheet& sheet,
                                      AbilityType primary,
                                      std::optional<AbilityType> secondary) {
    if (sheet.availableASIs <= 0) return false;

    auto applyBonus = [&](AbilityType ability, int bonus) {
        auto& score = sheet.attributes.get(ability);
        // Base score (not counting racial/equipment/temp) cap is 20 for normal advancement
        int currentBase = score.base;
        score.base = std::min(20, currentBase + bonus);
        return score.base - currentBase;  // actual amount applied
    };

    if (!secondary.has_value()) {
        // +2 to primary
        if (sheet.attributes.get(primary).base >= 20) return false;
        applyBonus(primary, 2);
    } else {
        // +1 to primary, +1 to secondary
        bool primaryAtCap   = sheet.attributes.get(primary).base >= 20;
        bool secondaryAtCap = sheet.attributes.get(*secondary).base >= 20;
        if (primaryAtCap && secondaryAtCap) return false;
        applyBonus(primary, 1);
        applyBonus(*secondary, 1);
    }

    sheet.availableASIs--;
    sheet.recalculate();
    return true;
}

// ---------------------------------------------------------------------------
// Short rest
// ---------------------------------------------------------------------------

CharacterProgression::ShortRestResult CharacterProgression::shortRest(
    CharacterSheet& sheet, int hitDiceToSpend, DiceSystem& dice)
{
    ShortRestResult result;
    if (sheet.currentHP <= 0) return result;  // can't short rest while dying

    int toSpend = hitDiceToSpend;

    for (auto& pool : sheet.hitDicePools) {
        if (toSpend <= 0) break;
        while (toSpend > 0 && pool.remaining > 0) {
            auto roll = dice.roll(static_cast<DieType>(pool.faces));
            int conMod = sheet.attributes.constitution.modifier();
            int recovery = std::max(1, roll.dice[0] + conMod);

            int actualHeal = sheet.heal(recovery);
            result.hpRecovered += actualHeal;
            result.hitDiceSpent++;
            pool.remaining--;
            --toSpend;
        }
    }

    result.success = (result.hitDiceSpent > 0);
    return result;
}

// ---------------------------------------------------------------------------
// Long rest
// ---------------------------------------------------------------------------

CharacterProgression::LongRestResult CharacterProgression::longRest(CharacterSheet& sheet) {
    LongRestResult result;

    // Full HP
    int hpRestored   = sheet.maxHP - sheet.currentHP;
    sheet.currentHP  = sheet.maxHP;
    sheet.temporaryHP = 0;
    result.hpRestored = hpRestored;

    // Reset death saves
    sheet.deathSaves.reset();

    // Restore hit dice: recover up to half of total (rounded up), min 1
    int totalHD = totalHitDiceMax(sheet);
    int restoreCount = std::max(1, (totalHD + 1) / 2);

    for (auto& pool : sheet.hitDicePools) {
        int missing = pool.total - pool.remaining;
        if (missing <= 0 || restoreCount <= 0) continue;
        int toRestore = std::min(missing, restoreCount);
        pool.remaining += toRestore;
        restoreCount   -= toRestore;
        result.hitDiceRestored += toRestore;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Hit dice helpers
// ---------------------------------------------------------------------------

int CharacterProgression::totalHitDiceRemaining(const CharacterSheet& sheet) {
    int total = 0;
    for (const auto& pool : sheet.hitDicePools) total += pool.remaining;
    return total;
}

int CharacterProgression::totalHitDiceMax(const CharacterSheet& sheet) {
    int total = 0;
    for (const auto& pool : sheet.hitDicePools) total += pool.total;
    return total;
}

} // namespace Core
} // namespace Phyxel
