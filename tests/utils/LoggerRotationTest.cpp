#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "utils/Logger.h"

namespace fs = std::filesystem;
using Phyxel::Utils::Logger;

// ============================================================================
// Log rotation (robustness, 2026-08-20): phyxel.log opened append-FOREVER and
// had grown to 11.5M lines across sessions — and an unbounded log is exactly
// the artifact you can't read when a silent death needs diagnosing. The file
// is now size-capped: rotated at open time (an oversized leftover moves to
// .1 before the session starts) AND at runtime (a spammy session rolls over
// mid-flight), keeping <name>.1 / <name>.2 generations.
//
// The Logger is a process singleton — each test restores file output OFF and
// the default cap so the rest of the suite is unaffected.
// ============================================================================

namespace {

struct TmpLogDir {
    fs::path dir;
    TmpLogDir() {
        dir = fs::temp_directory_path() / "phyxel_logger_rotation_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
    }
    ~TmpLogDir() {
        Logger::enableFileOutput(false);
        Logger::setMaxLogFileBytes(64ull * 1024 * 1024);   // restore default
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string log() const { return (dir / "test.log").string(); }
};

}  // namespace

// Runtime rotation: writing past the cap rolls the file over mid-session.
TEST(LoggerRotationTest, RotatesAtRuntimeWhenCapExceeded) {
    TmpLogDir t;
    Logger::setMaxLogFileBytes(4096);
    Logger::enableFileOutput(true, t.log());

    const std::string filler(100, 'x');
    for (int i = 0; i < 200; ++i) Logger::info("RotTest", filler);   // ~20 KB total
    Logger::flush();

    ASSERT_TRUE(fs::exists(t.log()));
    EXPECT_LT(fs::file_size(t.log()), 4096u + 512u)
        << "current log did not roll over at the cap";
    EXPECT_TRUE(fs::exists(t.log() + ".1")) << "no rotated generation was produced";
}

// Open-time rotation: an oversized leftover from an earlier session is moved
// aside before this session appends to it.
TEST(LoggerRotationTest, RotatesOversizedFileAtOpen) {
    TmpLogDir t;
    {
        std::ofstream pre(t.log(), std::ios::binary);
        pre << std::string(10000, 'y');
    }
    Logger::setMaxLogFileBytes(4096);
    Logger::enableFileOutput(true, t.log());
    Logger::info("RotTest", "first line of the fresh session");
    Logger::flush();

    ASSERT_TRUE(fs::exists(t.log() + ".1")) << "oversized leftover was not rotated at open";
    EXPECT_GE(fs::file_size(t.log() + ".1"), 10000u);
    EXPECT_LT(fs::file_size(t.log()), 1024u) << "session did not start on a fresh file";
}

// Generations shift: .1 becomes .2; only the configured count is kept.
TEST(LoggerRotationTest, KeepsTwoGenerations) {
    TmpLogDir t;
    Logger::setMaxLogFileBytes(2048);
    Logger::enableFileOutput(true, t.log());

    const std::string filler(100, 'z');
    for (int i = 0; i < 200; ++i) Logger::info("RotTest", filler);   // several rotations
    Logger::flush();

    EXPECT_TRUE(fs::exists(t.log() + ".1"));
    EXPECT_TRUE(fs::exists(t.log() + ".2"));
    EXPECT_FALSE(fs::exists(t.log() + ".3")) << "more generations kept than configured";
}

// A small existing file appends as before (no rotation churn under the cap),
// and cap 0 = legacy unlimited.
TEST(LoggerRotationTest, UnderCapAppendsAndZeroMeansUnlimited) {
    TmpLogDir t;
    {
        std::ofstream pre(t.log(), std::ios::binary);
        pre << "previous session line\n";
    }
    Logger::setMaxLogFileBytes(1 << 20);
    Logger::enableFileOutput(true, t.log());
    Logger::info("RotTest", "appended line");
    Logger::flush();
    EXPECT_FALSE(fs::exists(t.log() + ".1")) << "under-cap file should not rotate";
    {
        std::ifstream in(t.log());
        std::string first;
        std::getline(in, first);
        EXPECT_EQ(first, "previous session line") << "append semantics were lost";
    }

    Logger::enableFileOutput(false);
    Logger::setMaxLogFileBytes(0);   // unlimited
    Logger::enableFileOutput(true, t.log());
    const std::string filler(100, 'w');
    for (int i = 0; i < 100; ++i) Logger::info("RotTest", filler);
    Logger::flush();
    EXPECT_FALSE(fs::exists(t.log() + ".1")) << "cap 0 must never rotate";
    EXPECT_GT(fs::file_size(t.log()), 10000u);
}
