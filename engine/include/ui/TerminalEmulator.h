#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Phyxel::UI {

/// A single cell in the terminal grid.
struct TerminalCell {
    char32_t ch = ' ';
    uint32_t fgColor = 0xFFBFBFBF; // packed ABGR (IM_COL32 format), default light gray
    uint32_t bgColor = 0xFF14140F; // packed ABGR, default near-black
    bool bold = false;
    bool underline = false;
    bool inverse = false;
};

/// VT100/ANSI terminal emulator — parses escape sequences and maintains a
/// character grid with scrollback. Designed for use with ConPTY output.
class TerminalEmulator {
public:
    explicit TerminalEmulator(int cols = 120, int rows = 30);

    /// Feed raw bytes from the PTY into the parser.
    void processOutput(const char* data, int length);
    void processOutput(const std::string& data) { processOutput(data.data(), static_cast<int>(data.size())); }

    /// Resize the terminal grid.  Existing content is reflowed.
    void resize(int cols, int rows);

    // Grid access
    int getCols() const { return m_cols; }
    int getRows() const { return m_rows; }
    int getCursorCol() const { return m_cursorCol; }
    int getCursorRow() const { return m_cursorRow; }
    bool isCursorVisible() const { return m_cursorVisible; }
    const TerminalCell& cell(int col, int row) const;
    const std::string& getTitle() const { return m_title; }

    // Scrollback
    int getScrollbackSize() const { return static_cast<int>(m_scrollback.size()); }
    const std::vector<TerminalCell>& getScrollbackLine(int index) const;

    /// Pack R,G,B into uint32_t in IM_COL32 format (ABGR on little-endian).
    static constexpr uint32_t packColor(uint8_t r, uint8_t g, uint8_t b) {
        return 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
    }
    /// Convert ANSI 256-color index (0-255) to packed color.
    static uint32_t ansi256Color(uint8_t index);

    static constexpr uint32_t DEFAULT_FG = 0xFFBFBFBF; // light gray (ANSI 7)
    static constexpr uint32_t DEFAULT_BG = 0xFF14140F;  // near-black

private:
    // Character grid (rows x cols)
    using Row = std::vector<TerminalCell>;
    std::vector<Row> m_grid;
    std::vector<Row> m_scrollback;
    int m_cols, m_rows;

    // Cursor
    int m_cursorCol = 0;
    int m_cursorRow = 0;
    bool m_cursorVisible = true;

    // Current text attributes (packed ABGR colors)
    uint32_t m_currentFg = DEFAULT_FG;
    uint32_t m_currentBg = DEFAULT_BG;
    bool m_bold = false;
    bool m_underline = false;
    bool m_inverse = false;

    // UTF-8 multi-byte decoder state
    char32_t m_utf8Codepoint = 0;
    int m_utf8Remaining = 0;

    // Alternate screen buffer
    std::vector<Row> m_altGrid;
    int m_altCursorCol = 0;
    int m_altCursorRow = 0;
    bool m_altScreenActive = false;

    // Parser state machine
    enum class State { Normal, Escape, CSI, OSC, OSCEscape, StringSequence, StringSeqEscape };
    State m_state = State::Normal;
    std::string m_csiParams;   // accumulated CSI parameter bytes
    std::string m_oscString;   // accumulated OSC payload
    
    // Scroll region (top, bottom inclusive, 0-indexed)
    int m_scrollTop = 0;
    int m_scrollBottom = 0;  // set to m_rows-1

    // Title
    std::string m_title;

    // Saved cursor
    int m_savedCursorCol = 0;
    int m_savedCursorRow = 0;

    static constexpr int MAX_SCROLLBACK = 5000;

    // Internal operations
    void putChar(char32_t ch);
    void newLine();
    void scrollUp();
    void scrollDown();
    void carriageReturn();
    void backspace();
    void tab();
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void setCursorPosition(int row, int col);
    void moveCursorUp(int n);
    void moveCursorDown(int n);
    void moveCursorForward(int n);
    void moveCursorBack(int n);
    void insertLines(int n);
    void deleteLines(int n);
    void deleteChars(int n);
    void eraseChars(int n);
    void setScrollRegion(int top, int bottom);
    void saveCursor();
    void restoreCursor();
    
    // Parser
    void processNormal(char ch);
    void processEscape(char ch);
    void processCSI(char ch);
    void processOSC(char ch);
    void executeCSI(char command);
    void executeSGR(const std::vector<int>& params);
    std::vector<int> parseCSIParams() const;

    // Ensure cursor is within bounds
    void clampCursor();
    TerminalCell makeCell(char32_t ch) const;
    Row makeRow() const;
};

} // namespace Phyxel::UI
