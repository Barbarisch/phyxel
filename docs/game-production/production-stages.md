# Production stages (Axis A)

The project-wide **maturity gate**. Distinct from per-milestone completeness (Axis B) and feel
(Axis C) — see [`README.md`](README.md) §4a. A project sits at exactly one `stage` in
`production.json`; it `advance_stage`s only when the current stage's **exit criteria** are met.

The point of an ordered stage axis: you cannot tick `credits`/`package` while `core_loop` is still a
prototype, and you must **prove one deep slice before scaling breadth** (the failure mode of every
consumer AI builder — "every screen exists once but nothing is deep").

| # | Stage | Exit criterion (the gate to leave this stage) |
|---|-------|-----------------------------------------------|
| 1 | `concept` | `design_brief` done: `GAMEPLAN.md` filled — genre, demographic, core loop, win/lose stated. |
| 2 | `vertical_slice` | **One** complete slice at final quality: a level playable start→win with the *real* core loop (not placeholder), and its interactive milestones' **feel pass done**. Prove depth before breadth. |
| 3 | `feature_complete` | Every **required** milestone present (may be rough) — **feature-freeze**. New systems stop; only content + polish from here. |
| 4 | `content_complete` | All content in, placeholders gone, **content-volume targets met** (§10.4), balance tuned — **content-lock**. |
| 5 | `shippable` | Perf target met, `qa_pass` + adversarial playtest clean, packaged — **code-freeze**. Ready to ship. |

Notes:

- **Freeze vocabulary matters.** `feature_complete` = feature-freeze (stop adding systems);
  `content_complete` = content-lock (stop adding content); `shippable` = code-freeze. These prevent
  the "just one more system" churn that keeps a game perpetually 90% done.
- The **vertical-slice gate is the most important one** — it is where feel (Axis C) first becomes
  mandatory, and it matches how real fast AI builds actually worked (one polished slice, then scale).
- Stages are **scope maturity**, orthogonal to the per-milestone **L0–L4 quality** ladder. A project
  can be `stage: vertical_slice` with most milestones still `L0` — that's expected; the slice proves
  the *spine* end-to-end before the rest is filled in.
- `advance_stage` (Phase 2 MCP rollup) checks these criteria and refuses to skip ahead; until then the
  stage is set by hand / the workflow.
