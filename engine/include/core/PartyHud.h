#pragma once

#include "ui/HudDataContext.h"

#include <functional>
#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

class Party;

/// Look up an entity's health. Returns false when the entity is not in the world right now -
/// which is NORMAL: the party persists across scenes, so a companion is absent whenever the
/// player is in a scene that member has not been respawned into yet.
using PartyHealthLookup = std::function<bool(const std::string& entityId, float& hp, float& maxHp)>;

/// One HUD record per party member, for a `bind: "party"` repeater (G-161).
///
/// Pure and free-standing for the same reason ProgressionHud is: the only test a party panel
/// would otherwise get is "it looked right in a screenshot".
///
/// Fields per record:
///   texts["name"]        member name
///   texts["levelText"]   "Lv 3"
///   texts["hpText"]      "12 / 18" while present, "--" while absent
///   texts["statusText"]  "Fallen" when down, "Away" when not in this scene, else ""
///   floats["alive"]      1 when up, 0 when fallen
///   floats["present"]    1 when the entity exists in the current scene
///   floats["hpPercent"]  0..100 for a bar; 0 when fallen or absent
///
/// A FALLEN member is still listed. Ravenmere keeps the dead in the party until a long rest, and
/// a companion who silently vanished from the panel would read as a bug rather than as a death.
std::vector<UI::HudRecord> partyHudRecords(const Party& party, const PartyHealthLookup& healthOf);

}  // namespace Core
}  // namespace Phyxel
