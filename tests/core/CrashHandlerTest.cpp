// Core::CrashHandler — the last-chance crash reporter (Ravenmere G-130: a shipped
// game died at a town load leaving only "faulting module: unknown, offset 0").
// Raises a real access violation under SEH, hands the EXCEPTION_POINTERS to the
// reporter exactly as the unhandled filter would, and checks the report names the
// exception, the faulting module and a stack, and that a minidump exists.
#include <gtest/gtest.h>
#include "core/CrashHandler.h"

#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string g_reportPath;

int reportAndSwallow(EXCEPTION_POINTERS* ep) {
    g_reportPath = Phyxel::Core::CrashHandler::writeReport(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

// No C++ objects with destructors in a __try function (C2712).
void faultUnderSeh() {
    __try {
        volatile int* p = nullptr;
        *p = 42;
    } __except (reportAndSwallow(GetExceptionInformation())) {
    }
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

TEST(CrashHandlerTest, ReportNamesTheExceptionModuleAndStackAndWritesAMinidump) {
    const std::string dir = "crash_handler_test_out";
    std::filesystem::remove_all(dir);
    Phyxel::Core::CrashHandler::install(dir);
    EXPECT_EQ(Phyxel::Core::CrashHandler::directory(), dir);

    g_reportPath.clear();
    faultUnderSeh();

    ASSERT_FALSE(g_reportPath.empty()) << "writeReport returned no path";
    ASSERT_TRUE(std::filesystem::exists(g_reportPath));
    const std::string report = slurp(g_reportPath);
    EXPECT_NE(report.find("0xC0000005"), std::string::npos) << report;
    EXPECT_NE(report.find("ACCESS_VIOLATION"), std::string::npos) << report;
    EXPECT_NE(report.find("write of address 0x0000000000000000"), std::string::npos) << report;
    EXPECT_NE(report.find("phyxel_tests.exe+0x"), std::string::npos) << "faulting module missing:\n" << report;
    EXPECT_NE(report.find("\n#0 "), std::string::npos) << "no stack frames:\n" << report;

    const std::string dmp = g_reportPath.substr(0, g_reportPath.size() - 4) + ".dmp";
    ASSERT_TRUE(std::filesystem::exists(dmp)) << dmp;
    EXPECT_GT(std::filesystem::file_size(dmp), 4096u);

    std::filesystem::remove_all(dir);
}

TEST(CrashHandlerTest, NullPointersProduceNoReport) {
    EXPECT_EQ(Phyxel::Core::CrashHandler::writeReport(nullptr), "");
}

#endif // _WIN32
