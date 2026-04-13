# Phyxel D&D RPG System

Complete D&D 5e ruleset implemented as first-class engine features.
All subsystems live in `engine/include/core/` and `engine/src/core/` under the
`Phyxel::Core` namespace. They are data-only / logic-only — no rendering
dependencies — so they compile into `phyxel_core` and are fully unit-testable.

---

## Architecture Overview

```
Phase 0 — Foundation          Phase 1 — Identity        Phase 2 — Combat
──────────────────────        ──────────────────         ────────────────
DiceSystem                    CharacterSheet             ActionEconomy
CharacterAttributes           CharacterProgression       InitiativeTracker
ProficiencySystem             resources/rpg/*.json       AttackResolver
                                                         ConditionSystem

Phase 3 — Magic               Phase 4 — Items            Phase 5 — Social
───────────────               ───────────────            ────────────────
SpellDefinition               RpgItem                    ReputationSystem
SpellcasterComponent          CurrencySystem             DialogueSkillCheck
SpellResolver                 AttunementSystem           SocialInteractionResolver
resources/spells/*.json       EncumbranceSystem          resources/factions/*.json

Phase 6 — Rest & Time         Phase 7 — Campaign GM      Phase 8 — MCP Layer
─────────────────────         ─────────────────────      ────────────────────
RestSystem                    Party                      /api/rpg/* HTTP routes
WorldClock                    LootTable                  14 MCP tools
                              EncounterBuilder           roll_dice, check_dc
                              CampaignJournal            (+ engine-connected tools)
```

**All phases pass their test suites (total 357 D&D-specific unit tests).**
Tests live in `tests/core/` following the `<ClassName>Test.cpp` naming convention.

---

## Phase 0 — Foundation

### DiceSystem (`DiceSystem.h`)

Static class with internal seeded `mt19937` RNG.

```cpp
// Die types
enum class DieType { D4, D6, D8, D10, D12, D20, D100 };

// Single die
RollResult DiceSystem::roll(DieType die);
RollResult DiceSystem::rollAdvantage(DieType die);   // roll twice, take higher
RollResult DiceSystem::rollDisadvantage(DieType die);// roll twice, take lower
RollResult DiceSystem::rollCritical(DieType die);    // double the dice

// Expression parsing
RollResult DiceSystem::rollExpression(const std::string& expr); // "2d6+3"

// DC check
bool DiceSystem::checkDC(int roll, int dc);

// Utility
float  DiceSystem::rollFloat();         // [0.0, 1.0) — used by LootTable
double DiceSystem::averageValue(const std::string& expr);
void   DiceSystem::setSeed(uint32_t seed); // 0 = restore randomness
```

`RollResult` carries: `dice[]`, `modifier`, `total`, `isCriticalSuccess`,
`isCriticalFailure`, `hadAdvantage`, `hadDisadvantage`, `droppedRoll`, `describe()`.

### CharacterAttributes (`CharacterAttributes.h`)

```cpp
enum class AbilityType { Strength, Dexterity, Constitution,
                         Intelligence, Wisdom, Charisma };

struct AbilityScore {
    int base = 10, racial = 0, equipment = 0, temporary = 0;
    int total() const;    // sum of all layers
    int modifier() const; // (total - 10) / 2
};

struct CharacterAttributes {
    AbilityScore scores[6];
    int  modifier(AbilityType) const;
    int  score(AbilityType) const;
    int  initiativeBonus() const;   // DEX modifier
    int  unarmoredAC() const;       // 10 + DEX mod
    float carryCapacity() const;    // STR × 15 lbs
    float pushDragLift() const;     // carryCapacity × 2
    void setAll(int str, int dex, int con, int intel, int wis, int cha);
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json&);
};
```

### ProficiencySystem (`ProficiencySystem.h`)

