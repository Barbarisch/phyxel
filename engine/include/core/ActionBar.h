#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <string>

namespace Phyxel {
namespace Core {

/// One thing a bar slot can hold. `kind` is what tells the host HOW to fire it — a spell
/// is armed or cast, an item is used, a built-in action goes straight to the turn
/// controller — so it is stored rather than re-derived by sniffing the id.
struct ActionSlot {
    enum class Kind { Empty, Spell, Item, Action };

    Kind        kind = Kind::Empty;
    std::string id;   ///< spell id, item id, or a built-in ("attack" / "end_turn")

    bool empty() const { return kind == Kind::Empty || id.empty(); }

    static const char* kindName(Kind k) {
        switch (k) {
            case Kind::Spell:  return "spell";
            case Kind::Item:   return "item";
            case Kind::Action: return "action";
            case Kind::Empty:  break;
        }
        return "empty";
    }
    static Kind kindFromString(const std::string& s) {
        if (s == "spell")  return Kind::Spell;
        if (s == "item")   return Kind::Item;
        if (s == "action") return Kind::Action;
        return Kind::Empty;
    }
};

/**
 * @brief The player's action bar: a FIXED array of slots, each holding an assignment.
 *
 * Fixed, not derived. The bar used to be rebuilt every frame from the player's known
 * spells, which meant it had no empty slots, no order of the player's choosing, and
 * nothing to persist — rearranging it was impossible by construction. A slot is now a
 * stored assignment and the derived list is only the DEFAULT fill.
 *
 * SLOT_COUNT is 12 because the bar is driven by keys 1-9, 0, -, = — the WoW convention
 * the third-person controls already follow (docs/game-production/RavenmereGapLedger.md
 * G-150). Slot 0 is key "1"; the off-by-one lives here and nowhere else.
 *
 * Out-of-range slots are REFUSED, never clamped: clamping would write to slot 11 when a
 * caller meant 12, and the caller's own assertion would then pass against the wrong slot.
 */
class ActionBar {
public:
    static constexpr int SLOT_COUNT = 12;

    /// Assign `id` to a slot. Returns false (changing nothing) for an out-of-range slot
    /// or an empty id — use clear() to empty a slot, so a typo'd id cannot silently
    /// blank one. The same id in several slots is allowed: duplicating a spell across
    /// two bars is a legitimate layout, not a broken invariant.
    bool assign(int slot, ActionSlot::Kind kind, const std::string& id) {
        if (!inRange(slot) || id.empty() || kind == ActionSlot::Kind::Empty) return false;
        slots_[static_cast<size_t>(slot)] = ActionSlot{kind, id};
        return true;
    }

    bool clear(int slot) {
        if (!inRange(slot)) return false;
        slots_[static_cast<size_t>(slot)] = ActionSlot{};
        return true;
    }

    /// Exchange two slots. This is what a drag between slots does — a swap rather than an
    /// overwrite, so a drag can never destroy the assignment it lands on.
    bool swap(int a, int b) {
        if (!inRange(a) || !inRange(b)) return false;
        std::swap(slots_[static_cast<size_t>(a)], slots_[static_cast<size_t>(b)]);
        return true;
    }

    const ActionSlot& at(int slot) const {
        static const ActionSlot kNone{};
        return inRange(slot) ? slots_[static_cast<size_t>(slot)] : kNone;
    }

    static bool inRange(int slot) { return slot >= 0 && slot < SLOT_COUNT; }

    /// True while the player has never arranged the bar, so the host may fill it with a
    /// sensible default. Once ANY slot is assigned the layout is the player's and is
    /// never auto-rewritten — otherwise clearing your last slot would silently refill it.
    bool isDefault() const {
        for (const auto& s : slots_) if (!s.empty()) return false;
        return true;
    }

    int filledCount() const {
        int n = 0;
        for (const auto& s : slots_) if (!s.empty()) ++n;
        return n;
    }

    nlohmann::json toJson() const {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& s : slots_) {
            if (s.empty()) out.push_back(nlohmann::json::object());
            else out.push_back({{"kind", ActionSlot::kindName(s.kind)}, {"id", s.id}});
        }
        return out;
    }

    /// Tolerant by design: a save written before this existed, a short array, or a row
    /// naming a spell that no longer exists all load as an empty slot rather than
    /// refusing the whole profile.
    void fromJson(const nlohmann::json& j) {
        slots_ = {};
        if (!j.is_array()) return;
        const size_t n = std::min<size_t>(j.size(), SLOT_COUNT);
        for (size_t i = 0; i < n; ++i) {
            if (!j[i].is_object()) continue;
            const std::string kind = j[i].value("kind", "empty");
            const std::string id   = j[i].value("id", "");
            if (id.empty()) continue;
            slots_[i] = ActionSlot{ActionSlot::kindFromString(kind), id};
        }
    }

private:
    std::array<ActionSlot, SLOT_COUNT> slots_{};
};

} // namespace Core
} // namespace Phyxel
