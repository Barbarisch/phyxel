#include <gtest/gtest.h>
#include "core/CurrencySystem.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Currency — totalInCopper
// ---------------------------------------------------------------------------

TEST(CurrencyTest, EmptyWalletIsZero) {
    Currency c;
    EXPECT_EQ(c.totalInCopper(), 0);
}

TEST(CurrencyTest, TotalInCopperConversions) {
    Currency c;
    c.platinum = 1;  // 1000 cp
    c.gold     = 1;  // 100 cp
    c.electrum = 1;  //  50 cp
    c.silver   = 1;  //  10 cp
    c.copper   = 1;  //   1 cp
    EXPECT_EQ(c.totalInCopper(), 1161);
}

TEST(CurrencyTest, TotalInCopperGoldOnly) {
    Currency c;
    c.gold = 15;
    EXPECT_EQ(c.totalInCopper(), 1500);
}

// ---------------------------------------------------------------------------
// Currency — canAfford / spend
// ---------------------------------------------------------------------------

TEST(CurrencyTest, CanAffordTrueWhenSufficient) {
    Currency c;
    c.gold = 1;  // 100 cp
    EXPECT_TRUE(c.canAfford(50));
    EXPECT_TRUE(c.canAfford(100));
}

TEST(CurrencyTest, CanAffordFalseWhenInsufficient) {
    Currency c;
    c.silver = 3;  // 30 cp
    EXPECT_FALSE(c.canAfford(31));
}

TEST(CurrencyTest, SpendReducesTotal) {
    Currency c;
    c.gold = 1;  // 100 cp
    EXPECT_TRUE(c.spend(30));
    EXPECT_EQ(c.totalInCopper(), 70);
}

TEST(CurrencyTest, SpendAllLeaveZero) {
    Currency c;
    c.gold = 1;
    EXPECT_TRUE(c.spend(100));
    EXPECT_EQ(c.totalInCopper(), 0);
}

TEST(CurrencyTest, SpendFailsWhenInsufficient) {
    Currency c;
    c.copper = 5;
    EXPECT_FALSE(c.spend(10));
    EXPECT_EQ(c.totalInCopper(), 5);  // unchanged
}

TEST(CurrencyTest, SpendZeroAlwaysSucceeds) {
    Currency c;
    EXPECT_TRUE(c.spend(0));
}

TEST(CurrencyTest, SpendBreaksPlatinum) {
    Currency c;
    c.platinum = 1;  // 1000 cp
    EXPECT_TRUE(c.spend(1));
    EXPECT_EQ(c.totalInCopper(), 999);
}

// ---------------------------------------------------------------------------
// Currency — add
// ---------------------------------------------------------------------------

TEST(CurrencyTest, AddConvertsToOptimalDenominations) {
    Currency c;
    c.add(1000);  // should become 1 pp
    EXPECT_EQ(c.totalInCopper(), 1000);
    EXPECT_EQ(c.platinum, 1);
    EXPECT_EQ(c.gold,     0);
    EXPECT_EQ(c.silver,   0);
    EXPECT_EQ(c.copper,   0);
}

TEST(CurrencyTest, AddSplitsDenominations) {
    Currency c;
    c.add(115);  // 1 gp (100) + 1 sp (10) + 5 cp
    EXPECT_EQ(c.gold,   1);
    EXPECT_EQ(c.silver, 1);
    EXPECT_EQ(c.copper, 5);
    EXPECT_EQ(c.totalInCopper(), 115);
}

TEST(CurrencyTest, AddCoins) {
    Currency c;
    c.addCoins(5, 3, 0, 2, 1);  // 5cp + 30cp + 200cp + 1000cp = 1235 cp
    EXPECT_EQ(c.totalInCopper(), 1235);
    EXPECT_EQ(c.copper,   5);
    EXPECT_EQ(c.silver,   3);
    EXPECT_EQ(c.gold,     2);
    EXPECT_EQ(c.platinum, 1);
}

// ---------------------------------------------------------------------------
// Currency — toString
// ---------------------------------------------------------------------------

