# Story Engine — Implementation Progress

*Last updated: March 23, 2026*

## Overview

Building a hybrid story engine with a tunable constraint spectrum (scripted ↔ emergent).
Full design doc: `docs/StoryEngineDesign.md`

## Implementation Phases

### Phase S1: World State & Character Profiles ✅
**Completed: March 21, 2026 | Tests: 72 new (583 total, 62 suites)**

Built the core data model for the story engine:

**New files created:**
| File | Purpose |
|------|---------|
| `engine/include/story/StoryTypes.h` | `WorldState`, `Faction`, `Location`, `WorldVariable`, `WorldEvent` structs |
| `engine/src/story/StoryTypes.cpp` | WorldState mutations, queries, full JSON round-trip serialization |
| `engine/include/story/CharacterProfile.h` | `AgencyLevel` (0–3), `PersonalityTraits` (Big Five + custom), `EmotionalState`, `CharacterGoal`, `Relationship`, `CharacterProfile` |
| `engine/src/story/CharacterProfile.cpp` | Emotion decay (tied to neuroticism), personality trait queries, JSON serialization |
| `engine/include/story/CharacterMemory.h` | `KnowledgeSource`, `KnowledgeFact`, `CharacterMemory` — the knowledge asymmetry core |
| `engine/src/story/CharacterMemory.cpp` | witness/hear/rumor propagation, confidence decay, personality-based perception distortion, AI context summary builder |
| `engine/include/story/StoryEngine.h` | `StoryEngine` facade — world setup, character management, runtime updates, save/load |
| `engine/src/story/StoryEngine.cpp` | Facade implementation: wires characters to factions, memory + emotion lifecycle, full state serialization |
| `tests/story/StoryTypesTest.cpp` | 18 tests: WorldVariable, Location, Faction, WorldEvent, WorldState queries + JSON |
| `tests/story/CharacterProfileTest.cpp` | 22 tests: AgencyLevel, PersonalityTraits, EmotionalState, Goals, Relationships, Profile round-trip |
| `tests/story/CharacterMemoryTest.cpp` | 17 tests: witness, hear, innate, confidence, distortion, decay, fading, serialization |
| `tests/story/StoryEngineTest.cpp` | 17 tests: world setup, character CRUD, faction linking, knowledge injection, goal priority, save/load |

**Modified files:**
| File | Change |
|------|--------|
| `tests/CMakeLists.txt` | Added `"story/*.cpp"` to test GLOB |

**Key design decisions implemented:**
- Agency levels 0 (Scripted) → 3 (Autonomous) with string serialization
- Big Five personality model + developer-defined custom traits
- Emotion decay rate tied to neuroticism (high neuroticism = emotions linger)
- Perception distortion: neurotic → exaggerate threat, agreeable → downplay conflict, low openness → miss unusual details
- Confidence decay: witnessed/innate = permanent, told-by = slow (0.005/s), rumors = fast (0.02/s), forgotten at 0
- Agreeableness affects trust in secondhand info (high agreeableness = higher confidence when told)
- StoryEngine facade manages worldState + characters + memories, saves/loads as single JSON blob

---

### Phase S2: Event System & Knowledge Propagation ✅
**Completed: March 22, 2026 | Tests: 36 new (619 total, 64 suites)**

Built the event broadcasting and knowledge propagation systems:

**New files created:**
| File | Purpose |
|------|---------|
| `engine/include/story/EventBus.h` | Thread-safe event bus — `subscribe()`, `subscribeToType()`, `unsubscribe()`, `emit()` with snapshot-based emission to avoid deadlocks |
| `engine/src/story/EventBus.cpp` | Implementation: copies listener list under lock, invokes without lock |
| `engine/include/story/KnowledgePropagator.h` | Three knowledge channels: witnessing (spatial), dialogue (trust-filtered), rumors (background spread). Configurable thresholds. `PositionLookup` callback for game integration |
| `engine/src/story/KnowledgePropagator.cpp` | `findWitnesses()` (participants + spatial), `propagateWitness()`, `selectFactsToShare()` (scored by confidence × extraversion × trust), `propagateDialogue()`, `propagateRumors()` (O(n²) pair check with position cache), `spreadRumorBetween()` (deterministic hash-based selection, 50% confidence reduction) |
| `tests/story/EventBusTest.cpp` | 12 tests: subscribe/emit, type filtering, unsubscribe, subscriber count, clear, case sensitivity, unique IDs |
| `tests/story/KnowledgePropagatorTest.cpp` | 16 tests: participant witnesses, spatial witnesses, no-position fallback, zero radius, witness confidence, dialogue sharing, max facts, low confidence skip, distrust block, already-known skip, rumor spread nearby/distant, innate skip, confidence reduction, configuration |

