#include <gtest/gtest.h>

#include "core/water/ShoreBand.h"

#include <cmath>
#include <cstdio>

// WaterCore Phase G (docs/WaterCore.md 18.3 key 2, 18.5): the shoreline band around a synthetic
// beach. The bed and the stored tops are pure functions of world position (the engine's queries are
// the chunk occupancy and the chunk spans); the band's box is the only camera-derived quantity and
// it must be a cost bound: the interior is independent of the outer ring's width.

namespace Phyxel::Core::Water {
namespace {

constexpr float kDt = 1.0f / 60.0f;

// A 1:20 beach rising toward +z: bed = 10 at z <= 100 (deep floor: 6 m below the still level 16),
// then + (z - 100) / 20 - the still line is crossed at z = 220.
BedQuery beachBed() {
    return [](float, float wz, float yLo, float) -> BedSample {
        const float b = wz <= 100.0f ? 10.0f : 10.0f + (wz - 100.0f) / 20.0f;
        return BedSample{true, std::max(b, yLo)};
    };
}
StoredTopQuery beachStored() {   // the ocean at 16 wherever the bed is below it
    return [](int, int wz) -> StoredTop {
        const float b = wz <= 100 ? 10.0f : 10.0f + (wz + 0.5f - 100.0f) / 20.0f;
        return b < 16.0f ? StoredTop{true, 16.0f} : StoredTop{false, 0.0f};
    };
}

TEST(ShoreBandTest, SitingClassifiesColumns) {
    ShoreBand band;
    ShoreBandParams p; p.radius = 20.0f; p.inner = 6; p.cellSize = 1.0f;
    std::string err;
    // centred 8 m seaward of the waterline (z = 220): the seaward ring is 1.15-1.4 m deep (the wavemaker),
    // the landward edge is dry sand 0.6 m above the still line
    ASSERT_TRUE(band.site(glm::vec2(50.0f, 212.0f), p, beachBed(), beachStored(), &err)) << err;
    const ShoreBandRecord& r = band.record();
    EXPECT_EQ(r.n, 40);
    EXPECT_NEAR(r.still, 16.0f, 1e-3f);
    EXPECT_EQ(r.columns, 40 * 40);
    EXPECT_GT(r.prescribed, 0);
    EXPECT_GT(r.sponge, 0) << "the ring's shallow lateral columns are a sponge";
    EXPECT_EQ(r.walls, 0);
    // the ring's seaward columns are prescribed, its landward (dry sand) columns are not; the interior is free
    EXPECT_EQ(band.role(20, 0), ShoreColumnRole::Prescribed);
    EXPECT_EQ(band.role(20, 39), ShoreColumnRole::Free);     // dry sand at z = 231.5: bed 16.575 > still, inside runUp
    EXPECT_EQ(band.role(20, 20), ShoreColumnRole::Free);
    EXPECT_EQ(band.role(0, 27), ShoreColumnRole::Sponge);    // lateral ring at z = 219.5: 2.5 cm deep
    // the surface: ocean columns at 16, sand columns dry (eta == bed)
    EXPECT_NEAR(band.solver()->col(20, 0).eta, 16.0, 1e-6);
    EXPECT_NEAR(band.solver()->col(20, 39).eta, band.solver()->col(20, 39).bed, 1e-6);
    // the field: one run per wet column, none on dry sand
    EXPECT_EQ(band.field().at(20, 0).runs, 1.0f);
    EXPECT_EQ(band.field().at(20, 39).runs, 0.0f);
    EXPECT_EQ(band.field().nx, 40);
    // the mask box covers the band's voxel columns
    const auto box = band.maskBox();
    EXPECT_EQ(box.first.x, 30); EXPECT_EQ(box.second.x, 69);
    EXPECT_EQ(box.first.z, 192); EXPECT_EQ(box.second.z, 231);
}

TEST(ShoreBandTest, UnknownGroundIsAWallAndNoOceanRefuses) {
    ShoreBand band;
    ShoreBandParams p; p.radius = 8.0f; p.inner = 4;
    // ground unknown east of x = 60: those columns are walls (the 5.1 hold rule)
    BedQuery bed = [](float wx, float wz, float yLo, float yHi) -> BedSample {
        if (wx > 60.0f) return BedSample{false, 0.0f};
        return beachBed()(wx, wz, yLo, yHi);
    };
    std::string err;
    ASSERT_TRUE(band.site(glm::vec2(58.0f, 150.0f), p, bed, beachStored(), &err)) << err;
    EXPECT_GT(band.record().walls, 0);
    for (int z = 0; z < band.record().n; ++z) for (int x = 0; x < band.record().n; ++x) {
        const glm::vec2 c = band.columnCentre(x, z);
        EXPECT_EQ(band.role(x, z) == ShoreColumnRole::Wall, c.x > 60.0f) << "column " << x << "," << z;
    }
    // high and dry: a band sited on the dunes has no stored water in its ring -> refused, loudly
    ShoreBand dry;
    EXPECT_FALSE(dry.site(glm::vec2(50.0f, 300.0f), p, beachBed(), beachStored(), &err));
    EXPECT_NE(err.find("no ocean"), std::string::npos) << err;
    EXPECT_FALSE(dry.active());
}

TEST(ShoreBandTest, StillOceanStaysStillAndMassIsOnlyTheRings) {
    // no swell: nothing moves, the ledger stays zero, the sand stays dry
    ShoreBand band;
    ShoreBandParams p; p.radius = 16.0f; p.inner = 6;
    std::string err;
    ASSERT_TRUE(band.site(glm::vec2(50.0f, 220.0f), p, beachBed(), beachStored(), &err)) << err;
    const double m0 = band.record().mass;
    SeaSwellParams calm; calm.amplitude = 0.0f;
    for (int k = 0; k < 300; ++k) band.tick(kDt, k * kDt, calm);
    EXPECT_NEAR(band.record().mass, m0, 1e-9 * m0);
    EXPECT_NEAR(band.record().exchanged, 0.0, 1e-9);
    EXPECT_EQ(band.record().runUpMax, 0.0f);
    EXPECT_EQ(band.field().at(16, 31).runs, 0.0f);
}

TEST(ShoreBandTest, SwellRunsUpTheSandAndDrainsBack) {
    // the sheet's Coast swell (0.45 m, 14 m, wind 0.6 rad -> mostly +x; a 1:20 beach toward +z, so
    // the band is sited with the wind blowing shoreward): water climbs above the still line and the
    // peak run-up is bounded by Hunt (R = H xi) within a factor of 2 either way - the beach is
    // oblique to the swell and the ring sits in 3-6 m of water, so this is a sanity bound, not S12
    ShoreBand band;
    ShoreBandParams p; p.radius = 24.0f; p.inner = 8; p.cellSize = 1.0f;
    std::string err;
    ASSERT_TRUE(band.site(glm::vec2(50.0f, 216.0f), p, beachBed(), beachStored(), &err)) << err;
    SeaSwellParams sw; sw.amplitude = 0.45f; sw.wavelength = 14.0f; sw.windRad = 1.5707963f;   // shoreward (+z)
    float peak = 0.0f; double exch = 0.0; float wetLandMax = 0.0f; float riseMax = 0.0f; float foamPeak = 0.0f;
    for (int k = 1; k <= 1800; ++k) {
        band.tick(kDt, k * kDt, sw);
        peak = std::max(peak, band.record().runUpMax);
        riseMax = std::max(riseMax, band.record().meanFreeRise);
        foamPeak = std::max(foamPeak, band.record().foamMax);
    }
    exch = band.record().exchanged;
    // drains back: with the swell off, after 20 s nothing stays above the still line + 1 cm of film
    SeaSwellParams calm;
    for (int k = 1; k <= 1200; ++k) band.tick(kDt, 30.0f + k * kDt, calm);
    const ShoreBandRecord& r = band.record();
    int wlx = -1, wlz = -1;
    for (int z = 0; z < r.n; ++z) for (int x = 0; x < r.n; ++x) {
        const ShoreColumn& c = band.solver()->col(x, z);
        if (band.role(x, z) != ShoreColumnRole::Free || c.bed <= r.still) continue;
        const float d = static_cast<float>(c.eta - c.bed);
        if (d > wetLandMax) { wetLandMax = d; wlx = x; wlz = z; }
    }
    if (wlx >= 0) std::printf("  wettest land column (%d,%d): bed %.3f, depth %.4f, u %.3f w %.3f, roles W/E/S/N %d %d %d %d\n", wlx, wlz, band.solver()->col(wlx, wlz).bed, wetLandMax,
                              band.solver()->col(wlx, wlz).u, band.solver()->col(wlx, wlz).w,
                              wlx > 0 ? static_cast<int>(band.role(wlx - 1, wlz)) : -1, wlx + 1 < r.n ? static_cast<int>(band.role(wlx + 1, wlz)) : -1,
                              wlz > 0 ? static_cast<int>(band.role(wlx, wlz - 1)) : -1, wlz + 1 < r.n ? static_cast<int>(band.role(wlx, wlz + 1)) : -1);
    // Hunt on the sheet's whole swell: four components, total height 2 x (1 + 0.52 + 0.28 + 0.70) a = 5.0 a, the 14 m period
    const float H = 5.0f * sw.amplitude, T = 2.0f * 3.14159265f / std::sqrt(9.81f * 6.2831853f / 14.0f), L0 = 9.81f * T * T / 6.2831853f;
    const float hunt = H * (0.05f / std::sqrt(H / L0));
    std::printf("  band swell: peak run-up %.3f m (Hunt on H = %.2f m: %.3f), exchanged %.3f m^3 over 30 s, wet land after calm %.4f m, step %.2f ms (%ld columns), max speed %.2f, mean rise peak %.3f m / after calm %.3f m\n",
                peak, H, hunt, exch, wetLandMax, r.stepMs, r.columns, r.maxSpeed, riseMax, r.meanFreeRise);
    EXPECT_LT(riseMax, 0.08f) << "the ocean ring must not pump the band up (Stokes transport compensated, return flow absorbed)";
    std::printf("  band foam: peak %.2f during the swell, %.3f after calm\n", foamPeak, r.foamMax);
    EXPECT_GT(foamPeak, 0.5f) << "the swash must foam (G2)";
    EXPECT_LT(r.foamMax, 0.05f) << "calm water carries no foam (G2)";
    RecordProperty("runup_peak_m", peak); RecordProperty("hunt_m", hunt);
    EXPECT_GT(peak, 0.02f) << "the swell must climb the sand";
    EXPECT_LT(peak, 2.0f * hunt + 0.1f);
    EXPECT_LT(wetLandMax, 0.01f) << "nothing stays above the swash line once the sea is calm";
    EXPECT_GT(r.sitings, 0);
}

TEST(ShoreBandTest, InteriorIndependentOfOuterWidth) {
    // 18.3 key 2: the prescribed ring's width is a cost bound. The same shore, the same centre,
    // inner 6 vs 12 (box radius 20 vs 26 so the FREE region is the same 28 columns): after 10 s of
    // the swell every free column's surface agrees within 1 cm.
    auto run = [](float radius, int inner, ShoreBand& band) {
        ShoreBandParams p; p.radius = radius; p.inner = inner; p.cellSize = 1.0f; p.ramp = 6.0f;
        std::string err;
        ASSERT_TRUE(band.site(glm::vec2(50.0f, 216.0f), p, beachBed(), beachStored(), &err)) << err;
        SeaSwellParams sw; sw.amplitude = 0.45f; sw.wavelength = 14.0f; sw.windRad = 1.5707963f;
        for (int k = 1; k <= 600; ++k) band.tick(kDt, k * kDt, sw);
    };
    ShoreBand a, b;
    run(20.0f, 6, a); run(26.0f, 12, b);
    ASSERT_EQ(a.record().n, 40); ASSERT_EQ(b.record().n, 52);
    float maxDiff = 0.0f; long compared = 0; int wx = -1, wz = -1; double ea = 0.0, eb = 0.0;
    for (int z = 0; z < a.record().n; ++z) for (int x = 0; x < a.record().n; ++x) {
        if (a.role(x, z) != ShoreColumnRole::Free) continue;
        const glm::vec2 c = a.columnCentre(x, z);
        const int bx = static_cast<int>(std::floor((c.x - b.record().originXZ.x) / b.record().h)), bz = static_cast<int>(std::floor((c.y - b.record().originXZ.y) / b.record().h));
        ASSERT_EQ(b.role(bx, bz), ShoreColumnRole::Free) << "the free region must be the same set of columns";
        const float dd = static_cast<float>(std::abs(a.solver()->col(x, z).eta - b.solver()->col(bx, bz).eta));
        if (dd > maxDiff) { maxDiff = dd; wx = x; wz = z; ea = a.solver()->col(x, z).eta; eb = b.solver()->col(bx, bz).eta; }
        ++compared;
    }
    std::printf("  ring-width independence: %ld free columns compared, max surface difference %.4f m at a(%d,%d): %.3f vs %.3f; max speed %.2f / %.2f, mean rise %.3f / %.3f\n", compared, maxDiff, wx, wz, ea, eb, a.record().maxSpeed, b.record().maxSpeed, a.record().meanFreeRise, b.record().meanFreeRise);
    EXPECT_GT(compared, 500);
    EXPECT_LT(maxDiff, 0.01f);
}

TEST(ShoreBandTest, ResiteCarriesTheStateAndAnEditLowersTheBed) {
    ShoreBand band;
    ShoreBandParams p; p.radius = 16.0f; p.inner = 6;
    std::string err;
    ASSERT_TRUE(band.site(glm::vec2(50.0f, 216.0f), p, beachBed(), beachStored(), &err)) << err;
    SeaSwellParams sw; sw.amplitude = 0.45f; sw.wavelength = 14.0f; sw.windRad = 1.5707963f;
    for (int k = 1; k <= 300; ++k) band.tick(kDt, k * kDt, sw);
    // walk 6 m along the shore: no resite yet (half the radius is 8); 9 m: resite
    EXPECT_FALSE(band.needsResite(glm::vec2(56.0f, 216.0f)));
    EXPECT_TRUE(band.needsResite(glm::vec2(59.0f, 216.0f)));
    const double etaBefore = band.solver()->col(20, 16).eta;   // world column x 54.5, z 216.5
    const float uBefore = band.solver()->col(20, 16).u;
    ASSERT_TRUE(band.site(glm::vec2(59.0f, 216.0f), p, beachBed(), beachStored(), &err)) << err;
    EXPECT_EQ(band.record().sitings, 2);
    // the same world column in the new box keeps its surface and velocity
    const glm::vec2 o = band.record().originXZ;
    const int nx = static_cast<int>(std::floor((54.5f - o.x) / 1.0f)), nz = static_cast<int>(std::floor((216.5f - o.y) / 1.0f));
    EXPECT_NEAR(band.solver()->col(nx, nz).eta, etaBefore, 1e-9);
    EXPECT_FLOAT_EQ(band.solver()->col(nx, nz).u, uBefore);
    // an edit: the bed under a wet ocean column drops 2 m (a dug hole); the surface stays, the depth grows, mass is counted
    const int ex = static_cast<int>(std::floor(o.x)) + 10, ez = static_cast<int>(std::floor(o.y)) + 10;
    const int cx = 10, cz = 10;
    const float bedBefore = band.solver()->col(cx, cz).bed;
    BedQuery dug = [ex, ez](float wx, float wz, float yLo, float yHi) -> BedSample {
        if (static_cast<int>(std::floor(wx)) == ex && static_cast<int>(std::floor(wz)) == ez) return BedSample{true, std::max(yLo, 8.0f)};
        return beachBed()(wx, wz, yLo, yHi);
    };
    ASSERT_TRUE(band.site(glm::vec2(59.0f, 216.0f), p, dug, beachStored(), &err)) << err;   // installs the new bed query
    band.noteEdit(ex, ez);
    band.tick(kDt, 5.0f, sw);
    EXPECT_EQ(band.record().bedUpdates, 1);
    EXPECT_LT(band.solver()->col(cx, cz).bed, bedBefore);
    EXPECT_GE(band.solver()->col(cx, cz).eta, band.solver()->col(cx, cz).bed);
}


// G4 (docs/WaterCore.md 18.8): the fine band nested in the 1 m band
ShoreBand siteOuter(const glm::vec2& c) {
    ShoreBand outer;
    ShoreBandParams p; p.radius = 24.0f; p.inner = 8; p.cellSize = 1.0f;
    std::string err;
    EXPECT_TRUE(outer.site(c, p, beachBed(), beachStored(), &err)) << err;
    return outer;
}

TEST(ShoreBandTest, NestedStillStaysStill) {
    ShoreBand outer = siteOuter(glm::vec2(50.0f, 216.0f));
    ShoreBand fine;
    ShoreBandParams fp; fp.radius = 8.0f; fp.inner = 6; fp.ramp = 2.0f; fp.seawardBias = false;
    std::string err;
    ASSERT_TRUE(fine.siteNested(outer, glm::vec2(50.0f, 220.0f), fp, beachBed(), &err)) << err;
    EXPECT_EQ(fine.record().n, 48);
    EXPECT_TRUE(fine.record().nested);
    EXPECT_GT(fine.record().prescribed, 0);
    SeaSwellParams calm;
    for (int k = 0; k < 300; ++k) { outer.tick(kDt, k * kDt, calm); fine.tick(kDt, 0.0f, calm); }
    EXPECT_LT(fine.record().maxSpeed, 1e-6f);
    EXPECT_EQ(fine.record().foamMax, 0.0f);
    for (int z = 0; z < 48; ++z) for (int x = 0; x < 48; ++x) {
        const ShoreColumn& c = fine.solver()->col(x, z);
        EXPECT_NEAR(c.eta, std::max(16.0, static_cast<double>(c.bed)), 1e-6) << x << "," << z;
    }
}

TEST(ShoreBandTest, NestedFollowsTheOuter) {
    // the swell: over the fine band's free columns the time-mean surface agrees with the 1 m band's
    // at the same points (mean |difference| < 2 cm), the run-up agrees within +-30 %, and the swash
    // edge foam is reported for both (the point of the fine band: more of the edge carries foam)
    ShoreBand outer = siteOuter(glm::vec2(50.0f, 216.0f));
    ShoreBand fine;
    ShoreBandParams fp; fp.radius = 10.0f; fp.inner = 6; fp.ramp = 2.0f; fp.seawardBias = false;
    std::string err;
    ASSERT_TRUE(fine.siteNested(outer, glm::vec2(50.0f, 220.0f), fp, beachBed(), &err)) << err;
    SeaSwellParams sw; sw.amplitude = 0.45f; sw.wavelength = 14.0f; sw.windRad = 1.5707963f;
    const int n = fine.record().n;
    std::vector<double> sumFine(static_cast<size_t>(n) * n, 0.0), sumOuter(static_cast<size_t>(n) * n, 0.0);
    long samples = 0; float peakOuter = 0.0f, peakFine = 0.0f;
    long edgeO = 0, edgeFoamO = 0, edgeF = 0, edgeFoamF = 0; double msFine = 0.0, msOuter = 0.0;
    for (int k = 1; k <= 1800; ++k) {
        outer.tick(kDt, k * kDt, sw); fine.tick(kDt, 0.0f, sw);
        msOuter += outer.record().stepMs; msFine += fine.record().stepMs;
        if (k < 300) continue;
        peakOuter = std::max(peakOuter, outer.record().runUpMax); peakFine = std::max(peakFine, fine.record().runUpMax);
        edgeO += outer.record().edgeColumns; edgeFoamO += outer.record().edgeFoamColumns;
        edgeF += fine.record().edgeColumns; edgeFoamF += fine.record().edgeFoamColumns;
        ++samples;
        for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
            if (fine.role(x, z) != ShoreColumnRole::Free) continue;
            const glm::vec2 c = fine.columnCentre(x, z);
            double eo; glm::vec2 vo;
            const ShoreColumn& fc = fine.solver()->col(x, z);
            sumFine[static_cast<size_t>(x) + static_cast<size_t>(n) * z] += fc.eta;
            sumOuter[static_cast<size_t>(x) + static_cast<size_t>(n) * z] += outer.sample(c.x, c.y, eo, vo) ? std::max(eo, static_cast<double>(fc.bed)) : static_cast<double>(fc.bed);
        }
    }
    double diffSum = 0.0, diffMax = 0.0; long cols = 0;
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
        if (fine.role(x, z) != ShoreColumnRole::Free) continue;
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n) * z;
        const double d = std::abs(sumFine[i] - sumOuter[i]) / samples;
        diffSum += d; diffMax = std::max(diffMax, d); ++cols;
    }
    const double fracO = edgeO ? static_cast<double>(edgeFoamO) / edgeO : 0.0, fracF = edgeF ? static_cast<double>(edgeFoamF) / edgeF : 0.0;
    std::printf("  nest: %ld free columns, time-mean |fine - outer| mean %.4f max %.4f m; run-up outer %.3f fine %.3f; edge foam fraction outer %.3f (%ld) fine %.3f (%ld); step outer %.2f fine %.2f ms\n",
                cols, diffSum / cols, diffMax, peakOuter, peakFine, fracO, edgeO, fracF, edgeF, msOuter / 1800.0, msFine / 1800.0);
    RecordProperty("edge_foam_outer", static_cast<int>(fracO * 1000)); RecordProperty("edge_foam_fine", static_cast<int>(fracF * 1000));
    EXPECT_GT(cols, 1000);
    EXPECT_LT(diffSum / cols, 0.02);
    ASSERT_GT(peakOuter, 0.02f);
    EXPECT_NEAR(peakFine, peakOuter, 0.3f * peakOuter + 0.05f);
}

