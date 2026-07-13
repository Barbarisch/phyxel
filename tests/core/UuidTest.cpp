#include <gtest/gtest.h>

#include "core/Uuid.h"

#include <regex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace Phyxel {
namespace {

using Core::Uuid::generate;
using Core::Uuid::isNilOrEmpty;
using Core::Uuid::isValid;
using Core::Uuid::nil;

// Canonical RFC-4122 v4: 8-4-4-4-12 lowercase hex, version nibble '4', variant in [89ab].
const std::regex kV4Re(
    "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");

TEST(UuidTest, GeneratesCanonicalV4Format) {
    // Falsifiable: a wrong version/variant/length/separator fails the regex. If
    // generate() emitted the version nibble as '3', or uppercase, this fails.
    for (int i = 0; i < 1000; ++i) {
        const std::string u = generate();
        ASSERT_EQ(u.size(), 36u) << u;
        EXPECT_TRUE(std::regex_match(u, kV4Re)) << "not canonical v4: " << u;
        EXPECT_EQ(u[14], '4') << "version nibble not 4: " << u;
        const char var = u[19];
        EXPECT_TRUE(var == '8' || var == '9' || var == 'a' || var == 'b')
            << "variant nibble wrong: " << u;
    }
}

TEST(UuidTest, IsValidAcceptsGeneratedRejectsLegacy) {
    EXPECT_TRUE(isValid(generate()));
    // The whole point: legacy ids must NOT validate as uuids (resolve-by-either
    // depends on this to disambiguate).
    EXPECT_FALSE(isValid("test_chair_3"));
    EXPECT_FALSE(isValid("entity_7"));
    EXPECT_FALSE(isValid("player"));
    EXPECT_FALSE(isValid("cottage_1"));
    EXPECT_FALSE(isValid(""));
    EXPECT_FALSE(isValid("550e8400e29b41d4a716446655440000"));       // no hyphens
    EXPECT_FALSE(isValid("550E8400-E29B-41D4-A716-446655440000"));   // uppercase
    EXPECT_FALSE(isValid("550e8400-e29b-31d4-a716-446655440000"));   // version 3
    EXPECT_FALSE(isValid("550e8400-e29b-41d4-c716-446655440000"));   // bad variant 'c'
    EXPECT_FALSE(isValid("550e8400-e29b-41d4-a716-44665544000"));    // too short
    // nil is a sentinel, not a valid v4.
    EXPECT_FALSE(isValid(nil()));
    EXPECT_TRUE(isNilOrEmpty(nil()));
    EXPECT_TRUE(isNilOrEmpty(""));
    EXPECT_FALSE(isNilOrEmpty(generate()));
}

TEST(UuidTest, UniqueAtVolume) {
    // De-risks the "uniqueness at scale" concern before any registry depends on
    // it: 1,000,000 uuids, zero collisions.
    constexpr int kN = 1'000'000;
    std::unordered_set<std::string> seen;
    seen.reserve(kN * 2);
    for (int i = 0; i < kN; ++i) {
        ASSERT_TRUE(seen.insert(generate()).second) << "collision at i=" << i;
    }
    EXPECT_EQ(static_cast<int>(seen.size()), kN);
}

TEST(UuidTest, UniqueAcrossThreads) {
    // thread_local seeding must produce disjoint streams — 8 threads x 100k.
    constexpr int kThreads = 8;
    constexpr int kPer = 100'000;
    std::vector<std::vector<std::string>> perThread(kThreads);
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&perThread, t, kPer] {
            auto& out = perThread[t];
            out.reserve(kPer);
            for (int i = 0; i < kPer; ++i) out.push_back(generate());
        });
    }
    for (auto& w : workers) w.join();

    std::unordered_set<std::string> all;
    all.reserve(kThreads * kPer * 2);
    for (auto& v : perThread)
        for (auto& u : v)
            ASSERT_TRUE(all.insert(u).second) << "cross-thread collision: " << u;
    EXPECT_EQ(static_cast<int>(all.size()), kThreads * kPer);
}

}  // namespace
}  // namespace Phyxel
