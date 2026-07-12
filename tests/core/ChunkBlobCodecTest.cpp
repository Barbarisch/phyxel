#include <gtest/gtest.h>
#include "core/ChunkBlobCodec.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"

namespace Phyxel {
namespace Testing {

// Storage format v2 codec — docs/LargeWorldScalePlan.md Phase 1.

class ChunkBlobCodecTest : public ::testing::Test {
protected:
    std::unique_ptr<Chunk> makeChunk(const glm::ivec3& origin = glm::ivec3(0)) {
        auto chunk = std::make_unique<Chunk>(origin);
        chunk->initializeForLoading();
        return chunk;
    }

    // Round-trip helper: encode `src`, decode into a fresh chunk at the same
    // origin, hard-fail on decode errors.
    std::unique_ptr<Chunk> roundTrip(const Chunk& src) {
        auto blob = ChunkBlobCodec::encode(src);
        auto dst = makeChunk(src.getWorldOrigin());
        EXPECT_TRUE(ChunkBlobCodec::decode(blob.data(), blob.size(), *dst));
        return dst;
    }
};

// --- Basic cube round-trips ---

TEST_F(ChunkBlobCodecTest, EmptyChunkRoundTrips) {
    auto src = makeChunk();
    ChunkBlobCodec::Counts counts;
    auto blob = ChunkBlobCodec::encode(*src, &counts);
    EXPECT_EQ(counts.cubes, 0u);

    auto dst = makeChunk();
    ChunkBlobCodec::Counts decoded;
    ASSERT_TRUE(ChunkBlobCodec::decode(blob.data(), blob.size(), *dst, &decoded));
    EXPECT_EQ(decoded.cubes, 0u);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(dst->getCubeAt(glm::ivec3(i, i, i)), nullptr);
    }
}

TEST_F(ChunkBlobCodecTest, SparseCubesRoundTripWithMaterials) {
    auto src = makeChunk();
    src->addCube(glm::ivec3(0, 0, 0), "Stone");
    src->addCube(glm::ivec3(31, 31, 31), "Wood");
    src->addCube(glm::ivec3(5, 17, 23), "Glass");

    auto dst = roundTrip(*src);
    ASSERT_NE(dst->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
    ASSERT_NE(dst->getCubeAt(glm::ivec3(31, 31, 31)), nullptr);
    ASSERT_NE(dst->getCubeAt(glm::ivec3(5, 17, 23)), nullptr);
    EXPECT_EQ(dst->getCubeAt(glm::ivec3(0, 0, 0))->getMaterialName(), "Stone");
    EXPECT_EQ(dst->getCubeAt(glm::ivec3(31, 31, 31))->getMaterialName(), "Wood");
    EXPECT_EQ(dst->getCubeAt(glm::ivec3(5, 17, 23))->getMaterialName(), "Glass");
    EXPECT_EQ(dst->getCubeAt(glm::ivec3(1, 0, 0)), nullptr);
}

TEST_F(ChunkBlobCodecTest, FullSolidChunkRoundTripsEveryCell) {
    auto src = makeChunk();
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 32; ++y)
            for (int z = 0; z < 32; ++z)
                src->addCube(glm::ivec3(x, y, z), (x + y + z) % 2 ? "Stone" : "Dirt");

    auto dst = roundTrip(*src);
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 32; ++y)
            for (int z = 0; z < 32; ++z) {
                const Cube* cube = dst->getCubeAt(glm::ivec3(x, y, z));
                ASSERT_NE(cube, nullptr) << x << "," << y << "," << z;
                EXPECT_EQ(cube->getMaterialName(), (x + y + z) % 2 ? "Stone" : "Dirt");
            }
}

// --- Size: RLE must collapse uniform content ---

TEST_F(ChunkBlobCodecTest, UniformSolidChunkEncodesUnder1KB) {
    auto src = makeChunk();
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 32; ++y)
            for (int z = 0; z < 32; ++z)
                src->addCube(glm::ivec3(x, y, z), "Stone");

    auto blob = ChunkBlobCodec::encode(*src);
    // One material, one long run (split only by the u16 run-length cap):
    // header + palette + a handful of runs.
    EXPECT_LE(blob.size(), 1024u) << "RLE failed to collapse a uniform chunk";
}

// --- Subcubes / microcubes with tint + state ---

TEST_F(ChunkBlobCodecTest, SubcubeTintStateRoundTrip) {
    auto src = makeChunk();
    ASSERT_TRUE(src->addSubcube(glm::ivec3(4, 5, 6), glm::ivec3(1, 2, 0),
                                "Wood", 0x88CC44u, 3));
    ASSERT_TRUE(src->addSubcube(glm::ivec3(4, 5, 6), glm::ivec3(0, 0, 1),
                                "Bricks")); // default tint/state

    auto dst = roundTrip(*src);
    auto subs = dst->getStaticSubcubesAt(glm::ivec3(4, 5, 6));
    ASSERT_EQ(subs.size(), 2u);

    const Subcube* tinted = nullptr;
    const Subcube* plain = nullptr;
    for (const auto* s : subs) {
        if (s->getLocalPosition() == glm::ivec3(1, 2, 0)) tinted = s;
        if (s->getLocalPosition() == glm::ivec3(0, 0, 1)) plain = s;
    }
    ASSERT_NE(tinted, nullptr);
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(tinted->getMaterialName(), "Wood");
    EXPECT_EQ(tinted->getTint(), 0x88CC44u);
    EXPECT_EQ(tinted->getState(), 3);
    EXPECT_EQ(plain->getMaterialName(), "Bricks");
    EXPECT_EQ(plain->getTint(), 0xFFFFFFu);
    EXPECT_EQ(plain->getState(), 0);
}

