#pragma once

#include <string>

namespace Phyxel {
namespace Core {

struct CharacterSheet;

/// HUD-ready view of a character's XP progress (G-172).
///
/// Pure and free-standing on purpose: no UI, no engine state, no Vulkan. GameShell publishes
/// these into the HUD data context, but the arithmetic lives here so it can be unit tested
/// directly - a bar whose only test is "it looked right in a screenshot" is exactly the kind of
/// claim this project does not accept.
///
/// THE BRACKET IS THE CHARACTER'S ACTUAL LEVEL, not the level its XP total would buy. With
/// auto-level on (the shipped path) the two agree. When they do not - a game that makes the
/// player choose when to level - the bar fills to 100% and stays there, which reads as "you can
/// level up now". Taking the XP bracket instead would show a nearly-empty bar to a character who
/// is two levels overdue, which is worse than useless.
struct ProgressionHudValues {
    int   level       = 1;      ///< the character's level (sheet), 1 when nothing is authored
    int   xpTotal     = 0;      ///< lifetime XP
    int   xpIntoLevel = 0;      ///< XP earned since this level's threshold (never negative)
    int   xpLevelSpan = 0;      ///< XP between this level's threshold and the next; 0 at level 20
    int   xpToNext    = 0;      ///< XP still needed for the next level; 0 at level 20
    float percent     = 0.0f;   ///< 0..100 for a progress bar; 100 at level 20
    std::string text;           ///< "Lv 3  -  1250 / 2700 XP", or "Lv 20  -  MAX"
};

/// Read a sheet's progression into HUD-ready values. Safe on an empty/unauthored sheet.
ProgressionHudValues progressionHud(const CharacterSheet& sheet);

}  // namespace Core
}  // namespace Phyxel
