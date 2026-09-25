// LightManager::census, the data behind GET /api/debug/light_stats (docs/PerfProgram2026-09.md, I3).
//
// The census exists to measure the duplicate-light waste found live on 2026-09-24: one generated
// tavern registered 29 point lights at 16 positions, 8 of them identical at the chandelier, because
// every emissive micro/subcube registers its own light at its PARENT cube centre. Coverage of the
// source tag is enforced by the compiler (the source is a required first parameter of
// addPointLight/addSpotLight); these tests pin what the census reports.
#include <gtest/gtest.h>

#include "graphics/LightManager.h"

using namespace Phyxel::Graphics;

namespace {
size_t src(const LightManager::Census& c, LightSource s) { return c.bySource[static_cast<size_t>(s)]; }
size_t srcUp(const LightManager::Census& c, LightSource s) { return c.bySourceUploaded[static_cast<size_t>(s)]; }
}  // namespace

// K emissive lights at ONE position (a chandelier cube's glow micros) count as K registered but a
// single unique position.
TEST(LightStatsTest, CountsUniquePositions) {
    LightManager lm;
    const glm::vec3 chandelier(3.5f, 19.5f, 6.5f);
    for (int i = 0; i < 8; ++i) lm.addPointLight(LightSource::EmissiveVoxel, chandelier, glm::vec3(1), 1.0f, 7.5f);
    lm.addPointLight(LightSource::Fixture, glm::vec3(0.5f, 19.5f, 5.5f), glm::vec3(1), 1.0f, 6.0f);
    lm.addPointLight(LightSource::Fixture, glm::vec3(8.5f, 19.5f, 0.5f), glm::vec3(1), 1.0f, 6.0f);
    lm.setViewerWorld(glm::vec3(5.0f, 19.0f, 4.0f));
    lm.getGPUData();

    const auto c = lm.census();
    EXPECT_EQ(c.registeredPoint, 10u);
    EXPECT_EQ(c.uploadedPoint, 10u);
    EXPECT_EQ(c.droppedPoint, 0u);
    EXPECT_EQ(c.uniquePositionsRegistered, 3u);
    EXPECT_EQ(c.uniquePositionsUploaded, 3u);
    EXPECT_EQ(src(c, LightSource::EmissiveVoxel), 8u);
    EXPECT_EQ(src(c, LightSource::Fixture), 2u);
    EXPECT_EQ(srcUp(c, LightSource::EmissiveVoxel), 8u);
}

// Past the upload budget the census separates uploaded from dropped, and the unique-position count
// is over the UPLOADED set.
TEST(LightStatsTest, UploadedAndDroppedAtTheCap) {
    LightManager lm;
    const uint32_t n = MAX_POINT_LIGHTS + 8;
    for (uint32_t i = 0; i < n; ++i)
        lm.addPointLight(LightSource::Api, glm::vec3(float(i) * 3.0f, 0.0f, 0.0f), glm::vec3(1), 1.0f, 2.0f);
    lm.setViewerWorld(glm::vec3(0.0f));
    lm.getGPUData();

    const auto c = lm.census();
    EXPECT_EQ(c.registeredPoint, n);
    EXPECT_EQ(c.enabledPoint, n);
    EXPECT_EQ(c.uploadedPoint, MAX_POINT_LIGHTS);
    EXPECT_EQ(c.droppedPoint, 8u);
    EXPECT_EQ(c.uniquePositionsUploaded, MAX_POINT_LIGHTS);
    EXPECT_EQ(c.selections, 1u);
}

// The source is registration metadata on the entry: replacing the light struct keeps it.
TEST(LightStatsTest, SourceSurvivesUpdate) {
    LightManager lm;
    const int id = lm.addPointLight(LightSource::Fixture, glm::vec3(0), glm::vec3(1), 1.0f, 5.0f);
    PointLight replaced;
    replaced.position = glm::vec3(1, 2, 3);
    replaced.radius = 9.0f;
    ASSERT_TRUE(lm.updatePointLight(id, replaced));
    const auto c = lm.census();
    EXPECT_EQ(src(c, LightSource::Fixture), 1u);
    EXPECT_EQ(src(c, LightSource::Api), 0u);
}

// Spot lights count toward sources and positions, not the point radius histogram.
TEST(LightStatsTest, SpotLightsCounted) {
    LightManager lm;
    lm.addSpotLight(LightSource::Editor, glm::vec3(1, 1, 1), glm::vec3(0, -1, 0));
    lm.addPointLight(LightSource::Vfx, glm::vec3(1, 1, 1));
    lm.getGPUData();
    const auto c = lm.census();
    EXPECT_EQ(c.registeredSpot, 1u);
    EXPECT_EQ(c.uploadedSpot, 1u);
    EXPECT_EQ(src(c, LightSource::Editor), 1u);
    EXPECT_EQ(src(c, LightSource::Vfx), 1u);
    EXPECT_EQ(c.uniquePositionsUploaded, 1u);   // the spot and the point share a position
    size_t histTotal = 0;
    for (size_t b = 0; b < LightManager::Census::kRadiusBins; ++b) histTotal += c.radiusHist[b];
    EXPECT_EQ(histTotal, 1u);
}

// Radius bins: [0,2) [2,4) [4,6) [6,8) [8,10) [10,15) [15,inf).
TEST(LightStatsTest, RadiusHistogramBins) {
    LightManager lm;
    for (float r : {1.0f, 2.0f, 7.5f, 9.0f, 20.0f})
        lm.addPointLight(LightSource::Api, glm::vec3(r, 0, 0), glm::vec3(1), 1.0f, r);
    const auto c = lm.census();
    EXPECT_EQ(c.radiusHist[0], 1u);   // 1
    EXPECT_EQ(c.radiusHist[1], 1u);   // 2 (the lower edge belongs to its bin)
    EXPECT_EQ(c.radiusHist[3], 1u);   // 7.5
    EXPECT_EQ(c.radiusHist[4], 1u);   // 9
    EXPECT_EQ(c.radiusHist[6], 1u);   // 20
}
