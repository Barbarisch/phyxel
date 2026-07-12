# {{NAME}} — Game Plan (GDD)

> The **design narrative** (the *why*) for this game. Machine state lives in
> `.phyxel/production.json` (the *what* + progress); this file holds design intent. Keep the two in
> sync — the workflow updates both. Genre(s): **{{GENRES}}**. Scaffolded {{DATE}}.
>
> A completeness test from GDD practice: *"detailed enough that someone could build a clone from
> this."* Fill it in as the game takes shape — empty sections are open work, not decoration.

## Vision & Pitch
_One paragraph: what is this game, and why is it fun?_

## Genre / Demographic / Tone
_Target player, comparables, mood. Genre: {{GENRES}}._

## Core Loop
_The primary verbs and the 30-second-to-few-minute loop the player repeats._

## Story & Setting
_Premise, world, key characters, arcs._

## World & Levels
_List the levels/areas and how the player moves between them._

## Mechanics
_Each system, specified **precisely** (vague "make the door open" burns agent tokens; "door slides
on +X when lever pulled" is cheap and deterministic). One sub-section per mechanic._

## UI / Screens
_Main menu, HUD, pause, options, victory/game-over, credits, and any genre screens (character
creation, inventory, quest log, …)._

## Audio Direction
_Menu music, gameplay music, core SFX, ambience. (Audio is the most-dropped scope — pin it here.)_

## Feel / Juice Targets
_Per interactive system: sound-on-input, easing (no linear snaps), impact feedback (shake/hit-stop/
particles), and coyote-time/input-buffering where there's movement. This is Axis C — it gates the
vertical slice._

## Win / Lose & Difficulty
_Explicit win condition(s), lose/fail condition(s), difficulty curve, and any difficulty options._

## Art Direction
_Style, palette, reference. Remember: an asset is "done" when it's a wired game **object** (collision/
depth/anchors), not raw art; stateful props need every state (e.g. crop seed→grown→harvested)._

## Accessibility & Localization
_Ordering-critical — decide **now**, before authoring strings/UI: remappable controls, subtitles,
text size, no color-only info, flash safety, separate volume sliders; externalize strings with stable
IDs, Unicode font, ~35% text-expansion-tolerant UI._

## Systems-Interaction Matrix
_The systemic-game centerpiece (§10.3): "system A × system B → expected effect." Each row is an L3
scripted check. Seeded from the genre template(s) — extend it as systems are added._

{{INTERACTION_MATRIX}}

_Survival: also confirm **resource-loop-closure** — every needed consumable has a reachable source
(no sink without a source = a survival softlock)._

## Content Targets
_Volume bars for `content_complete` (§10.4). `current` is countable from data; update as you build._

{{CONTENT_TARGETS}}

## Milestone Notes
_Per-milestone rationale / decisions. The checklist below comes from the merged genre template(s);
production state for each lives in `.phyxel/production.json`._

{{MILESTONE_NOTES}}

## Known Workarounds
_Engine gotchas learned while building (the "a minute here saves ten correcting output" file). Log
engine feature-requests with `/feedback`._

## Open Questions
_Unresolved design decisions. For these genres, decide early: turn-based vs real-time combat;
permadeath vs respawn; New Game+._
