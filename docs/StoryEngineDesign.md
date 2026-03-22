# Phyxel — Story Engine Design

*Created: March 21, 2026*
*Status: Design Phase*

## Design Philosophy

The story engine is a **single system with a tunable constraint spectrum**, not two separate scripted/emergent systems. A developer sets how much creative freedom the AI has per-character and per-story-arc. At maximum constraint, behavior is deterministic and hand-authored. At minimum constraint, everything emerges from character goals, personalities, and knowledge.

The core principle: **characters act on what they know, not what is true.**

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     STORY DIRECTOR (GM)                     │
│  Omniscient. Sees full WorldState. Manages pacing & arcs.   │
│  Constraint level determines how aggressively it steers.    │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐   │
│  │  Story Arc    │   │  Story Arc    │   │  Story Arc    │  │
│  │  "Dragon War" │   │  "Trade Route"│   │  (emergent)   │  │
│  │  Hard beats   │   │  Soft beats   │   │  No beats     │  │
│  └──────┬───────┘   └──────┬───────┘   └──────┬───────┘   │
│         └──────────────────┴──────────────────┘             │
│                            │                                 │
│                    Event Injection                            │
│                            │                                 │
├────────────────────────────┼────────────────────────────────┤
│                     EVENT BUS                                │
│  All world events flow through here.                         │
│  Witnesses are determined by spatial proximity.              │
│  Knowledge propagates through social connections.            │
├────────────────────────────┼────────────────────────────────┤
│                            │                                 │
│  ┌────────────────────────────────────────────────────────┐ │
│  │                    WORLD STATE                          │ │
│  │  Objective truth. Factions, locations, resources,       │ │
│  │  relationships, event history. Characters never         │ │
│  │  read this directly.                                    │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐      │
│  │Character│  │Character│  │Character│  │Character│       │
│  │ Guard   │  │Merchant │  │ Thief   │  │ Dragon  │       │
│  │         │  │         │  │         │  │         │       │
│  │ Mind:   │  │ Mind:   │  │ Mind:   │  │ Mind:   │       │
│  │ Memory  │  │ Memory  │  │ Memory  │  │ Memory  │       │
│  │ Goals   │  │ Goals   │  │ Goals   │  │ Goals   │       │
│  │ Traits  │  │ Traits  │  │ Traits  │  │ Traits  │       │
│  │ Bonds   │  │ Bonds   │  │ Bonds   │  │ Bonds   │       │
│  │ Emotion │  │ Emotion │  │ Emotion │  │ Emotion │       │
│  │         │  │         │  │         │  │         │       │
│  │Agency: 2│  │Agency: 1│  │Agency: 3│  │Agency: 0│       │
│  └─────────┘  └─────────┘  └─────────┘  └─────────┘       │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## The Constraint Spectrum

### Character Agency Levels

Each character has an `agencyLevel` that determines how much AI drives their behavior:

| Level | Name | Behavior | Dialogue | Use Case |
|-------|------|----------|----------|----------|
| **0** | Scripted | Fixed `NPCBehavior` (Idle/Patrol) | `DialogueTree` only | Background NPCs, shopkeepers |
| **1** | Templated | Fixed behavior, AI-flavored dialogue | AI fills dialogue templates using personality + knowledge | Named NPCs with personality |
| **2** | Guided | AI chooses from allowed actions | AI generates contextual dialogue | Important NPCs that respond to events |
| **3** | Autonomous | AI decides all actions from goals | Fully dynamic dialogue | Key characters, companion NPCs |

Agency level can change at runtime. A background guard (Level 0) who witnesses a murder might get promoted to Level 2 to handle the aftermath dynamically.

### Story Arc Constraint Modes

Each story arc has a `constraintMode` controlling how strongly the Story Director steers:

| Mode | Beat Types | Director Behavior | Feel |
|------|------------|-------------------|------|
| **Scripted** | All hard beats, fixed order | Forces events at triggers, overrides character agency | Linear RPG |
| **Guided** | Mix of hard + soft beats | Nudges toward beats, creates opportunities, waits for characters to take them | Open-world RPG |
| **Emergent** | Soft + optional beats only | Observes, injects catalysts for drama, never forces | Simulation/sandbox |
| **Freeform** | No beats | Director only manages pacing (prevent boredom/overwhelm) | Pure emergence |

