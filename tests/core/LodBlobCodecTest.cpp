#include <gtest/gtest.h>

#include <memory>

#include "core/Chunk.h"
#include "core/LodBlobCodec.h"
#include "core/LodBrick.h"
#include "core/LodChunkMesh.h"

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

/// Every persisted field must survive the round trip, not just occupancy.
void expectVolumesEqual(const LodVolume& a, const LodVolume& b, const char* what) {
    ASSERT_EQ(a.dim(), b.dim()) << what;
    ASSERT_EQ(a.level(), b.level()) << what;
    for (int x = 0; x < a.dim().x; ++x)
        for (int y = 0; y < a.dim().y; ++y)
            for (int z = 0; z < a.dim().z; ++z) {
                const LodCell& p = a.at(x, y, z);
                const LodCell& q = b.at(x, y, z);
                ASSERT_EQ(p.coverage, q.coverage) << what << " coverage at " << x << "," << y << "," << z;
                ASSERT_EQ(p.bulkMaterial, q.bulkMaterial) << what << " bulk at " << x << "," << y << "," << z;
                ASSERT_EQ(p.skinMaterial, q.skinMaterial) << what << " skin at " << x << "," << y << "," << z;
                ASSERT_EQ(p.preserveOpening, q.preserveOpening) << what << " opening flag";
                ASSERT_EQ(p.openingCoverage, q.openingCoverage) << what << " opening volume";
            }
}

LodVolume mixedVolume() {
    LodVolume v(glm::ivec3(8, 8, 8), 1);
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 8; ++z) {
                LodCell& c = v.at(x, y, z);
                c.coverage = LodVolume::kFullCoverage * 8;   // level-1 cell = 8 cubes
                c.bulkMaterial = 1;
                c.skinMaterial = (y == 3) ? 2 : 1;           // a distinct skin on top
            }
    LodCell& partial = v.at(2, 4, 2);                        // a partially-filled cell
    partial.coverage = 37;
    partial.bulkMaterial = partial.skinMaterial = 3;
    LodCell& windowed = v.at(5, 2, 5);                       // a cell carrying an opening
    windowed.preserveOpening = true;
    windowed.openingCoverage = 1234;
    return v;
}

const std::vector<std::string> kPalette{"", "Stone", "Grass", "WoodPlanks"};

} // namespace

TEST(LodBlobCodecTest, RoundTripsEveryFieldOfAMixedVolume) {
    const LodVolume src = mixedVolume();
    std::vector<uint8_t> blob = LodBlobCodec::encode(src, kPalette);
    ASSERT_FALSE(blob.empty());

    LodVolume back; std::vector<std::string> pal;
    ASSERT_TRUE(LodBlobCodec::decode(blob.data(), blob.size(), back, pal));
    EXPECT_EQ(pal, kPalette);
    expectVolumesEqual(src, back, "mixed volume");
}

/// The pyramid is persisted per level, so every level the renderer can reach must round-trip.
/// updateChunkLod caps at maxLevel = 5.
TEST(LodBlobCodecTest, RoundTripsEveryLevelOfARealChunkPyramid) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 6; ++y)
                c->addCube(glm::ivec3(x, y, z), (y == 5) ? "Grass" : "Stone");
    for (int y = 0; y < 3; ++y)
        c->addSubcube(glm::ivec3(4, 6, 4), glm::ivec3(1, y, 1), "Wood");

    std::vector<std::string> palette;
    LodVolume v = LodChunkMesh::volumeFromChunk(*c, &palette);
    for (int level = 0; level <= 5; ++level) {
        if (level > 0) v = squash(v, SquashConfig{});
        std::vector<uint8_t> blob = LodBlobCodec::encode(v, palette);
        LodVolume back; std::vector<std::string> pal;
        ASSERT_TRUE(LodBlobCodec::decode(blob.data(), blob.size(), back, pal))
            << "level " << level << " failed to decode";
        expectVolumesEqual(v, back, ("pyramid level " + std::to_string(level)).c_str());
    }
}

