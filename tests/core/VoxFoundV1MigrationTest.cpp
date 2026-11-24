// docs/VoxelFoundationPlan.md step 7 D3c-2 -- the v1 row -> PXB2 migration WITHOUT a Chunk object
// (encodePxb2FromRows). D3c-5 deleted the legacy Chunk + ChunkBlobCodec::encode A/B; what stays pinned is that the
// rows encoder's blobs round-trip through convertPxb2 (synthetic, and on real v1 DBs when PHYXEL_V1_DBS is set).
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

// The rows encoder's output round-trips through the 1/8 m converter (convertPxb2), which reads the PXB2 format
// itself: every chunk's blob converts, and the converter sees exactly the cube / subcube / microcube entries the
// encoder wrote. (Until D3c-5 this compared against the legacy Chunk + ChunkBlobCodec::encode path; that A/B
// went with the Chunk 1/9 m store.)
TEST(VoxFoundV1Migration, RowsEncoderRoundTripsThroughConvertPxb2) {
    Core::VoxFound::V1ChunkRows rows;
    rows.cubes.push_back({glm::ivec3(1, 2, 3), "Stone"});
    rows.cubes.push_back({glm::ivec3(31, 0, 31), "Wood"});
    rows.subs.push_back({glm::ivec3(4, 5, 6), glm::ivec3(1, 0, 2), "Bricks"});
    for (int s = 0; s < 27; ++s)   // a cell fully packed with one material's subcubes -> one uniform block
        rows.subs.push_back({glm::ivec3(8, 8, 8), glm::ivec3(s % 3, (s / 3) % 3, s / 9), "Glass"});
    rows.micros.push_back({glm::ivec3(7, 8, 9), glm::ivec3(0, 1, 2), glm::ivec3(2, 2, 0), "Metal"});
    Core::VoxFound::Pxb2Counts written;
    const std::vector<uint8_t> blob = Core::VoxFound::encodePxb2FromRows(rows, &written);
    Core::VoxFound::BlockStore store;
    std::vector<std::string> names;
    Core::VoxFound::Pxb2ConvertStats st;
    std::string err;
    ASSERT_TRUE(Core::VoxFound::convertPxb2(blob.data(), blob.size(), store, names, &st, &err)) << err;
    EXPECT_EQ(st.cubes, written.cubes);
    EXPECT_EQ(st.subcubes, written.subcubes);
    EXPECT_EQ(st.microcubes, written.microcubes);
    EXPECT_EQ(written.cubes, 2u);
    EXPECT_EQ(written.subcubes, 28u);
    EXPECT_EQ(written.microcubes, 1u);
    uint8_t m = 0;
    ASSERT_TRUE(store.uniformBlock(glm::ivec3(1, 2, 3), m));
    EXPECT_EQ(names[m], "Stone");
    ASSERT_TRUE(store.uniformBlock(glm::ivec3(31, 0, 31), m));
    EXPECT_EQ(names[m], "Wood");
    ASSERT_TRUE(store.uniformBlock(glm::ivec3(8, 8, 8), m));
    EXPECT_EQ(names[m], "Glass");
}

// The same on real v1 DBs (PHYXEL_V1_DBS): every legacy chunk's rows encode to a blob convertPxb2 accepts, with the
// entry counts the encoder reported, and the cube materials of the blob equal the rows' cube materials.
TEST(VoxFoundV1Migration, RowsEncoderRoundTripsOnRealV1Dbs) {
    const char* env = std::getenv("PHYXEL_V1_DBS");
    if (!env || !*env) GTEST_SKIP() << "PHYXEL_V1_DBS not set (real v1 DBs, run by hand)";
    std::stringstream ss(env);
    std::string path;
    size_t dbs = 0, chunks = 0, good = 0;
    while (std::getline(ss, path, ';')) {
        if (path.empty()) continue;
        const auto tmp = std::filesystem::temp_directory_path() / ("v1mig_" + std::to_string(dbs) + ".db");
        std::filesystem::copy_file(path, tmp, std::filesystem::copy_options::overwrite_existing);
        WorldStorage::s_skipMigration = true;
        WorldStorage ws(tmp.string());
        ASSERT_TRUE(ws.initialize()) << path;
        WorldStorage::s_skipMigration = false;
        ++dbs;
        for (const auto& cc : ws.legacyChunkCoords()) {
            Core::VoxFound::V1ChunkRows rows;
            ASSERT_TRUE(ws.readLegacyRows(cc, rows));
            Core::VoxFound::Pxb2Counts written;
            const std::vector<uint8_t> nu = Core::VoxFound::encodePxb2FromRows(rows, &written);
            ++chunks;
            Core::VoxFound::BlockStore store;
            std::vector<std::string> names;
            Core::VoxFound::Pxb2ConvertStats st;
            std::string err;
            const bool ok = Core::VoxFound::convertPxb2(nu.data(), nu.size(), store, names, &st, &err);
            const Canon b = canon(nu);
            std::vector<std::string> want(32768);
            for (const auto& c : rows.cubes) {
                const size_t i = size_t(c.p.z + c.p.y * 32 + c.p.x * 1024);
                if (c.visible && !c.subdivided && want[i].empty()) want[i] = c.mat;
            }
            if (ok && b.ok && st.cubes == written.cubes && st.subcubes == written.subcubes &&
                st.microcubes == written.microcubes && b.cubes == want)
                ++good;
            else
                ADD_FAILURE() << path << " chunk (" << cc.x << "," << cc.y << "," << cc.z << "): " << err;
        }
    }
    std::printf("[v1mig] %zu DBs, %zu chunks: round-trip good %zu\n", dbs, chunks, good);
    EXPECT_EQ(good, chunks);
}