**Modified files:**
| File | Change |
|------|--------|
| `engine/include/story/StoryEngine.h` | Added `EventBus` + `KnowledgePropagator` includes, `shareKnowledge()`, `spreadRumors()`, `getEventBus()`, `getPropagator()` accessors, `m_eventBus` + `m_propagator` members |
| `engine/src/story/StoryEngine.cpp` | `triggerEvent()` now broadcasts via EventBus + propagates witnesses; new `shareKnowledge()` + `spreadRumors()` implementations |
| `tests/story/StoryEngineTest.cpp` | 8 new S2 integration tests: event bus broadcast, type filtering, witness propagation, knowledge sharing, rumor spreading, accessor tests |

**Key design decisions implemented:**
- Three knowledge propagation channels: witnessing (spatial proximity + participants), dialogue (trust-filtered, extraversion-boosted), rumors (background spread with confidence decay)
- EventBus uses snapshot-based emission (copies listeners under mutex, invokes without lock) to be thread-safe without risk of deadlock
- Rumor spread uses deterministic hash-based selection (no RNG state needed) — same character pair picks same conversation topic
- Rumor confidence reduced by 50% per hop (telephone game effect)
- Dialogue sharing scored by `confidence × (0.5 + 0.5 × extraversion) × (0.5 + 0.5 × trust)` — extroverted characters with high trust share more
- Facts below 0.3 confidence aren't shared in dialogue; facts below 0.2 confidence aren't spread as rumors
- Innate knowledge excluded from rumor spread (not gossip-worthy)
- `PositionLookup` callback decouples story system from entity system — game sets the callback to bridge the gap

---

### Phase S3: Story Director Core ✅
**Completed: March 22, 2026 | Tests: 47 new (666 total, 71 suites)**

Built the GM agent — story arcs with four constraint modes, beat evaluation, tension/pacing management:

**New files created:**
| File | Purpose |
|------|---------|
| `engine/include/story/StoryDirectorTypes.h` | `BeatType` (Hard/Soft/Optional), `ArcConstraintMode` (Scripted/Guided/Emergent/Freeform), `BeatStatus`, `StoryBeat`, `StoryArc`, `DirectorAction` structs |
| `engine/src/story/StoryDirectorTypes.cpp` | Enum conversions, JSON serialization, StoryArc helpers (completedBeatCount, recalculateProgress, getBeat) |
| `engine/include/story/StoryDirector.h` | `StoryDirector` class — arc management, 4-mode beat evaluation, pacing, action emission, EventBus integration, serialization |
| `engine/src/story/StoryDirector.cpp` | Full implementation: Scripted (sequential forced), Guided (flexible with pacing/nudges), Emergent (skip hard, organic soft), Freeform (skip all), default condition evaluator, tension curve interpolation |
| `tests/story/StoryDirectorTypesTest.cpp` | 12 tests: enum round-trips, StoryBeat JSON, StoryArc helpers + JSON, DirectorAction JSON |
| `tests/story/StoryDirectorTest.cpp` | 33 tests: arc CRUD, all 4 constraint modes, condition evaluation (default + custom), director actions, manual beat control, tension/pacing, EventBus integration, serialization, StoryEngine integration |

**Modified files:**
| File | Changes |
|------|---------|
| `engine/include/story/StoryEngine.h` | Added `StoryDirector.h` include, `getDirector()` accessor, `addStoryArc()` method, `m_director` member |
| `engine/src/story/StoryEngine.cpp` | `update()` calls `m_director.update()`, `addStoryArc()` activates arc + wires director to EventBus, serialization includes director state |

**Key design decisions implemented:**
- Four constraint modes form a spectrum: Scripted → Guided → Emergent → Freeform, each progressively loosening director control
- **Scripted**: Sequential beats, forced in order, blocks on untriggered hard beats
- **Guided**: Flexible ordering with pacing enforcement (minTimeBetweenBeats per-beat), emits "nudge_beat" action when stalled past maxTimeWithoutProgress on hard beats
- **Emergent**: Automatically skips all Hard beats, only activates Soft/Optional if trigger conditions naturally met, director actions tagged as "suggestion"
- **Freeform**: Skips all beats, pacing management only — purely emergent gameplay
- Default condition evaluator checks WorldVariable truthiness via `std::visit` on `variant<bool,int,float,string>`
- Tension curve interpolation: linear interpolation between user-defined (progress, tension) keypoints
- Smooth `dramaTension` adjustment: moves toward target at 0.1/sec rate (no jarring jumps)
- `DirectorActionCallback` lets game code react to director decisions (event injection, agency promotion, goal suggestion, faction relation changes, world variable setting)
- EventBus integration: director listens for world events and can trigger beat conditions in response

