#include "core/ProgressionHud.h"

#include "core/CharacterProgression.h"
#include "core/CharacterSheet.h"

#include <algorithm>
#include <string>

namespace Phyxel {
namespace Core {

ProgressionHudValues progressionHud(const CharacterSheet& sheet) {
    ProgressionHudValues v;

    // An unauthored sheet still gets published every frame by the shell, so level 0 must never
    // reach the player's eyes as "Lv 0".
    const int sheetLevel = sheet.totalLevel();
    v.level   = std::clamp(sheetLevel > 0 ? sheetLevel : 1, 1, 20);
    v.xpTotal = std::max(sheet.experiencePoints, 0);

    const int here = CharacterProgression::xpForLevel(v.level);

    if (v.level >= 20) {
        v.xpIntoLevel = std::max(v.xpTotal - here, 0);
        v.xpLevelSpan = 0;
        v.xpToNext    = 0;
        v.percent     = 100.0f;
        v.text        = "Lv 20  -  MAX";
        return v;
    }

    const int next = CharacterProgression::xpForLevel(v.level + 1);
    v.xpLevelSpan  = std::max(next - here, 0);
    v.xpIntoLevel  = std::clamp(v.xpTotal - here, 0, v.xpLevelSpan);
    v.xpToNext     = std::max(next - v.xpTotal, 0);
    v.percent      = v.xpLevelSpan > 0
                   ? std::clamp(100.0f * static_cast<float>(v.xpIntoLevel) /
                                static_cast<float>(v.xpLevelSpan), 0.0f, 100.0f)
                   : 100.0f;

    // The total, not the amount into this level: "1250 / 2700" is the shape every RPG player
    // already knows how to read, and it matches the threshold table the rules use.
    v.text = "Lv " + std::to_string(v.level) + "  -  " +
             std::to_string(v.xpTotal) + " / " + std::to_string(next) + " XP";
    return v;
}

}  // namespace Core
}  // namespace Phyxel