/// A decoded volume must produce the SAME faces as the original. This is the property C3
/// actually needs: a distant chunk served from storage must look identical to one meshed
/// from resident voxels. Round-tripping fields is necessary but not sufficient.
TEST(LodBlobCodecTest, DecodedVolumeMeshesIdenticallyToTheOriginal) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 5; ++y) c->addCube(glm::ivec3(x, y, z), "Stone");
    for (int x = 8; x < 20; ++x)
        for (int y = 5; y < 10; ++y) c->addCube(glm::ivec3(x, y, 10), "WoodPlanks");

    std::vector<std::string> palette;
    LodVolume v = LodChunkMesh::volumeFromChunk(*c, &palette);
    for (int level = 1; level <= 3; ++level) {
        v = squash(v, SquashConfig{});
        std::vector<uint8_t> blob = LodBlobCodec::encode(v, palette);
        LodVolume back; std::vector<std::string> pal;
        ASSERT_TRUE(LodBlobCodec::decode(blob.data(), blob.size(), back, pal));

        std::vector<InstanceData> a, b;
        LodChunkMesh::emitFaces(v, palette, a);
        LodChunkMesh::emitFaces(back, pal, b);
        ASSERT_EQ(a.size(), b.size()) << "level " << level << ": face COUNT differs after storage";
        for (size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(a[i].packedData, b[i].packedData) << "level " << level << " face " << i;
            EXPECT_EQ(a[i].textureIndex, b[i].textureIndex) << "level " << level << " face " << i;
        }
    }
}

/// An all-air volume is the common case for sky chunks; it must stay tiny, or persisting the
/// pyramid costs more than the residency it is meant to save.
TEST(LodBlobCodecTest, EmptyVolumeCostsAlmostNothing) {
    LodVolume empty(glm::ivec3(16, 16, 16), 1);
    std::vector<uint8_t> blob = LodBlobCodec::encode(empty, kPalette);
    EXPECT_LT(blob.size(), 64u) << "an all-air level should RLE to a single run";

    LodVolume back; std::vector<std::string> pal;
    ASSERT_TRUE(LodBlobCodec::decode(blob.data(), blob.size(), back, pal));
    expectVolumesEqual(empty, back, "empty volume");
}

// --- corruption must be REFUSED, never half-decoded -------------------------------------
// A partially-populated volume renders holes and looks like a mesher bug, so the codec has to
// reject bad input outright rather than return what it managed to read.

TEST(LodBlobCodecTest, RejectsTruncatedBlob) {
    std::vector<uint8_t> blob = LodBlobCodec::encode(mixedVolume(), kPalette);
    ASSERT_GT(blob.size(), 20u);
    for (size_t cut : {blob.size() / 4, blob.size() / 2, blob.size() - 1}) {
        LodVolume back; std::vector<std::string> pal;
        EXPECT_FALSE(LodBlobCodec::decode(blob.data(), cut, back, pal))
            << "accepted a blob truncated to " << cut << " bytes";
    }
}

TEST(LodBlobCodecTest, RejectsBadMagicAndUnknownVersion) {
    std::vector<uint8_t> blob = LodBlobCodec::encode(mixedVolume(), kPalette);
    LodVolume back; std::vector<std::string> pal;

    std::vector<uint8_t> badMagic = blob;
    badMagic[0] ^= 0xFF;
    EXPECT_FALSE(LodBlobCodec::decode(badMagic.data(), badMagic.size(), back, pal));

    std::vector<uint8_t> badVersion = blob;
    badVersion[4] = 99;
    EXPECT_FALSE(LodBlobCodec::decode(badVersion.data(), badVersion.size(), back, pal))
        << "a future codec version must be refused, not guessed at";

    EXPECT_FALSE(LodBlobCodec::decode(nullptr, 0, back, pal));
}

/// Run lengths must tile the volume EXACTLY. Claiming fewer cells than the header's dimensions
/// would leave the tail as default-constructed air -- a silent hole.
TEST(LodBlobCodecTest, RejectsRunsThatDoNotTileTheVolume) {
    LodVolume v(glm::ivec3(4, 4, 4), 0);
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 4; ++z) v.at(x, y, z).coverage = LodVolume::kFullCoverage;
    std::vector<uint8_t> blob = LodBlobCodec::encode(v, kPalette);

    // The single run's length sits right after the u32 run count. Halve it.
    const size_t runCountOff = blob.size() - (2 + 1 + 8 + 1 + 1) - 4;
    uint16_t& len = *reinterpret_cast<uint16_t*>(blob.data() + runCountOff + 4);
    ASSERT_EQ(len, 64u) << "layout assumption changed; fix this test's offset";
    len = 32;

    LodVolume back; std::vector<std::string> pal;
    EXPECT_FALSE(LodBlobCodec::decode(blob.data(), blob.size(), back, pal))
        << "accepted runs covering only half the volume -- the rest would render as holes";
}

