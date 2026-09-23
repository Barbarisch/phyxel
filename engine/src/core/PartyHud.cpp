#include "core/PartyHud.h"

#include "core/Party.h"

#include <algorithm>
#include <string>

namespace Phyxel {
namespace Core {

std::vector<UI::HudRecord> partyHudRecords(const Party& party, const PartyHealthLookup& healthOf) {
    std::vector<UI::HudRecord> out;
    out.reserve(party.getMembers().size());

    for (const auto& m : party.getMembers()) {
        UI::HudRecord r;
        float hp = 0.0f, maxHp = 0.0f;
        const bool present = healthOf ? healthOf(m.entityId, hp, maxHp) : false;

        r.texts["name"]      = m.name;
        r.texts["levelText"] = "Lv " + std::to_string(std::max(m.characterLevel, 1));
        r.floats["alive"]    = m.isAlive ? 1.0f : 0.0f;
        r.floats["present"]  = present ? 1.0f : 0.0f;

        if (!m.isAlive) {
            // Down, but still in the party until a long rest.
            r.texts["hpText"]     = "0 / " + std::to_string(static_cast<int>(maxHp + 0.5f));
            r.texts["statusText"] = "Fallen";
            r.floats["hpPercent"] = 0.0f;
        } else if (!present) {
            // In the party, not in this scene. Saying "0 HP" here would read as dead.
            r.texts["hpText"]     = "--";
            r.texts["statusText"] = "Away";
            r.floats["hpPercent"] = 0.0f;
        } else {
            r.texts["hpText"]     = std::to_string(static_cast<int>(hp + 0.5f)) + " / " +
                                    std::to_string(static_cast<int>(maxHp + 0.5f));
            r.texts["statusText"] = "";
            r.floats["hpPercent"] = maxHp > 0.0f
                                  ? std::clamp(100.0f * hp / maxHp, 0.0f, 100.0f)
                                  : 0.0f;
        }
        // One compact line for the panel: "Bram   12 / 18". The separate name/hpText/status
        // fields stay available for a game that wants a roomier layout.
        r.texts["rowText"] = r.texts["name"] + "   " +
                             (r.texts["statusText"].empty() ? r.texts["hpText"]
                                                            : r.texts["statusText"]);
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace Core
}  // namespace Phyxel
