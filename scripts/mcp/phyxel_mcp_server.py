#!/usr/bin/env python3
"""
Phyxel Engine MCP Server — Claude Code / AI Agent Bridge

An MCP (Model Context Protocol) server that connects AI coding agents
(Claude Code, Goose, etc.) to the running Phyxel game engine via the
HTTP API on localhost:8090.

Unlike the original phyxel_extension.py (which maintained local state
and never connected to the engine), this server makes real HTTP calls
to the EngineAPIServer running inside the game process.

Setup for Claude Code:
  Add to ~/.claude/claude_code_config.json:
  {
    "mcpServers": {
      "phyxel": {
        "command": "python",
        "args": ["scripts/mcp/phyxel_mcp_server.py"],
        "cwd": "<path-to-phyxel-repo>"
      }
    }
  }

Requirements:
  pip install mcp httpx

The game must be running (phyxel.exe) for tools to work.
"""

import json
import sys
import os
import logging
import subprocess
import httpx
from typing import Any
from pathlib import Path

# MCP SDK imports
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent

# ============================================================================
# Configuration
# ============================================================================

ENGINE_API_URL = os.environ.get("PHYXEL_API_URL", "http://localhost:8090")
ENGINE_API_TIMEOUT = float(os.environ.get("PHYXEL_API_TIMEOUT", "10"))

# Logging to stderr (stdout is MCP protocol)
logging.basicConfig(
    stream=sys.stderr,
    level=logging.INFO,
    format="[phyxel-mcp] %(levelname)s: %(message)s"
)
logger = logging.getLogger("phyxel_mcp")

# ============================================================================
# HTTP Client (async — must not block the MCP event loop)
# ============================================================================

http_client = httpx.AsyncClient(base_url=ENGINE_API_URL, timeout=ENGINE_API_TIMEOUT)


async def api_get(path: str, params: dict | None = None) -> dict:
    """GET request to the engine API. Returns parsed JSON."""
    try:
        resp = await http_client.get(path, params=params)
        resp.raise_for_status()
        return resp.json()
    except httpx.ConnectError:
        return {"error": f"Engine not running. Start phyxel.exe first. (tried {ENGINE_API_URL}{path})"}
    except Exception as e:
        return {"error": str(e)}


async def api_post(path: str, body: dict) -> dict:
    """POST request to the engine API. Returns parsed JSON."""
    try:
        resp = await http_client.post(path, json=body)
        resp.raise_for_status()
        return resp.json()
    except httpx.ConnectError:
        return {"error": f"Engine not running. Start phyxel.exe first. (tried {ENGINE_API_URL}{path})"}
    except Exception as e:
        return {"error": str(e)}


# ============================================================================
# MCP Server Setup
# ============================================================================

server = Server("phyxel-engine")