TEST(CurrencyTest, ToStringGoldAndCopper) {
    Currency c;
    c.gold   = 15;
    c.copper = 3;
    std::string s = c.toString();
    EXPECT_NE(s.find("15 gp"), std::string::npos);
    EXPECT_NE(s.find("3 cp"),  std::string::npos);
}

TEST(CurrencyTest, ToStringEmpty) {
    Currency c;
    EXPECT_EQ(c.toString(), "0 cp");
}

TEST(CurrencyTest, ToStringOmitsZeroDenominations) {
    Currency c;
    c.gold = 5;
    std::string s = c.toString();
    EXPECT_EQ(s.find("cp"), std::string::npos);
    EXPECT_EQ(s.find("sp"), std::string::npos);
    EXPECT_NE(s.find("gp"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Currency — JSON round-trip
// ---------------------------------------------------------------------------

TEST(CurrencyTest, JsonRoundTrip) {
    Currency c;
    c.copper   = 7;
    c.silver   = 3;
    c.gold     = 15;
    c.platinum = 2;

    auto j = c.toJson();
    Currency c2;
    c2.fromJson(j);

    EXPECT_EQ(c2.copper,   7);
    EXPECT_EQ(c2.silver,   3);
    EXPECT_EQ(c2.gold,    15);
    EXPECT_EQ(c2.platinum, 2);
    EXPECT_EQ(c2.totalInCopper(), c.totalInCopper());
}

// ---------------------------------------------------------------------------
// CurrencySystem static helpers
// ---------------------------------------------------------------------------

TEST(CurrencySystemTest, ToCopper) {
    EXPECT_EQ(CurrencySystem::toCopper(0,  0, 0, 1,  0), 100);
    EXPECT_EQ(CurrencySystem::toCopper(0,  0, 0, 0,  1), 1000);
    EXPECT_EQ(CurrencySystem::toCopper(5, 10, 0, 2,  0), 5 + 100 + 200);
    EXPECT_EQ(CurrencySystem::toCopper(0,  0, 2, 0,  0), 100);  // 2 electrum = 100 cp
}

TEST(CurrencySystemTest, FromCopper) {
    auto c = CurrencySystem::fromCopper(1111);
    EXPECT_EQ(c.totalInCopper(), 1111);
    // 1 pp = 1000, remainder 111 = 1 gp (100) + 1 sp (10) + 1 cp
    EXPECT_EQ(c.platinum, 1);
    EXPECT_EQ(c.gold,     1);
    EXPECT_EQ(c.silver,   1);
    EXPECT_EQ(c.copper,   1);
}

TEST(CurrencySystemTest, FromCopperZero) {
    auto c = CurrencySystem::fromCopper(0);
    EXPECT_EQ(c.totalInCopper(), 0);
}

TEST(CurrencySystemTest, FormatValue) {
    std::string s = CurrencySystem::formatValue(1500);
    EXPECT_NE(s.find("gp"), std::string::npos);
}

TEST(CurrencySystemTest, RarityBaseValues) {
    EXPECT_GT(CurrencySystem::rarityBaseValueCp(0), 0);   // Common
    EXPECT_GT(CurrencySystem::rarityBaseValueCp(1),       // Uncommon > Common
              CurrencySystem::rarityBaseValueCp(0));
    EXPECT_GT(CurrencySystem::rarityBaseValueCp(2),       // Rare > Uncommon
              CurrencySystem::rarityBaseValueCp(1));
    EXPECT_EQ(CurrencySystem::rarityBaseValueCp(5), 0);   // Artifact = priceless
}

// ---------------------------------------------------------------------------
// Round-trip spend + add
// ---------------------------------------------------------------------------

TEST(CurrencyTest, SpendAndAddRoundTrip) {
    Currency wallet;
    wallet.add(5000);  // 50 gp
    EXPECT_EQ(wallet.totalInCopper(), 5000);

    wallet.spend(1500);  // spend 15 gp
    EXPECT_EQ(wallet.totalInCopper(), 3500);

    wallet.add(500);   // add 5 gp
    EXPECT_EQ(wallet.totalInCopper(), 4000);
}
