#pragma once

#include <string>

namespace Phyxel {
namespace Core {

/// Process-wide last-chance crash reporter.
///
/// A shipped game that dies with 0xC0000005 leaves nothing behind but a Windows
/// event-log row ("faulting module: unknown, offset 0") — that is all we had for
/// the Ravenmere town-load crash (gap G-130), and it is not debuggable. On an
/// unhandled SEH exception this writes, before the process dies:
///
///   <dir>/crash_<yyyymmdd_hhmmss>.txt  exception code + address, the faulting
///                                      module + offset, and the faulting thread's
///                                      stack — symbolized (function, file:line)
///                                      when a PDB sits beside the executable
///   <dir>/crash_<yyyymmdd_hhmmss>.dmp  a minidump (open in WinDbg / VS)
///
/// The filter returns EXCEPTION_CONTINUE_SEARCH afterwards, so Windows Error
/// Reporting still runs and the event-log row is still written. Idempotent;
/// a no-op off Windows. `dir` is relative to the working directory.
class CrashHandler {
public:
    static void install(const std::string& dir = "crashes");

    /// Write the report for an exception. `exceptionPointers` is a
    /// `EXCEPTION_POINTERS*` (void to keep <windows.h> out of the header). The
    /// installed filter calls this; tests call it from their own __except.
    /// Returns the .txt path, or "" when nothing could be written.
    static std::string writeReport(void* exceptionPointers);

    static const std::string& directory();
};

} // namespace Core
} // namespace Phyxel