A single game can mix these. The "main quest" might be Guided while the world around it is Emergent.

---

## Data Model

### WorldState

The objective truth about the world. Only the Story Director reads this directly.

```cpp
struct WorldState {
    // Factions and their standings
    std::unordered_map<std::string, Faction> factions;
    
    // Named locations with properties
    std::unordered_map<std::string, Location> locations;
    
    // Global variables (quest flags, world flags, etc.)
    std::unordered_map<std::string, WorldVariable> variables;
    
    // Complete event history (append-only)
    std::vector<WorldEvent> eventHistory;
    
    // Current world time (game time, not real time)
    float worldTime = 0.0f;
    
    // Global tension/drama level (0.0 = peaceful, 1.0 = crisis)
    float dramaTension = 0.0f;
};

struct Faction {
    std::string id;
    std::string name;
    std::unordered_map<std::string, float> relations; // factionId → standing (-1 to 1)
    std::vector<std::string> memberCharacterIds;
    std::vector<std::string> controlledLocationIds;
};

struct Location {
    std::string id;
    std::string name;
    glm::vec3 worldPosition;
    float radius;                    // Area of influence
    std::string controllingFaction;  // Faction that controls this area
    std::vector<std::string> tags;   // "tavern", "market", "dungeon", etc.
};

struct WorldVariable {
    std::string key;
    std::variant<bool, int, float, std::string> value;
};
```

### WorldEvent

Immutable record of something that happened. The atomic unit of the knowledge system.

```cpp
struct WorldEvent {
    std::string id;              // Unique event ID
    std::string type;            // "combat", "dialogue", "trade", "death", "discovery", etc.
    float timestamp;             // World time when it occurred
    glm::vec3 location;          // Where it happened
    float audibleRadius;         // How far it can be heard (for witness detection)
    float visibleRadius;         // How far it can be seen
    
    std::vector<std::string> participants;    // Character IDs directly involved
    std::vector<std::string> affectedFactions;
    
    // Structured data about what happened
    nlohmann::json details;
    
    // Importance affects how far/fast it spreads as rumor (0.0-1.0)
    float importance = 0.5f;
};
```

### CharacterProfile

The character's personality, goals, and relationships. This is **authored by the game developer** (though AI can help generate it).

```cpp
struct PersonalityTraits {
    // Big Five personality model (each 0.0 to 1.0)
    float openness = 0.5f;        // Curiosity, creativity vs. caution
    float conscientiousness = 0.5f; // Organization, discipline vs. impulsiveness
    float extraversion = 0.5f;    // Sociability, energy vs. reservation
    float agreeableness = 0.5f;   // Cooperation, trust vs. suspicion
    float neuroticism = 0.5f;     // Emotional volatility vs. stability
    
    // Game-specific traits (developer-defined)
    std::unordered_map<std::string, float> customTraits;
    // e.g. "bravery": 0.8, "greed": 0.3, "loyalty": 0.9
};

struct CharacterGoal {
    std::string id;
    std::string description;     // Human-readable for AI context
    float priority;              // 0.0-1.0, can change dynamically
    bool isActive = true;
    
    // Optional: conditions that complete/fail this goal
    std::string completionCondition;  // Expression evaluated against WorldState
    std::string failureCondition;
};

struct Relationship {
    std::string targetCharacterId;
    float trust = 0.0f;         // -1.0 (betrayal) to 1.0 (unconditional)
    float affection = 0.0f;     // -1.0 (hatred) to 1.0 (love)
    float respect = 0.0f;       // -1.0 (contempt) to 1.0 (reverence)
    float fear = 0.0f;          // 0.0 (none) to 1.0 (terror)
    std::string label;           // "friend", "rival", "mentor", "enemy", etc.
};

struct EmotionalState {
    float joy = 0.0f;           // -1 (grief) to 1 (elation)
    float anger = 0.0f;         // 0 to 1
    float fear = 0.0f;          // 0 to 1
    float surprise = 0.0f;      // 0 to 1
    float disgust = 0.0f;       // 0 to 1
    
    // Emotional decay: emotions return to baseline over time
    // Rate depends on personality (high neuroticism = slow decay)
    void decay(float dt, const PersonalityTraits& personality);
};

enum class AgencyLevel {
    Scripted = 0,    // Fixed behavior + dialogue trees
    Templated = 1,   // Fixed behavior, AI-flavored dialogue
    Guided = 2,      // AI chooses from allowed actions
    Autonomous = 3   // AI drives everything
};

struct CharacterProfile {
    std::string id;
    std::string name;
    std::string description;     // Background text for AI context
    std::string factionId;
    
    PersonalityTraits traits;
    std::vector<CharacterGoal> goals;
    std::vector<Relationship> relationships;
    EmotionalState emotion;
    
    AgencyLevel agencyLevel = AgencyLevel::Scripted;
    
    // For Level 0-1: fallback behavior and dialogue
    std::string defaultBehavior;        // "idle", "patrol", etc.
    std::string defaultDialogueFile;    // "guard_intro.json", etc.
    
    // For Level 2-3: allowed action set (empty = unrestricted)
    std::vector<std::string> allowedActions;  // "speak", "trade", "fight", "flee", etc.
    
    // Tags for the Story Director to query
    std::vector<std::string> roles;    // "questgiver", "villain", "merchant", "companion"
};
```