```cpp
enum class Skill { Acrobatics, AnimalHandling, Arcana, Athletics,
                   Deception, History, Insight, Intimidation, Investigation,
                   Medicine, Nature, Perception, Performance, Persuasion,
                   Religion, SlightOfHand, Stealth, Survival };

enum class ProficiencyLevel { None, HalfProf, Proficient, Expert };

// All static helpers
int  ProficiencySystem::proficiencyBonus(int characterLevel);  // 2 at L1, 6 at L17+
int  ProficiencySystem::skillBonus(Skill, ProficiencyLevel, int abilityMod, int charLevel);
int  ProficiencySystem::passiveCheck(int skillBonus);          // 10 + bonus
int  ProficiencySystem::savingThrowBonus(AbilityType, ProficiencyLevel, int abilityMod, int charLevel);
```

---

## Phase 1 — Identity

### CharacterSheet (`CharacterSheet.h`)

Top-level character container. Wraps `CharacterAttributes` + `ProficiencySystem`
state + identity fields.

```cpp
struct CharacterSheet {
    std::string entityId, name, classId, raceId, backgroundId;
    int level = 1, experiencePoints = 0;
    int maxHitPoints = 0, currentHitPoints = 0;
    int hitDiceRemaining = 0;
    CharacterAttributes attributes;
    std::unordered_map<Skill, ProficiencyLevel> skillProficiencies;
    std::vector<std::string> savingThrowProficiencies; // e.g. "Strength"
    std::vector<std::string> features;  // class/race feature IDs
    int getSkillBonus(Skill) const;
    int getSavingThrowBonus(AbilityType) const;
    int passivePerception() const;
    int armorClass = 10;
    int speed = 30;
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json&);
};
```

### CharacterProgression (`CharacterProgression.h`)

XP tables, level-up logic, ASI scheduling, multiclass validation.

```cpp
class CharacterProgression {
    static int xpForLevel(int level);          // D&D 5e XP table
    static int levelForXp(int xp);
    static bool isASILevel(const std::string& classId, int level);

    // Mutates a CharacterSheet in place
    static bool applyXp(CharacterSheet&, int xpGained, DiceSystem&);
    static void applyLevelUp(CharacterSheet&, const ClassDefinition&, DiceSystem&);
};
```

Data files in `resources/rpg/classes/*.json`, `resources/rpg/races/*.json`,
`resources/rpg/backgrounds/*.json`.

---

## Phase 2 — Combat

### ActionEconomy (`ActionEconomy.h`)

Tracks available actions per entity per turn.

```cpp
struct ActionEconomy {
    bool hasAction = true, hasBonusAction = true, hasReaction = true;
    int movementRemaining = 30; // feet
    void reset(int speedFeet = 30);
    bool useAction();       // returns false if already used
    bool useBonusAction();
    bool useReaction();
    void useMovement(int feet);
};
```

### InitiativeTracker (`InitiativeTracker.h`)

```cpp
class InitiativeTracker {
    void startCombat();
    void rollInitiative(const std::string& entityId, int dexBonus);
    void setInitiative(const std::string& entityId, int value);
    void setSurprised(const std::string& entityId, bool surprised);
    void sortOrder();

    std::string endTurn();           // returns next entityId
    void nextRound();
    void removeParticipant(const std::string& entityId);
    void endCombat();

    bool        isCombatActive() const;
    int         currentRound() const;
    std::string currentEntityId() const;
    bool        canReact(const std::string& entityId) const;
    bool        useReaction(const std::string& entityId);

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json&);
};
```

### AttackResolver + ConditionSystem

```cpp
// AttackResolver
AttackResult AttackResolver::resolve(
    const AttackParams& attacker, const DefenseParams& defender, DiceSystem&);
// Returns: hit, critical, damageRolls[], totalDamage, missedBy/hitBy

// ConditionSystem — 15 standard conditions
enum class Condition {
    Blinded, Charmed, Deafened, Exhaustion, Frightened, Grappled,
    Incapacitated, Invisible, Paralyzed, Petrified, Poisoned,
    Prone, Restrained, Stunned, Unconscious
};
class ConditionSystem {
    void applyCondition(entityId, Condition, float durationSeconds = -1);
    void removeCondition(entityId, Condition);
    bool hasCondition(entityId, Condition) const;
    void update(float dt); // ticks duration, removes expired
};
```

---

## Phase 3 — Magic

