#pragma once

// ============================================================================
// ValidationReport / Issue — shared validation result type for Structure
// Generation v2 (docs/structure-generation/StructureGenerationV2.md).
//
// One report type for every gate in the pipeline: the pre-build BuildingProgram
// validator, the AssetValidator, and the post-build geometry checks. An Issue is
// a single finding; a ValidationReport collects them. `ok()` is the gate verdict
// (no errors; warnings are advisory). Mirrors the v1 Python validator's
// Issue/ValidationReport so the two stay conceptually aligned during the port.
//
// Header-only: it is pure data + small helpers, used across many translation
// units; no .cpp keeps the dependency surface trivial.
// ============================================================================

#include <string>
#include <vector>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

enum class Severity { Error, Warning };

inline const char* severityName(Severity s) {
    return s == Severity::Error ? "error" : "warning";
}

struct Issue {
    Severity    severity = Severity::Error;
    std::string code;       ///< stable machine code, e.g. "room_overlap", "dim_out_of_range"
    std::string message;    ///< human-readable explanation
    std::string where;      ///< optional locus, e.g. a room id, archetype name, "story 1"

    std::string str() const {
        std::ostringstream os;
        os << "[" << severityName(severity) << "] " << code;
        if (!where.empty()) os << " (" << where << ")";
        os << ": " << message;
        return os.str();
    }
};

class ValidationReport {
public:
    void add(const Issue& issue) { m_issues.push_back(issue); }

    void addError(const std::string& code, const std::string& message,
                  const std::string& where = "") {
        m_issues.push_back({Severity::Error, code, message, where});
    }
    void addWarning(const std::string& code, const std::string& message,
                    const std::string& where = "") {
        m_issues.push_back({Severity::Warning, code, message, where});
    }

    /// Fold another report's issues into this one (used to compose gates).
    void merge(const ValidationReport& other) {
        m_issues.insert(m_issues.end(), other.m_issues.begin(), other.m_issues.end());
    }

    /// Gate verdict: passes iff there are no errors (warnings are advisory).
    bool ok() const {
        for (const auto& i : m_issues)
            if (i.severity == Severity::Error) return false;
        return true;
    }

    bool hasWarnings() const {
        for (const auto& i : m_issues)
            if (i.severity == Severity::Warning) return true;
        return false;
    }

    size_t errorCount() const { return count(Severity::Error); }
    size_t warningCount() const { return count(Severity::Warning); }

    const std::vector<Issue>& issues() const { return m_issues; }
    bool empty() const { return m_issues.empty(); }
    void clear() { m_issues.clear(); }

    std::string summary() const {
        std::ostringstream os;
        os << (ok() ? "OK" : "FAILED") << " — " << errorCount() << " error(s), "
           << warningCount() << " warning(s)";
        for (const auto& i : m_issues) os << "\n  " << i.str();
        return os.str();
    }

    nlohmann::json toJson() const {
        nlohmann::json issues = nlohmann::json::array();
        for (const auto& i : m_issues)
            issues.push_back({{"severity", severityName(i.severity)}, {"code", i.code},
                              {"message", i.message}, {"where", i.where}});
        return {{"ok", ok()}, {"errors", errorCount()}, {"warnings", warningCount()},
                {"issues", issues}};
    }

private:
    size_t count(Severity s) const {
        size_t n = 0;
        for (const auto& i : m_issues) if (i.severity == s) ++n;
        return n;
    }

    std::vector<Issue> m_issues;
};

} // namespace Core
} // namespace Phyxel