TEST_F(ChunkBlobCodecTest, MicrocubeTintStateRoundTrip) {
    auto src = makeChunk();
    ASSERT_TRUE(src->addMicrocube(glm::ivec3(10, 11, 12), glm::ivec3(0, 1, 2),
                                  glm::ivec3(2, 0, 1), "Stone", 0x336699u, 4));

    auto dst = roundTrip(*src);
    ASSERT_EQ(dst->getStaticMicrocubeCount(), 1u);
    const auto& micro = dst->getStaticMicrocubes()[0];
    EXPECT_EQ(micro->getMaterialName(), "Stone");
    EXPECT_EQ(micro->getSubcubeLocalPosition(), glm::ivec3(0, 1, 2));
    EXPECT_EQ(micro->getMicrocubeLocalPosition(), glm::ivec3(2, 0, 1));
    EXPECT_EQ(micro->getTint(), 0x336699u);
    EXPECT_EQ(micro->getState(), 4);
}

// --- Negative world origin (parent-local math must not break) ---

TEST_F(ChunkBlobCodecTest, NegativeOriginChunkRoundTrips) {
    auto src = makeChunk(glm::ivec3(-64, -32, -96));
    src->addCube(glm::ivec3(15, 15, 15), "Sand");
    ASSERT_TRUE(src->addSubcube(glm::ivec3(3, 3, 3), glm::ivec3(2, 2, 2), "Ice"));

    auto dst = roundTrip(*src);
    ASSERT_NE(dst->getCubeAt(glm::ivec3(15, 15, 15)), nullptr);
    EXPECT_EQ(dst->getCubeAt(glm::ivec3(15, 15, 15))->getMaterialName(), "Sand");
    auto subs = dst->getStaticSubcubesAt(glm::ivec3(3, 3, 3));
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0]->getMaterialName(), "Ice");
}

// --- Determinism: same content → identical bytes ---

TEST_F(ChunkBlobCodecTest, EncodeIsDeterministic) {
    auto build = [this]() {
        auto chunk = makeChunk();
        chunk->addCube(glm::ivec3(1, 2, 3), "Stone");
        chunk->addCube(glm::ivec3(4, 5, 6), "Wood");
        chunk->addSubcube(glm::ivec3(7, 8, 9), glm::ivec3(1, 1, 1), "Glass", 0x102030u, 2);
        return chunk;
    };
    auto a = ChunkBlobCodec::encode(*build());
    auto b = ChunkBlobCodec::encode(*build());
    EXPECT_EQ(a, b);
}

// --- Malformed input must fail cleanly, never crash ---

TEST_F(ChunkBlobCodecTest, DecodeRejectsGarbageAndTruncation) {
    auto dst = makeChunk();

    // Garbage magic
    std::vector<uint8_t> garbage(64, 0xAB);
    EXPECT_FALSE(ChunkBlobCodec::decode(garbage.data(), garbage.size(), *dst));

    // Empty / null
    EXPECT_FALSE(ChunkBlobCodec::decode(nullptr, 0, *dst));
    EXPECT_FALSE(ChunkBlobCodec::decode(garbage.data(), 0, *dst));

    // Valid blob truncated at every prefix length must fail, not crash.
    auto src = makeChunk();
    src->addCube(glm::ivec3(1, 1, 1), "Stone");
    src->addSubcube(glm::ivec3(2, 2, 2), glm::ivec3(1, 0, 0), "Wood", 0x123456u, 1);
    auto blob = ChunkBlobCodec::encode(*src);
    for (size_t len = 0; len < blob.size(); ++len) {
        auto fresh = makeChunk();
        EXPECT_FALSE(ChunkBlobCodec::decode(blob.data(), len, *fresh))
            << "truncated decode at " << len << " bytes did not fail";
    }
}

TEST_F(ChunkBlobCodecTest, DecodeRejectsWrongVersion) {
    auto src = makeChunk();
    src->addCube(glm::ivec3(0, 0, 0), "Stone");
    auto blob = ChunkBlobCodec::encode(*src);
    blob[4] = static_cast<uint8_t>(ChunkBlobCodec::kCodecVersion + 1); // version byte
    auto dst = makeChunk();
    EXPECT_FALSE(ChunkBlobCodec::decode(blob.data(), blob.size(), *dst));
}

} // namespace Testing
} // namespace Phyxel