TEST(ShoreBandTest, NestedInteriorIndependentOfRingWidth) {
    // the fine ring's width is a cost bound: ring 2 m (radius 8) vs 4 m (radius 10), the same 12 m
    // free square at the same centre, the ramp the same in metres -> the free columns agree within 1 cm
    auto run = [](float radius, int inner, ShoreBand& outer, ShoreBand& fine) {
        ShoreBandParams fp; fp.radius = radius; fp.inner = inner; fp.ramp = 1.5f; fp.seawardBias = false;
        std::string err;
        ASSERT_TRUE(fine.siteNested(outer, glm::vec2(50.0f, 220.0f), fp, beachBed(), &err)) << err;
        SeaSwellParams sw; sw.amplitude = 0.45f; sw.wavelength = 14.0f; sw.windRad = 1.5707963f;
        for (int k = 1; k <= 600; ++k) { outer.tick(kDt, k * kDt, sw); fine.tick(kDt, 0.0f, sw); }
    };
    ShoreBand oa = siteOuter(glm::vec2(50.0f, 216.0f)), ob = siteOuter(glm::vec2(50.0f, 216.0f));
    ShoreBand a, b;
    run(8.0f, 6, oa, a); run(10.0f, 12, ob, b);
    float maxDiff = 0.0f; long compared = 0;
    for (int z = 0; z < a.record().n; ++z) for (int x = 0; x < a.record().n; ++x) {
        if (a.role(x, z) != ShoreColumnRole::Free) continue;
        const glm::vec2 c = a.columnCentre(x, z);
        const int bx = static_cast<int>(std::floor((c.x - b.record().originXZ.x) / b.record().h)), bz = static_cast<int>(std::floor((c.y - b.record().originXZ.y) / b.record().h));
        ASSERT_EQ(b.role(bx, bz), ShoreColumnRole::Free);
        maxDiff = std::max(maxDiff, static_cast<float>(std::abs(a.solver()->col(x, z).eta - b.solver()->col(bx, bz).eta)));
        ++compared;
    }
    std::printf("  nest ring-width independence: %ld columns, max difference %.4f m\n", compared, maxDiff);
    EXPECT_GT(compared, 1000);
    EXPECT_LT(maxDiff, 0.01f);
}