### CharacterMemory (The Knowledge Store)

What a character **believes** about the world. This is the key to asymmetric information.

```cpp
enum class KnowledgeSource {
    Witnessed,    // Saw/heard it directly (high confidence)
    ToldBy,       // Another character told them (medium confidence)
    Rumor,        // Heard through the grapevine (low confidence)
    Innate        // Part of their background/starting knowledge
};

struct KnowledgeFact {
    std::string factId;          // References a WorldEvent ID or a static fact
    std::string summary;         // Natural language summary for AI context
    KnowledgeSource source;
    std::string sourceCharacterId;  // Who told them (if ToldBy/Rumor)
    float confidence;            // 0.0-1.0 (decays over time for rumors)
    float timestamp;             // When they learned it
    
    // Personality can distort facts. This is what the character *remembers*,
    // which may differ from what actually happened.
    nlohmann::json perceivedDetails;
};

class CharacterMemory {
public:
    // Add knowledge from direct observation
    void witness(const WorldEvent& event, const PersonalityTraits& personality);
    
    // Add knowledge from being told by another character
    void hearFrom(const std::string& tellerId, const KnowledgeFact& fact,
                  const PersonalityTraits& listenerPersonality);
    
    // Add starting/background knowledge
    void addInnateKnowledge(const std::string& factId, const std::string& summary,
                            const nlohmann::json& details);
    
    // Query
    bool knowsAbout(const std::string& factId) const;
    const KnowledgeFact* getFact(const std::string& factId) const;
    std::vector<const KnowledgeFact*> getFactsAbout(const std::string& topic) const;
    std::vector<const KnowledgeFact*> getRecentFacts(int count) const;
    
    // Get all knowledge as context string for AI agents
    std::string buildContextSummary(int maxFacts = 20) const;
    
    // Decay confidence over time (rumors fade, details blur)
    void update(float dt);
    
private:
    std::unordered_map<std::string, KnowledgeFact> m_facts;
    
    // Personality-based distortion when witnessing events
    // High neuroticism: exaggerate danger. High agreeableness: soften conflict.
    nlohmann::json distortPerception(const nlohmann::json& realDetails,
                                      const PersonalityTraits& personality);
};
```

### Knowledge Propagation

How information spreads from character to character:

```cpp
class KnowledgePropagator {
public:
    // Called when a WorldEvent occurs. Determines who witnesses it.
    void onWorldEvent(const WorldEvent& event, const EntityRegistry& registry);
    
    // Called during NPC-NPC dialogue. Characters share knowledge based on:
    // - Trust level between them
    // - Extraversion of speaker (chatty people share more)
    // - Importance of the fact
    // - Personality-based filtering (what they choose to share or withhold)
    void propagateDuringDialogue(CharacterMemory& speaker, CharacterMemory& listener,
                                  const CharacterProfile& speakerProfile,
                                  const CharacterProfile& listenerProfile);
    
    // Rumor spread: background knowledge propagation for characters in the same location
    void spreadRumors(float dt, const std::vector<CharacterProfile*>& nearbyCharacters);
    
private:
    // Determine NPC witnesses within audible/visible radius
    std::vector<std::string> findWitnesses(const WorldEvent& event,
                                            const EntityRegistry& registry);
};
```

---

## Story Director

The GM agent. Manages narrative pacing and arc progression.