---

### Phase S4: Character AI Agents ✅
**Completed: March 22, 2026 | Tests: 64 new (730 total, 76 suites)**

Built the character AI agent system — abstract interface, rule-based fallback, LLM-backed agent, and NPCBehavior bridge:

**New files created:**
| File | Purpose |
|------|---------|
| `engine/include/story/CharacterAgent.h` | `CharacterDecisionContext` (subjective view — never sees WorldState), `CharacterDecision` (action + params + dialogue + emotion), abstract `CharacterAgent` interface (decide, generateDialogue) |
| `engine/src/story/CharacterAgent.cpp` | `CharacterDecision` JSON serialization |
| `engine/include/story/RuleBasedCharacterAgent.h` | `RuleBasedCharacterAgent` — offline fallback using personality traits, emotions, goals, relationships. Custom `Rule` support (condition → priority score, action → decision) |
| `engine/src/story/RuleBasedCharacterAgent.cpp` | Personality-based scoring: flee (fear + neuroticism - bravery), attack (anger + low agreeableness), trade (trust + agreeableness), speak (extraversion), idle (baseline). Custom rules override built-in logic. Dialogue templates keyed by emotion. |
| `engine/include/story/LLMCharacterAgent.h` | `LLMCharacterAgent` — AI-backed agent via `LLMRequestCallback`. Builds structured prompts, parses JSON responses. Falls back to any `CharacterAgent*` on failure. |
| `engine/src/story/LLMCharacterAgent.cpp` | Prompt builders: character identity, Big Five personality, goals, roles, knowledge summary, situation, conversation partner/history, available actions, JSON response format. Dialogue prompt for in-character speech. Graceful fallback chain: LLM → parse → fallback agent → idle. |
| `engine/include/scene/behaviors/StoryDrivenBehavior.h` | `StoryDrivenBehavior : NPCBehavior` — bridges NPC system with story agents. Configurable decision interval, `DecisionCallback`, custom `SituationBuilder`, interactor profile lookup. |
| `engine/src/scene/behaviors/StoryDrivenBehavior.cpp` | Builds `CharacterDecisionContext` from `NPCContext` + CharacterMemory + CharacterProfile. Decision timer throttles agent calls. onInteract generates AI dialogue for AgencyLevel ≥ Guided, falls back to decide() for Scripted/Templated. |
| `tests/story/CharacterAgentTest.cpp` | 5 tests: CharacterDecision JSON round-trip + minimal fields, CharacterDecisionContext defaults + with profile |
| `tests/story/RuleBasedCharacterAgentTest.cpp` | 20 tests: personality-based decisions (flee/attack/trade/speak/idle), bravery counteracting fear, custom rules (override, priority, clear, zero-score), dialogue generation (extrovert/introvert/templates/no-profile), agent name |
| `tests/story/LLMCharacterAgentTest.cpp` | 28 tests: fallback chains (no callback, empty response, invalid JSON), valid JSON parsing, dialogue generation (JSON "dialogue"/"text" keys, raw text, empty fallback), prompt building (character info, conversation partner, default actions), parse response helpers, configuration (system prompt, max tokens, availability), callback verification |
| `tests/story/StoryDrivenBehaviorTest.cpp` | 17 tests (with TestEntity stub): behavior name, decision interval, agent access, decision timing (before/after interval), null safety, custom/default situation builders, knowledge summary in context, onInteract (Scripted/Guided), onEvent safety, allowed actions forwarding |

**Key design decisions implemented:**
- **Knowledge asymmetry enforced by design**: `CharacterDecisionContext` contains only what the character knows — never the full `WorldState`. This is the core architectural principle.
- **Three-tier agent fallback**: LLM response → JSON parse → fallback agent → idle. Agents degrade gracefully.
- **Rule-based scoring**: Each action scored by personality traits and emotional state. Custom rules (condition lambda → priority float) override built-in logic. Bravery custom trait demonstrates extensibility.
- **LLM prompt structure**: Markdown-formatted with sections (Character, Knowledge, Situation, Actions, Response Format). Expects JSON response for decisions, allows raw text for dialogue.
- **Decision throttling**: `StoryDrivenBehavior` uses configurable interval (default 1s) to avoid per-frame agent calls. `DecisionCallback` notifies game code of decisions.
- **Agency level routing**: onInteract uses `generateDialogue()` for AgencyLevel ≥ Guided, `decide()` for Scripted/Templated. Supports mixed-agency worlds.