Spells are data-driven JSON in `resources/spells/`. The engine ships with a
starter library covering all schools.

```cpp
// SpellcasterComponent — attached to a CharacterSheet
struct SpellcasterComponent {
    std::string castingType;      // "full", "half", "pact"
    int         spellcastingLevel;
    SpellSlots  slots();          // accessor (not getSlots())
    void onShortRest();           // pact magic restores
    void onLongRest();            // full restore

    bool canCast(const SpellDefinition&, int atSlotLevel) const;
    CastResult cast(const SpellDefinition&, int atSlotLevel, DiceSystem&);
};
```

`SpellResolver::resolve()` handles area targeting, saves, concentration checks,
and upcast scaling.

---

## Phase 4 — Items

```cpp
enum class ItemRarity { Common, Uncommon, Rare, VeryRare, Legendary, Artifact };

struct RpgItem {
    std::string id, name, description;
    ItemRarity  rarity;
    bool        requiresAttunement;
    // weapon/armor stats, weight, value in GP
};

// CurrencySystem: GP / SP / CP with conversion helpers
// AttunementSystem: 3-slot limit per RAW, item effects activate on attune
// EncumbranceSystem: compares carried weight to CharacterAttributes::carryCapacity()
```

Item definitions live in `resources/rpg_items/*.json`.

---

## Phase 5 — Social

### ReputationSystem (`ReputationSystem.h`)

```cpp
enum class ReputationTier {
    Hostile,     // < -500
    Unfriendly,  // -500 to -1
    Neutral,     // 0 to 249
    Friendly,    // 250 to 499
    Honored,     // 500 to 749
    Exalted      // >= 750
};

class ReputationSystem {
    // Per-entity, per-faction score in [-1000, +1000]
    int  getReputation(entityId, factionId) const;  // falls back to FactionRegistry default
    void setReputation(entityId, factionId, int score);
    void adjustReputation(entityId, factionId, int delta);
    ReputationTier getTier(entityId, factionId) const;
    bool isHostile(entityId, factionId) const;
    bool isFriendly(entityId, factionId) const;
};

class FactionRegistry { // singleton
    static FactionRegistry& instance();
    void loadFromFile(const std::string& path);
    const FactionDefinition* getFaction(const std::string& factionId) const;
};
```

Faction JSON lives in `resources/factions/`. `common_factions.json` ships with
7 factions including mutual enemy/ally relationships.

### DialogueSkillCheck (`DialogueSkillCheck.h`)

Wires skill checks into `DialogueChoice::condition` functions.

```cpp
enum class DialogueCheckType { SkillCheck, AbilityCheck, ReputationGate };

struct DialogueSkillCheck {
    DialogueCheckType type;
    Skill skill;           // for SkillCheck
    AbilityType ability;   // for AbilityCheck
    int dc;
    std::string factionId; // for ReputationGate
    ReputationTier minimumTier;

    RollResult resolve(int bonus, DiceSystem&) const;
    bool resolveReputation(int reputationScore) const;
    std::string label() const; // "[Persuasion DC 15]" / "[Friendly: town_guard]"
};
```

### SocialInteractionResolver (`SocialInteractionResolver.h`)

```cpp
enum class SocialSkill { Persuasion, Deception, Intimidation, Insight, Performance };

struct SocialResult {
    bool success; int roll, total, dc; SocialSkill skill;
    std::string description;
};

class SocialInteractionResolver {
    static SocialResult resolve(SocialSkill, int skillBonus,
                                ReputationTier targetTier, DiceSystem&,
                                bool advantage=false, bool disadvantage=false);
    static InsightResult resolveInsight(int playerBonus, int npcBonus, DiceSystem&);
    static int persuasionDC(ReputationTier);    // Exalted=5 … Hostile=25
    static int intimidationDC(ReputationTier);
    static int reputationDelta(SocialSkill, bool succeeded); // +25 persuasion success etc.
};
```

---

## Phase 6 — Rest & Time

### RestSystem (`RestSystem.h`)

