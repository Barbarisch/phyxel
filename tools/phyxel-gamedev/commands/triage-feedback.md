---
description: Review the Phyxel game-dev feedback inbox and fold actionable items into the engine roadmap.
---
Triage the game-dev feedback inbox. This is an **engine-dev** task — run it from the Phyxel
engine repo.

Steps:
1. Read `docs/feedback/inbox.md`.
2. Group the entries by type (bug / gotcha / feature-request) and summarize them, collapsing
   duplicates and noting how often each theme recurs.
3. Propose what to act on and how:
   - actionable items → fold into `docs/AgentContext.md`'s "Current workstreams & roadmap";
   - bugs → flag clearly for a fix;
   - vague/low-value items → suggest dropping.
4. After the user confirms, move the handled entries from `inbox.md` to
   `docs/feedback/archive.md` (preserve each entry's `## <date> — <project> — <type>` header),
   leaving anything deferred in the inbox.

Never delete feedback without archiving it. Don't commit unless the user asks.
