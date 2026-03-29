# Game Development Prompt Catalog

A curated list of prompts for building voxel games with the Phyxel engine using the `@game-creator` Copilot agent, Claude Code with MCP, or any MCP-compatible AI assistant. Each prompt exercises specific engine features and can be used for development or regression testing.

> **Usage:** Copy a prompt and paste it to your AI assistant. The assistant will use MCP tools to build the game in the running engine. Combine prompts from different sections to create richer games.

---

## Quick Reference

| Category | Prompts | Features Tested |
|----------|---------|-----------------|
| [World Building](#world-building) | 6 | Terrain generation, materials, structures |
| [NPC & Dialogue](#npc--dialogue) | 6 | NPC spawning, behaviors, dialogue trees, AI conversation |
| [Story & Quests](#story--quests) | 4 | Story arcs, objectives, progression |
| [Combat & Health](#combat--health) | 4 | Health, damage, respawn, equipment, combat |
| [Atmosphere](#atmosphere--environment) | 4 | Day/night, lighting, music, audio |
| [Game State](#game-state--persistence) | 3 | Save/load, pause, player profile |
| [Complete Games](#complete-games) | 8 | Full game definitions testing many features |
| [Advanced](#advanced-workflows) | 5 | Iterative building, packaging, testing |

---

## World Building

### 1. Flat Arena with Walls
> Create a flat arena with stone walls. Make the floor 32x32 using Stone material at Y=16. Add 4-block-high walls around the perimeter using Metal material. Leave a 2-block-wide entrance on the north side.

**Features tested:** `fill_region`, materials (Stone, Metal), hollow structures

### 2. Mountain Village Terrain
> Generate a mountain terrain with 3x3 chunks (from -1,0,-1 to 1,0,1) using seed 42. Then place 3 trees (use tree.txt template) on flat areas above the terrain. Take a screenshot so I can see the result.

**Features tested:** `generate_world` (Mountains), `spawn_template`, `screenshot`, `get_terrain_height`

### 3. Underground Dungeon
> Create a cave world (3x3 chunks). Then carve out a dungeon room at Y=10 using clear_region — make it 20x8x20 blocks. Add Stone pillars inside (4 pillars in a grid). Place glow material blocks on top of each pillar for lighting. Add a point light at the center of the room.

**Features tested:** `generate_world` (Caves), `clear_region`, `fill_region`, `place_voxel` (glow), `add_point_light`

### 4. Multi-Material Castle
> Build a castle on flat terrain. Use Stone for the base and walls (20x10x20, hollow). Add Metal for the gate frame. Use Wood for the interior floor. Place Glass windows (2x2 openings with Glass blocks). Add 4 corner towers (5x15x5 each, Stone, hollow). Put glow blocks at the top of each tower.

**Features tested:** `fill_region` with multiple materials, `place_voxel`, `place_voxels_batch`

### 5. Procedural City
> Generate a City-type world with 5x5 chunks (from -2,0,-2 to 2,0,2). Take a screenshot from above. Then add street lights — place a point light every 10 blocks along X at Y=25 in the center row (Z=0). Set the time to night so we can see the lights.

**Features tested:** `generate_world` (City), `add_point_light`, `set_day_night`, `screenshot`

### 6. Island with Beach
> Generate a Perlin terrain (3x3 chunks, heightScale=6). Clear all voxels below Y=14 to create water level. Then fill the layer at Y=13 with Glass material to simulate water. Place Cork material along the shoreline (Y=14-15) to create a sandy beach effect.

**Features tested:** `generate_world` (Perlin), `clear_region`, `fill_region`, material variety

---

## NPC & Dialogue

### 7. Patrol Guard
> Spawn an NPC named "Guard" at position (16, 25, 16) with patrol behavior. Set waypoints in a square pattern: (10,25,10) → (22,25,10) → (22,25,22) → (10,25,22). Give the guard a dialogue tree where he says "Halt! State your business." with two choices: "I'm just passing through" (leads to "Move along then.") and "I'm looking for work" (leads to "Speak to the Elder in the village center.").

**Features tested:** `spawn_npc`, `set_npc_behavior` (patrol), `set_npc_dialogue`, branching dialogue

### 8. Merchant with Inventory
> Create a merchant NPC named "Trader" at (20, 25, 16). Set behavior to idle. Give them a dialogue tree with a greeting "Welcome to my shop!" and choices to buy items. When the player selects "Buy health potion", give the player a health_potion item. Set the merchant's appearance with a larger body and blue color.

**Features tested:** `spawn_npc`, `set_npc_dialogue`, `set_npc_appearance`, `give_item`

### 9. Behavior Tree NPC
> Spawn an NPC named "Scout" with behavior_tree behavior at (16, 25, 20). Set blackboard values: patrol_radius=15, alert_level=0, home_x=16, home_z=20. Check the NPC's perception state to see what it can detect. Then spawn a physics entity near it and check perception again.

**Features tested:** `spawn_npc` (behavior_tree), `set_npc_blackboard`, `get_npc_perception`, `spawn_entity`

### 10. Social NPC Network
> Create 3 NPCs: "Alice" at (10,25,10), "Bob" at (20,25,10), "Carol" at (15,25,20). Set Alice and Bob's relationship to high trust (0.8) and affection (0.7). Set Bob and Carol's relationship to low trust (0.2) and high fear (0.6). Give Alice the belief "Bob is trustworthy" with confidence 0.9. Give Carol the opinion that "Bob" has sentiment -0.5 (negative). Apply a "greet" interaction between Alice and Bob. Check all relationships afterward.

**Features tested:** `spawn_npc`, `set_npc_relationship`, `set_npc_belief`, `set_npc_opinion`, `apply_npc_interaction`, `get_npc_relationships`

### 11. NPC with Daily Schedule
> Create an NPC named "Farmer" at (16,25,16). Register three locations: "farm" at (10,25,10), "market" at (25,25,25), "home" at (16,25,16). Set the farmer's schedule: work at the farm from 6:00-12:00, go to market from 12:00-15:00, rest at home from 15:00-22:00, sleep at home from 22:00-6:00. Set the time to 7:00 and check the farmer's behavior. Then set time to 13:00 and check again.

**Features tested:** `spawn_npc`, `add_location`, `set_npc_schedule`, `set_day_night`, `get_npc_blackboard`

### 12. AI-Powered Conversation
> Create an NPC named "Sage" at (16,25,16) with idle behavior. Give the Sage personality traits: high openness (0.9), low extraversion (0.3), high conscientiousness (0.8). Add knowledge: "knows the location of the ancient temple", "studied magic for 50 years". Configure the AI system for Claude (or whatever provider is available). Start an AI conversation with the Sage. 

**Features tested:** `spawn_npc`, `story_add_character` (personality traits), `story_add_knowledge`, `configure_ai`, `start_ai_conversation`

---

## Story & Quests

### 13. Main Quest Line
> Create a story arc called "The Lost Crown" with Guided constraint mode. Add 3 beats: (1) "Meet the elder" (Hard beat), (2) "Explore the cave" (Soft beat), (3) "Return the crown" (Hard beat). Add an objective "Find the Elder" with high priority. Create an NPC named "Elder" with dialogue about the lost crown. When the player talks to the elder, complete the first objective and add a new one: "Explore the Northern Cave".

**Features tested:** `story_add_arc`, `add_objective`, `spawn_npc`, `set_npc_dialogue`, `complete_objective`

### 14. Side Quest with Branching Outcomes
> Add 3 objectives: "Gather 5 wood" (category: "gathering", priority 3), "Help the blacksmith" (category: "village", priority 5), and a hidden objective "Find the secret passage" (hidden: true, priority 8). Complete "Gather 5 wood". Fail "Help the blacksmith". Reveal and complete "Find the secret passage". Check all objectives to see the status of each.

**Features tested:** `add_objective` (priority, category, hidden), `complete_objective`, `fail_objective`, `get_objectives`

### 15. Multi-Arc Story
> Set up two parallel story arcs. Arc 1: "Village Defense" (Strict mode) with beats "Fortify walls" → "Train militia" → "Defend the siege". Arc 2: "Mystery of the Well" (Open mode) with beats "Hear the rumor" / "Investigate at night" / "Discover the tunnel". Add story characters: "Captain" (faction: militia, agency: 2) and "Scholar" (faction: academics, agency: 1). Set world variable "threat_level" to 3.

**Features tested:** `story_add_arc` (multiple arcs, different modes), `story_add_character`, `story_set_variable`

### 16. Dynamic Quest Tracking
> Add 6 objectives with different priorities (10, 8, 6, 4, 2, 1). The HUD should show the top 5. Take a screenshot to verify the objective panel. Then complete the highest-priority objective and take another screenshot — the 6th objective should now appear in the panel.

**Features tested:** `add_objective` (6+), `complete_objective`, `get_objectives`, `screenshot`, HUD rendering

---

## Combat & Health

### 17. Arena Combat
> Set up an arena: flat terrain, stone floor, walled perimeter. Spawn the player as animated type at the center. Set player health to 100. Spawn 3 NPC enemies around the arena. Equip the player with an iron_sword in MainHand. Perform an attack toward an enemy. Check the damage dealt. Heal the player by 20. Take a screenshot.

**Features tested:** `spawn_entity`, `set_entity_health`, `spawn_npc`, `equip_item`, `attack`, `heal_player`, `screenshot`

### 18. Death & Respawn Cycle
> Set the player spawn point to (16, 25, 16). Damage the player for 50 HP. Check health. Damage for 60 more (should kill). Check respawn state — should be dead with a timer counting down. Force respawn immediately. Check health again — should be alive at full health. Check death count. Take a screenshot.

**Features tested:** `set_spawn_point`, `damage_player`, `get_player_health`, `kill_player`, `get_respawn_state`, `force_respawn`, `revive_player`

### 19. Equipment Loadout
> Create 3 NPCs. Equip the first with iron_sword (MainHand) and iron_helmet (Head). Equip the second with wooden_pickaxe (MainHand) and iron_chestplate (Chest). Leave the third unequipped. Check equipment on all three. Unequip the helmet from the first NPC. Check equipment again.

**Features tested:** `spawn_npc`, `equip_item` (multiple slots), `get_equipment`, `unequip_item`

### 20. Health Management Across Entities
> Spawn 5 NPCs. Set their health to different values: 100, 75, 50, 25, 10. Damage each by 30 HP. Check which ones are alive and which are dead. Revive the dead ones at half health. Kill one specific NPC. Check the final state of all entities.

**Features tested:** `spawn_npc`, `set_entity_health`, `damage_entity`, `heal_entity`, `kill_entity`, `revive_entity`, `list_entities`

---

## Atmosphere & Environment

### 21. Sunset Scene
> Set the time to 18:30 (sunset). Set day length to 600 seconds. Add warm orange point lights around a campfire area (4 lights in a circle at Y=18). Set ambient light to 0.3. Place glow material blocks in a ring pattern at the campfire location. Take a screenshot.

**Features tested:** `set_day_night`, `add_point_light`, `set_ambient_light`, `place_voxel` (glow), `screenshot`

### 22. Spotlight Theater
> Create a flat stage area (20x1x20, Wood material at Y=16). Add 3 spot lights pointing down at different positions on the stage — red, green, and blue. Add a point light overhead (white, low intensity). Spawn 3 NPCs on the stage, one under each spotlight. Set time to night for dramatic effect.

**Features tested:** `fill_region`, `add_spot_light`, `add_point_light`, `spawn_npc`, `set_day_night`

### 23. Background Music Playlist
> Add 3 music tracks to the playlist (use any .wav files available — check list_sounds first). Set the playlist to Shuffle mode. Set volume to 0.7. Start playing. Check the music state. Skip to next track. Check state again. Stop the music. Verify it stopped.

**Features tested:** `list_sounds`, `control_music` (add_track, set_mode, set_volume, play, next, stop), `get_music_state`

### 24. Full Day/Night Cycle
> Set day length to 120 seconds (fast cycle). Set time to 6:00 (dawn). Take a screenshot. Set time to 12:00 (noon), screenshot. Set time to 18:00 (dusk), screenshot. Set time to 0:00 (midnight), screenshot. Compare the ambient lighting across all 4 screenshots.

**Features tested:** `set_day_night` (time, dayLength), `screenshot` (multiple), day/night visual comparison

---

## Game State & Persistence

### 25. Save/Load Player Profile
> Set up a player: move camera to (30, 40, 30), set spawn point to (16, 25, 16), damage player for 20 HP, give player 3 items (iron_sword, bread, health_potion). Save the player profile. Then change everything — move camera, full-heal, clear inventory. Load the player profile. Verify camera position, health, and inventory are restored to the saved state.

**Features tested:** `set_camera`, `set_spawn_point`, `damage_player`, `give_item`, `save_player`, `heal_player`, `clear_inventory`, `load_player`, `get_player_health`, `get_inventory`

### 26. Pause System
> Start a game with NPCs patrolling. Check that the game is not paused. Toggle pause. Verify pause state. Try to move an entity — it shouldn't update. Toggle pause again. Verify the game resumes. Check NPC positions to confirm they're moving again.

**Features tested:** `toggle_pause`, `get_pause_state`, `list_npcs`, entity state during pause

### 27. World Persistence Pipeline
> Generate terrain (Perlin, 3x3 chunks). Build a small structure. Spawn 2 NPCs. Add 3 objectives. Set spawn point. Save the world. Save the player profile. Export the game definition. Verify all data is persisted by checking the exports.

**Features tested:** `generate_world`, `fill_region`, `spawn_npc`, `add_objective`, `set_spawn_point`, `save_world`, `save_player`, `export_game_definition`

---

## Complete Games

These prompts create full playable games that exercise many engine features simultaneously.

### 28. Medieval Village RPG
> Create a medieval village game. Use Mountains terrain (3x3 chunks, seed 100). Build a village center with 3 buildings: a tavern (Wood, 10x8x10), a blacksmith (Stone, 8x6x8 with Metal details), and a temple (Stone, 12x10x12 with Glass windows). Place trees around the village. Create 4 NPCs: a Barkeeper (idle in tavern, sells food), a Blacksmith (idle in smithy, repairs equipment), a Priest (idle in temple, heals), and a Guard (patrols the village perimeter). Each NPC should have dialogue. Add a main quest: "The Stolen Relic" — talk to the Priest, find clues from the Barkeeper, confront a suspect at the edge of town. Set player spawn in the village center. Add background music. Set time to morning.

**Features tested:** World generation, structures, templates, 4 NPCs with dialogue, story arc, objectives, music, day/night

### 29. Dungeon Crawler
> Create a dungeon crawler. Use Flat terrain (1 chunk). Build an underground dungeon: clear a large area below Y=15, add Stone walls forming a maze of 5 rooms connected by corridors. Place glow blocks and point lights in each room. Spawn enemy NPCs in 3 rooms. Place health potions (give_item) as rewards. Add objectives: "Clear Room 1", "Clear Room 2", "Clear Room 3", "Find the treasure", "Escape the dungeon". Equip enemies with swords. Set the player spawn at the entrance. Set time to midnight for dark atmosphere.

**Features tested:** Underground building, lighting, combat NPCs, equipment, objectives, item rewards, dark atmosphere

### 30. Survival Island
> Create a survival island game. Use Perlin terrain (5x5 chunks, low heightScale for gentle hills). Clear voxels below Y=13 for ocean (fill with Glass at Y=12 for water). Place Cork material at shore level for beaches. Build a small shelter using Wood. Add a Campfire spot with glow blocks. Create an NPC named "Castaway" with dialogue about survival tips. Add objectives: "Build a shelter", "Find fresh water", "Signal for rescue". Place crafting recipe for "wooden_planks". Set spawn on the beach. Add ambient music. Set day length to 300 seconds.

**Features tested:** Terrain editing, water simulation, materials variety, shelter building, NPC dialogue, objectives, crafting, music, day/night cycle

### 31. City Defense Game
> Build a walled city using City terrain type (3x3 chunks). Add Stone walls around the perimeter (fill_region, 3 blocks thick, 8 blocks high). Place 4 guard towers at corners (Stone, 5x12x5). Station a guard NPC at each tower with patrol behavior (patrolling the wall top). Create a Captain NPC in the city center with dialogue about the incoming threat. Add objectives: "Speak to the Captain", "Inspect the walls", "Station the archers", "Survive the night". Add spot lights on each tower. Set the story arc "The Siege" with 4 beats. Set time to dusk.

**Features tested:** City terrain, large structures, multiple patrol NPCs, story arc, objectives, spot lights

### 32. Puzzle Temple
> Create a puzzle temple. Use Flat terrain (1 chunk). Build a 3-story Stone temple (30x30 base, each floor 6 blocks high). Each floor is a room with a glow-block puzzle: the player must find and interact with NPCs who give clues. Floor 1: "Doorkeeper" asks a riddle (dialogue with right/wrong choices). Floor 2: "Scholar" wants an item (quest key). Floor 3: "Oracle" reveals the ending. Add objectives for each floor. Place colored lights (red floor 1, blue floor 2, green floor 3). Use snapshots before building each floor so you can undo mistakes.

**Features tested:** Multi-story building, dialogue choices, item quests, objectives, colored lights, snapshots

### 33. Racing Circuit
> Create a racing track. Use Flat terrain (3x3 chunks). Build a 2-block-wide track using Metal material, elevated 1 block above the ground (at Y=17). Make it a circuit. Place Cork material as guardrails on both sides. Put glow blocks at checkpoints. Spawn the player at the starting line. Add objectives: "Complete Lap 1", "Complete Lap 2", "Complete Lap 3". Place point lights at each checkpoint. Add spectator NPCs along the track with idle behavior and speech bubbles ("Go! Go! Go!"). Set time to noon for good visibility.

**Features tested:** Track building, checkpoint lighting, objectives, spectator NPCs, speech bubbles

### 34. Haunted Mansion
> Create a haunted mansion. Use Flat terrain (1 chunk). Build a large mansion using Wood and Stone (25x15x25, hollow, 3 stories). Add Glass windows. Clear some floor sections to create holes (traps). Place glow material sparingly for an eerie glow. Set time to midnight. Set ambient light very low (0.1). Add spot lights that create dramatic shadows. Create ghost NPCs with wander behavior. Add creepy dialogue. Set music. Add objectives: "Explore the basement", "Find the master bedroom", "Escape alive". Set player health to 75 (already injured). Set spawn point at the entrance. Create a hazard zone inside.

**Features tested:** Dark atmosphere, multi-story interior, lighting, wander NPCs, health setup, objectives, hazards

### 35. Trading Post
> Build a trading post at a crossroads. Mountains terrain (3x3 chunks). Build a main trading hall (Wood, 15x8x15, hollow) with a Stone foundation. Place 3 market stalls around the hall (small Wood structures). Create NPCs: "Merchant" (sells tools, idle at stall 1), "Alchemist" (sells potions, idle at stall 2), "Arms Dealer" (sells weapons, idle at stall 3), "Traveler" (wanders the area, gives quest). Each NPC has unique dialogue. Add items to player inventory (gold equivalent — iron_sword as trade goods). Set up crafting recipes. Add objectives: "Browse the market", "Complete a trade", "Accept the traveler's quest". Morning time, light music.

**Features tested:** Structure building, multiple NPCs with different behaviors, inventory, crafting, dialogue, objectives

---

## Advanced Workflows

### 36. Iterative World Building with Undo
> Generate Perlin terrain (3x3 chunks). Take a snapshot named "original_terrain". Build a castle. Take a snapshot named "with_castle". Decide you don't like the castle — restore "original_terrain". Build a different structure. If you like it, save the world. If not, try again.

**Features tested:** `create_snapshot`, `restore_snapshot`, iterative building workflow

### 37. Copy-Paste Architecture
> Build one decorative tower (Stone base, Wood interior, Glass windows, glow top). Copy the tower as a region. Paste it at 4 different locations with 90° rotation increments. This creates a matching set of towers around a courtyard.

**Features tested:** `fill_region`, `copy_region`, `paste_region` (with rotation), architectural reuse

### 38. Game Definition Export & Reimport
> Create a complete game manually (terrain, structures, NPCs, story, objectives). Export it as a game definition JSON. Validate the exported definition. Clear everything. Reimport the definition. Take screenshots before and after to verify they match.

**Features tested:** `export_game_definition`, `validate_game_definition`, `clear_all_entities`, `load_game_definition`, round-trip integrity

### 39. Project Scaffolding & Packaging
> Create a game definition with terrain, NPCs, and story. Scaffold a new project from it. Build and launch the project to verify it works. Package it for distribution. Verify the package contains all required files.

**Features tested:** `create_project`, `build_game`, `run_game`, `package_game`, full project lifecycle

### 40. Stress Test — Large World
> Generate a 7x7 chunk world (from -3,0,-3 to 3,0,3) using Mountains terrain. Place 20 NPCs across the world with various behaviors (mix of idle, patrol, behavior_tree). Add 10 point lights and 5 spot lights. Set up a story with 3 arcs. Add 10 objectives. Save the world. Take a screenshot. Check performance with engine_status.

**Features tested:** Large world size, many NPCs, multiple systems active simultaneously, performance

---

## Feature Coverage Matrix

Each prompt is tagged with the primary features it exercises. Use this to ensure full engine coverage during testing.

| Feature | Prompts |
|---------|---------|
| `generate_world` | 2, 3, 5, 6, 28-35, 40 |
| `fill_region` | 1, 3, 4, 22, 28-35 |
| `spawn_template` | 2, 28, 30 |
| `spawn_npc` | 7-12, 17-20, 28-35, 40 |
| `set_npc_dialogue` | 7, 8, 10, 28-35 |
| `set_npc_behavior` | 7, 9, 11, 31, 34 |
| `set_npc_blackboard` | 9, 11 |
| `set_npc_relationship` | 10 |
| `set_npc_schedule` | 11 |
| `start_ai_conversation` | 12 |
| `story_add_arc` | 13, 15, 31, 40 |
| `add_objective` | 13, 14, 16, 28-35, 40 |
| `complete_objective` | 13, 14, 16 |
| `attack` | 17 |
| `equip_item` / `unequip_item` | 17, 19, 29 |
| `damage_player` / `heal_player` | 18, 20, 25 |
| `set_spawn_point` / `force_respawn` | 18, 27, 28-35 |
| `set_day_night` | 5, 21, 22, 24, 28-35 |
| `add_point_light` / `add_spot_light` | 3, 5, 21, 22, 31, 32, 40 |
| `control_music` | 23, 28, 30, 34, 35 |
| `save_player` / `load_player` | 25, 27 |
| `toggle_pause` | 26 |
| `save_world` | 27, 36, 40 |
| `create_snapshot` / `restore_snapshot` | 32, 36 |
| `copy_region` / `paste_region` | 37 |
| `export_game_definition` | 38 |
| `screenshot` | 2, 5, 16, 17, 21, 24, 38, 40 |
| `give_item` / `take_item` | 8, 25, 35 |
| `create_project` / `package_game` | 39 |