```cpp
class RestSystem {
    void registerCharacter(entityId, maxHp, hitDiceCount, DieType, charLevel,
                           castingType = "");

    ShortRestResult shortRest(entityId, hitDiceToSpend, constitutionMod,
                              DiceSystem&, SpellcasterComponent* = nullptr);
    LongRestResult  longRest(entityId, SpellcasterComponent* = nullptr);
    // Long rest: full HP, restore max(halfMax,1) hit dice, full spell slots
};
```

### WorldClock (`WorldClock.h`)

Fantasy calendar, independent of the real-time `DayNightCycle`.

```cpp
// Constants
constexpr int DAYS_PER_MONTH  = 30;   // 12 months × 30 days = 360-day year
constexpr int LUNAR_CYCLE_DAYS = 28;
constexpr int DAYS_PER_WEEK   = 7;

enum class Season    { Spring, Summer, Autumn, Winter };
enum class MoonPhase { NewMoon, WaxingCrescent, FirstQuarter, WaxingGibbous,
                       FullMoon, WaningGibbous, LastQuarter, WaningCrescent };

struct CalendarDate { int day, month, year;
    std::string toLongString() const;  // "15th Greenleaf, Year 3"
    std::string toShortString() const; // "15/4/3"
};

class WorldClock {
    void setTotalDays(int);
    void advanceDays(int);
    int  getTotalDays() const;
    CalendarDate getDate() const;
    int  getDayOfMonth() const, getMonth() const, getYear() const;
    int  getDayOfWeek() const;   // 0=Moonday … 6=Starday
    Season    getSeason() const;
    MoonPhase getMoonPhase() const;
    bool isHoliday() const;
    std::string getHolidayName() const;

    static const char* seasonName(Season);
    static const char* moonPhaseName(MoonPhase);
    static const char* dayOfWeekName(int);   // "Moonday" … "Starday"
    static const char* monthName(int);       // "Deepwinter" … "Midwinter"
};
```

**Default holidays**: Midwinter Festival (12/1), Spring Equinox (3/20),
Midsummer Feast (6/21), Harvest Day (9/22), Day of the Dead (10/31),
New Year's Dawn (1/1).

**Month names**: Deepwinter, Firstthaw, Budding, Greenleaf, Bloomtide, Highsun,
Fireharvest, Goldfall, Harvestend, Frostmere, Snowmantle, Midwinter.

---

## Phase 7 — Campaign GM

### Party (`Party.h`)

```cpp
class Party {
    void addMember(entityId, name, characterLevel); // no-op on duplicate
    void removeMember(entityId);        // auto-promotes leader if needed
    void setLeader(entityId);
    void setAlive(entityId, bool);

    const std::string& getLeaderId() const;
    int size() const, aliveCount() const;
    int totalLevel() const, averageLevel() const;  // floor division, 0 if empty
    bool isWiped() const;  // aliveCount() == 0

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json&);
};
```

First `addMember` call automatically becomes party leader.

### LootTable (`LootTable.h`)

```cpp
struct LootEntry {
    std::string itemId;
    float weight = 1.0f;  // relative weight for selection
    float chance = 1.0f;  // independent pass/fail chance [0,1]
    int minCount = 1, maxCount = 1;
};

struct LootTable {
    std::string id, name;
    std::vector<LootEntry> entries;
    int minRolls = 1, maxRolls = 1;  // how many draws per roll() call

    std::vector<LootResult> roll(DiceSystem&) const;
    // Stacks results for the same itemId
};

class LootTableRegistry { // singleton
    static LootTableRegistry& instance();
    void loadFromFile(const std::string& path);
    void loadFromDirectory(const std::string& dir);
    const LootTable* getTable(const std::string& id) const;
};
```

Built-in tables in `resources/loot_tables/common_loot.json`:
`goblin_hoard`, `bandit_stash`, `dragon_hoard`, `empty`.

### EncounterBuilder (`EncounterBuilder.h`)