@server.list_tools()
async def list_tools() -> list[Tool]:
    """Return all available engine tools."""
    return [
        # ================================================================
        # Status & State
        # ================================================================
        Tool(
            name="engine_status",
            description="Check if the Phyxel game engine is running and responsive.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="get_world_state",
            description="Get a full snapshot of the game world: all entities (with positions and types), camera position/orientation, and entity count.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="get_camera",
            description="Get the current camera position, orientation (yaw/pitch), and front direction vector.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # Entity Management
        # ================================================================
        Tool(
            name="list_entities",
            description="List all registered entities in the world with their IDs, positions, and types.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="get_entity",
            description="Get detailed information about a specific entity: position, rotation, scale, debug color.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID to look up"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="spawn_entity",
            description="Spawn a new game entity (character) in the world. Types: 'physics' (humanoid with physics), 'spider' (spider enemy), 'animated' (animated voxel character).",
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {
                        "type": "string",
                        "description": "Entity type to spawn",
                        "enum": ["physics", "spider", "animated"]
                    },
                    "x": {"type": "number", "description": "Spawn X coordinate"},
                    "y": {"type": "number", "description": "Spawn Y coordinate"},
                    "z": {"type": "number", "description": "Spawn Z coordinate"},
                    "id": {"type": "string", "description": "Optional ID to assign (auto-generated if omitted)"},
                    "animFile": {"type": "string", "description": "Animation file for 'animated' type (default: character.anim)"}
                },
                "required": ["type", "x", "y", "z"]
            }
        ),
        Tool(
            name="move_entity",
            description="Teleport an entity to a new position instantly.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID to move"},
                    "x": {"type": "number", "description": "Target X coordinate"},
                    "y": {"type": "number", "description": "Target Y coordinate"},
                    "z": {"type": "number", "description": "Target Z coordinate"}
                },
                "required": ["id", "x", "y", "z"]
            }
        ),
        Tool(
            name="remove_entity",
            description="Remove an entity from the world.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID to remove"}
                },
                "required": ["id"]
            }
        ),

        # ================================================================
        # Voxel World Building
        # ================================================================
        Tool(
            name="place_voxel",
            description="Place a single voxel (cube) at world coordinates. Optionally specify a material.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "X coordinate"},
                    "y": {"type": "integer", "description": "Y coordinate"},
                    "z": {"type": "integer", "description": "Z coordinate"},
                    "material": {"type": "string", "description": "Material name (e.g., 'stone', 'wood', 'dirt')"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="remove_voxel",
            description="Remove a voxel (cube) at world coordinates.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "X coordinate"},
                    "y": {"type": "integer", "description": "Y coordinate"},
                    "z": {"type": "integer", "description": "Z coordinate"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="query_voxel",
            description="Check if a voxel exists at the given world coordinates.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "X coordinate"},
                    "y": {"type": "integer", "description": "Y coordinate"},
                    "z": {"type": "integer", "description": "Z coordinate"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="place_voxels_batch",
            description="Place multiple voxels at once. More efficient than calling place_voxel repeatedly. Pass an array of voxel positions with optional materials.",
            inputSchema={
                "type": "object",
                "properties": {
                    "voxels": {
                        "type": "array",
                        "description": "Array of voxels to place",
                        "items": {
                            "type": "object",
                            "properties": {
                                "x": {"type": "integer"},
                                "y": {"type": "integer"},
                                "z": {"type": "integer"},
                                "material": {"type": "string"}
                            },
                            "required": ["x", "y", "z"]
                        }
                    }
                },
                "required": ["voxels"]
            }
        ),

        # ================================================================
        # Templates
        # ================================================================
        Tool(
            name="list_templates",
            description="List all available voxel object templates (e.g., 'castle', 'tree', 'sphere') that can be spawned in the world.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="spawn_template",
            description="Spawn a pre-built voxel object template at a world position. Use list_templates first to see available templates.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Template name (e.g., 'castle', 'tree')"},
                    "x": {"type": "number", "description": "X position"},
                    "y": {"type": "number", "description": "Y position"},
                    "z": {"type": "number", "description": "Z position"},
                    "static": {"type": "boolean", "description": "If true, merges into terrain. If false, creates physics objects.", "default": True}
                },
                "required": ["name", "x", "y", "z"]
            }
        ),

        # ================================================================
        # Camera Control
        # ================================================================
        Tool(
            name="set_camera",
            description="Move the camera to a specific position and/or orientation.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "number", "description": "Camera X position"},
                    "y": {"type": "number", "description": "Camera Y position"},
                    "z": {"type": "number", "description": "Camera Z position"},
                    "yaw": {"type": "number", "description": "Camera yaw (horizontal rotation in degrees)"},
                    "pitch": {"type": "number", "description": "Camera pitch (vertical rotation in degrees)"}
                },
                "required": []
            }
        ),

        # ================================================================
        # Scripting
        # ================================================================
        Tool(
            name="run_script",
            description="Execute Python code inside the running engine. Has access to the full phyxel Python API: phyxel.get_app(), phyxel.Logger, etc. Use this for operations not covered by other tools.",
            inputSchema={
                "type": "object",
                "properties": {
                    "code": {"type": "string", "description": "Python code to execute in the engine's embedded interpreter"}
                },
                "required": ["code"]
            }
        ),

        # ================================================================
        # Region Filling
        # ================================================================
        Tool(
            name="fill_region",
            description="Fill a 3D box region with voxels. Specify two opposite corners. Optionally make it hollow (shell only). Max 100,000 voxels per call.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"},
                    "material": {"type": "string", "description": "Material name (e.g. 'Stone', 'Wood', 'Metal', 'Glass', 'glow'). Use list_materials to see all."},
                    "hollow": {"type": "boolean", "description": "If true, only fill the outer shell (walls/floor/ceiling)", "default": False}
                },
                "required": ["x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),

        # ================================================================
        # Materials
        # ================================================================
        Tool(
            name="list_materials",
            description="List all available voxel materials with their physical properties (mass, friction, restitution, color tint, metallic, roughness).",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # Chunk Info
        # ================================================================
        Tool(
            name="get_chunk_info",
            description="Get information about loaded chunks: count, world origins, cube counts per chunk, and rendering statistics.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # Entity Property Update
        # ================================================================
        Tool(
            name="update_entity",
            description="Update one or more properties of an entity: position, rotation (quaternion), scale, and debug color. Only provided fields are changed.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "x": {"type": "number", "description": "New X position"},
                    "y": {"type": "number", "description": "New Y position"},
                    "z": {"type": "number", "description": "New Z position"},
                    "rotation_w": {"type": "number", "description": "Quaternion W"},
                    "rotation_x": {"type": "number", "description": "Quaternion X"},
                    "rotation_y": {"type": "number", "description": "Quaternion Y"},
                    "rotation_z": {"type": "number", "description": "Quaternion Z"},
                    "scale_x": {"type": "number", "description": "Scale X"},
                    "scale_y": {"type": "number", "description": "Scale Y"},
                    "scale_z": {"type": "number", "description": "Scale Z"},
                    "color_r": {"type": "number", "description": "Debug color red (0-1)"},
                    "color_g": {"type": "number", "description": "Debug color green (0-1)"},
                    "color_b": {"type": "number", "description": "Debug color blue (0-1)"},
                    "color_a": {"type": "number", "description": "Debug color alpha (0-1)"}
                },
                "required": ["id"]
            }
        ),

        # ================================================================
        # Screenshot / Observation
        # ================================================================
        Tool(
            name="screenshot",
            description="Capture a screenshot of the current game view. Returns the path to the saved PNG file along with dimensions. Use this to observe the visual result of your actions.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # Region Scanning
        # ================================================================
        Tool(
            name="scan_region",
            description="Scan a 3D region and return all voxels within it. Returns position, material, and visibility for each occupied voxel. Max 100k volume. Use to understand what's in a region before modifying it.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),

        # ================================================================
        # Region Clearing
        # ================================================================
        Tool(
            name="clear_region",
            description="Remove all voxels in a 3D bounding box. Max 100k volume. Returns count of removed and skipped voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),

        # ================================================================
        # World Persistence
        # ================================================================
        Tool(
            name="save_world",
            description="Save the current world state to the SQLite database. By default saves only modified (dirty) chunks. Set 'all' to true to force saving every chunk.",
            inputSchema={
                "type": "object",
                "properties": {
                    "all": {"type": "boolean", "description": "Save all chunks (true) or only dirty chunks (false, default)", "default": False}
                },
                "required": []
            }
        ),

        # ================================================================
        # Event Polling
        # ================================================================
        Tool(
            name="poll_events",
            description="Poll game events since a cursor. Returns entity spawned/removed/moved/updated, voxel placed/removed, region filled/cleared, and world save events. Pass the 'cursor' value from the previous response to get only new events. Start with cursor=0 to get all buffered events (up to 1000).",
            inputSchema={
                "type": "object",
                "properties": {
                    "cursor": {"type": "integer", "description": "Event cursor from last poll (0 for first poll)", "default": 0}
                },
                "required": []
            }
        ),

        # ================================================================
        # Snapshots (undo/redo)
        # ================================================================
        Tool(
            name="create_snapshot",
            description="Create a named snapshot of a voxel region for undo. Captures all voxels (with materials) in the bounding box. Use before making destructive changes. Restore with restore_snapshot. Max region 100k voxels, max 50 snapshots (oldest auto-evicted).",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Unique name for this snapshot (e.g. 'before_castle')"},
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["name", "x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),
        Tool(
            name="restore_snapshot",
            description="Restore a previously created snapshot, undoing changes to that region. Clears the region first, then places all voxels from the snapshot with their original materials.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the snapshot to restore"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="list_snapshots",
            description="List all stored snapshots with metadata (name, region bounds, voxel count). Does not include voxel data.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="delete_snapshot",
            description="Delete a named snapshot to free memory.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the snapshot to delete"}
                },
                "required": ["name"]
            }
        ),

        # ================================================================
        # Clipboard (copy / paste regions)
        # ================================================================
        Tool(
            name="copy_region",
            description="Copy a voxel region into the clipboard. Captures all voxels with their materials as relative offsets from the min corner. Use paste_region to place the copied data at a new location. Max region 100k voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),
        Tool(
            name="paste_region",
            description="Paste the clipboard contents at a new world position. Optionally rotate by 0/90/180/270 degrees clockwise around the Y axis. The clipboard min corner maps to the given position. Does NOT clear the destination first — use clear_region beforehand if needed.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "Destination X (maps to clipboard min corner)"},
                    "y": {"type": "integer", "description": "Destination Y"},
                    "z": {"type": "integer", "description": "Destination Z"},
                    "rotate": {"type": "integer", "description": "Rotation in degrees: 0, 90, 180, or 270 (clockwise around Y)", "default": 0}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="get_clipboard",
            description="Check the clipboard status: whether it has data, its size, and voxel count.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # World Generation
        # ================================================================
        Tool(
            name="generate_world",
            description="Generate procedural terrain for one or more chunks. Types: Random (70% fill), Perlin (height-map, base Y=16), Flat (solid below Y=16), Mountains (multi-octave, peaks ~60), Caves (Perlin + cave carving), City (flat ground + buildings). Specify chunks as a list of chunk coordinates OR a from/to range. Max 64 chunks per call. Chunk coordinates are world_pos/32 (e.g., chunk {0,0,0} covers world 0-31, chunk {1,0,0} covers 32-63).",
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {"type": "string", "description": "Generation type: Random, Perlin, Flat, Mountains, Caves, or City", "enum": ["Random", "Perlin", "Flat", "Mountains", "Caves", "City"]},
                    "seed": {"type": "integer", "description": "Random seed (0 for default)", "default": 0},
                    "chunks": {"type": "array", "description": "List of chunk coordinates [{x,y,z}, ...]", "items": {"type": "object", "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "z": {"type": "integer"}}}},
                    "from": {"type": "object", "description": "Range start chunk coord {x,y,z} (use with 'to')", "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "z": {"type": "integer"}}},
                    "to": {"type": "object", "description": "Range end chunk coord {x,y,z} (use with 'from')", "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "z": {"type": "integer"}}},
                    "params": {"type": "object", "description": "Optional terrain parameters", "properties": {
                        "heightScale": {"type": "number", "description": "Max terrain height (default 16)"},
                        "frequency": {"type": "number", "description": "Noise frequency (default 0.05)"},
                        "octaves": {"type": "integer", "description": "Noise octaves (default 4)"},
                        "persistence": {"type": "number", "description": "Octave contribution (default 0.5)"},
                        "lacunarity": {"type": "number", "description": "Frequency multiplier per octave (default 2.0)"},
                        "caveThreshold": {"type": "number", "description": "Cave threshold (default 0.3)"},
                        "stoneLevel": {"type": "number", "description": "Stone level Y (default 8)"}
                    }}
                },
                "required": ["type"]
            }
        ),

        # ================================================================
        # Template Creation
        # ================================================================
        Tool(
            name="save_template",
            description="Save a voxel region as a reusable .txt template file. Scans all cubes in the bounding box, converts to relative coordinates, and writes to resources/templates/<name>.txt. The template is immediately available for spawn_template. Max 100k voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Template name (becomes filename, no extension)"},
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["name", "x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),

        # ================================================================
        # NPC Management
        # ================================================================
        Tool(
            name="spawn_npc",
            description="Spawn an NPC with an animated voxel character. NPCs can have idle or patrol behaviors and can be interacted with using the E key. Supports dialogue trees via set_npc_dialogue.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Unique NPC name/identifier"},
                    "animFile": {"type": "string", "description": "Animation file (default: character.anim)", "default": "character.anim"},
                    "position": {"type": "object", "description": "Spawn position {x,y,z}", "properties": {
                        "x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}
                    }},
                    "behavior": {"type": "string", "description": "Behavior type: idle or patrol", "enum": ["idle", "patrol"], "default": "idle"},
                    "waypoints": {"type": "array", "description": "Patrol waypoints [{x,y,z}, ...] (required for patrol)", "items": {
                        "type": "object", "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}
                    }},
                    "walkSpeed": {"type": "number", "description": "Walk speed for patrol (default 2.0)", "default": 2.0},
                    "waitTime": {"type": "number", "description": "Wait time at waypoints (default 2.0)", "default": 2.0}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="remove_npc",
            description="Remove an NPC from the world by name.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name to remove"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="list_npcs",
            description="List all NPCs in the world with their positions, behaviors, and interaction radii.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="set_npc_behavior",
            description="Change an NPC's behavior (idle or patrol with waypoints).",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"},
                    "behavior": {"type": "string", "description": "Behavior type: idle or patrol", "enum": ["idle", "patrol"]},
                    "waypoints": {"type": "array", "description": "Patrol waypoints (for patrol behavior)", "items": {
                        "type": "object", "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}
                    }},
                    "walkSpeed": {"type": "number", "description": "Walk speed", "default": 2.0},
                    "waitTime": {"type": "number", "description": "Wait time at waypoints", "default": 2.0}
                },
                "required": ["name", "behavior"]
            }
        ),

        # ================================================================
        # Dialogue & Speech Bubbles
        # ================================================================
        Tool(
            name="set_npc_dialogue",
            description="Assign a dialogue tree (JSON) to an NPC. When a player presses E near this NPC, the dialogue will play in the RPG dialogue box at the bottom of the screen. Dialogue trees have nodes with speaker, text, optional choices, and navigation.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name to assign dialogue to"},
                    "tree": {
                        "type": "object",
                        "description": "Dialogue tree JSON: {id, startNodeId, nodes: [{id, speaker, text, emotion?, nextNodeId?, choices?: [{text, targetNodeId}]}]}",
                        "properties": {
                            "id": {"type": "string"},
                            "startNodeId": {"type": "string"},
                            "nodes": {"type": "array", "items": {"type": "object"}}
                        },
                        "required": ["id", "startNodeId", "nodes"]
                    }
                },
                "required": ["name", "tree"]
            }
        ),
        Tool(
            name="start_dialogue",
            description="Immediately start a dialogue conversation. If the named NPC exists, their dialogue tree is used. Otherwise a standalone dialogue is started with the provided tree. The RPG dialogue box appears at the bottom of the screen with typewriter text reveal. Player uses Enter to advance, 1-4 for choices, Esc to end.",
            inputSchema={
                "type": "object",
                "properties": {
                    "npc": {"type": "string", "description": "Speaker name (or NPC name if NPC exists)"},
                    "tree": {
                        "type": "object",
                        "description": "Dialogue tree JSON",
                        "properties": {
                            "id": {"type": "string"},
                            "startNodeId": {"type": "string"},
                            "nodes": {"type": "array", "items": {"type": "object"}}
                        },
                        "required": ["id", "startNodeId", "nodes"]
                    }
                },
                "required": ["npc", "tree"]
            }
        ),
        Tool(
            name="end_dialogue",
            description="End the currently active dialogue conversation.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="say_bubble",
            description="Show a floating speech bubble above an entity. The bubble fades out after the specified duration. Great for ambient NPC chatter or quick messages.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entityId": {"type": "string", "description": "Entity registry ID (e.g. 'npc_Guard')"},
                    "text": {"type": "string", "description": "Text to display in the bubble"},
                    "duration": {"type": "number", "description": "How long the bubble lasts in seconds (default 3.0)", "default": 3.0}
                },
                "required": ["entityId", "text"]
            }
        ),
        Tool(
            name="get_dialogue_state",
            description="Get the current state of the dialogue system. Returns whether a conversation is active, current speaker, text, revealed text (typewriter progress), emotion, and available choices.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="advance_dialogue",
            description="Advance the active dialogue: skip the typewriter effect or move to the next node. Equivalent to pressing Enter.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="select_dialogue_choice",
            description="Select a dialogue choice by index (0-based) during choice selection state.",
            inputSchema={
                "type": "object",
                "properties": {
                    "index": {"type": "integer", "description": "Choice index (0-based)"}
                },
                "required": ["index"]
            }
        ),
        Tool(
            name="load_dialogue_file",
            description="Load a dialogue tree from a JSON file in resources/dialogues/. Optionally assign it to an NPC. If no NPC is specified, returns the parsed tree.",
            inputSchema={
                "type": "object",
                "properties": {
                    "filename": {"type": "string", "description": "JSON filename (e.g. 'guard_intro.json')"},
                    "npc": {"type": "string", "description": "Optional NPC name to assign the dialogue to"}
                },
                "required": ["filename"]
            }
        ),
        Tool(
            name="list_dialogue_files",
            description="List all available dialogue JSON files in resources/dialogues/.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),

        # ================================================================
        # Story System
        # ================================================================
        Tool(
            name="story_get_state",
            description="Get the full story engine state including all characters, arcs, world state, and memories.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="story_list_characters",
            description="List all story characters with their IDs, names, and factions.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="story_get_character",
            description="Get detailed info about a specific story character including profile, memory, and emotional state.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Character ID"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="story_list_arcs",
            description="List all story arcs with their current status.',",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="story_get_arc",
            description="Get detailed info about a specific story arc including beats and tension.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Arc ID"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="story_get_world",
            description="Get the story world state: factions, locations, and variables.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="story_load_world",
            description="Load a full world definition from JSON. Defines factions, characters, story arcs, and world variables in one call.",
            inputSchema={
                "type": "object",
                "properties": {
                    "definition": {"type": "object", "description": "Full world definition JSON object with world, characters, and storyArcs sections"}
                },
                "required": ["definition"]
            }
        ),
        Tool(
            name="story_add_character",
            description="Add a single character to the story engine.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Unique character ID"},
                    "name": {"type": "string", "description": "Character display name"},
                    "faction": {"type": "string", "description": "Faction ID (optional)"},
                    "agencyLevel": {"type": "integer", "description": "0=Scripted, 1=Templated, 2=Guided, 3=Autonomous"},
                    "traits": {"type": "object", "description": "Big Five personality traits (openness, conscientiousness, extraversion, agreeableness, neuroticism) from 0.0-1.0"},
                    "goals": {"type": "array", "items": {"type": "object"}, "description": "Character goals with id, description, priority"}
                },
                "required": ["id", "name"]
            }
        ),
        Tool(
            name="story_remove_character",
            description="Remove a character from the story engine.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Character ID to remove"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="story_trigger_event",
            description="Trigger a world event in the story engine. Events propagate to characters and can advance story beats.",
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {"type": "string", "description": "Event type name"},
                    "data": {"type": "object", "description": "Event data as key-value pairs"},
                    "location": {"type": "string", "description": "Location ID where event occurs (optional)"},
                    "participants": {"type": "array", "items": {"type": "string"}, "description": "Character IDs involved (optional)"}
                },
                "required": ["type"]
            }
        ),
        Tool(
            name="story_add_arc",
            description="Add a story arc with beats and constraint mode.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Unique arc ID"},
                    "name": {"type": "string", "description": "Arc display name"},
                    "constraintMode": {"type": "string", "description": "Scripted, Guided, Emergent, or Freeform"},
                    "beats": {"type": "array", "items": {"type": "object"}, "description": "Story beats with id, type, conditions"},
                    "tensionCurve": {"type": "array", "items": {"type": "number"}, "description": "Tension values over arc progress (optional)"}
                },
                "required": ["id", "name"]
            }
        ),
        Tool(
            name="story_set_variable",
            description="Set a world variable in the story engine.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Variable name"},
                    "value": {"description": "Variable value (bool, int, float, or string)"}
                },
                "required": ["name", "value"]
            }
        ),
        Tool(
            name="story_set_agency",
            description="Set a character's agency level (how autonomous their behavior is).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Character ID"},
                    "level": {"type": "integer", "description": "0=Scripted, 1=Templated, 2=Guided, 3=Autonomous"}
                },
                "required": ["id", "level"]
            }
        ),
        Tool(
            name="story_add_knowledge",
            description="Add a piece of knowledge to a character's memory.",
            inputSchema={
                "type": "object",
                "properties": {
                    "characterId": {"type": "string", "description": "Character ID"},
                    "fact": {"type": "string", "description": "Knowledge fact text"},
                    "category": {"type": "string", "description": "Knowledge category (optional)"}
                },
                "required": ["characterId", "fact"]
            }
        ),

        # ================================================================
        # GAME DEFINITION (AI Game Development)
        # ================================================================

        Tool(
            name="load_game_definition",
            description=(
                "Load a complete game from a single JSON definition. This is the primary tool for AI-driven "
                "game creation. Provide a game definition with world generation, structures, player, NPCs "
                "(with dialogue and story roles), camera setup, and story arcs — all in one call. "
                "Sections: name, description, version, world, structures, player, camera, npcs, story. "
                "All sections are optional. Example: {\"name\": \"My Game\", \"world\": {\"type\": \"Perlin\", "
                "\"from\": {\"x\":-1,\"y\":0,\"z\":-1}, \"to\": {\"x\":1,\"y\":0,\"z\":1}}, "
                "\"player\": {\"type\": \"physics\", \"position\": {\"x\":16,\"y\":20,\"z\":16}}, "
                "\"npcs\": [{\"name\": \"Guard\", \"position\": {\"x\":10,\"y\":20,\"z\":10}, "
                "\"dialogue\": {\"id\":\"talk\", \"startNodeId\":\"start\", "
                "\"nodes\": [{\"id\":\"start\",\"speaker\":\"Guard\",\"text\":\"Hello!\"}]}}]}"
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Game name"},
                    "description": {"type": "string", "description": "Game description"},
                    "version": {"type": "string", "description": "Version string"},
                    "world": {
                        "type": "object",
                        "description": "World generation config. type: Perlin|Flat|Mountains|Caves|City|Random. "
                                       "Specify chunks as array of {x,y,z} or from/to range. Max 64 chunks.",
                        "properties": {
                            "type": {"type": "string", "enum": ["Perlin", "Flat", "Mountains", "Caves", "City", "Random"]},
                            "seed": {"type": "integer"},
                            "chunks": {"type": "array", "items": {"type": "object"}},
                            "from": {"type": "object"},
                            "to": {"type": "object"},
                            "params": {"type": "object", "description": "heightScale, frequency, octaves, persistence, lacunarity, caveThreshold, stoneLevel"}
                        }
                    },
                    "structures": {
                        "type": "array",
                        "description": "Array of structures to place. Each has type='fill' (from/to/material/hollow) or type='template' (template/position/dynamic).",
                        "items": {"type": "object"}
                    },
                    "player": {
                        "type": "object",
                        "description": "Player entity. type: physics|spider|animated. position: {x,y,z}. Optional: id, animFile.",
                        "properties": {
                            "type": {"type": "string", "enum": ["physics", "spider", "animated"]},
                            "position": {"type": "object"},
                            "id": {"type": "string"},
                            "animFile": {"type": "string"}
                        }
                    },
                    "camera": {
                        "type": "object",
                        "description": "Camera position and orientation.",
                        "properties": {
                            "position": {"type": "object"},
                            "yaw": {"type": "number"},
                            "pitch": {"type": "number"}
                        }
                    },
                    "npcs": {
                        "type": "array",
                        "description": "Array of NPCs. Each has: name, animFile, position, behavior (idle|patrol), "
                                       "waypoints, walkSpeed, waitTime, dialogue (DialogueTree JSON), "
                                       "storyCharacter (id, faction, agencyLevel, traits, goals, roles).",
                        "items": {"type": "object"}
                    },
                    "story": {
                        "type": "object",
                        "description": "Story engine setup. world: {factions, locations, variables}. arcs: [{id, name, constraintMode, beats}]."
                    }
                }
            }
        ),
        Tool(
            name="export_game_definition",
            description="Export the current game state as a game definition JSON. Returns camera, NPCs, and story state.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="validate_game_definition",
            description="Validate a game definition JSON without loading it. Returns {valid: bool, error?: string}.",
            inputSchema={
                "type": "object",
                "properties": {
                    "definition": {"type": "object", "description": "The game definition to validate"}
                },
                "required": ["definition"]
            }
        ),
        Tool(
            name="create_game_npc",
            description=(
                "Create a complete NPC in one call: spawn animated character + set dialogue tree + "
                "register story character with personality, goals, and faction. More convenient than "
                "calling spawn_npc + set_npc_dialogue + story_add_character separately."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name (unique identifier)"},
                    "animFile": {"type": "string", "description": "Animation file (default: character.anim)"},
                    "position": {"type": "object", "description": "{x, y, z} world coordinates",
                                 "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}},
                    "behavior": {"type": "string", "enum": ["idle", "patrol"], "description": "NPC behavior type"},
                    "waypoints": {"type": "array", "description": "Patrol waypoints [{x,y,z}]", "items": {"type": "object"}},
                    "walkSpeed": {"type": "number"},
                    "waitTime": {"type": "number"},
                    "dialogue": {
                        "type": "object",
                        "description": "Dialogue tree: {id, startNodeId, nodes: [{id, speaker, text, emotion?, nextNodeId?, choices?: [{text, targetNodeId}]}]}"
                    },
                    "storyCharacter": {
                        "type": "object",
                        "description": "Story character profile: {id, faction, agencyLevel (0-3), traits: {openness, conscientiousness, extraversion, agreeableness, neuroticism}, goals: [{id, description, priority}], roles: [string]}"
                    }
                },
                "required": ["name"]
            }
        ),

        # ================================================================
        # PROJECT LIFECYCLE
        # ================================================================

        Tool(
            name="build_project",
            description=(
                "Build the Phyxel engine project using CMake. Runs cmake -B build -S . and "
                "cmake --build build --config <config>. Returns build output including errors."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Build configuration (default: Debug)"},
                    "reconfigure": {"type": "boolean", "description": "Run cmake configure before build (default: false)"}
                }
            }
        ),
        Tool(
            name="launch_engine",
            description=(
                "Launch the Phyxel engine executable as a background process. "
                "The engine must be built first. Once running, all other tools become available. "
                "Returns the process ID."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Which build config to launch (default: Debug)"},
                    "args": {"type": "array", "items": {"type": "string"}, "description": "Additional command-line arguments"}
                }
            }
        ),
        Tool(
            name="engine_running",
            description="Check if the engine process is currently running and responsive.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="package_game",
            description=(
                "Package a game into a standalone distributable directory. "
                "Creates a self-contained folder with game executable, shaders, "
                "resources, and game definition — no source code, build system, or dev tools. "
                "Use projectDir to package from a scaffolded game project (own C++ exe). "
                "Use prebakeWorld to save the engine's current world to SQLite for instant startup."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Game name (used for exe and folder)"},
                    "definition": {"type": "object", "description": "Game definition JSON (optional — if omitted, exports current engine state)"},
                    "output": {"type": "string", "description": "Output directory path (default: Documents/PhyxelProjects/<name>)"},
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Build config binary to package (default: Debug)"},
                    "projectDir": {"type": "string", "description": "Path to a scaffolded game project directory (uses its own compiled exe)"},
                    "prebakeWorld": {"type": "boolean", "description": "Save the running engine's world to SQLite first (default: false)"},
                    "allResources": {"type": "boolean", "description": "Include all resources, not just those referenced (default: false)"},
                    "includeMcp": {"type": "boolean", "description": "Include MCP server for AI iteration (default: false)"},
                    "title": {"type": "string", "description": "Window title (default: game name)"}
                },
                "required": ["name"]
            }
        ),

        # ================================================================
        # IN-ENGINE PROJECT BUILD / RUN
        # ================================================================

        Tool(
            name="project_info",
            description=(
                "Get info about the game project currently loaded in the engine "
                "(via --project flag). Returns project dir, whether game.json/CMakeLists.txt/exe exist."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="build_game",
            description=(
                "Build the game project from within the running engine. "
                "Requires the engine to be running with --project <dir>. "
                "Runs cmake configure (if needed) and cmake build on the project directory. "
                "Returns build output including any errors."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Build configuration (default: Debug)"},
                    "reconfigure": {"type": "boolean", "description": "Force cmake reconfigure (default: false)"}
                }
            }
        ),
        Tool(
            name="run_game",
            description=(
                "Launch the built game executable from within the running engine. "
                "Requires the engine to be running with --project <dir> and the game to be built."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),

        # ================================================================
        # ASYNC JOB SYSTEM
        # ================================================================

        Tool(
            name="submit_job",
            description=(
                "Submit a long-running operation as a background job. The engine continues "
                "running while the job executes. Returns a job_id to poll for status. "
                "Supported job types: fill_region, clear_region, generate_world, save_world, project_build."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {
                        "type": "string",
                        "enum": ["fill_region", "clear_region", "generate_world", "save_world", "project_build"],
                        "description": "The job type to submit"
                    },
                    "params": {
                        "type": "object",
                        "description": (
                            "Job-specific parameters. "
                            "fill_region: {x1,y1,z1,x2,y2,z2, material?, hollow?}. "
                            "clear_region: {x1,y1,z1,x2,y2,z2}. "
                            "generate_world: {type, from_x,from_y,from_z, to_x,to_y,to_z, seed?}. "
                            "save_world: {dirty_only?}. "
                            "project_build: {config?}."
                        )
                    }
                },
                "required": ["type", "params"]
            }
        ),
        Tool(
            name="get_job_status",
            description=(
                "Get the status of a background job by ID. Returns state (Pending/Running/"
                "Completing/Complete/Failed/Cancelled), progress (0.0-1.0), message, and "
                "result (when complete)."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "job_id": {"type": "integer", "description": "The job ID returned by submit_job"}
                },
                "required": ["job_id"]
            }
        ),
        Tool(
            name="list_jobs",
            description="List all active and recent background jobs with their status.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="cancel_job",
            description="Cancel a running or pending background job.",
            inputSchema={
                "type": "object",
                "properties": {
                    "job_id": {"type": "integer", "description": "The job ID to cancel"}
                },
                "required": ["job_id"]
            }
        ),
    ]


