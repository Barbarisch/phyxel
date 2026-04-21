#include "ui/TerminalEmulator.h"

#include <algorithm>
#include <sstream>

namespace Phyxel::UI {

// Full 256-color ANSI palette (0-15 standard, 16-231 color cube, 232-255 grayscale)
uint32_t TerminalEmulator::ansi256Color(uint8_t index) {
    // Standard 16 colors
    static const uint32_t base16[] = {
        packColor(0,   0,   0),     // 0  Black
        packColor(205, 49,  49),    // 1  Red
        packColor(13,  188, 121),   // 2  Green
        packColor(229, 229, 16),    // 3  Yellow
        packColor(36,  114, 200),   // 4  Blue
        packColor(188, 63,  188),   // 5  Magenta
        packColor(17,  168, 205),   // 6  Cyan
        packColor(191, 191, 191),   // 7  White (light gray)
        packColor(102, 102, 102),   // 8  Bright Black (dark gray)
        packColor(241, 76,  76),    // 9  Bright Red
        packColor(35,  209, 139),   // 10 Bright Green
        packColor(245, 245, 67),    // 11 Bright Yellow
        packColor(59,  142, 234),   // 12 Bright Blue
        packColor(214, 112, 214),   // 13 Bright Magenta
        packColor(41,  184, 219),   // 14 Bright Cyan
        packColor(229, 229, 229),   // 15 Bright White
    };

    if (index < 16) return base16[index];

    // 216-color cube (indices 16-231): 6x6x6 cube
    if (index < 232) {
        int ci = index - 16;
        int b = ci % 6;
        int g = (ci / 6) % 6;
        int r = ci / 36;
        static const uint8_t levels[] = {0, 95, 135, 175, 215, 255};
        return packColor(levels[r], levels[g], levels[b]);
    }

    // Grayscale ramp (indices 232-255): 24 shades from dark to light
    uint8_t v = static_cast<uint8_t>(8 + (index - 232) * 10);
    return packColor(v, v, v);
}

TerminalEmulator::TerminalEmulator(int cols, int rows)
    : m_cols(cols), m_rows(rows), m_scrollBottom(rows - 1) {
    m_grid.resize(rows);
    for (auto& row : m_grid) row = makeRow();
}

TerminalEmulator::Row TerminalEmulator::makeRow() const {
    return Row(m_cols, makeCell(' '));
}

TerminalCell TerminalEmulator::makeCell(char32_t ch) const {
    TerminalCell c;
    c.ch = ch;
    c.fgColor = m_currentFg;
    c.bgColor = m_currentBg;
    c.bold = m_bold;
    c.underline = m_underline;
    c.inverse = m_inverse;
    return c;
}

const TerminalCell& TerminalEmulator::cell(int col, int row) const {
    static TerminalCell blank{};
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return blank;
    return m_grid[row][col];
}

const std::vector<TerminalCell>& TerminalEmulator::getScrollbackLine(int index) const {
    static std::vector<TerminalCell> empty;
    if (index < 0 || index >= static_cast<int>(m_scrollback.size())) return empty;
    return m_scrollback[index];
}

void TerminalEmulator::resize(int cols, int rows) {
    if (cols == m_cols && rows == m_rows) return;
    
    // Simple resize: grow/shrink grid
    m_cols = cols;
    m_rows = rows;
    m_scrollBottom = rows - 1;
    m_scrollTop = 0;

    // Adjust grid rows
    while (static_cast<int>(m_grid.size()) < rows) {
        m_grid.push_back(makeRow());
    }
    while (static_cast<int>(m_grid.size()) > rows) {
        m_scrollback.push_back(std::move(m_grid.front()));
        m_grid.erase(m_grid.begin());
    }
    // Adjust grid cols
    for (auto& row : m_grid) {
        row.resize(cols, makeCell(' '));
    }
    // Trim scrollback
    while (static_cast<int>(m_scrollback.size()) > MAX_SCROLLBACK) {
        m_scrollback.erase(m_scrollback.begin());
    }

    clampCursor();
}

void TerminalEmulator::clampCursor() {
    m_cursorCol = std::clamp(m_cursorCol, 0, m_cols - 1);
    m_cursorRow = std::clamp(m_cursorRow, 0, m_rows - 1);
}

// ─────────────────────────────────────────────────────────────────
// Feed raw PTY output into the parser
// ─────────────────────────────────────────────────────────────────

void TerminalEmulator::processOutput(const char* data, int length) {
    for (int i = 0; i < length; ++i) {
        char ch = data[i];
        switch (m_state) {
            case State::Normal:   processNormal(ch); break;
            case State::Escape:   processEscape(ch); break;
            case State::CSI:      processCSI(ch);    break;
            case State::OSC:      processOSC(ch);    break;
            case State::OSCEscape:
                if (ch == '\\') {
                    // ST (String Terminator) — end of OSC
                    m_title = m_oscString;
                    m_state = State::Normal;
                } else {
                    m_state = State::Normal; // malformed, bail
                }
                break;
            case State::StringSequence:
                // Consume DCS/APC/PM payload until ESC (for ST) or BEL
                if (ch == '\x1b') m_state = State::StringSeqEscape;
                else if (ch == '\a') m_state = State::Normal;
                break;
            case State::StringSeqEscape:
                // ESC \ = ST (end of string sequence)
                m_state = State::Normal; // whether '\\' or not, bail out
                break;
        }
    }
}

void TerminalEmulator::processNormal(char ch) {
    unsigned char uch = static_cast<unsigned char>(ch);

    // UTF-8 continuation byte
    if (m_utf8Remaining > 0) {
        if ((uch & 0xC0) == 0x80) {
            m_utf8Codepoint = (m_utf8Codepoint << 6) | (uch & 0x3F);
            m_utf8Remaining--;
            if (m_utf8Remaining == 0) {
                putChar(m_utf8Codepoint);
            }
            return;
        }
        // Invalid continuation — reset and fall through to process this byte
        m_utf8Remaining = 0;
    }

    // Control characters (all single-byte ASCII)
    switch (ch) {
        case '\x1b': m_state = State::Escape; return;
        case '\r':   carriageReturn(); return;
        case '\n':   newLine(); return;
        case '\b':   backspace(); return;
        case '\t':   tab(); return;
        case '\a':   return; // bell
        case '\x0f': return; // SI
        case '\x0e': return; // SO
        default: break;
    }

    // UTF-8 start bytes
    if (uch >= 0xF0 && uch < 0xF8) {
        m_utf8Codepoint = uch & 0x07;
        m_utf8Remaining = 3;
    } else if (uch >= 0xE0) {
        m_utf8Codepoint = uch & 0x0F;
        m_utf8Remaining = 2;
    } else if (uch >= 0xC0) {
        m_utf8Codepoint = uch & 0x1F;
        m_utf8Remaining = 1;
    } else if (uch >= 32) {
        putChar(static_cast<char32_t>(uch));
    }
}

void TerminalEmulator::processEscape(char ch) {
    switch (ch) {
        case '[':
            m_state = State::CSI;
            m_csiParams.clear();
            break;
        case ']':
            m_state = State::OSC;
            m_oscString.clear();
            break;
        case 'P': // DCS — Device Control String
        case '_': // APC — Application Program Command
        case '^': // PM — Privacy Message
            m_state = State::StringSequence; // consume until ST
            break;
        case '7': saveCursor(); m_state = State::Normal; break;
        case '8': restoreCursor(); m_state = State::Normal; break;
        case 'D': // Index (scroll up)
            if (m_cursorRow == m_scrollBottom) scrollUp();
            else m_cursorRow++;
            m_state = State::Normal;
            break;
        case 'M': // Reverse Index (scroll down)
            if (m_cursorRow == m_scrollTop) scrollDown();
            else m_cursorRow--;
            m_state = State::Normal;
            break;
        case 'c': // RIS — full reset
            *this = TerminalEmulator(m_cols, m_rows);
            break;
        default:
            m_state = State::Normal;
            break;
    }
}

void TerminalEmulator::processCSI(char ch) {
    if (ch >= 0x20 && ch <= 0x3f) {
        // Parameter or intermediate byte
        m_csiParams += ch;
    } else if (ch >= 0x40 && ch <= 0x7e) {
        // Final byte — execute
        executeCSI(ch);
        m_state = State::Normal;
    } else {
        // Invalid — bail
        m_state = State::Normal;
    }
}

void TerminalEmulator::processOSC(char ch) {
    if (ch == '\x1b') {
        m_state = State::OSCEscape; // ESC waiting for backslash
    } else if (ch == '\a') {
        // BEL terminates OSC
        m_title = m_oscString;
        m_state = State::Normal;
    } else {
        m_oscString += ch;
    }
}

// ─────────────────────────────────────────────────────────────────
// CSI command execution
// ─────────────────────────────────────────────────────────────────

std::vector<int> TerminalEmulator::parseCSIParams() const {
    std::vector<int> params;
    // Strip leading '?' or '>' prefix (private mode)
    std::string paramStr = m_csiParams;
    if (!paramStr.empty() && (paramStr[0] == '?' || paramStr[0] == '>' || paramStr[0] == '!')) {
        paramStr = paramStr.substr(1);
    }
    
    std::istringstream ss(paramStr);
    std::string tok;
    while (std::getline(ss, tok, ';')) {
        if (tok.empty()) params.push_back(0);
        else {
            try { params.push_back(std::stoi(tok)); }
            catch (...) { params.push_back(0); }
        }
    }
    return params;
}

void TerminalEmulator::executeCSI(char cmd) {
    auto params = parseCSIParams();
    bool isPrivate = !m_csiParams.empty() && m_csiParams[0] == '?';

    switch (cmd) {
        case 'A': // CUU — Cursor Up
            moveCursorUp(params.empty() ? 1 : std::max(1, params[0]));
            break;
        case 'B': // CUD — Cursor Down
            moveCursorDown(params.empty() ? 1 : std::max(1, params[0]));
            break;
        case 'C': // CUF — Cursor Forward
            moveCursorForward(params.empty() ? 1 : std::max(1, params[0]));
            break;
        case 'D': // CUB — Cursor Back
            moveCursorBack(params.empty() ? 1 : std::max(1, params[0]));
            break;
        case 'E': // CNL — Cursor Next Line
            moveCursorDown(params.empty() ? 1 : std::max(1, params[0]));
            m_cursorCol = 0;
            break;
        case 'F': // CPL — Cursor Previous Line
            moveCursorUp(params.empty() ? 1 : std::max(1, params[0]));
            m_cursorCol = 0;
            break;
        case 'G': // CHA — Cursor Horizontal Absolute
            m_cursorCol = (params.empty() ? 1 : params[0]) - 1;
            clampCursor();
            break;
        case 'H': // CUP — Cursor Position
        case 'f': // HVP — same as CUP
        {
            int row = (params.size() >= 1 ? params[0] : 1) - 1;
            int col = (params.size() >= 2 ? params[1] : 1) - 1;
            setCursorPosition(row, col);
            break;
        }
        case 'J': // ED — Erase in Display
            eraseInDisplay(params.empty() ? 0 : params[0]);
            break;
        case 'K': // EL — Erase in Line
            eraseInLine(params.empty() ? 0 : params[0]);
            break;
        case 'L': // IL — Insert Lines
            insertLines(params.empty() ? 1 : std::max(1, params[0]));
            break;
        case 'M': // DL — Delete Lines
            deleteLines(params.empty() ? 1 : std::max(1, params[0]));
            break;
        case 'P': // DCH — Delete Characters
            deleteChars(params.empty() ? 1 : std::max(1, params[0]));
            break;
        case 'X': // ECH — Erase Characters
            eraseChars(params.empty() ? 1 : std::max(1, params[0]));
            break;
        case 'S': // SU — Scroll Up
            for (int i = 0; i < (params.empty() ? 1 : std::max(1, params[0])); ++i) scrollUp();
            break;
        case 'T': // SD — Scroll Down
            for (int i = 0; i < (params.empty() ? 1 : std::max(1, params[0])); ++i) scrollDown();
            break;
        case 'd': // VPA — Line Position Absolute
            m_cursorRow = (params.empty() ? 1 : params[0]) - 1;
            clampCursor();
            break;
        case 'm': // SGR — Select Graphic Rendition
        {
            // SGR needs special parsing: some terminals use colon subparameters
            // (e.g., "38:2:R:G:B" instead of "38;2;R;G;B"). Parse both.
            std::string paramStr = m_csiParams;
            if (!paramStr.empty() && (paramStr[0] == '?' || paramStr[0] == '>' || paramStr[0] == '!'))
                paramStr = paramStr.substr(1);
            // Replace colons with semicolons so "38:2:255:0:0" becomes "38;2;255;0;0"
            for (char& c : paramStr) if (c == ':') c = ';';
            std::vector<int> sgrParams;
            std::istringstream ss(paramStr);
            std::string tok;
            while (std::getline(ss, tok, ';')) {
                if (tok.empty()) sgrParams.push_back(0);
                else { try { sgrParams.push_back(std::stoi(tok)); } catch (...) { sgrParams.push_back(0); } }
            }
            if (sgrParams.empty()) sgrParams.push_back(0);
            executeSGR(sgrParams);
            break;
        }
        case 'r': // DECSTBM — Set Scrolling Region
        {
            int top = (params.size() >= 1 && params[0] > 0) ? params[0] - 1 : 0;
            int bottom = (params.size() >= 2 && params[1] > 0) ? params[1] - 1 : m_rows - 1;
            setScrollRegion(top, bottom);
            break;
        }
        case 'h': // SM / DECSET
            if (isPrivate) {
                for (auto p : params) {
                    if (p == 25) m_cursorVisible = true;       // DECTCEM
                    if (p == 1049) {
                        // Save screen + switch to alternate screen buffer
                        m_altGrid = m_grid;
                        m_altCursorCol = m_cursorCol;
                        m_altCursorRow = m_cursorRow;
                        m_altScreenActive = true;
                        eraseInDisplay(2);
                        setCursorPosition(0, 0);
                    }
                }
            }
            break;
        case 'l': // RM / DECRST
            if (isPrivate) {
                for (auto p : params) {
                    if (p == 25) m_cursorVisible = false;      // DECTCEM
                    if (p == 1049 && m_altScreenActive) {
                        // Restore main screen buffer
                        m_grid = m_altGrid;
                        m_cursorCol = m_altCursorCol;
                        m_cursorRow = m_altCursorRow;
                        m_altGrid.clear();
                        m_altScreenActive = false;
                    }
                }
            }
            break;
        case 's': // SCP — Save Cursor Position
            saveCursor();
            break;
        case 'u': // RCP — Restore Cursor Position
            restoreCursor();
            break;
        case '@': // ICH — Insert Characters (shift right)
        {
            int n = params.empty() ? 1 : std::max(1, params[0]);
            auto& row = m_grid[m_cursorRow];
            for (int i = m_cols - 1; i >= m_cursorCol + n; --i) {
                row[i] = row[i - n];
            }
            for (int i = m_cursorCol; i < std::min(m_cursorCol + n, m_cols); ++i) {
                row[i] = makeCell(' ');
            }
            break;
        }
        default:
            break; // Unhandled — silently ignore
    }
}

void TerminalEmulator::executeSGR(const std::vector<int>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        int p = params[i];
        switch (p) {
            case 0: // Reset
                m_currentFg = DEFAULT_FG; m_currentBg = DEFAULT_BG;
                m_bold = false; m_underline = false; m_inverse = false;
                break;
            case 1: m_bold = true; break;
            case 2: break;    // dim — ignore
            case 3: break;    // italic — ignore
            case 4: m_underline = true; break;
            case 7: m_inverse = true; break;
            case 22: m_bold = false; break;
            case 24: m_underline = false; break;
            case 27: m_inverse = false; break;
            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                m_currentFg = ansi256Color(static_cast<uint8_t>(p - 30 + (m_bold ? 8 : 0)));
                break;
            case 38: // Extended foreground
                if (i + 1 < params.size() && params[i + 1] == 5 && i + 2 < params.size()) {
                    // 256-color mode: ESC[38;5;Nm
                    m_currentFg = ansi256Color(static_cast<uint8_t>(std::clamp(params[i + 2], 0, 255)));
                    i += 2;
                } else if (i + 1 < params.size() && params[i + 1] == 2 && i + 4 < params.size()) {
                    // 24-bit RGB mode: ESC[38;2;R;G;Bm
                    m_currentFg = packColor(
                        static_cast<uint8_t>(std::clamp(params[i + 2], 0, 255)),
                        static_cast<uint8_t>(std::clamp(params[i + 3], 0, 255)),
                        static_cast<uint8_t>(std::clamp(params[i + 4], 0, 255)));
                    i += 4;
                }
                break;
            case 39: m_currentFg = DEFAULT_FG; break; // Default foreground
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                m_currentBg = ansi256Color(static_cast<uint8_t>(p - 40));
                break;
            case 48: // Extended background
                if (i + 1 < params.size() && params[i + 1] == 5 && i + 2 < params.size()) {
                    // 256-color mode: ESC[48;5;Nm
                    m_currentBg = ansi256Color(static_cast<uint8_t>(std::clamp(params[i + 2], 0, 255)));
                    i += 2;
                } else if (i + 1 < params.size() && params[i + 1] == 2 && i + 4 < params.size()) {
                    // 24-bit RGB mode: ESC[48;2;R;G;Bm
                    m_currentBg = packColor(
                        static_cast<uint8_t>(std::clamp(params[i + 2], 0, 255)),
                        static_cast<uint8_t>(std::clamp(params[i + 3], 0, 255)),
                        static_cast<uint8_t>(std::clamp(params[i + 4], 0, 255)));
                    i += 4;
                }
                break;
            case 49: m_currentBg = DEFAULT_BG; break; // Default background
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                m_currentFg = ansi256Color(static_cast<uint8_t>(p - 90 + 8)); // Bright foreground
                break;
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                m_currentBg = ansi256Color(static_cast<uint8_t>(p - 100 + 8)); // Bright background
                break;
            default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// Terminal operations
// ─────────────────────────────────────────────────────────────────

void TerminalEmulator::putChar(char32_t ch) {
    if (m_cursorCol >= m_cols) {
        // Line wrap
        m_cursorCol = 0;
        newLine();
    }
    m_grid[m_cursorRow][m_cursorCol] = makeCell(ch);
    m_cursorCol++;
}

void TerminalEmulator::newLine() {
    if (m_cursorRow == m_scrollBottom) {
        scrollUp();
    } else if (m_cursorRow < m_rows - 1) {
        m_cursorRow++;
    }
}

void TerminalEmulator::scrollUp() {
    // Move top line of scroll region to scrollback
    if (m_scrollTop == 0) {
        m_scrollback.push_back(std::move(m_grid[m_scrollTop]));
        if (static_cast<int>(m_scrollback.size()) > MAX_SCROLLBACK) {
            m_scrollback.erase(m_scrollback.begin());
        }
    }
    // Shift lines up within scroll region
    for (int r = m_scrollTop; r < m_scrollBottom; ++r) {
        m_grid[r] = std::move(m_grid[r + 1]);
    }
    m_grid[m_scrollBottom] = makeRow();
}

void TerminalEmulator::scrollDown() {
    // Shift lines down within scroll region
    for (int r = m_scrollBottom; r > m_scrollTop; --r) {
        m_grid[r] = std::move(m_grid[r - 1]);
    }
    m_grid[m_scrollTop] = makeRow();
}

void TerminalEmulator::carriageReturn() {
    m_cursorCol = 0;
}

void TerminalEmulator::backspace() {
    if (m_cursorCol > 0) m_cursorCol--;
}

void TerminalEmulator::tab() {
    m_cursorCol = std::min(m_cols - 1, (m_cursorCol / 8 + 1) * 8);
}

void TerminalEmulator::eraseInDisplay(int mode) {
    switch (mode) {
        case 0: // Erase below (cursor to end)
            eraseInLine(0);
            for (int r = m_cursorRow + 1; r < m_rows; ++r)
                m_grid[r] = makeRow();
            break;
        case 1: // Erase above (start to cursor)
            eraseInLine(1);
            for (int r = 0; r < m_cursorRow; ++r)
                m_grid[r] = makeRow();
            break;
        case 2: // Erase all
        case 3: // Erase all + scrollback
            for (int r = 0; r < m_rows; ++r)
                m_grid[r] = makeRow();
            if (mode == 3) m_scrollback.clear();
            break;
    }
}

void TerminalEmulator::eraseInLine(int mode) {
    auto& row = m_grid[m_cursorRow];
    switch (mode) {
        case 0: // Cursor to end
            for (int c = m_cursorCol; c < m_cols; ++c)
                row[c] = makeCell(' ');
            break;
        case 1: // Start to cursor
            for (int c = 0; c <= m_cursorCol; ++c)
                row[c] = makeCell(' ');
            break;
        case 2: // Entire line
            for (int c = 0; c < m_cols; ++c)
                row[c] = makeCell(' ');
            break;
    }
}

void TerminalEmulator::setCursorPosition(int row, int col) {
    m_cursorRow = std::clamp(row, 0, m_rows - 1);
    m_cursorCol = std::clamp(col, 0, m_cols - 1);
}

void TerminalEmulator::moveCursorUp(int n) {
    m_cursorRow = std::max(0, m_cursorRow - n);
}

void TerminalEmulator::moveCursorDown(int n) {
    m_cursorRow = std::min(m_rows - 1, m_cursorRow + n);
}

void TerminalEmulator::moveCursorForward(int n) {
    m_cursorCol = std::min(m_cols - 1, m_cursorCol + n);
}

void TerminalEmulator::moveCursorBack(int n) {
    m_cursorCol = std::max(0, m_cursorCol - n);
}

void TerminalEmulator::insertLines(int n) {
    if (m_cursorRow < m_scrollTop || m_cursorRow > m_scrollBottom) return;
    for (int i = 0; i < n; ++i) {
        if (m_scrollBottom < static_cast<int>(m_grid.size())) {
            m_grid.erase(m_grid.begin() + m_scrollBottom);
        }
        m_grid.insert(m_grid.begin() + m_cursorRow, makeRow());
    }
}

void TerminalEmulator::deleteLines(int n) {
    if (m_cursorRow < m_scrollTop || m_cursorRow > m_scrollBottom) return;
    for (int i = 0; i < n; ++i) {
        m_grid.erase(m_grid.begin() + m_cursorRow);
        m_grid.insert(m_grid.begin() + m_scrollBottom, makeRow());
    }
}

void TerminalEmulator::deleteChars(int n) {
    auto& row = m_grid[m_cursorRow];
    for (int i = m_cursorCol; i < m_cols; ++i) {
        int src = i + n;
        row[i] = (src < m_cols) ? row[src] : makeCell(' ');
    }
}

void TerminalEmulator::eraseChars(int n) {
    auto& row = m_grid[m_cursorRow];
    for (int i = m_cursorCol; i < std::min(m_cursorCol + n, m_cols); ++i) {
        row[i] = makeCell(' ');
    }
}

void TerminalEmulator::setScrollRegion(int top, int bottom) {
    m_scrollTop = std::clamp(top, 0, m_rows - 1);
    m_scrollBottom = std::clamp(bottom, 0, m_rows - 1);
    if (m_scrollTop > m_scrollBottom) std::swap(m_scrollTop, m_scrollBottom);
    m_cursorCol = 0;
    m_cursorRow = m_scrollTop;
}

void TerminalEmulator::saveCursor() {
    m_savedCursorCol = m_cursorCol;
    m_savedCursorRow = m_cursorRow;
}

void TerminalEmulator::restoreCursor() {
    m_cursorCol = m_savedCursorCol;
    m_cursorRow = m_savedCursorRow;
    clampCursor();
}

} // namespace Phyxel::UI