```cpp
enum class EncounterDifficulty { Easy, Medium, Hard, Deadly };

class EncounterBuilder {  // fluent builder
    EncounterBuilder& setId(const std::string&);
    EncounterBuilder& setName(const std::string&);
    EncounterBuilder& setLootTable(const std::string&);
    EncounterBuilder& addMonster(monsterId, name, xpValue, count, challengeRating);
    Encounter build();

    static EncounterBudget    calculateBudget(const Party&);   // skips dead members
    static EncounterDifficulty evaluateDifficulty(const Encounter&, const Party&);
    static bool isValidForDifficulty(const Encounter&, const Party&, EncounterDifficulty);
    static int  xpThreshold(int characterLevel, EncounterDifficulty); // full D&D 5e table
    static float encounterMultiplier(int monsterCount); // 1→1.0x, 2→1.5x, 3-6→2.0x …
};
```

### CampaignJournal (`CampaignJournal.h`)

```cpp
enum class JournalEntryType {
    SessionNote, WorldEvent, CharacterEvent, QuestUpdate, Discovery
};

struct JournalEntry {
    int id = 0;
    JournalEntryType type;
    int dayNumber = 0;
    std::string title, content;
    std::vector<std::string> tags;
    nlohmann::json toJson() const;
    static JournalEntry fromJson(const nlohmann::json&);
};

class CampaignJournal {
    int addEntry(JournalEntryType, std::string title, std::string content,
                 int dayNumber = 0, std::vector<std::string> tags = {});
    void removeEntry(int id);
    const JournalEntry* getEntry(int id) const;

    // Queries — all return non-owning pointers into internal storage
    std::vector<const JournalEntry*> getEntriesByType(JournalEntryType) const;
    std::vector<const JournalEntry*> getEntriesByDay(int) const;
    std::vector<const JournalEntry*> getEntriesByTag(const std::string&) const;
    std::vector<const JournalEntry*> searchEntries(const std::string& query) const; // case-insensitive

    int entryCount() const;
    void clear();  // resets ID counter to 1
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json&);
};
```

---

## Phase 8 — MCP Layer

All D&D state is exposed over the engine's HTTP API at `/api/rpg/<action>` and
through 16 MCP tools in `scripts/mcp/phyxel_mcp_server.py`.

The `Application` owns four value-type RPG members:

```cpp
Core::Party           m_rpgParty;
Core::InitiativeTracker m_rpgInitiative;
Core::WorldClock      m_rpgWorldClock;
Core::CampaignJournal m_rpgJournal;
```

The single `RpgHandler` lambda in `Application::initAPIServer()` dispatches all
`/api/rpg/*` sub-actions to these members. See `docs/MCPIntegration.md` for
the full HTTP API reference and MCP tool listing.

### Stateless tools (no engine required)

`roll_dice` and `check_dc` are implemented entirely in Python inside
`phyxel_mcp_server.py` (`_rpg_roll_dice`, `_rpg_check_dc`). They are listed in
`_NO_PROJECT_TOOLS` so they work even without `phyxel.exe` running.

---

## Data File Locations

| Directory | Contents |
|-----------|----------|
| `resources/rpg/classes/` | Class definitions (features, hit dice, slot progression) |
| `resources/rpg/races/` | Race definitions (ASIs, traits, speed) |
| `resources/rpg/backgrounds/` | Background definitions (skills, tools, languages) |
| `resources/spells/` | Spell definitions (school, level, components, damage, save) |
| `resources/rpg_items/` | Item definitions (weapon/armor stats, rarity, attunement) |
| `resources/factions/` | Faction definitions with starting rep and enemy/ally lists |
| `resources/loot_tables/` | Loot table definitions (weights, chances, roll counts) |

---

## Architecture Decisions

- **D&D combat is opt-in** via `InitiativeTracker` — real-time physics still works alongside it
- **Data-driven content**: spells/classes/races/feats/monsters are JSON; no recompile for new content
- **CharacterSheet is an optional overlay** on NPCEntity/CharacterProfile — non-D&D games ignore it entirely
- **DiceSystem uses static methods** + internal RNG singleton; `setSeed(0)` restores randomness
- **WorldClock is independent** of `DayNightCycle` (real-time rendering) — calendar advances only when `advanceDays()` is called
- **CampaignJournal IDs** are sequential integers starting at 1; `clear()` resets the counter
- **Party budget** skips dead members when calculating XP thresholds for encounter difficulty
