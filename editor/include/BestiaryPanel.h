#pragma once

#include <functional>
#include <string>

#include <glm/glm.hpp>

#include "BestiaryHall.h"

namespace Phyxel::Core { class NPCManager; }

namespace Phyxel::Editor {

/// Dockable panel driving the Bestiary Hall: a searchable tree of every
/// creature rig, grouped by category, with per-state playback.
///
/// Clicking a row selects that creature, which ghosts every other creature in
/// the world and (optionally) flies the camera to it. The list is the only
/// place the hall is driven from, so what you see in the panel and what you
/// see in the viewport cannot disagree.
class BestiaryPanel {
public:
    void setHall(BestiaryHall* hall)          { m_hall = hall; }
    void setNPCManager(Core::NPCManager* npc) { m_npcs = npc; }

    /// Move the camera to frame a creature (set by Application).
    std::function<void(const glm::vec3& pos, const glm::vec3& target)> onFocusCamera;
    /// Stage the hall (set by Application — it knows the ground height).
    std::function<bool()> onStageHall;

    void render(bool* open);

    /// Advance the auto-cycle. Call once per frame with the frame delta; does
    /// nothing unless the user turned cycling on.
    void tick(float dt);

    bool followSelection() const { return m_followCamera; }

private:
    void renderRow(BestiaryHall::Entry& e);
    void playCurrent(bool allRigs);

    BestiaryHall*      m_hall = nullptr;
    Core::NPCManager*  m_npcs = nullptr;

    char  m_filter[96]{};
    int   m_state = 0;               ///< index into kStates
    bool  m_followCamera = true;
    bool  m_autoCycle = false;
    float m_cycleSeconds = 2.5f;
    float m_cycleTimer = 0.0f;

    static constexpr const char* kStates[] = {"Idle", "Walk", "Attack", "Death"};
    static constexpr int kStateCount = 4;
};

}  // namespace Phyxel::Editor
