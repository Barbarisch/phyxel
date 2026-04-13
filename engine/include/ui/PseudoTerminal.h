#pragma once

#ifdef _WIN32

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <windows.h>

namespace Phyxel::UI {

/// Win32 ConPTY wrapper — spawns a child process with a pseudo-terminal.
/// Provides non-blocking read via a background reader thread.
class PseudoTerminal {
public:
    PseudoTerminal();
    ~PseudoTerminal();

    PseudoTerminal(const PseudoTerminal&) = delete;
    PseudoTerminal& operator=(const PseudoTerminal&) = delete;

    /// Spawn a child process with a pseudo-terminal.
    /// @param command  Command line to execute (e.g. "powershell.exe" or "cmd.exe")
    /// @param cols     Initial terminal width in columns
    /// @param rows     Initial terminal height in rows
    /// @return true on success
    bool spawn(const std::string& command, int cols = 120, int rows = 30);

    /// Close the pseudo-terminal and terminate the child.
    void close();

    /// Check if the child process is still running.
    bool isAlive() const;

    /// Drain buffered output from the reader thread (non-blocking).
    /// Returns the accumulated bytes since last drain.
    std::string drainOutput();

    /// Write input bytes to the child's stdin.
    bool write(const char* data, int length);
    bool write(const std::string& data) { return write(data.data(), static_cast<int>(data.size())); }

    /// Resize the pseudo-terminal.
    void resize(int cols, int rows);

    int getCols() const { return m_cols; }
    int getRows() const { return m_rows; }

private:
    void readerThread();

    HPCON m_hPC = nullptr;
    HANDLE m_inputWrite = INVALID_HANDLE_VALUE;
    HANDLE m_outputRead = INVALID_HANDLE_VALUE;
    HANDLE m_inputRead = INVALID_HANDLE_VALUE;   // PTY side
    HANDLE m_outputWrite = INVALID_HANDLE_VALUE;  // PTY side
    PROCESS_INFORMATION m_processInfo = {};

    std::thread m_reader;
    std::mutex m_bufferMutex;
    std::string m_readBuffer;
    std::atomic<bool> m_running{false};

    int m_cols = 120;
    int m_rows = 30;
};

} // namespace Phyxel::UI

#endif // _WIN32