### Story Arc Definition

```cpp
enum class BeatType {
    Hard,       // Must happen. Director forces it if needed.
    Soft,       // Should happen. Director creates opportunities.
    Optional    // Nice to have. Happens if conditions align naturally.
};

enum class ArcConstraintMode {
    Scripted,   // All hard beats, fixed order
    Guided,     // Mix of hard + soft, flexible order
    Emergent,   // Soft + optional only
    Freeform    // No beats, director manages pacing only
};

struct StoryBeat {
    std::string id;
    std::string description;          // What should happen
    BeatType type;
    
    // Trigger conditions (when this beat CAN fire)
    std::string triggerCondition;     // Expression against WorldState
    
    // What the director does to make this beat happen
    std::vector<std::string> directorActions;  // "inject_event", "promote_npc_agency", etc.
    
    // Characters involved (director ensures they're in position)
    std::vector<std::string> requiredCharacters;
    
    // Ordering constraints
    std::vector<std::string> prerequisites;    // Beat IDs that must complete first
    
    // Completion: how do we know this beat happened?
    std::string completionCondition;  // Expression against WorldState
    
    bool completed = false;
};

struct StoryArc {
    std::string id;
    std::string name;
    std::string description;
    ArcConstraintMode constraintMode;
    
    std::vector<StoryBeat> beats;
    
    // Pacing parameters
    float minTimeBetweenBeats = 60.0f;    // Game-time seconds
    float maxTimeWithoutProgress = 300.0f; // Director intervenes if stalled
    
    // Dramatic curve target
    // Director tries to match this tension profile over the arc
    // e.g. {0.2, 0.3, 0.5, 0.7, 0.4, 0.9, 0.3} for classic dramatic arc
    std::vector<float> tensionCurve;
    
    bool isActive = false;
    bool isCompleted = false;
    float progress = 0.0f;  // 0.0 to 1.0 based on completed beats
};
```

### Story Director Agent

```cpp
class StoryDirector {
public:
    // Register story arcs (authored by game developer)
    void addArc(StoryArc arc);
    void activateArc(const std::string& arcId);
    
    // Main update — called each frame or at intervals
    // The director evaluates world state and decides what to do
    void update(float dt, WorldState& worldState);
    
    // The director can:
    void injectEvent(WorldEvent event);              // Make something happen
    void promoteCharacterAgency(const std::string& charId, AgencyLevel level);
    void suggestGoalToCharacter(const std::string& charId, CharacterGoal goal);
    void spawnCharacter(const CharacterProfile& profile, glm::vec3 position);
    void modifyFactionRelation(const std::string& a, const std::string& b, float delta);
    void setWorldVariable(const std::string& key, WorldVariable value);
    
    // AI integration — for Guided/Emergent/Freeform modes
    // The AI agent decides which director actions to take
    void setAIBackend(AIBackend* backend);
    
    // Query
    float getCurrentTension() const;
    const std::vector<StoryArc>& getArcs() const;
    
private:
    // For Scripted mode: deterministic beat evaluation
    void evaluateScriptedBeats(WorldState& worldState);
    
    // For Guided mode: check if opportunities exist, create them if not
    void evaluateGuidedBeats(WorldState& worldState);
    
    // For Emergent/Freeform: monitor drama tension, inject catalysts
    void managePacing(WorldState& worldState);
    
    // AI decision: "given current world state + arc progress, what should happen?"
    std::vector<DirectorAction> consultAI(const WorldState& worldState);
    
    std::vector<StoryArc> m_arcs;
    WorldState* m_worldState = nullptr;
    AIBackend* m_aiBackend = nullptr;
    float m_timeSinceLastBeat = 0.0f;
};
```

---

## Character AI Agent Interface

Each character agent receives ONLY their own knowledge, personality, and goals — never the WorldState.

