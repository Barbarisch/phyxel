Gather full context about the running Phyxel engine and current game world. Run these MCP tools in parallel and summarize the results:

1. `engine_status` — Check if engine is running
2. `get_world_state` — All entities, camera, entity count
3. `list_npcs` — NPCs with positions and behaviors
4. `list_placed_objects` — Furniture, structures in the world
5. `get_active_scene` — Current scene info
6. `get_objectives` — Active quests/objectives
7. `story_get_state` — Story engine state
8. `get_day_night` — Time of day
9. `get_player_health` — Player state
10. `list_doors` — Registered doors
11. `get_chunk_info` — Loaded chunks and rendering stats

Summarize everything concisely: what scene is active, what's in the world (entities, NPCs, structures, placed objects), player state, story state, and any active objectives. This gives full context for working with the engine.