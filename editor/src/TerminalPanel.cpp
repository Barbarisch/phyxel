#ifdef _WIN32

// Prevent Windows.h min/max macros from interfering with std::min/max
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "TerminalPanel.h"
#include "ui/PseudoTerminal.h"
#include "ui/TerminalEmulator.h"
#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace {
// Convert a Unicode codepoint to a UTF-8 byte sequence. Returns the byte count (1-4).
int char32ToUtf8(char32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}
} // anonymous namespace

namespace Phyxel::Editor {

TerminalPanel::TerminalPanel() = default;

TerminalPanel::~TerminalPanel() {
    // Tabs hold unique_ptrs — destructors handle cleanup
    m_tabs.clear();
}

void TerminalPanel::addTerminal(const std::string& title, const std::string& command) {
    auto tab = std::make_unique<Tab>();
    tab->title = title;
    tab->pty = std::make_unique<UI::PseudoTerminal>();
    tab->emulator = std::make_unique<UI::TerminalEmulator>(120, 30);

    if (!tab->pty->spawn(command, 120, 30)) {
        LOG_ERROR("TerminalPanel", "Failed to spawn: " + command);
        return;
    }

    m_tabs.push_back(std::move(tab));
    m_activeTab = static_cast<int>(m_tabs.size()) - 1;
}

int TerminalPanel::getTabCount() const {
    return static_cast<int>(m_tabs.size());
}

bool TerminalPanel::hasLiveTabs() const {
    for (auto& t : m_tabs)
        if (t->pty && t->pty->isAlive()) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────

void TerminalPanel::render(bool* open) {
    if (m_font) ImGui::PushFont(m_font);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool visible = ImGui::Begin("Terminal", open, flags);

    if (visible) {
        // Tab bar for multiple terminals
        if (ImGui::BeginTabBar("##TermTabs", ImGuiTabBarFlags_AutoSelectNewTabs |
                                              ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            // "+" button to add new terminal
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
                addTerminal();
            }

            int closeIdx = -1;
            for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
                auto& tab = *m_tabs[i];
                bool tabOpen = true;

                // Show dead indicator
                std::string label = tab.title;
                if (tab.pty && !tab.pty->isAlive()) label += " (exited)";
                label += "###tab" + std::to_string(i);

                if (ImGui::BeginTabItem(label.c_str(), &tabOpen)) {
                    m_activeTab = i;
                    renderTab(tab);
                    ImGui::EndTabItem();
                }
                if (!tabOpen) closeIdx = i;
            }
            ImGui::EndTabBar();

            // Handle tab close
            if (closeIdx >= 0) {
                m_tabs[closeIdx]->pty->close();
                m_tabs.erase(m_tabs.begin() + closeIdx);
                if (m_activeTab >= static_cast<int>(m_tabs.size()))
                    m_activeTab = std::max(0, static_cast<int>(m_tabs.size()) - 1);
            }
        }

        // If no tabs, show a prompt
        if (m_tabs.empty()) {
            ImGui::TextDisabled("No terminals open. Click '+' to add one.");
        }
    }

    ImGui::End();
    if (m_font) ImGui::PopFont();
}

void TerminalPanel::renderTab(Tab& tab) {
    // Pump PTY output into the emulator
    pumpOutput(tab);

    // Calculate cell size from current font
    float cellW = ImGui::CalcTextSize("M").x;   // width of 'M' in current font
    float cellH = ImGui::GetTextLineHeight();    // line height

    // Available region
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int visibleCols = std::max(1, static_cast<int>(avail.x / cellW));
    int visibleRows = std::max(1, static_cast<int>(avail.y / cellH));

    // Resize emulator/PTY if dimensions changed
    if (visibleCols != tab.emulator->getCols() || visibleRows != tab.emulator->getRows()) {
        tab.emulator->resize(visibleCols, visibleRows);
        if (tab.pty) tab.pty->resize(visibleCols, visibleRows);
    }

    // Handle scrolling with mouse wheel
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            tab.scrollOffset += static_cast<int>(-wheel * 3);
            int maxScroll = tab.emulator->getScrollbackSize();
            tab.scrollOffset = std::clamp(tab.scrollOffset, 0, maxScroll);
            tab.autoScroll = (tab.scrollOffset == 0);
        }
    }

    // Render the character grid
    renderGrid(tab, cellW, cellH);

    // Handle keyboard input when this window is focused
    handleKeyboardInput(tab);
}