/// C3's WHOLE PREMISE, as a gate. Persisting the pyramid only pays if a coarse level costs far
/// less than the ~1.28 MB of working set a RESIDENT chunk costs
/// (docs/evidence/lod_residency_wall_20260730.txt). If this format ever bloats past that, C3
/// stops being worth building and this test should say so loudly rather than let it rot.
TEST(LodBlobCodecTest, PyramidCostsFarLessThanResidency) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    // A realistic terrain chunk: a heightfield with a grass skin and a bit of structure.
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z) {
            const int h = 8 + ((x * 7 + z * 5) % 6);
            for (int y = 0; y < h; ++y)
                c->addCube(glm::ivec3(x, y, z), (y == h - 1) ? "Grass" : "Stone");
        }
    for (int x = 10; x < 22; ++x)
        for (int y = 14; y < 20; ++y) c->addCube(glm::ivec3(x, y, 16), "WoodPlanks");

    std::vector<std::string> palette;
    LodVolume v = LodChunkMesh::volumeFromChunk(*c, &palette);

    size_t pyramidBytes = 0;
    for (int level = 1; level <= 5; ++level) {
        v = squash(v, SquashConfig{});
        const size_t n = LodBlobCodec::encode(v, palette).size();
        pyramidBytes += n;
        std::cout << "  level " << level << " (" << v.dim().x << "^3 cells): " << n << " bytes\n";
    }
    std::cout << "  pyramid levels 1-5 total: " << pyramidBytes << " bytes\n";

    // A resident chunk costs ~1.28 MB. Persisting its whole coarse pyramid must be at least an
    // order of magnitude cheaper or C3 does not break the R^2 wall.
    constexpr size_t kResidentChunkBytes = 1280u * 1024u;
    EXPECT_LT(pyramidBytes, kResidentChunkBytes / 10)
        << "the persisted pyramid is not decisively cheaper than keeping the chunk resident";
}

// ===========================================================================
// decode() IS FED DATA FROM DISK. It must never hand the renderer a bad volume, and it must
// never crash. Both cases below were found by a solution-auditor probing the real binary; the
// header's "bounds-validates every field" promise was simply untrue.
// ===========================================================================
namespace {
/// Hand-craft a header so these tests do not depend on encode()'s byte layout staying put.
struct BlobBuilder {
    std::vector<uint8_t> b;
    void u8v(uint8_t v)  { b.push_back(v); }
    void u16v(uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
    void u32v(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void u64v(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void header(uint16_t paletteCount, uint16_t dx, uint16_t dy, uint16_t dz, uint8_t level = 0) {
        u32v(LodBlobCodec::kMagic);
        u8v(LodBlobCodec::kCodecVersion);
        u8v(level);
        u8v(0);                       // narrow palette
        u16v(paletteCount); u16v(dx); u16v(dy); u16v(dz);
    }
    void name(const std::string& s) { u8v(uint8_t(s.size())); b.insert(b.end(), s.begin(), s.end()); }
};
} // namespace

/// A cell may not reference a palette entry that does not exist. Anything downstream doing
/// palette[cell.bulkMaterial] -- exactly what the mesher does -- would read out of bounds.
TEST(LodBlobCodecTest, RejectsOutOfRangePaletteIndex) {
    BlobBuilder bb;
    bb.header(/*paletteCount*/ 2, /*dx*/ 2, /*dy*/ 1, /*dz*/ 1);
    bb.name(""); bb.name("Stone");
    bb.u32v(1);                       // one run
    bb.u16v(2); bb.u8v(0x01);         // len 2, solid
    bb.u64v(729);
    bb.u8v(200); bb.u8v(200);         // palette indices 200 against a 2-entry palette

    LodVolume back; std::vector<std::string> pal;
    EXPECT_FALSE(LodBlobCodec::decode(bb.b.data(), bb.b.size(), back, pal))
        << "accepted a cell referencing palette index 200 of a 2-entry palette";
}

/// Dimensions come from the blob, and LodVolume allocates dx*dy*dz cells eagerly. A row
/// claiming 65535^3 must be refused BEFORE allocating -- otherwise decode() throws bad_alloc
/// out of a function whose contract is "return false on bad data", killing the process on the
/// read path.
TEST(LodBlobCodecTest, RejectsAbsurdDimensionsWithoutThrowing) {
    BlobBuilder bb;
    bb.header(/*paletteCount*/ 1, /*dx*/ 65535, /*dy*/ 65535, /*dz*/ 65535);
    bb.name("");
    bb.u32v(0);                       // no runs

    LodVolume back; std::vector<std::string> pal;
    bool result = true;
    EXPECT_NO_THROW({ result = LodBlobCodec::decode(bb.b.data(), bb.b.size(), back, pal); })
        << "decode() threw on absurd dimensions instead of returning false";
    EXPECT_FALSE(result);
}