@server.call_tool()
async def call_tool(name: str, arguments: dict) -> list[TextContent]:
    """Route tool calls to the engine HTTP API."""
    logger.info(f"Tool call: {name}({json.dumps(arguments, default=str)[:200]})")

    result = await _dispatch_tool(name, arguments)

    return [TextContent(type="text", text=json.dumps(result, indent=2))]


async def _dispatch_tool(name: str, args: dict) -> dict:
    """Dispatch a tool call to the appropriate API endpoint."""

    # --- Status & State ---
    if name == "engine_status":
        return await api_get("/api/status")

    elif name == "get_world_state":
        return await api_get("/api/state")

    elif name == "get_camera":
        return await api_get("/api/camera")

    # --- Entity Management ---
    elif name == "list_entities":
        return await api_get("/api/entities")

    elif name == "get_entity":
        return await api_get(f"/api/entity/{args['id']}")

    elif name == "spawn_entity":
        body: dict[str, Any] = {
            "type": args["type"],
            "position": {"x": args["x"], "y": args["y"], "z": args["z"]}
        }
        if "id" in args:
            body["id"] = args["id"]
        if "animFile" in args:
            body["animFile"] = args["animFile"]
        return await api_post("/api/entity/spawn", body)

    elif name == "move_entity":
        return await api_post("/api/entity/move", {
            "id": args["id"],
            "position": {"x": args["x"], "y": args["y"], "z": args["z"]}
        })

    elif name == "remove_entity":
        return await api_post("/api/entity/remove", {"id": args["id"]})

    # --- Voxel World ---
    elif name == "place_voxel":
        body = {"x": args["x"], "y": args["y"], "z": args["z"]}
        if "material" in args:
            body["material"] = args["material"]
        return await api_post("/api/world/voxel", body)

    elif name == "remove_voxel":
        return await api_post("/api/world/voxel/remove", {
            "x": args["x"], "y": args["y"], "z": args["z"]
        })

    elif name == "query_voxel":
        return await api_get("/api/world/voxel", {
            "x": str(args["x"]), "y": str(args["y"]), "z": str(args["z"])
        })

    elif name == "place_voxels_batch":
        return await api_post("/api/world/voxel/batch", {"voxels": args["voxels"]})

    # --- Templates ---
    elif name == "list_templates":
        return await api_get("/api/templates")

    elif name == "spawn_template":
        return await api_post("/api/world/template", {
            "name": args["name"],
            "position": {"x": args["x"], "y": args["y"], "z": args["z"]},
            "static": args.get("static", True)
        })

    # --- Camera ---
    elif name == "set_camera":
        body: dict[str, Any] = {}
        if "x" in args or "y" in args or "z" in args:
            body["position"] = {
                "x": args.get("x", 0),
                "y": args.get("y", 0),
                "z": args.get("z", 0)
            }
        if "yaw" in args:
            body["yaw"] = args["yaw"]
        if "pitch" in args:
            body["pitch"] = args["pitch"]
        return await api_post("/api/camera", body)

    # --- Scripting ---
    elif name == "run_script":
        return await api_post("/api/script", {"code": args["code"]})

    # --- Region Fill ---
    elif name == "fill_region":
        body: dict[str, Any] = {
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        }
        if "material" in args:
            body["material"] = args["material"]
        if "hollow" in args:
            body["hollow"] = args["hollow"]
        return await api_post("/api/world/fill", body)

    # --- Materials ---
    elif name == "list_materials":
        return await api_get("/api/materials")

    # --- Chunk Info ---
    elif name == "get_chunk_info":
        return await api_get("/api/world/chunks")

    # --- Entity Update ---
    elif name == "update_entity":
        body: dict[str, Any] = {"id": args["id"]}
        # Build position if any coord given
        if any(k in args for k in ("x", "y", "z")):
            body["position"] = {
                "x": args.get("x", 0), "y": args.get("y", 0), "z": args.get("z", 0)
            }
        # Build rotation if any quat component given
        if any(k in args for k in ("rotation_w", "rotation_x", "rotation_y", "rotation_z")):
            body["rotation"] = {
                "w": args.get("rotation_w", 1), "x": args.get("rotation_x", 0),
                "y": args.get("rotation_y", 0), "z": args.get("rotation_z", 0)
            }
        # Build scale if any component given
        if any(k in args for k in ("scale_x", "scale_y", "scale_z")):
            body["scale"] = {
                "x": args.get("scale_x", 1), "y": args.get("scale_y", 1), "z": args.get("scale_z", 1)
            }
        # Build debugColor if any component given
        if any(k in args for k in ("color_r", "color_g", "color_b", "color_a")):
            body["debugColor"] = {
                "r": args.get("color_r", 1), "g": args.get("color_g", 1),
                "b": args.get("color_b", 1), "a": args.get("color_a", 1)
            }
        return await api_post("/api/entity/update", body)

    # --- Screenshot ---
    elif name == "screenshot":
        return await api_get("/api/screenshot")

    # --- Region Scan ---
    elif name == "scan_region":
        return await api_get("/api/world/scan", {
            "x1": str(args["x1"]), "y1": str(args["y1"]), "z1": str(args["z1"]),
            "x2": str(args["x2"]), "y2": str(args["y2"]), "z2": str(args["z2"])
        })

    # --- Region Clear ---
    elif name == "clear_region":
        return await api_post("/api/world/clear", {
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    # --- World Save ---
    elif name == "save_world":
        body: dict[str, Any] = {}
        if "all" in args:
            body["all"] = args["all"]
        return await api_post("/api/world/save", body)

    # --- Event Polling ---
    elif name == "poll_events":
        params = {}
        if "cursor" in args:
            params["since"] = str(args["cursor"])
        return await api_get("/api/events", params)

    # --- Snapshots ---
    elif name == "create_snapshot":
        return await api_post("/api/snapshot/create", {
            "name": args["name"],
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    elif name == "restore_snapshot":
        return await api_post("/api/snapshot/restore", {"name": args["name"]})

    elif name == "list_snapshots":
        return await api_get("/api/snapshots")

    elif name == "delete_snapshot":
        return await api_post("/api/snapshot/delete", {"name": args["name"]})

    # --- Clipboard ---
    elif name == "copy_region":
        return await api_post("/api/clipboard/copy", {
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    elif name == "paste_region":
        body = {"x": args["x"], "y": args["y"], "z": args["z"]}
        if "rotate" in args:
            body["rotate"] = args["rotate"]
        return await api_post("/api/clipboard/paste", body)

    elif name == "get_clipboard":
        return await api_get("/api/clipboard")

    # --- World Generation ---
    elif name == "generate_world":
        body: dict[str, Any] = {"type": args["type"]}
        if "seed" in args:
            body["seed"] = args["seed"]
        if "chunks" in args:
            body["chunks"] = args["chunks"]
        if "from" in args:
            body["from"] = args["from"]
        if "to" in args:
            body["to"] = args["to"]
        if "params" in args:
            body["params"] = args["params"]
        return await api_post("/api/world/generate", body)

    # --- Template Save ---
    elif name == "save_template":
        return await api_post("/api/template/save", {
            "name": args["name"],
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    # --- NPC Management ---
    elif name == "spawn_npc":
        body: dict[str, Any] = {"name": args["name"]}
        if "animFile" in args:
            body["animFile"] = args["animFile"]
        if "position" in args:
            body["position"] = args["position"]
        if "behavior" in args:
            body["behavior"] = args["behavior"]
        if "waypoints" in args:
            body["waypoints"] = args["waypoints"]
        if "walkSpeed" in args:
            body["walkSpeed"] = args["walkSpeed"]
        if "waitTime" in args:
            body["waitTime"] = args["waitTime"]
        return await api_post("/api/npc/spawn", body)

    elif name == "remove_npc":
        return await api_post("/api/npc/remove", {"name": args["name"]})

    elif name == "list_npcs":
        return await api_get("/api/npcs")

    elif name == "set_npc_behavior":
        body = {"name": args["name"], "behavior": args["behavior"]}
        if "waypoints" in args:
            body["waypoints"] = args["waypoints"]
        if "walkSpeed" in args:
            body["walkSpeed"] = args["walkSpeed"]
        if "waitTime" in args:
            body["waitTime"] = args["waitTime"]
        return await api_post("/api/npc/behavior", body)

    # --- Dialogue & Speech Bubbles ---
    elif name == "set_npc_dialogue":
        return await api_post("/api/npc/dialogue", {
            "name": args["name"], "tree": args["tree"]
        })

    elif name == "start_dialogue":
        return await api_post("/api/dialogue/start", {
            "npc": args["npc"], "tree": args["tree"]
        })

    elif name == "end_dialogue":
        return await api_post("/api/dialogue/end", {})

    elif name == "say_bubble":
        body = {"entityId": args["entityId"], "text": args["text"]}
        if "duration" in args:
            body["duration"] = args["duration"]
        return await api_post("/api/speech/say", body)

    elif name == "get_dialogue_state":
        return await api_get("/api/dialogue/state")

    elif name == "advance_dialogue":
        return await api_post("/api/dialogue/advance", {})

    elif name == "select_dialogue_choice":
        return await api_post("/api/dialogue/choice", {"index": args["index"]})

    elif name == "load_dialogue_file":
        body: dict[str, Any] = {"filename": args["filename"]}
        if "npc" in args:
            body["npc"] = args["npc"]
        return await api_post("/api/dialogue/load", body)

    elif name == "list_dialogue_files":
        return await api_get("/api/dialogue/files")

    # --- Story System ---
    elif name == "story_get_state":
        return await api_get("/api/story/state")

    elif name == "story_list_characters":
        return await api_get("/api/story/characters")

    elif name == "story_get_character":
        return await api_get(f"/api/story/character/{args['id']}")

    elif name == "story_list_arcs":
        return await api_get("/api/story/arcs")

    elif name == "story_get_arc":
        return await api_get(f"/api/story/arc/{args['id']}")

    elif name == "story_get_world":
        return await api_get("/api/story/world")

    elif name == "story_load_world":
        return await api_post("/api/story/load", {"definition": args["definition"]})

    elif name == "story_add_character":
        return await api_post("/api/story/character/add", args)

    elif name == "story_remove_character":
        return await api_post("/api/story/character/remove", {"id": args["id"]})

    elif name == "story_trigger_event":
        body: dict[str, Any] = {"type": args["type"]}
        if "data" in args:
            body["data"] = args["data"]
        if "location" in args:
            body["location"] = args["location"]
        if "participants" in args:
            body["participants"] = args["participants"]
        return await api_post("/api/story/event", body)

    elif name == "story_add_arc":
        return await api_post("/api/story/arc/add", args)

    elif name == "story_set_variable":
        return await api_post("/api/story/variable", {
            "name": args["name"], "value": args["value"]
        })

    elif name == "story_set_agency":
        return await api_post("/api/story/agency", {
            "id": args["id"], "level": args["level"]
        })

    elif name == "story_add_knowledge":
        body = {"characterId": args["characterId"], "fact": args["fact"]}
        if "category" in args:
            body["category"] = args["category"]
        return await api_post("/api/story/knowledge", body)

    # --- Game Definition (AI Game Development) ---

    elif name == "load_game_definition":
        return await api_post("/api/game/load_definition", args)

    elif name == "export_game_definition":
        return await api_get("/api/game/export_definition")

    elif name == "validate_game_definition":
        definition = args.get("definition", args)
        return await api_post("/api/game/validate_definition", definition)

    elif name == "create_game_npc":
        return await api_post("/api/game/create_npc", args)

    # --- Project Lifecycle ---

    elif name == "build_project":
        return await _build_project(args)

    elif name == "launch_engine":
        return await _launch_engine(args)

    elif name == "engine_running":
        return await _check_engine_running()

    elif name == "package_game":
        return await _package_game(args)

    # --- In-Engine Project Build / Run ---

    elif name == "project_info":
        return await api_get("/api/project/info")

    elif name == "build_game":
        return await api_post("/api/project/build", args)

    elif name == "run_game":
        return await api_post("/api/project/run", args)

    # --- Async Job System ---

    elif name == "submit_job":
        return await api_post("/api/job/submit", {
            "type": args["type"],
            "params": args.get("params", {})
        })

    elif name == "get_job_status":
        job_id = args["job_id"]
        return await api_get(f"/api/job/{job_id}")

    elif name == "list_jobs":
        return await api_get("/api/jobs")

    elif name == "cancel_job":
        job_id = args["job_id"]
        return await api_post(f"/api/job/{job_id}/cancel", {})

    else:
        return {"error": f"Unknown tool: {name}"}


# ============================================================================
# Project Lifecycle Helpers
# ============================================================================

# Resolve project root (parent of scripts/mcp/)
PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent

# CMake path (MSVC 2022)
CMAKE_PATH = r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"


def _get_cmake():
    """Return cmake executable path."""
    if os.path.exists(CMAKE_PATH):
        return CMAKE_PATH
    # Fallback to PATH
    return "cmake"


async def _build_project(args: dict) -> dict:
    """Build the project using CMake."""
    config = args.get("config", "Debug")
    reconfigure = args.get("reconfigure", False)
    cmake = _get_cmake()
    build_dir = PROJECT_ROOT / "build"

    output_lines = []

    if reconfigure or not build_dir.exists():
        try:
            result = subprocess.run(
                [cmake, "-B", "build", "-S", "."],
                cwd=str(PROJECT_ROOT),
                capture_output=True, text=True, timeout=120
            )
            output_lines.append("=== CMake Configure ===")
            if result.stdout:
                output_lines.append(result.stdout[-2000:])
            if result.returncode != 0:
                output_lines.append(f"Configure failed (exit {result.returncode})")
                if result.stderr:
                    output_lines.append(result.stderr[-2000:])
                return {"success": False, "output": "\n".join(output_lines)}
        except subprocess.TimeoutExpired:
            return {"success": False, "error": "CMake configure timed out"}

    try:
        result = subprocess.run(
            [cmake, "--build", "build", "--config", config],
            cwd=str(PROJECT_ROOT),
            capture_output=True, text=True, timeout=300
        )
        output_lines.append(f"=== CMake Build ({config}) ===")
        if result.stdout:
            # Trim to last 3000 chars to avoid huge output
            output_lines.append(result.stdout[-3000:])
        if result.returncode != 0:
            output_lines.append(f"Build failed (exit {result.returncode})")
            if result.stderr:
                output_lines.append(result.stderr[-2000:])
            return {"success": False, "output": "\n".join(output_lines)}
        return {"success": True, "output": "\n".join(output_lines)}
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "CMake build timed out (300s)"}


_engine_process = None


async def _launch_engine(args: dict) -> dict:
    """Launch the engine executable as a background process."""
    global _engine_process
    config = args.get("config", "Debug")
    extra_args = args.get("args", [])

    # Check if already running
    if _engine_process and _engine_process.poll() is None:
        return {"success": True, "message": "Engine already running", "pid": _engine_process.pid}

    exe_path = PROJECT_ROOT / "build" / "game" / config / "phyxel.exe"
    if not exe_path.exists():
        # Try root copy
        exe_path = PROJECT_ROOT / "phyxel.exe"
    if not exe_path.exists():
        return {"error": f"Engine executable not found. Build the project first."}

    try:
        _engine_process = subprocess.Popen(
            [str(exe_path)] + extra_args,
            cwd=str(PROJECT_ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        return {"success": True, "pid": _engine_process.pid, "executable": str(exe_path)}
    except Exception as e:
        return {"error": f"Failed to launch engine: {e}"}


async def _check_engine_running() -> dict:
    """Check if the engine is running and responsive."""
    global _engine_process

    process_alive = _engine_process is not None and _engine_process.poll() is None

    try:
        result = await api_get("/api/status")
        api_ok = "error" not in result
    except Exception:
        api_ok = False

    return {
        "process_alive": process_alive,
        "api_responsive": api_ok,
        "pid": _engine_process.pid if process_alive else None
    }


# ============================================================================
# Game Packaging
# ============================================================================

async def _package_game(args: dict) -> dict:
    """Package a game into a standalone distributable directory."""
    name = args.get("name", "MyGame")
    definition = args.get("definition")
    output_path = args.get("output")
    config = args.get("config", "Debug")
    all_resources = args.get("allResources", False)
    include_mcp = args.get("includeMcp", False)
    title = args.get("title")
    project_dir_path = args.get("projectDir")
    prebake = args.get("prebakeWorld", False)

    # If no definition provided, export from running engine
    if not definition:
        try:
            result = await api_get("/api/game/export_definition")
            if "error" in result:
                return {"error": f"Cannot export from engine: {result['error']}"}
            definition = result
        except Exception as e:
            return {"error": f"Engine not running and no definition provided: {e}"}

    # Import the packaging tool
    import importlib.util
    pkg_script = PROJECT_ROOT / "tools" / "package_game.py"
    if not pkg_script.exists():
        return {"error": "tools/package_game.py not found"}

    spec = importlib.util.spec_from_file_location("package_game", str(pkg_script))
    pkg_mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pkg_mod)

    if output_path:
        output_dir = Path(output_path)
    else:
        docs = Path(os.environ.get("USERPROFILE", os.path.expanduser("~"))) / "Documents" / "PhyxelProjects" / name
        output_dir = docs
    output_dir = output_dir.resolve()

    # Pre-bake world if requested
    world_db_path = None
    if prebake:
        try:
            world_db_path = pkg_mod.prebake_world(ENGINE_API_URL)
        except Exception as e:
            return {"error": f"Failed to pre-bake world: {e}"}

    project_dir = Path(project_dir_path).resolve() if project_dir_path else None

    result = pkg_mod.package_game(
        name=name,
        output_dir=output_dir,
        definition=definition,
        config=config,
        include_all_resources=all_resources,
        include_mcp=include_mcp,
        window_title=title,
        project_dir=project_dir,
        world_db_path=world_db_path,
    )

    return result


# ============================================================================
# Entry Point
# ============================================================================

async def main():
    logger.info(f"Starting Phyxel MCP server (engine API: {ENGINE_API_URL})")
    async with stdio_server() as (read_stream, write_stream):
        init_options = server.create_initialization_options()
        await server.run(read_stream, write_stream, init_options)


if __name__ == "__main__":
    import asyncio
    asyncio.run(main())
