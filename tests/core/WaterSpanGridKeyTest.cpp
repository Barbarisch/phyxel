#include <gtest/gtest.h>

#include "core/WaterSpanGridKey.h"

#include <algorithm>
#include <vector>

// WaterCore Phase D slice D3 (docs/WaterCore.md 16.4): the span render grid rebuilds when the
// RESIDENT SET changes, not when its COUNT changes. The defect this pins (WaterRethink WP1 step 6,
// measured in WP0): streaming swapped 5,120 columns' worth of chunks for the same number of others
// and the grid never rebuilt, so terrain rendered with dry water where the chunks held spans.

namespace Phyxel::Core {
namespace {

std::vector<glm::ivec3> coords(std::initializer_list<glm::ivec3> l) { return {l}; }

TEST(WaterSpanGridKeyTest, SameCountDifferentSetRebuilds) {
    const auto a = coords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}});
    const auto b = coords({{0, 0, 0}, {2, 0, 0}, {0, 0, 1}});   // one chunk swapped, count equal
    ASSERT_EQ(a.size(), b.size());
    EXPECT_NE(residentSetHash(a.begin(), a.end()), residentSetHash(b.begin(), b.end()))
        << "equal counts must not collide: this is the 5,120-column stale-grid defect";
}

TEST(WaterSpanGridKeyTest, OrderOfIterationDoesNotMatter) {
    // chunkMap is an unordered_map: the hash must be a function of the SET, so a rehash that
    // reorders iteration cannot trigger a rebuild (cost) or, worse, hide one.
    auto a = coords({{3, 1, -2}, {-7, 0, 5}, {0, 0, 0}, {12, 2, 12}});
    auto b = a;
    std::reverse(b.begin(), b.end());
    std::swap(b[1], b[2]);
    EXPECT_EQ(residentSetHash(a.begin(), a.end()), residentSetHash(b.begin(), b.end()));
}

TEST(WaterSpanGridKeyTest, NegativeAndPositiveCoordsAreDistinct) {
    const auto a = coords({{-1, 0, 0}});
    const auto b = coords({{1, 0, 0}});
    EXPECT_NE(residentSetHash(a.begin(), a.end()), residentSetHash(b.begin(), b.end()));
    const auto c = coords({{0, 0, -1}});
    const auto d = coords({{-1, 0, 0}});
    EXPECT_NE(residentSetHash(c.begin(), c.end()), residentSetHash(d.begin(), d.end()))
        << "axes must not be interchangeable";
}

TEST(WaterSpanGridKeyTest, EmptySetHasItsOwnKeyAndRevisionsChangeIt) {
    const std::vector<glm::ivec3> none;
    const auto a = coords({{0, 0, 0}});
    SpanGridKey k0 = makeSpanGridKey(none.begin(), none.end(), /*spanRevision=*/0, /*awakeRevision=*/0);
    SpanGridKey k1 = makeSpanGridKey(a.begin(), a.end(), 0, 0);
    EXPECT_NE(k0, k1);
    // A runtime span write (write-back, edit, ground sync) bumps the span revision: same residency,
    // different key, so the grid re-reads the chunks it already had.
    EXPECT_NE(k1, makeSpanGridKey(a.begin(), a.end(), 1, 0));
    // An active volume waking or sleeping changes which columns the grid must mask.
    EXPECT_NE(k1, makeSpanGridKey(a.begin(), a.end(), 0, 1));
    EXPECT_EQ(k1, makeSpanGridKey(a.begin(), a.end(), 0, 0));
    // Phase D5: a look / wind / wave change re-packs the grid's colour channels
    EXPECT_NE(k1, makeSpanGridKey(a.begin(), a.end(), 0, 0, 1));
}

}  // namespace
}  // namespace Phyxel::Core
