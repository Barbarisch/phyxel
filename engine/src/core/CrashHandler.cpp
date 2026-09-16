#include "core/CrashHandler.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace Phyxel {
namespace Core {

namespace {

std::string& reportDir() {
    static std::string dir = "crashes";
    return dir;
}

std::string stampNow() {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmv);
    return buf;
}

#ifdef _WIN32

std::string moduleOf(DWORD64 addr, DWORD64* offset) {
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(addr)), &mod) && mod) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(mod, path, MAX_PATH);
        if (offset) *offset = addr - reinterpret_cast<DWORD64>(mod);
        std::string p = path;
        size_t slash = p.find_last_of("\\/");
        return slash == std::string::npos ? p : p.substr(slash + 1);
    }
    if (offset) *offset = 0;
    return "<unknown>";
}

const char* exceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
        case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
        case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
        case 0xE06D7363:                         return "CPP_EXCEPTION";
        default:                                 return "";
    }
}

// Walk the faulting thread's stack from the exception context and print one
// frame per line: "#n 0xADDR module!symbol+0xOFF (file:line)". Symbols need a
// PDB beside the binary; without one the module+offset still lands.
void writeStack(FILE* f, const CONTEXT& exceptionContext) {
    HANDLE proc = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    const BOOL symOk = SymInitialize(proc, nullptr, TRUE);

    CONTEXT ctx = exceptionContext;   // StackWalk64 mutates its context
    STACKFRAME64 frame{};
#if defined(_M_X64) || defined(__x86_64__)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_ARM64)
    const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset    = ctx.Pc;
    frame.AddrFrame.Offset = ctx.Fp;
    frame.AddrStack.Offset = ctx.Sp;
#else
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset    = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
#endif
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512];
    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(machine, proc, thread, &frame, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        const DWORD64 pc = frame.AddrPC.Offset;
        if (pc == 0) break;
        DWORD64 modOff = 0;
        const std::string mod = moduleOf(pc, &modOff);
        std::fprintf(f, "#%-2d 0x%016llx %s+0x%llx", i, static_cast<unsigned long long>(pc),
                     mod.c_str(), static_cast<unsigned long long>(modOff));
        if (symOk) {
            auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 511;
            DWORD64 disp = 0;
            if (SymFromAddr(proc, pc, &disp, sym)) {
                std::fprintf(f, "  %s+0x%llx", sym->Name, static_cast<unsigned long long>(disp));
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct = sizeof(line);
                DWORD lineDisp = 0;
                if (SymGetLineFromAddr64(proc, pc, &lineDisp, &line))
                    std::fprintf(f, "  (%s:%lu)", line.FileName, static_cast<unsigned long>(line.LineNumber));
            }
        }
        std::fputc('\n', f);
    }
    if (symOk) SymCleanup(proc);
}

std::atomic<bool> g_reporting{false};

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* ep) {
    CrashHandler::writeReport(ep);
    return EXCEPTION_CONTINUE_SEARCH;   // WER still runs: dialog + event-log row stay
}

#endif // _WIN32

} // namespace

const std::string& CrashHandler::directory() { return reportDir(); }

void CrashHandler::install(const std::string& dir) {
    reportDir() = dir.empty() ? std::string("crashes") : dir;
#ifdef _WIN32
    static std::atomic<bool> installed{false};
    if (installed.exchange(true)) return;
    SetUnhandledExceptionFilter(unhandledFilter);
#endif
}

std::string CrashHandler::writeReport(void* exceptionPointers) {
#ifdef _WIN32
    auto* ep = static_cast<EXCEPTION_POINTERS*>(exceptionPointers);
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) return "";
    if (g_reporting.exchange(true)) return "";   // a crash inside the reporter: give up

    std::error_code ec;
    std::filesystem::create_directories(reportDir(), ec);
    const std::string base = reportDir() + "/crash_" + stampNow();
    const std::string txtPath = base + ".txt";
    const std::string dmpPath = base + ".dmp";

    const EXCEPTION_RECORD& er = *ep->ExceptionRecord;
    const DWORD64 addr = reinterpret_cast<DWORD64>(er.ExceptionAddress);
    DWORD64 modOff = 0;
    const std::string mod = moduleOf(addr, &modOff);

    FILE* f = nullptr;
    if (fopen_s(&f, txtPath.c_str(), "w") == 0 && f) {
        std::fprintf(f, "Phyxel crash report\n");
        std::fprintf(f, "Exception 0x%08lX %s at 0x%016llx  (%s+0x%llx)\n",
                     static_cast<unsigned long>(er.ExceptionCode), exceptionName(er.ExceptionCode),
                     static_cast<unsigned long long>(addr), mod.c_str(), static_cast<unsigned long long>(modOff));
        if ((er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || er.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
            er.NumberParameters >= 2) {
            const char* kind = er.ExceptionInformation[0] == 0 ? "read" : er.ExceptionInformation[0] == 1 ? "write" : "execute";
            std::fprintf(f, "Access violation: %s of address 0x%016llx\n", kind,
                         static_cast<unsigned long long>(er.ExceptionInformation[1]));
        }
        std::fprintf(f, "Thread %lu   Minidump: %s\n\nStack (faulting thread):\n",
                     static_cast<unsigned long>(GetCurrentThreadId()), dmpPath.c_str());
        writeStack(f, *ep->ContextRecord);
        std::fclose(f);
    }

    HANDLE h = CreateFileA(dmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory |
                                                     MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, type, &mei, nullptr, nullptr);
        CloseHandle(h);
    }

    std::fprintf(stderr, "[CrashHandler] exception 0x%08lX at %s+0x%llx — report: %s\n",
                 static_cast<unsigned long>(er.ExceptionCode), mod.c_str(),
                 static_cast<unsigned long long>(modOff), txtPath.c_str());
    std::fflush(stderr);
    g_reporting = false;
    return txtPath;
#else
    (void)exceptionPointers;
    return "";
#endif
}

} // namespace Core
} // namespace Phyxel
