// docs/VoxelFoundationPlan.md step 7 D3c-2 -- the v1 row -> PXB2 migration WITHOUT a Chunk object
// (encodePxb2FromRows) equals the legacy Chunk + ChunkBlobCodec::encode path, chunk by chunk, on real v1 DBs.
// PHYXEL_V1_DBS = ';'-separated paths of v1 world DBs (copied to a temp file before opening; never migrated).
// Equality is CONTENT (cube material per index, the sorted subcube / microcube entries, by material NAME) -- the
// encodings may order the palette or the sparse entries differently; byte-identity is reported, not required.
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>
#include <tuple>

#include "core/WorldStorage.h"
#include "core/voxfound/StorageLab.h"

using namespace Phyxel;

namespace {

struct Canon {
    std::vector<std::string> cubes;                                   // 32768 material names ("" = air)
    std::vector<std::tuple<int, int, int, std::string>> subs, micros; // (cube, sub, micro, material), sorted
    bool ok = false;
};

Canon canon(const std::vector<uint8_t>& b) {
    Canon c;
    size_t at = 0;
    auto need = [&](size_t n) { return at + n <= b.size(); };
    auto u8 = [&]() { return need(1) ? uint32_t(b[at++]) : 0u; };
    auto u16 = [&]() { uint32_t v = u8(); return v | (u8() << 8); };
    auto u32 = [&]() { uint32_t v = u16(); return v | (u16() << 16); };
    if (u32() != 0x32425850u) return c;
    u8();
    const bool wide = (u8() & 1u) != 0;
    const uint32_t nPal = u16(), nCube = u32(), nSub = u32(), nMicro = u32();
    (void)nCube;
    std::vector<std::string> pal(nPal);
    for (auto& n : pal) {
        const uint32_t l = u8();
        if (!need(l)) return c;
        n.assign(reinterpret_cast<const char*>(b.data() + at), l);
        at += l;
    }
    auto idx = [&]() { return wide ? u16() : u8(); };
    const uint32_t runs = u32();
    for (uint32_t r = 0; r < runs; ++r) {
        const uint32_t len = u16(), i = idx();
        for (uint32_t k = 0; k < len; ++k) c.cubes.push_back(i < pal.size() ? pal[i] : "?");
    }
    auto skipState = [&]() { if (u8() & 0x80u) at += 3; };
    for (uint32_t k = 0; k < nSub; ++k) {
        const int cube = int(u16()), sub = int(u8());
        const uint32_t i = idx();
        skipState();
        c.subs.emplace_back(cube, sub, 0, i < pal.size() ? pal[i] : "?");
    }
    for (uint32_t k = 0; k < nMicro; ++k) {
        const int cube = int(u16()), sub = int(u8()), mic = int(u8());
        const uint32_t i = idx();
        skipState();
        c.micros.emplace_back(cube, sub, mic, i < pal.size() ? pal[i] : "?");
    }
    std::sort(c.subs.begin(), c.subs.end());
    std::sort(c.micros.begin(), c.micros.end());
    c.ok = c.cubes.size() == 32768;
    return c;
}

}  // namespace

TEST(VoxFoundV1Migration, RowsEncoderEqualsChunkEncoder) {
    const char* env = std::getenv("PHYXEL_V1_DBS");
    if (!env || !*env) GTEST_SKIP() << "PHYXEL_V1_DBS not set (real v1 DBs, run by hand)";
    std::stringstream ss(env);
    std::string path;
    size_t dbs = 0, chunks = 0, equal = 0, bytesEqual = 0;
    while (std::getline(ss, path, ';')) {
        if (path.empty()) continue;
        const auto tmp = std::filesystem::temp_directory_path() / ("v1mig_" + std::to_string(dbs) + ".db");
        std::filesystem::copy_file(path, tmp, std::filesystem::copy_options::overwrite_existing);
        WorldStorage::s_skipMigration = true;
        WorldStorage ws(tmp.string());
        ASSERT_TRUE(ws.initialize()) << path;
        WorldStorage::s_skipMigration = false;
        ++dbs;
        size_t dbBad = 0;
        for (const auto& cc : ws.legacyChunkCoords()) {
            const std::vector<uint8_t> old = ws.encodeLegacyViaChunk(cc);
            Core::VoxFound::V1ChunkRows rows;
            ASSERT_TRUE(ws.readLegacyRows(cc, rows));
            const std::vector<uint8_t> nu = Core::VoxFound::encodePxb2FromRows(rows);
            ++chunks;
            if (old == nu) ++bytesEqual;
            const Canon a = canon(old), b = canon(nu);
            ASSERT_TRUE(a.ok && b.ok) << path << " chunk " << cc.x << "," << cc.y << "," << cc.z;
            const bool same = a.cubes == b.cubes && a.subs == b.subs && a.micros == b.micros;
            if (same) ++equal;
            else if (dbBad++ < 3) {
                size_t cd = 0;
                for (size_t i = 0; i < 32768; ++i) cd += a.cubes[i] != b.cubes[i];
                ADD_FAILURE() << path << " chunk (" << cc.x << "," << cc.y << "," << cc.z << "): cubes differ " << cd
                              << ", subs " << a.subs.size() << " vs " << b.subs.size() << ", micros "
                              << a.micros.size() << " vs " << b.micros.size();
            }
        }
        std::printf("[v1mig] %s: %zu chunks so far\n", path.c_str(), chunks);
    }
    std::printf("[v1mig] %zu DBs, %zu chunks: content-equal %zu, byte-equal %zu\n", dbs, chunks, equal, bytesEqual);
    EXPECT_EQ(equal, chunks);
}
