#!/usr/bin/env bash
# ============================================================================
# test_count_check.sh — reconcile the ACTUAL gtest total against the TEST()
# macros a commit adds, before quoting a "Full suite: N tests" line anywhere.
#
# WHY THIS EXISTS: three commits in a row (2272cb3, de1cabd, 0c6449c) quoted an
# identical "Full suite: 3031 tests, 3027 passed, 4 skipped, 0 failed" line, even
# though 0c6449c added 4 TEST() cases -- so the total could not possibly have been
# unchanged. The number had been copy-pasted forward instead of re-run. A
# solution-auditor caught it only by doing two ~12-minute full runs by hand; the
# arithmetic mismatch alone (added 4 tests, total moved 0) was enough to flag it.
#
# Usage:
#   tools/test_count_check.sh            # count TEST macros added since HEAD~1, run suite
#   tools/test_count_check.sh <ref>      # ...since <ref>
#
# It does NOT decide whether the numbers are right -- it prints the delta the diff
# implies next to the total the suite actually reports, so a mismatch is visible
# before it reaches a commit message.
# ============================================================================
set -uo pipefail
cd "$(git rev-parse --show-toplevel)" || exit 1

REF="${1:-HEAD~1}"
EXE="build/tests/Debug/phyxel_tests.exe"

if [ ! -x "$EXE" ]; then
  echo "ERROR: $EXE not found. Build phyxel_tests first." >&2
  exit 1
fi

# TEST()/TEST_F() macros added and removed in test sources since REF.
ADDED=$(git diff "$REF"..HEAD -- 'tests/**/*.cpp' | grep -cE '^\+[[:space:]]*TEST(_F)?\(' || true)
REMOVED=$(git diff "$REF"..HEAD -- 'tests/**/*.cpp' | grep -cE '^-[[:space:]]*TEST(_F)?\(' || true)
NET=$((ADDED - REMOVED))

echo "TEST() macros since $REF:  +${ADDED} / -${REMOVED}  => net ${NET}"
echo "Running the full suite (this takes ~12 min in Debug)..."
SUMMARY=$("$EXE" --gtest_brief=1 2>&1 | grep -E "tests from .* test suites ran|\[  PASSED  \]|\[  SKIPPED \]|\[  FAILED  \]" | tail -5)
echo "$SUMMARY"

TOTAL=$(echo "$SUMMARY" | grep -oE '^\[==========\] [0-9]+' | grep -oE '[0-9]+' | head -1)
echo
echo "RECONCILE: the diff adds a net ${NET} test(s). If you are quoting a total in a"
echo "commit message, it must be the PREVIOUS ACTUAL total + ${NET} -- not the number"
echo "from the previous commit's MESSAGE. Actual total now: ${TOTAL:-unknown}."
echo "NOTE: verify the exe is fresh (a stale link silently reports the old count):"
ls -la "$EXE" | awk '{print "  exe:", $6, $7, $8}'
