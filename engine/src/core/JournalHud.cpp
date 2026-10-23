#include "core/JournalHud.h"

#include "core/ObjectiveTracker.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

namespace {

// Same ordering the objectives strip uses: higher priority first, then title, so the journal and
// the strip never disagree about which of two active steps comes first.
void sortForDisplay(std::vector<const Objective*>& v) {
    std::stable_sort(v.begin(), v.end(), [](const Objective* a, const Objective* b) {
        if (a->priority != b->priority) return a->priority > b->priority;
        return a->title < b->title;
    });
}

std::vector<UI::HudRecord> rowsFor(const std::vector<const Objective*>& items) {
    std::vector<UI::HudRecord> out;
    out.reserve(items.size());
    for (const Objective* o : items) {
        const bool done = o->status == Objective::Status::Completed;
        UI::HudRecord r;
        r.floats["complete"] = done ? 1.0f : 0.0f;
        r.texts["title"]     = std::string(done ? "[x] " : "[ ] ") + o->title;
        r.texts["detail"]    = o->description;
        r.texts["category"]  = o->category;
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace

JournalSections journalHudSections(const ObjectiveTracker& tracker) {
    std::vector<const Objective*> mainActive, sideActive, completed;
    for (const Objective* o : tracker.getAllObjectives()) {
        if (!o || o->hidden) continue;                       // hidden steps stay hidden here too
        if (o->status == Objective::Status::Failed) continue;
        if (o->status == Objective::Status::Completed) completed.push_back(o);
        else if (o->category == "main")                mainActive.push_back(o);
        else                                           sideActive.push_back(o);
    }
    sortForDisplay(mainActive);
    sortForDisplay(sideActive);
    sortForDisplay(completed);

    JournalSections s;
    s.main      = rowsFor(mainActive);
    s.side      = rowsFor(sideActive);
    s.completed = rowsFor(completed);
    return s;
}

}  // namespace Core
}  // namespace Phyxel