void TerminalPanel::renderGrid(Tab& tab, float cellW, float cellH) {
    auto& emu = *tab.emulator;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();

    int cols = emu.getCols();
    int rows = emu.getRows();
    int scrollback = tab.scrollOffset;

    // Draw background
    ImVec2 gridEnd = {origin.x + cols * cellW, origin.y + rows * cellH};
    drawList->AddRectFilled(origin, gridEnd, IM_COL32(15, 15, 20, 255));

    // Reserve the grid area so ImGui knows the space is used
    ImGui::Dummy({cols * cellW, rows * cellH});

    // Render each visible row
    char utf8[5] = {};
    for (int screenRow = 0; screenRow < rows; ++screenRow) {
        int dataRow = screenRow; // row in emulator grid
        bool fromScrollback = false;

        if (scrollback > 0) {
            int scrollbackStart = emu.getScrollbackSize() - scrollback;
            int scrollbackLine = scrollbackStart + screenRow;
            if (scrollbackLine < emu.getScrollbackSize()) {
                // Render from scrollback
                fromScrollback = true;
                const auto& line = emu.getScrollbackLine(scrollbackLine);
                float y = origin.y + screenRow * cellH;
                for (int c = 0; c < std::min(cols, static_cast<int>(line.size())); ++c) {
                    const auto& cell = line[c];
                    float x = origin.x + c * cellW;

                    uint32_t fg = cell.inverse ? cell.bgColor : cell.fgColor;
                    uint32_t bg = cell.inverse ? cell.fgColor : cell.bgColor;

                    // Background (skip default background)
                    if (bg != UI::TerminalEmulator::DEFAULT_BG) {
                        drawList->AddRectFilled({x, y}, {x + cellW, y + cellH}, bg);
                    }

                    // Character
                    if (cell.ch > 32) {
                        int len = char32ToUtf8(cell.ch, utf8);
                        utf8[len] = '\0';
                        drawList->AddText({x, y}, fg, utf8, utf8 + len);
                    }
                }
                continue;
            } else {
                // Past scrollback — render from grid
                dataRow = screenRow - (scrollback - (emu.getScrollbackSize() - scrollbackStart - screenRow));
                // Simpler: in scroll mode, remaining rows come from grid
                dataRow = scrollbackLine - emu.getScrollbackSize();
                if (dataRow < 0 || dataRow >= rows) continue;
            }
        }

        if (!fromScrollback) {
            float y = origin.y + screenRow * cellH;
            for (int c = 0; c < cols; ++c) {
                const auto& cell = emu.cell(c, dataRow);
                float x = origin.x + c * cellW;

                uint32_t fg = cell.inverse ? cell.bgColor : cell.fgColor;
                uint32_t bg = cell.inverse ? cell.fgColor : cell.bgColor;

                // Background (skip default background)
                if (bg != UI::TerminalEmulator::DEFAULT_BG) {
                    drawList->AddRectFilled({x, y}, {x + cellW, y + cellH}, bg);
                }

                // Character
                if (cell.ch > 32) {
                    int len = char32ToUtf8(cell.ch, utf8);
                    utf8[len] = '\0';
                    drawList->AddText({x, y}, fg, utf8, utf8 + len);
                }
            }

            // Cursor (only when not scrolled back, and on the active row)
            if (scrollback == 0 && dataRow == emu.getCursorRow() && emu.isCursorVisible()) {
                float cx = origin.x + emu.getCursorCol() * cellW;
                float cy = origin.y + screenRow * cellH;
                // Blinking block cursor
                float blink = static_cast<float>(fmod(ImGui::GetTime(), 1.0));
                if (blink < 0.5f) {
                    drawList->AddRectFilled({cx, cy}, {cx + cellW, cy + cellH},
                        IM_COL32(200, 200, 200, 180));
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// Keyboard input
// ─────────────────────────────────────────────────────────────────

void TerminalPanel::handleKeyboardInput(Tab& tab) {
    if (!tab.pty || !tab.pty->isAlive()) return;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;

    ImGuiIO& io = ImGui::GetIO();

    // Check modifier keys
    bool ctrl = io.KeyCtrl;
    bool shift = io.KeyShift;
    bool alt = io.KeyAlt;

    // Handle special keys
    struct SpecialKey { ImGuiKey key; const char* seq; };
    static const SpecialKey specialKeys[] = {
        {ImGuiKey_UpArrow,    "\x1b[A"},
        {ImGuiKey_DownArrow,  "\x1b[B"},
        {ImGuiKey_RightArrow, "\x1b[C"},
        {ImGuiKey_LeftArrow,  "\x1b[D"},
        {ImGuiKey_Home,       "\x1b[H"},
        {ImGuiKey_End,        "\x1b[F"},
        {ImGuiKey_Insert,     "\x1b[2~"},
        {ImGuiKey_Delete,     "\x1b[3~"},
        {ImGuiKey_PageUp,     "\x1b[5~"},
        {ImGuiKey_PageDown,   "\x1b[6~"},
        {ImGuiKey_F1,         "\x1bOP"},
        {ImGuiKey_F2,         "\x1bOQ"},
        {ImGuiKey_F3,         "\x1bOR"},
        {ImGuiKey_F4,         "\x1bOS"},
        {ImGuiKey_F5,         "\x1b[15~"},
        {ImGuiKey_F6,         "\x1b[17~"},
        {ImGuiKey_F7,         "\x1b[18~"},
        {ImGuiKey_F8,         "\x1b[19~"},
        {ImGuiKey_F9,         "\x1b[20~"},
        {ImGuiKey_F10,        "\x1b[21~"},
        {ImGuiKey_F11,        "\x1b[23~"},
        {ImGuiKey_F12,        "\x1b[24~"},
    };

    for (auto& sk : specialKeys) {
        if (ImGui::IsKeyPressed(sk.key, true)) {
            tab.pty->write(sk.seq);
            tab.scrollOffset = 0;
            tab.autoScroll = true;
            return;
        }
    }

    // Enter
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, true)) {
        tab.pty->write("\r");
        tab.scrollOffset = 0;
        tab.autoScroll = true;
        return;
    }

    // Backspace
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
        tab.pty->write("\x7f");
        return;
    }

    // Tab
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, true)) {
        tab.pty->write("\t");
        return;
    }

    // Escape
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        tab.pty->write("\x1b");
        return;
    }

    // Ctrl + letter (A-Z) → control codes 0x01-0x1a
    if (ctrl && !alt && !shift) {
        for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k))) {
                char code = static_cast<char>(1 + (k - ImGuiKey_A));
                tab.pty->write(&code, 1);
                return;
            }
        }
    }

    // Regular text input (printable characters from InputQueueCharacters)
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        ImWchar wch = io.InputQueueCharacters[i];
        if (wch >= 32 && wch < 127) {
            char ch = static_cast<char>(wch);
            tab.pty->write(&ch, 1);
        }
        // TODO: UTF-8 encoding for wch >= 128
    }
    // Consume the input queue so other ImGui widgets don't process it
    if (io.InputQueueCharacters.Size > 0 && ImGui::IsWindowFocused()) {
        io.InputQueueCharacters.resize(0);
    }
}

void TerminalPanel::pumpOutput(Tab& tab) {
    if (!tab.pty) return;
    std::string data = tab.pty->drainOutput();
    if (!data.empty()) {
        tab.emulator->processOutput(data);
        if (tab.autoScroll) tab.scrollOffset = 0;
    }
}

std::string TerminalPanel::specialKeySequence(int /*imguiKey*/, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
    // Extended modifier sequences (CSI 1;mod X) — future enhancement
    return {};
}

} // namespace Phyxel::Editor

#endif // _WIN32
