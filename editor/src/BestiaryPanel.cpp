#include "BestiaryPanel.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include <imgui.h>

#include "core/NPCManager.h"

namespace Phyxel::Editor {

namespace {
/// Case-insensitive substring match, so typing "hyd" finds "Hydra".
bool matches(const std::string& hay, const char* needle) {
    if (!needle || !*needle) return true;
    std::string h = hay, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != std::string::npos;
}
}  // namespace

void BestiaryPanel::render(bool* open) {
    if (!ImGui::Begin("Bestiary", open)) { ImGui::End(); return; }

    if (!m_hall) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No hall attached.");
        ImGui::End();
        return;
    }

    if (!m_hall->lastError().empty() && m_hall->entries().empty()) {
        // Say WHY there are no creatures. An empty list and a failed roster
        // load look identical, and that ambiguity wastes real debugging time.
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Roster unavailable");
        ImGui::TextWrapped("%s", m_hall->lastError().c_str());
        ImGui::End();
        return;
    }

    // --- staging -----------------------------------------------------------
    if (!m_hall->isStaged()) {
        ImGui::TextWrapped("%zu rigs in the roster, none staged yet.",
                           m_hall->entries().size());
        if (ImGui::Button("Stage the hall", ImVec2(-1, 0)) && onStageHall)
            onStageHall();
        ImGui::End();
        return;
    }

    int total = 0, shown = 0;
    for (const auto& e : m_hall->entries()) if (e.spawned) ++total;

    ImGui::Text("%d rigs staged", total);
    ImGui::SameLine();
    ImGui::TextDisabled("(%d stat blocks)", [&] {
        int n = 0;
        for (const auto& e : m_hall->entries()) n += e.statBlocks;
        return n;
    }());

    // --- playback ----------------------------------------------------------
    ImGui::Separator();
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##state", &m_state, kStates, kStateCount);
    ImGui::SameLine();
    if (ImGui::Button("Play on selected")) playCurrent(false);
    ImGui::SameLine();
    if (ImGui::Button("Play on all")) playCurrent(true);

    ImGui::Checkbox("Auto-cycle clips", &m_autoCycle);
    if (m_autoCycle) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("secs", &m_cycleSeconds, 1.0f, 8.0f, "%.1f");
    }
    ImGui::Checkbox("Camera follows selection", &m_followCamera);

    // --- list --------------------------------------------------------------
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "search creatures...", m_filter, sizeof(m_filter));

    if (!m_hall->selected().empty()) {
        if (ImGui::Button("Clear selection (un-ghost all)", ImVec2(-1, 0)))
            m_hall->select(m_npcs, "");
    }

    ImGui::BeginChild("##list", ImVec2(0, 0), true);

    std::map<std::string, std::vector<BestiaryHall::Entry*>> byCat;
    for (auto& e : m_hall->entries()) {
        if (!e.spawned) continue;
        if (!matches(e.name, m_filter) && !matches(e.id, m_filter) &&
            !matches(e.category, m_filter))
            continue;
        byCat[e.category].push_back(&e);
        ++shown;
    }

    if (shown == 0) {
        ImGui::TextDisabled("nothing matches \"%s\"", m_filter);
    }

    for (auto& [cat, list] : byCat) {
        // Filtered results open by default so a search shows its hits without
        // another click.
        ImGui::SetNextItemOpen(true, m_filter[0] ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        if (ImGui::TreeNode(cat.c_str(), "%s (%d)", cat.c_str(), (int)list.size())) {
            for (auto* e : list) renderRow(*e);
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void BestiaryPanel::renderRow(BestiaryHall::Entry& e) {
    const bool isSel = (e.id == m_hall->selected());

    ImGui::PushID(e.id.c_str());
    if (ImGui::Selectable(e.name.c_str(), isSel)) {
        m_hall->select(m_npcs, isSel ? "" : e.id);   // click again to deselect
        if (!isSel && m_followCamera && onFocusCamera) {
            glm::vec3 pos, target;
            if (m_hall->focusView(e.id, pos, target)) onFocusCamera(pos, target);
        }
        if (!isSel) playCurrent(false);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", e.name.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("rig      %s", e.id.c_str());
        ImGui::TextDisabled("size     %.2f h x %.2f w x %.2f d", e.height, e.width, e.depth);
        ImGui::TextDisabled("geometry %d boxes / %d bones", e.boxes, e.bones);
        ImGui::TextDisabled("carries  %d stat block%s", e.statBlocks,
                            e.statBlocks == 1 ? "" : "s");
        // Name the clips a rig CANNOT play — that is the actionable half.
        std::string missing;
        for (int i = 0; i < kStateCount; ++i)
            if (!e.canPlay(kStates[i])) {
                if (!missing.empty()) missing += ", ";
                missing += kStates[i];
            }
        if (!missing.empty())
            ImGui::TextColored(ImVec4(1, 0.65f, 0.3f, 1), "no clip:  %s", missing.c_str());
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%d", e.statBlocks);
    ImGui::PopID();
}

void BestiaryPanel::playCurrent(bool allRigs) {
    if (!m_hall) return;
    m_hall->playState(m_npcs, kStates[m_state], allRigs);
    m_cycleTimer = 0.0f;
}

void BestiaryPanel::tick(float dt) {
    if (!m_autoCycle || !m_hall || !m_hall->isStaged()) return;
    m_cycleTimer += dt;
    if (m_cycleTimer < m_cycleSeconds) return;
    m_cycleTimer = 0.0f;
    m_state = (m_state + 1) % kStateCount;
    // Cycling drives the WHOLE hall: watching 46 creatures die at once is the
    // fastest way to spot the one that dies standing up.
    m_hall->playState(m_npcs, kStates[m_state], true);
}

}  // namespace Phyxel::Editor
