---
description: Log a lesson-learned or engine feature-request from this game-dev session to the Phyxel engine's feedback inbox.
---
Capture game-dev feedback for the Phyxel engine team so it reaches engine development.

The user's note: $ARGUMENTS

Steps:
1. Distill the note (or, if it's empty, review this session) into one or more concise items.
2. Classify each as `bug`, `gotcha`, or `feature-request`.
3. Log each by running the Phyxel CLI (it resolves the engine repo + current project automatically):

   `phyxel feedback "<one concise paragraph>" --type <bug|gotcha|feature-request>`

4. Tell the user what you logged (type + summary).

Notes:
- Keep each entry self-contained — the engine-dev session reading it has no context from here.
- Prefer specific, actionable phrasing ("spawn_template can't rotate on placement" beats
  "templates are awkward").
- If `phyxel feedback` reports the engine home isn't set, tell the user to run `phyxel init`
  once on this machine.
