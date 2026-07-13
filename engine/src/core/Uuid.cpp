#include "core/Uuid.h"

#include <cstdint>
#include <cstdio>
#include <random>

namespace Phyxel {
namespace Core {
namespace Uuid {

namespace {

// Per-thread PRNG, seeded once from random_device. thread_local keeps generate()
// lock-free even when it is called inside a registry mutex. We fold eight
// random_device draws through a seed_seq rather than trusting a single word —
// std::random_device is allowed to be low-entropy / deterministic on some
// standard-library implementations, and eight words makes per-thread streams
// disjoint in practice.
std::mt19937_64& engine() {
    static thread_local std::mt19937_64 e = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    return e;
}

} // namespace

std::string generate() {
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t hi = dist(engine());
    uint64_t lo = dist(engine());

    // 128-bit layout (big-endian fields):
    //   time_low(4) time_mid(2) time_hi_and_version(2) | clk_seq(2) node(6)
    //   ^hi[63:32]  ^hi[31:16]  ^hi[15:0]                ^lo[63:48]  ^lo[47:0]
    // Version nibble = hi[15:12] -> 0x4; variant top two bits = lo[63:62] -> 0b10.
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL; // version 4
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL; // variant 10xx

    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                  static_cast<uint32_t>(hi >> 32),
                  static_cast<uint32_t>((hi >> 16) & 0xFFFFULL),
                  static_cast<uint32_t>(hi & 0xFFFFULL),
                  static_cast<uint32_t>(lo >> 48),
                  static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
    return std::string(buf);
}

bool isValid(const std::string& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else {
            const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            if (!hex) return false;  // canonical form is lowercase hex only
        }
    }
    if (s[14] != '4') return false;                    // version 4
    const char var = s[19];                            // RFC-4122 variant
    return var == '8' || var == '9' || var == 'a' || var == 'b';
}

const std::string& nil() {
    static const std::string k = "00000000-0000-0000-0000-000000000000";
    return k;
}

bool isNilOrEmpty(const std::string& s) {
    return s.empty() || s == nil();
}

} // namespace Uuid
} // namespace Core
} // namespace Phyxel
