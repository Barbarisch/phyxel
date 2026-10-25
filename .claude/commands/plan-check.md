Check the work done against the plan of record, and record the result. Run it periodically: after every finished plan item,
whenever the owner asks, and at the start of a session that picks the work up.

The plan to check: $ARGUMENTS (default: `docs/WaterCore.md` §30, the plan of record, with §30.4 the current track, §30.5 the
drift log, §30.6 the check log).

This is an audit, not a build step. Do not fix code. The only edits it makes are to the plan document (new drift rows, a
check-log row, and status corrections), and they are committed and pushed like any other step.

## 1. Read

- Read the plan section in full: the order, the rules, the current track's table, the drift log and the check log.
- Find the last row of the check log. Its "range checked" is where this check starts.
- List everything since then: `git log --oneline <last checked commit>..HEAD`, plus any uncommitted work (`git status`).
  Also list every document section written or changed in that range (`git diff <commit>..HEAD -- docs/`).
- Read the owner's directives since the last check: quotes in the plan document, the memory feedback files, and this
  conversation. Every owner directive must already appear in the plan.

## 2. Map every piece of work to the plan

For each commit and each new or changed section, state the plan item it belongs to (A-n, a track-B part, a track-C step,
W-n), or say that it is **drift**. Drift must have a row in the drift log with what was done, what the plan said, the
reason, and who approved it. A drift with no row is **unlogged drift**, the thing this check exists to catch.

## 3. Check each item finished in the range against the plan's gates

For each item whose status changed to built or signed off, answer with evidence (file:line, commit, test name, evidence
file). Do not restate the rule.
1. **Design written before the build:** its section existed in a commit before the code.
2. **`/design-check` was run** and its verdict recorded. A check written inline is drift.
3. **A red test shown failing first,** with the output recorded.
4. **Whole-frame cost** (`docs/PerformanceTiers.md` rule 4): the CPU frame, the GPU frame and fps, plus the feature's own CPU
   and GPU share, in a realistic scene and the worst case, and what was not measured. Fps alone does not pass.
5. **The tier table** (`PerformanceTiers.md` §4a), or a stated reason it has no knob. Does any tier change the outcome? That
   is a defect.
6. **The owner's look:** a video or capture in motion, with its path. The status says "not signed off" until the owner has
   said so, in quoted words.
7. **The standing rules** in the plan's status page (for water, §0a "Standing rules"), and any `solver_*.comp` gate such as
   `tools/debris_settle_bench.py`.

## 4. Is the plan itself current?

- Does the order match the owner's latest directive?
- Do the statuses in the current track's table match the build ledger sections?
- Is the next item clear? If the agent has recommended something not in the table, it is drift until it is added.
- Are open remedies in the drift log still open, and is any of them overdue (an earlier item already finished without it)?

## 5. Verdict and record

Give one verdict:
- **ON TRACK:** every piece of work maps to the plan, every gate passed, and the plan is current.
- **DRIFTING (logged):** the work left the plan, every drift has a row, and every bad reason has a remedy.
- **DRIFTING (unlogged):** some work left the plan with no record. List each instance.
- **PLAN STALE:** the plan does not reflect the owner's directives or the build ledger.

Then, in the plan document:
- add a drift-log row for each piece of unlogged drift found, giving its reason honestly ("none beyond speed" is an
  acceptable reason, and it gets a remedy);
- correct any stale status in the current track's table;
- append a check-log row: number, date, range checked (to the HEAD commit), verdict, drift found, and the rows it logged.

Commit and push the doc change (`plan-check(C-n): <verdict>`).

Report to the owner in plain language: the verdict, the drift found with its reasons, the remedies still open, and the next
item. Then stop. Do not start the next item in the same turn unless the owner has asked for that.