TEST(ShoreBandTest, HiddenColumnsDrawNothing) {
    // the 1 m band hides its columns under the fine band's box: no top or lateral quad there, and
    // none along the hole's edge; outside it the mesh is unchanged
    ShoreBand outer = siteOuter(glm::vec2(50.0f, 216.0f));
    WaterSurfaceMesh full; buildWaterSurfaceMesh(outer.field(), full);
    const glm::vec2 hMin(40.0f, 210.0f), hMax(60.0f, 230.0f);
    outer.setHiddenBox(true, hMin, hMax);
    outer.tick(kDt, 0.0f, SeaSwellParams{});
    WaterSurfaceMesh m; buildWaterSurfaceMesh(outer.field(), m);
    EXPECT_GT(outer.record().hidden, 300);
    long inside = 0;
    for (size_t q = 0; q < m.vertices.size(); q += 4) {
        glm::vec3 c(0.0f); for (size_t k = 0; k < 4; ++k) c += m.vertices[q + k].pos; c *= 0.25f;
        if (c.x > hMin.x - 0.5f && c.x < hMax.x + 0.5f && c.z > hMin.y - 0.5f && c.z < hMax.y + 0.5f) {
            const bool outside = c.x < hMin.x || c.x > hMax.x || c.z < hMin.y || c.z > hMax.y;
            if (!(outside && m.vertices[q].side == 0.0f)) ++inside;
        }
    }
    EXPECT_EQ(inside, 0) << "no quad under the hidden box, no lateral face at its edge";
    EXPECT_LT(m.topQuads, full.topQuads);
    EXPECT_EQ(m.sideQuads, full.sideQuads) << "the hole adds no lateral faces";
}

}  // namespace
}  // namespace Phyxel::Core::Water