```cpp
struct CharacterDecisionContext {
    // Who am I?
    const CharacterProfile* profile;
    
    // What do I know?
    std::string knowledgeSummary;   // Built from CharacterMemory
    
    // What's happening right now?
    std::string currentSituation;   // Nearby entities, current location, recent events
    
    // What are my options?
    std::vector<std::string> availableActions;
    
    // Who am I talking to (if in dialogue)?
    const CharacterProfile* conversationPartner;  // Only the public-facing info
    std::string conversationHistory;               // What's been said so far
};

struct CharacterDecision {
    std::string action;          // "speak", "move_to", "attack", "trade", "flee", "wait"
    nlohmann::json parameters;   // Action-specific params
    std::string reasoning;       // For debugging/logging
    
    // If action is "speak":
    std::string dialogueText;
    std::string emotion;
};

class CharacterAgent {
public:
    virtual ~CharacterAgent() = default;
    
    // Given the character's subjective view, decide what to do
    virtual CharacterDecision decide(const CharacterDecisionContext& context) = 0;
    
    // Generate dialogue for a conversation
    virtual std::string generateDialogue(const CharacterDecisionContext& context) = 0;
};

// Rule-based fallback (no AI needed, works offline)
class RuleBasedCharacterAgent : public CharacterAgent { /* ... */ };

// LLM-backed agent (requires AI backend)
class LLMCharacterAgent : public CharacterAgent { /* ... */ };
```

---

## Developer-Facing API

What game developers actually work with when building their games:

```cpp
class StoryEngine {
public:
    // ============================================================
    // WORLD SETUP (called during game initialization)
    // ============================================================
    
    // Define the world's factions, locations, and starting state
    void defineWorld(WorldState initialState);
    
    // Add a character to the world
    void addCharacter(CharacterProfile profile);
    
    // Give a character starting knowledge (before game begins)
    void addStartingKnowledge(const std::string& characterId,
                              const std::string& factSummary,
                              const nlohmann::json& details);
    
    // Define a story arc
    void addStoryArc(StoryArc arc);
    
    // ============================================================
    // RUNTIME
    // ============================================================
    
    // Main update (called each frame)
    void update(float dt);
    
    // Trigger a world event (explosion, arrival, discovery, etc.)
    void triggerEvent(WorldEvent event);
    
    // Query what a character knows
    const CharacterMemory* getCharacterMemory(const std::string& charId) const;
    
    // Query character's current emotional state
    EmotionalState getCharacterEmotion(const std::string& charId) const;
    
    // Modify a character's goal priority at runtime
    void setGoalPriority(const std::string& charId, const std::string& goalId, float priority);
    
    // ============================================================
    // AI CONFIGURATION
    // ============================================================
    
    // Set the AI backend (Goose, OpenAI, local LLM, etc.)
    void setAIBackend(std::unique_ptr<AIBackend> backend);
    
    // Set global default agency level
    void setDefaultAgencyLevel(AgencyLevel level);
    
    // ============================================================
    // SERIALIZATION
    // ============================================================
    
    // Save/load entire story state (for save games)
    nlohmann::json saveState() const;
    void loadState(const nlohmann::json& state);
};
```

---

## Integration with Existing Phyxel Systems

| Phyxel System | Integration Point |
|---|---|
| `NPCBehavior` | New `StoryDrivenBehavior` that queries CharacterAgent for decisions |
| `NPCContext` | Extended with `CharacterProfile*`, `CharacterMemory*`, `StoryEngine*` |
| `NPCManager` | `spawnNPC` can accept a `CharacterProfile` and auto-wire everything |
| `DialogueSystem` | Characters at Agency Level 2+ generate dialogue dynamically instead of using trees |
| `SpeechBubbleManager` | Characters react to witnessed events with ambient speech |
| `GameEventLog` | Feeds into `WorldState::eventHistory` and triggers knowledge propagation |
| `InteractionManager` | E-key starts AI-driven conversation at Level 2+, scripted at Level 0-1 |
| `EntityRegistry` | Spatial queries for witness detection and knowledge propagation |
| `LightManager` | Story Director can manipulate lighting for dramatic effect |
| `CameraManager` | Story Director can trigger cinematic camera moves for key beats |

---

## Implementation Phases

### Phase S1: World State & Character Profiles
- `WorldState`, `Faction`, `Location`, `WorldVariable`
- `CharacterProfile`, `PersonalityTraits`, `CharacterGoal`, `Relationship`, `EmotionalState`
- JSON serialization for all of the above
- Unit tests for data model

### Phase S2: Character Memory & Knowledge
- `CharacterMemory`, `KnowledgeFact`, `KnowledgeSource`
- `witness()`, `hearFrom()`, knowledge queries
- Personality-based perception distortion
- Confidence decay over time
- Context summary builder for AI
- Unit tests

