#ifdef _WIN32

#include "ui/PseudoTerminal.h"
#include "utils/Logger.h"

#include <sstream>

namespace Phyxel::UI {

PseudoTerminal::PseudoTerminal() {
    memset(&m_processInfo, 0, sizeof(m_processInfo));
}

PseudoTerminal::~PseudoTerminal() {
    close();
}

bool PseudoTerminal::spawn(const std::string& command, int cols, int rows) {
    if (m_running) {
        LOG_WARN("PseudoTerminal", "Already running, close first");
        return false;
    }

    m_cols = cols;
    m_rows = rows;

    // Create pipes for PTY I/O
    HANDLE inputRead = INVALID_HANDLE_VALUE;
    HANDLE inputWrite = INVALID_HANDLE_VALUE;
    HANDLE outputRead = INVALID_HANDLE_VALUE;
    HANDLE outputWrite = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0)) {
        LOG_ERROR("PseudoTerminal", "Failed to create input pipe");
        return false;
    }
    if (!CreatePipe(&outputRead, &outputWrite, nullptr, 0)) {
        LOG_ERROR("PseudoTerminal", "Failed to create output pipe");
        CloseHandle(inputRead);
        CloseHandle(inputWrite);
        return false;
    }

    // Create the pseudo console
    COORD size;
    size.X = static_cast<SHORT>(cols);
    size.Y = static_cast<SHORT>(rows);
    HRESULT hr = CreatePseudoConsole(size, inputRead, outputWrite, 0, &m_hPC);
    if (FAILED(hr)) {
        LOG_ERROR("PseudoTerminal", "CreatePseudoConsole failed: 0x" + 
                  ([&]{ std::ostringstream os; os << std::hex << hr; return os.str(); })());
        CloseHandle(inputRead);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        CloseHandle(outputWrite);
        return false;
    }

    // Keep handles for cleanup; our side of the pipes
    m_inputRead = inputRead;
    m_outputWrite = outputWrite;
    m_inputWrite = inputWrite;
    m_outputRead = outputRead;

    // Prepare startup info with the pseudo console
    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    // Allocate attribute list for PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<uint8_t> attrBuf(attrSize);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());

    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize)) {
        LOG_ERROR("PseudoTerminal", "InitializeProcThreadAttributeList failed");
        ClosePseudoConsole(m_hPC);
        m_hPC = nullptr;
        CloseHandle(inputRead); CloseHandle(inputWrite);
        CloseHandle(outputRead); CloseHandle(outputWrite);
        return false;
    }

    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_hPC, sizeof(HPCON), nullptr, nullptr)) {
        LOG_ERROR("PseudoTerminal", "UpdateProcThreadAttribute failed");
        DeleteProcThreadAttributeList(si.lpAttributeList);
        ClosePseudoConsole(m_hPC);
        m_hPC = nullptr;
        CloseHandle(inputRead); CloseHandle(inputWrite);
        CloseHandle(outputRead); CloseHandle(outputWrite);
        return false;
    }

    // Convert command to wide string
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wideCmd(wideLen);
    MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wideCmd.data(), wideLen);

    // Create the child process
    if (!CreateProcessW(nullptr, wideCmd.data(), nullptr, nullptr, FALSE,
            EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
            &si.StartupInfo, &m_processInfo)) {
        LOG_ERROR("PseudoTerminal", "CreateProcessW failed: " + std::to_string(GetLastError()));
        DeleteProcThreadAttributeList(si.lpAttributeList);
        ClosePseudoConsole(m_hPC);
        m_hPC = nullptr;
        CloseHandle(inputRead); CloseHandle(inputWrite);
        CloseHandle(outputRead); CloseHandle(outputWrite);
        return false;
    }

    DeleteProcThreadAttributeList(si.lpAttributeList);

    // Close the PTY-side pipe handles (child owns them now)
    CloseHandle(m_inputRead);
    m_inputRead = INVALID_HANDLE_VALUE;
    CloseHandle(m_outputWrite);
    m_outputWrite = INVALID_HANDLE_VALUE;

    m_running = true;
    m_reader = std::thread(&PseudoTerminal::readerThread, this);

    LOG_INFO("PseudoTerminal", "Spawned: " + command + " (" + 
             std::to_string(cols) + "x" + std::to_string(rows) + ")");
    return true;
}

void PseudoTerminal::close() {
    m_running = false;

    // Close our write handle so the child sees EOF
    if (m_inputWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(m_inputWrite);
        m_inputWrite = INVALID_HANDLE_VALUE;
    }

    // Close pseudo console (this also signals the child)
    if (m_hPC) {
        ClosePseudoConsole(m_hPC);
        m_hPC = nullptr;
    }

    // Wait for reader thread
    if (m_reader.joinable()) {
        m_reader.join();
    }

    // Close remaining handles
    if (m_outputRead != INVALID_HANDLE_VALUE) {
        CloseHandle(m_outputRead);
        m_outputRead = INVALID_HANDLE_VALUE;
    }
    if (m_inputRead != INVALID_HANDLE_VALUE) {
        CloseHandle(m_inputRead);
        m_inputRead = INVALID_HANDLE_VALUE;
    }
    if (m_outputWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(m_outputWrite);
        m_outputWrite = INVALID_HANDLE_VALUE;
    }

    // Cleanup process handles
    if (m_processInfo.hProcess) {
        TerminateProcess(m_processInfo.hProcess, 0);
        CloseHandle(m_processInfo.hProcess);
        m_processInfo.hProcess = nullptr;
    }
    if (m_processInfo.hThread) {
        CloseHandle(m_processInfo.hThread);
        m_processInfo.hThread = nullptr;
    }
}

bool PseudoTerminal::isAlive() const {
    if (!m_running || !m_processInfo.hProcess) return false;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(m_processInfo.hProcess, &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    return false;
}

std::string PseudoTerminal::drainOutput() {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    std::string result;
    result.swap(m_readBuffer);
    return result;
}

bool PseudoTerminal::write(const char* data, int length) {
    if (m_inputWrite == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    return WriteFile(m_inputWrite, data, static_cast<DWORD>(length), &written, nullptr) && 
           written == static_cast<DWORD>(length);
}

void PseudoTerminal::resize(int cols, int rows) {
    m_cols = cols;
    m_rows = rows;
    if (m_hPC) {
        COORD size;
        size.X = static_cast<SHORT>(cols);
        size.Y = static_cast<SHORT>(rows);
        ResizePseudoConsole(m_hPC, size);
    }
}

void PseudoTerminal::readerThread() {
    char buf[4096];
    while (m_running) {
        DWORD bytesRead = 0;
        // ReadFile blocks until data is available or pipe is closed
        BOOL ok = ReadFile(m_outputRead, buf, sizeof(buf), &bytesRead, nullptr);
        if (!ok || bytesRead == 0) {
            m_running = false;
            break;
        }
        {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_readBuffer.append(buf, bytesRead);
        }
    }
}

} // namespace Phyxel::UI

#endif // _WIN32
