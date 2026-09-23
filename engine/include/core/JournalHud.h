#pragma once

#include "ui/HudDataContext.h"

#include <vector>

namespace Phyxel {
namespace Core {

class ObjectiveTracker;

/// The journal window's rows (G-168): everything the player has been told to do, grouped the
/// way a quest log reads, with the DESCRIPTION the objectives strip never shows.
///
///   main       active, category "main"      (priority desc, then title)
///   side       active, any other category   (same order)
///   completed  completed, any category      (same order)
///
/// Hidden and failed objectives are omitted, exactly as the HUD strip omits them - a journal
/// that reveals a hidden step would spoil the quest chain that hides it on purpose.
///
/// Three lists rather than one list with heading rows: a repeater draws every row with the same
/// item template, so a heading row would be as tall as an entry (measured: ~70 px of empty
/// panel under each heading). With a static heading label above each repeater, and a vertical
/// repeater reporting its content height, empty sections collapse to nothing.
///
/// Fields per entry:
///   floats["complete"]  1 when completed
///   texts["title"]      "[ ] Speak with Reeve Aldric ..." / "[x] ..."
///   texts["detail"]     the objective's description ("" when unauthored)
///   texts["category"]   the raw category string
///
/// Pure and free-standing so it is unit-testable without Vulkan.
struct JournalSections {
    std::vector<UI::HudRecord> main;
    std::vector<UI::HudRecord> side;
    std::vector<UI::HudRecord> completed;
    bool empty() const { return main.empty() && side.empty() && completed.empty(); }
};

JournalSections journalHudSections(const ObjectiveTracker& tracker);

}  // namespace Core
}  // namespace Phyxel