### Phase S3: Event System & Knowledge Propagation
- `WorldEvent` struct + `EventBus` for broadcasting
- `KnowledgePropagator` — witness detection (spatial), NPC-NPC sharing, rumor spread
- Integration with `GameEventLog`
- Wire into NPC update loop
- Unit tests

### Phase S4: Story Director Core
- `StoryArc`, `StoryBeat`, constraint modes
- `StoryDirector` — beat evaluation (scripted mode first)
- Event injection, character agency promotion
- Pacing management (tension tracking)
- Unit tests

### Phase S5: Character AI Agents
- `CharacterAgent` interface, `CharacterDecisionContext`
- `RuleBasedCharacterAgent` (offline fallback)
- `LLMCharacterAgent` (Goose/OpenAI integration)
- `StoryDrivenBehavior` — new NPCBehavior that delegates to CharacterAgent
- Dynamic dialogue generation
- Unit tests

### Phase S6: Developer API & Tooling
- `StoryEngine` facade class
- JSON/YAML schema for defining worlds, characters, arcs
- API endpoints for runtime story queries/manipulation
- MCP tools for AI agent interaction with the story system
- Save/load story state
- Documentation + examples

---

## Example: Developer Defines a Game World

```json
{
    "world": {
        "factions": [
            { "id": "kingdom", "name": "The Kingdom of Aldren" },
            { "id": "bandits", "name": "Shadow Fang Brotherhood" },
            { "id": "merchants", "name": "Free Traders Guild" }
        ],
        "factionRelations": {
            "kingdom-bandits": -0.8,
            "kingdom-merchants": 0.5,
            "bandits-merchants": -0.6
        }
    },
    "characters": [
        {
            "id": "captain_elena",
            "name": "Captain Elena",
            "faction": "kingdom",
            "agencyLevel": 2,
            "traits": {
                "openness": 0.3, "conscientiousness": 0.9,
                "extraversion": 0.6, "agreeableness": 0.4, "neuroticism": 0.2,
                "bravery": 0.9, "loyalty": 0.95
            },
            "goals": [
                { "id": "protect_town", "description": "Keep the town safe from bandits", "priority": 0.9 },
                { "id": "find_spy", "description": "Root out the bandit spy in the guard", "priority": 0.7 }
            ],
            "relationships": [
                { "target": "merchant_sofia", "trust": 0.6, "affection": 0.3, "respect": 0.5 },
                { "target": "bandit_crow", "trust": -0.9, "affection": -0.5, "respect": 0.2, "fear": 0.1 }
            ],
            "roles": ["questgiver", "authority"],
            "startingKnowledge": [
                "Three merchant shipments have been raided in the past month",
                "The bandits have a hideout somewhere in the northern caves",
                "Someone inside the guard may be leaking patrol routes"
            ]
        }
    ],
    "storyArcs": [
        {
            "id": "bandit_threat",
            "name": "The Shadow Fang Threat",
            "constraintMode": "guided",
            "beats": [
                {
                    "id": "discover_raids", "type": "hard",
                    "description": "Player learns about the bandit raids",
                    "triggerCondition": "player.near(captain_elena) AND !player.knows(bandit_raids)"
                },
                {
                    "id": "find_evidence", "type": "soft",
                    "description": "Player finds evidence of the spy",
                    "prerequisites": ["discover_raids"]
                },
                {
                    "id": "confront_spy", "type": "soft",
                    "description": "The spy is confronted — could go many ways",
                    "prerequisites": ["find_evidence"]
                },
                {
                    "id": "raid_hideout", "type": "hard",
                    "description": "Assault on the bandit cave",
                    "prerequisites": ["discover_raids"]
                }
            ],
            "tensionCurve": [0.3, 0.4, 0.5, 0.7, 0.6, 0.9, 0.3]
        }
    ]
}
```

In this example, with `constraintMode: "guided"`:
- "discover_raids" is a **hard** beat — the director will ensure the player meets Captain Elena
- "find_evidence" and "confront_spy" are **soft** — the director sets up clues but doesn't force discovery
- The spy's identity, the confrontation outcome, and all NPC reactions emerge from character personalities and knowledge
- If Captain Elena is Agency Level 2, she'll dynamically react to player actions, share what she knows (filtered by her personality — she's not very open, so she'll be guarded with info), and pursue her goal of finding the spy in her own way
