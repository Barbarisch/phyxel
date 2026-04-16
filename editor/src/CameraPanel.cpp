#include "CameraPanel.h"
#include "graphics/Camera.h"
#include "graphics/CameraManager.h"
#include "core/EntityRegistry.h"
#include "input/InputManager.h"
#include <imgui.h>
#include <cstring>

namespace Phyxel::Editor {

void CameraPanel::render(bool* open) {
    if (!ImGui::Begin("Camera", open)) {
        ImGui::End();
        return;
    }

    if (!m_camera || !m_cameraManager) {
        ImGui::TextDisabled("Camera not available.");
        ImGui::End();
        return;
    }

    renderCurrentState();
    ImGui::Separator();
    renderModeSection();
    ImGui::Separator();
    renderSlotList();
    ImGui::Separator();
    renderFollowSection();

    ImGui::End();
}

void CameraPanel::renderCurrentState() {
    auto pos = m_camera->getPosition();
    float yaw = m_camera->getYaw();
    float pitch = m_camera->getPitch();

    ImGui::Text("Position: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);
    ImGui::Text("Yaw: %.1f  Pitch: %.1f", yaw, pitch);

    const auto& activeName = m_cameraManager->getActiveSlotName();
    if (!activeName.empty()) {
        ImGui::Text("Active Slot: %s", activeName.c_str());
    }
}

void CameraPanel::renderModeSection() {
    ImGui::Text("Mode");
    auto mode = m_camera->getMode();

    bool isFree = (mode == Graphics::CameraMode::Free);
    bool isFirst = (mode == Graphics::CameraMode::FirstPerson);
    bool isThird = (mode == Graphics::CameraMode::ThirdPerson);

    if (ImGui::RadioButton("Free", isFree)) {
        m_camera->setMode(Graphics::CameraMode::Free);
        // Sync InputManager so free-camera WASD starts from current position
        if (m_inputManager) {
            m_inputManager->setCameraPosition(m_camera->getPosition());
            m_inputManager->setYawPitch(m_camera->getYaw(), m_camera->getPitch());
        }
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("1st Person", isFirst)) {
        m_camera->setMode(Graphics::CameraMode::FirstPerson);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("3rd Person", isThird)) {
        m_camera->setMode(Graphics::CameraMode::ThirdPerson);
    }
}

void CameraPanel::renderSlotList() {
    const auto& slots = m_cameraManager->getSlots();
    const auto& activeName = m_cameraManager->getActiveSlotName();

    ImGui::Text("Slots (%zu)", slots.size());

    // Save current position button
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("##slotname", m_newSlotName, sizeof(m_newSlotName));
    ImGui::SameLine();
    if (ImGui::Button("Save Position")) {
        if (m_newSlotName[0] != '\0') {
            auto slot = m_cameraManager->captureCurrentState(m_newSlotName);
            // If slot already exists, update it; otherwise create it
            if (m_cameraManager->getSlot(m_newSlotName)) {
                m_cameraManager->updateSlot(m_newSlotName, slot);
            } else {
                m_cameraManager->createSlot(slot);
            }
        }
    }

    // Slot list
    if (slots.empty()) {
        ImGui::TextDisabled("  No slots.");
        return;
    }

    std::string slotToRemove;

    for (const auto& slot : slots) {
        bool isActive = (slot.name == activeName);
        ImGui::PushID(slot.name.c_str());

        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        }

        // Slot name and position
        bool nodeOpen = ImGui::TreeNode("##slot", "%s  (%.0f, %.0f, %.0f)",
            slot.name.c_str(), slot.position.x, slot.position.y, slot.position.z);

        if (isActive) {
            ImGui::PopStyleColor();
        }

        // Buttons on the same line as the tree node
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 130.0f);
        if (ImGui::SmallButton("Snap")) {
            m_cameraManager->setActiveSlot(slot.name);
            if (m_inputManager) {
                m_inputManager->setCameraPosition(m_camera->getPosition());
                m_inputManager->setYawPitch(m_camera->getYaw(), m_camera->getPitch());
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Transition")) {
            m_cameraManager->transitionToSlot(slot.name, 1.0f);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            slotToRemove = slot.name;
        }

        if (nodeOpen) {
            ImGui::Text("Yaw: %.1f  Pitch: %.1f", slot.yaw, slot.pitch);
            const char* modeStr = (slot.mode == Graphics::CameraMode::Free) ? "Free" :
                                  (slot.mode == Graphics::CameraMode::FirstPerson) ? "1st Person" : "3rd Person";
            ImGui::Text("Mode: %s", modeStr);
            if (!slot.followEntityId.empty()) {
                ImGui::Text("Following: %s (d=%.1f h=%.1f)",
                    slot.followEntityId.c_str(), slot.followDistance, slot.followHeight);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (!slotToRemove.empty()) {
        m_cameraManager->removeSlot(slotToRemove);
    }
}

void CameraPanel::renderFollowSection() {
    ImGui::Text("Entity Follow");

    if (!m_entityRegistry) {
        ImGui::TextDisabled("  No entity registry.");
        return;
    }

    const auto& activeName = m_cameraManager->getActiveSlotName();
    if (activeName.empty()) {
        ImGui::TextDisabled("  No active slot.");
        return;
    }

    const auto* activeSlot = m_cameraManager->getSlot(activeName);
    bool isFollowing = activeSlot && !activeSlot->followEntityId.empty();

    if (isFollowing) {
        ImGui::Text("Following: %s", activeSlot->followEntityId.c_str());
        if (ImGui::Button("Unfollow")) {
            m_cameraManager->unfollowEntity(activeName);
        }
    }

    // Entity combo
    auto ids = m_entityRegistry->getAllIds();
    if (ids.empty()) {
        ImGui::TextDisabled("  No entities in world.");
        return;
    }

    static int selectedIdx = 0;
    if (selectedIdx >= static_cast<int>(ids.size())) selectedIdx = 0;

    if (ImGui::BeginCombo("Entity", ids[selectedIdx].c_str())) {
        for (int i = 0; i < static_cast<int>(ids.size()); i++) {
            bool sel = (i == selectedIdx);
            if (ImGui::Selectable(ids[i].c_str(), sel)) {
                selectedIdx = i;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SliderFloat("Distance", &m_followDistance, 1.0f, 20.0f);
    ImGui::SliderFloat("Height", &m_followHeight, 0.0f, 5.0f);

    if (ImGui::Button("Follow")) {
        if (selectedIdx < static_cast<int>(ids.size())) {
            m_cameraManager->followEntity(activeName, ids[selectedIdx], m_followDistance, m_followHeight);
            m_camera->setMode(Graphics::CameraMode::ThirdPerson);
        }
    }
}

} // namespace Phyxel::Editor