---

### Phase S5: Developer API & Tooling — COMPLETE

**Completed: March 22, 2026 | Tests: 49 new (779 total, 77 suites)**

**Goal:** JSON world definition loader, HTTP API endpoints, MCP tools.

**Delivered:**

1. **StoryWorldLoader** (`engine/include/story/StoryWorldLoader.h`, `engine/src/story/StoryWorldLoader.cpp`)
   - `loadFromJson()` / `loadFromFile()` / `validate()` static methods
   - Parses full world definition: factions, faction relations (bidirectional), locations, world variables
   - Character parsing: Big Five + custom traits, goals, relationships, roles, allowed actions, starting knowledge auto-ID generation
   - Story arc parsing: constraint modes (case-insensitive), beats with prerequisites/conditions, director actions, tension curves, pacing params
   - Agency level accepts both integer (0-3) and string ("Scripted"/"Guided"/"Autonomous" etc.)

2. **HTTP API Endpoints** (`engine/include/core/EngineAPIServer.h`, `engine/src/core/EngineAPIServer.cpp`)
   - Read-only handlers: GET `/api/story/state`, `/api/story/characters`, `/api/story/character/:id`, `/api/story/arcs`, `/api/story/arc/:id`, `/api/story/world`
   - Mutation routes (queued): POST `/api/story/load`, `/api/story/character/add`, `/api/story/character/remove`, `/api/story/event`, `/api/story/arc/add`, `/api/story/variable`, `/api/story/agency`, `/api/story/knowledge`
   - Story handler typedefs + member variables in header; 14 new routes total

3. **MCP Tools** (`scripts/mcp/phyxel_mcp_server.py`)
   - 14 new story tools: `story_get_state`, `story_list_characters`, `story_get_character`, `story_list_arcs`, `story_get_arc`, `story_get_world`, `story_load_world`, `story_add_character`, `story_remove_character`, `story_trigger_event`, `story_add_arc`, `story_set_variable`, `story_set_agency`, `story_add_knowledge`
   - Full input schemas with descriptions, required fields

4. **Tests** (`tests/story/StoryWorldLoaderTest.cpp`)
   - 49 tests covering: basic loading, world parsing, character parsing, story arc parsing, validation, error handling, file loading, edge cases

---

### Application Integration — COMPLETE

**Completed: March 23, 2026**

Wired the StoryEngine into the game Application so all HTTP/MCP endpoints are functional at runtime.

**Modified files:**
| File | Change |
|------|--------|
| `editor/include/Application.h` | Added `#include "story/StoryEngine.h"`, `std::unique_ptr<Story::StoryEngine> storyEngine;` member |
| `editor/src/Application.cpp` | Added includes for `StoryWorldLoader.h` and `StoryDirectorTypes.h`; instantiates StoryEngine; wired 6 read-only handlers (state, character list/detail, arc list/detail, world); added 8 mutation command handlers in `processAPICommands()` (load_world, add/remove character, trigger event, add arc, set variable, set agency, add knowledge) |

**Manual testing verified all 14 endpoints via HTTP (engine running on localhost:8090):**
- Story state, character list/detail, arc list/detail, world state (GET)
- Load world definition, add/remove characters, trigger events, add arcs, set variables, set agency, add knowledge (POST)
- Dynamic dialogue integration test: NPC responses change based on world events (war declaration shifts guard dialogue from friendly to stern/angry)

---

## How to Resume

1. Open this file to see current status
2. The design doc at `docs/StoryEngineDesign.md` has the full architecture
3. The repo memory at `/memories/repo/game-mechanisms-roadmap.md` has a quick-reference summary
4. All tests pass: run `.\build\tests\Debug\phyxel_tests.exe` to verify (779 tests, 77 suites)
5. **All 5 story engine phases (S1-S5) are COMPLETE**
6. The engine source auto-discovers new files (`GLOB_RECURSE` in engine CMakeLists), but the test CMakeLists needs explicit directory globs (already has `"story/*.cpp"`)

## Commit History

- Game Mechanics Phases 1–4: complete (commit `4b526b4`)
- Story Engine Phases S1–S5 + Application wiring: complete (commit `aba0712`)
