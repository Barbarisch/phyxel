#include <gtest/gtest.h>

#include "core/ValidationReport.h"

using namespace Phyxel::Core;

TEST(ValidationReportTest, EmptyReportPasses) {
    ValidationReport r;
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(r.errorCount(), 0u);
    EXPECT_FALSE(r.hasWarnings());
}

TEST(ValidationReportTest, ErrorFailsTheGate) {
    ValidationReport r;
    r.addError("room_overlap", "rooms A and B overlap", "story 0");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.errorCount(), 1u);
    EXPECT_EQ(r.warningCount(), 0u);
}

TEST(ValidationReportTest, WarningIsAdvisoryOnly) {
    ValidationReport r;
    r.addWarning("tight_landing", "stair landing is tight but passable");
    EXPECT_TRUE(r.ok());            // warnings don't fail the gate
    EXPECT_TRUE(r.hasWarnings());
    EXPECT_EQ(r.warningCount(), 1u);
}

TEST(ValidationReportTest, MergeCombinesIssues) {
    ValidationReport a, b;
    a.addError("e1", "first");
    b.addWarning("w1", "second");
    b.addError("e2", "third");
    a.merge(b);
    EXPECT_EQ(a.issues().size(), 3u);
    EXPECT_EQ(a.errorCount(), 2u);
    EXPECT_EQ(a.warningCount(), 1u);
    EXPECT_FALSE(a.ok());
}

TEST(ValidationReportTest, IssueStringHasCodeWhereAndMessage) {
    Issue i{Severity::Error, "dim_out_of_range", "height 1.6 exceeds 0.9 +/- 0.15", "fence_picket"};
    std::string s = i.str();
    EXPECT_NE(s.find("error"), std::string::npos);
    EXPECT_NE(s.find("dim_out_of_range"), std::string::npos);
    EXPECT_NE(s.find("fence_picket"), std::string::npos);
    EXPECT_NE(s.find("height 1.6"), std::string::npos);
}
