#pragma once

#ifdef _WIN32

#include <memory>
#include <string>
#include <vector>

// Forward declarations — avoid pulling in full headers
struct ImFont;
namespace Phyxel::UI {
    class PseudoTerminal;
    class TerminalEmulator;
}

namespace Phyxel::Editor {

/// A dockable ImGui panel that hosts one or more terminal tabs.
/// Each tab runs an independent ConPTY subprocess.
class TerminalPanel {
public:
    TerminalPanel();
    ~TerminalPanel();

    TerminalPanel(const TerminalPanel&) = delete;
    TerminalPanel& operator=(const TerminalPanel&) = delete;

    /// Open a new terminal tab running the given command.
    /// @param title   Tab label (e.g. "PowerShell", "Claude Code")
    /// @param command Shell command (default: powershell.exe)
    void addTerminal(const std::string& title = "Terminal",
                     const std::string& command = "powershell.exe");

    /// Render the terminal panel as an ImGui window (call each frame).
    /// @param open  Pointer to visibility bool (nullptr = always visible)
    void render(bool* open = nullptr);

    /// Set the monospace font to use for terminal rendering.
    void setFont(ImFont* font) { m_font = font; }

    int getTabCount() const;
    bool hasLiveTabs() const;

private:
    struct Tab {
        std::string title;
        std::unique_ptr<UI::PseudoTerminal> pty;
        std::unique_ptr<UI::TerminalEmulator> emulator;
        int scrollOffset = 0; // >0 means scrolled back into history
        bool autoScroll = true;
    };

    std::vector<std::unique_ptr<Tab>> m_tabs;
    int m_activeTab = 0;
    ImFont* m_font = nullptr;

    void renderTab(Tab& tab);
    void renderGrid(Tab& tab, float cellW, float cellH);
    void handleKeyboardInput(Tab& tab);
    void pumpOutput(Tab& tab);

    /// Send a key sequence for a special key (arrow, home, end, etc.)
    static std::string specialKeySequence(int imguiKey, bool shift, bool ctrl, bool alt);
};

} // namespace Phyxel::Editor

#endif // _WIN32
