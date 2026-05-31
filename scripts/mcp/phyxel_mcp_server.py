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
import asyncio
import random
import httpx
from typing import Any
from pathlib import Path

# MCP SDK imports
from mcp.server import Server
from mcp.server.stdio import stdio_server
import base64
from mcp.types import Tool, TextContent, ImageContent

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


async def api_post(path: str, body: dict, timeout: float | None = None) -> dict:
    """POST request to the engine API. Returns parsed JSON."""
    try:
        resp = await http_client.post(path, json=body, timeout=timeout)
        resp.raise_for_status()
        return resp.json()
    except httpx.ConnectError:
        return {"error": f"Engine not running. Start phyxel.exe first. (tried {ENGINE_API_URL}{path})"}
    except Exception as e:
        return {"error": str(e)}


ASYNC_POLL_INTERVAL = 1.0    # seconds between polls
ASYNC_POLL_TIMEOUT  = 300.0  # max seconds to wait for async result


async def api_post_async(path: str, body: dict) -> dict:
    """POST to an async endpoint and poll until the result is ready.

    The engine returns {"status": "accepted", "async_id": "..."} immediately.
    We then poll GET /api/async/:id until status becomes "complete".
    """
    initial = await api_post(path, body)
    if "error" in initial:
        return initial
    if initial.get("status") != "accepted" or "async_id" not in initial:
        # Not an async response — return as-is (backwards compat)
        return initial

    async_id = initial["async_id"]
    elapsed = 0.0
    while elapsed < ASYNC_POLL_TIMEOUT:
        await asyncio.sleep(ASYNC_POLL_INTERVAL)
        elapsed += ASYNC_POLL_INTERVAL
        poll = await api_get(f"/api/async/{async_id}")
        if "error" in poll:
            return poll
        if poll.get("status") == "complete":
            return poll
        # Still processing — continue polling
    return {"error": f"Async operation timed out after {ASYNC_POLL_TIMEOUT}s", "async_id": async_id}


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
            description="Spawn a new game entity (character) in the world. Types: 'physics' (humanoid with physics), 'spider' (spider enemy), 'animated' (animated voxel character), 'active' (kinematic capsule + procedural skeleton), 'hybrid' (keyframed animation + procedural IK corrections).",
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {
                        "type": "string",
                        "description": "Entity type to spawn",
                        "enum": ["physics", "spider", "animated", "active", "hybrid"]
                    },
                    "x": {"type": "number", "description": "Spawn X coordinate"},
                    "y": {"type": "number", "description": "Spawn Y coordinate"},
                    "z": {"type": "number", "description": "Spawn Z coordinate"},
                    "id": {"type": "string", "description": "Optional ID to assign (auto-generated if omitted)"},
                    "animFile": {"type": "string", "description": "Animation file for 'animated' type (default: resources/animated_characters/humanoid.anim)"}
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
            description="Spawn a pre-built voxel object template at a world position. Use list_templates first to see available templates. Optionally rotate 0/90/180/270 degrees clockwise around Y axis.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Template name (e.g., 'castle', 'tree')"},
                    "x": {"type": "number", "description": "X position"},
                    "y": {"type": "number", "description": "Y position"},
                    "z": {"type": "number", "description": "Z position"},
                    "static": {"type": "boolean", "description": "If true, merges into terrain. If false, creates physics objects.", "default": True},
                    "rotation": {"type": "integer", "description": "Rotation in degrees: 0, 90, 180, or 270 (clockwise around Y)", "default": 0},
                    "parent_id": {"type": "string", "description": "Optional parent placed object ID (e.g. 'tavern_1') to establish ownership hierarchy"}
                },
                "required": ["name", "x", "y", "z"]
            }
        ),

        # ================================================================
        # Camera Control
        # ================================================================
        Tool(
            name="build_structure",
            description="Build a procedural structure (house, tavern, tower, wall, staircase, furniture). Returns placed voxel count and auto-registered locations.",
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {"type": "string", "description": "Structure type: house, tavern, tower, wall, room, box, staircase, subcube_staircase, table, chair, counter, bed, window_frame, door_frame, railing, half_wall, pitched_roof"},
                    "position": {"type": "object", "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "z": {"type": "integer"}}, "required": ["x", "y", "z"]},
                    "width": {"type": "integer", "description": "Width (X axis)"},
                    "depth": {"type": "integer", "description": "Depth (Z axis)"},
                    "height": {"type": "integer", "description": "Height (Y axis)"},
                    "stories": {"type": "integer", "description": "Number of stories (tavern)"},
                    "radius": {"type": "integer", "description": "Radius (tower)"},
                    "length": {"type": "integer", "description": "Length (counter, railing, half_wall)"},
                    "material": {"type": "string", "description": "Single material name"},
                    "materials": {"type": "object", "description": "Material palette: {wall, floor, roof, stairs, furniture}", "properties": {
                        "wall": {"type": "string"}, "floor": {"type": "string"}, "roof": {"type": "string"},
                        "stairs": {"type": "string"}, "furniture": {"type": "string"}
                    }},
                    "facing": {"type": "string", "description": "Door direction: north, east, south, west", "default": "south"},
                    "windows": {"type": "integer", "description": "Number of windows per wall (house)"},
                    "furnished": {"type": "boolean", "description": "Include furniture (house/tavern)", "default": True},
                    "hollow": {"type": "boolean", "description": "Hollow box", "default": False},
                    "end": {"type": "object", "description": "End point for wall segment", "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "z": {"type": "integer"}}},
                    "thickness": {"type": "integer", "description": "Wall thickness"},
                    "detail_level": {"type": "string", "description": "Detail level: rough (cube-only), detailed (cube+subcube trim), fine (future). Default: detailed", "default": "detailed", "enum": ["rough", "detailed", "fine"]},
                    "parent_id": {"type": "string", "description": "Optional parent placed object ID to establish ownership hierarchy"}
                },
                "required": ["type", "position"]
            }
        ),
        Tool(
            name="list_structure_types",
            description="List all available procedural structure types with their parameters and defaults.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # Doors (Kinematic Voxel Objects)
        # ================================================================
        Tool(
            name="register_door",
            description="Register a placed template object as an animated door. The template's static voxels are removed and replaced by a kinematic object that can swing open/closed around a Y-axis hinge. Place the template first with spawn_template, then register it as a door.",
            inputSchema={
                "type": "object",
                "properties": {
                    "placed_object_id": {"type": "string", "description": "ID of the placed object (returned by spawn_template)"},
                    "template_name": {"type": "string", "description": "Template name used for voxel data (e.g. 'door_wood')"},
                    "base_rotation": {"type": "integer", "description": "Placement rotation 0/90/180/270 degrees", "default": 0},
                    "open_angle": {"type": "number", "description": "Degrees of Y-axis rotation when fully open", "default": 90.0},
                    "swing_speed": {"type": "number", "description": "Animation speed in degrees per second", "default": 120.0},
                    "hinge": {"type": "object", "description": "World position of hinge (defaults to placed object origin)", "properties": {
                        "x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}
                    }},
                    "thickness": {"type": "integer", "description": "Door thickness in microcubes (1-16, default 16 = full cube). 2 = thin door.", "default": 16}
                },
                "required": ["placed_object_id", "template_name"]
            }
        ),
        Tool(
            name="toggle_door",
            description="Toggle a registered door open or closed. The door will animate smoothly to the opposite state.",
            inputSchema={
                "type": "object",
                "properties": {
                    "placed_object_id": {"type": "string", "description": "ID of the door's placed object"}
                },
                "required": ["placed_object_id"]
            }
        ),
        Tool(
            name="open_door",
            description="Open a registered door (no effect if already open).",
            inputSchema={
                "type": "object",
                "properties": {
                    "placed_object_id": {"type": "string", "description": "ID of the door's placed object"}
                },
                "required": ["placed_object_id"]
            }
        ),
        Tool(
            name="close_door",
            description="Close a registered door (no effect if already closed).",
            inputSchema={
                "type": "object",
                "properties": {
                    "placed_object_id": {"type": "string", "description": "ID of the door's placed object"}
                },
                "required": ["placed_object_id"]
            }
        ),
        Tool(
            name="list_doors",
            description="List all registered doors with their current state (open/closed, angle, locked, hinge position).",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="set_door_lock",
            description="Lock or unlock a door. Locked doors cannot be opened until unlocked. Optionally require a specific item key.",
            inputSchema={
                "type": "object",
                "properties": {
                    "placed_object_id": {"type": "string", "description": "ID of the door's placed object"},
                    "locked": {"type": "boolean", "description": "True to lock, false to unlock"},
                    "key_item_id": {"type": "string", "description": "Item ID required to unlock (empty = no key needed)", "default": ""}
                },
                "required": ["placed_object_id", "locked"]
            }
        ),
        Tool(
            name="unregister_door",
            description="Unregister a door, destroying its kinematic object. The voxels are NOT restored to the chunk.",
            inputSchema={
                "type": "object",
                "properties": {
                    "placed_object_id": {"type": "string", "description": "ID of the door's placed object"}
                },
                "required": ["placed_object_id"]
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
        # Material Management (runtime add/remove/save)
        # ================================================================
        Tool(
            name="add_material",
            description="Add a new material to the engine at runtime. Creates texture filename slots automatically. Use reload_atlas after adding to rebuild the texture atlas.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"description": "Unique material name (case-sensitive)", "type": "string"},
                    "emissive": {"description": "Whether the material glows (default false)", "type": "boolean"},
                    "physics": {
                        "description": "Physics properties (omit for system-only materials)",
                        "type": "object",
                        "properties": {
                            "mass": {"type": "number", "description": "Mass (default 1.0)"},
                            "friction": {"type": "number", "description": "Friction (default 0.5)"},
                            "restitution": {"type": "number", "description": "Bounciness (default 0.3)"}
                        }
                    }
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="remove_material",
            description="Remove a material from the engine. Does not delete texture files. Use reload_atlas after removing.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"description": "Material name to remove", "type": "string"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="save_materials",
            description="Save the current material definitions to materials.json on disk.",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {"description": "Output path (default: resources/materials.json)", "type": "string"}
                },
                "required": []
            }
        ),
        Tool(
            name="reload_atlas",
            description="Hot-reload the texture atlas from source PNGs. Rebuilds the atlas, uploads to GPU, and updates shader UV data. Call after modifying textures or adding/removing materials.",
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
            description="Capture a screenshot of the current game view. Returns the PNG image inline (Claude can see it directly) plus path and dimensions.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        Tool(
            name="get_visual_diagnostic",
            description=(
                "Capture a full visual diagnostic: screenshot (embedded for Claude to see), camera state, "
                "entity list, and optionally a debug overlay. Use this to diagnose visual bugs — "
                "rendering artifacts, lighting issues, wrong normals, reflection problems, animation pose errors. "
                "The 'overlays' parameter activates a specific debug visualization before capturing:\n"
                "  'normals'   — draws face normals as colored lines (diagnose winding order, flipped faces)\n"
                "  'wireframe' — wireframe rendering (diagnose geometry, culled faces)\n"
                "  'uv'        — UV coordinate visualization (diagnose texture mapping)\n"
                "  'emissive'  — emissive channel only (diagnose glow/emission)\n"
                "  'hierarchy' — chunk/object hierarchy coloring\n"
                "The overlay is automatically restored to its previous state after capture."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "overlay": {
                        "type": "string",
                        "enum": ["none", "normals", "wireframe", "uv", "emissive", "hierarchy"],
                        "description": "Debug overlay to activate before capturing (default: none)",
                        "default": "none"
                    }
                },
                "required": []
            }
        ),

        Tool(
            name="orbit_screenshots",
            description=(
                "Capture up to 6 screenshots orbiting a world position — north, south, east, west, top, and iso (diagonal). "
                "Use this to visually confirm a newly spawned asset looks correct from all angles. "
                "All images are embedded so Claude can see them directly. "
                "Pass the asset's world-space center (x, y, z) and its approximate radius in voxels. "
                "The camera is automatically positioned at each angle and the scene is rendered before capture. "
                "Optionally restrict to a subset of views with the 'views' parameter."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "x":      {"type": "number", "description": "World X of the asset center"},
                    "y":      {"type": "number", "description": "World Y of the asset center"},
                    "z":      {"type": "number", "description": "World Z of the asset center"},
                    "radius": {"type": "number", "description": "Approximate asset radius in voxels (default: 4)"},
                    "views":  {
                        "type": "array",
                        "items": {"type": "string", "enum": ["north", "south", "east", "west", "top", "iso"]},
                        "description": "Subset of views to capture (default: all 6)"
                    },
                    "port":   {"type": "integer", "description": "Engine API port (default: 8090 for game, 8091 for asset editor)"}
                },
                "required": ["x", "y", "z"]
            }
        ),

        Tool(
            name="set_debug_overlay",
            description=(
                "Enable or disable a debug visualization overlay on the rendered scene. "
                "Modes: normals (face normal vectors), wireframe (geometry edges), "
                "uv (texture coordinate visualization), emissive (emission channel), hierarchy (object tree). "
                "Use 'none' to turn off all overlays. Changes persist until changed again or engine restarts."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "overlay": {
                        "type": "string",
                        "enum": ["none", "normals", "wireframe", "uv", "emissive", "hierarchy"],
                        "description": "Overlay mode to activate, or 'none' to disable"
                    }
                },
                "required": ["overlay"]
            }
        ),

        Tool(
            name="get_engine_logs",
            description=(
                "Read recent lines from the engine log file (phyxel.log). "
                "Use after screenshots or after a visual bug appears to correlate what the engine was doing. "
                "Filter by module (e.g. 'RenderCoordinator', 'Vulkan', 'Physics') and/or level "
                "('trace','debug','info','warn','error') to focus on relevant output. "
                "Returns the most recent N lines matching the filter."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "lines":  {"type": "integer", "description": "Max lines to return (default 50)", "default": 50},
                    "module": {"type": "string",  "description": "Filter to lines containing this module name (case-insensitive)"},
                    "level":  {"type": "string",  "description": "Filter to lines at this level or above (trace/debug/info/warn/error)"}
                },
                "required": []
            }
        ),

        Tool(
            name="set_log_level",
            description=(
                "Set the runtime log level for a specific engine module or globally. "
                "Use 'debug' or 'trace' on a module to get verbose output for that system. "
                "Then call get_engine_logs to read the output. "
                "Key modules: RenderCoordinator, Vulkan, Physics, Application, ChunkManager, NPC. "
                "Use module='global' to change all modules at once."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "module": {"type": "string", "description": "Module name or 'global' for all modules"},
                    "level":  {"type": "string", "enum": ["trace", "debug", "info", "warn", "error", "off"],
                               "description": "Log level to set"}
                },
                "required": ["module", "level"]
            }
        ),

        Tool(
            name="get_render_stats",
            description=(
                "Get last-frame rendering statistics. Shows whether each render pass ran, "
                "how many draw calls each pass made, and mirror-specific data (plane position, "
                "normal, reflected camera position). Use this to immediately answer questions like "
                "'did the reflection pass run?', 'how many chunks were visible?', "
                "'where is the reflected camera?'. Essential for debugging rendering bugs."
            ),
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
        # Scene Management
        # ================================================================
        Tool(
            name="list_scenes",
            description="List all scenes in the loaded scene manifest. Returns scene IDs, names, active status, and whether a transition is in progress.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="get_active_scene",
            description="Get detailed information about the currently active scene including ID, name, world database, state, and re-entry data.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="transition_scene",
            description="Transition to a different scene. Unloads the current scene (saving dirty chunks, clearing entities/NPCs) and loads the target scene. Player health/inventory/story persist across transitions.",
            inputSchema={
                "type": "object",
                "properties": {
                    "scene_id": {"type": "string", "description": "ID of the scene to transition to"}
                },
                "required": ["scene_id"]
            }
        ),
        Tool(
            name="add_scene",
            description="Add a new scene definition to the manifest at runtime. Use sceneType='menu' for 2D menu scenes (no world DB needed), 'world' for voxel world scenes, 'cutscene' for non-interactive cinematics.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Unique scene identifier"},
                    "name": {"type": "string", "description": "Display name for the scene"},
                    "sceneType": {"type": "string", "enum": ["world", "menu", "cutscene"], "description": "Type of scene (default: world)"},
                    "worldDatabase": {"type": "string", "description": "SQLite database filename (e.g. 'dungeon.db') — world scenes only"},
                    "menuLayout": {"type": "object", "description": "Menu layout JSON for menu-type scenes (title, anchor, size, children array)"},
                    "world": {"type": "object", "description": "World generation config (type, seed, etc.) — world scenes only"},
                    "structures": {"type": "array", "description": "Structure definitions to build"},
                    "npcs": {"type": "array", "description": "NPC definitions to spawn"},
                    "camera": {"type": "object", "description": "Camera position/orientation"},
                    "player": {"type": "object", "description": "Player spawn config"},
                    "transitionStyle": {"type": "string", "enum": ["cut", "fade", "loading_screen"], "description": "Transition animation style"},
                    "onEnterScript": {"type": "string", "description": "Python script path to run when entering this scene"},
                    "onExitScript": {"type": "string", "description": "Python script path to run when leaving this scene"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="get_scene",
            description="Get the full definition of a specific scene by ID, including menuLayout for menu scenes.",
            inputSchema={
                "type": "object",
                "properties": {
                    "scene_id": {"type": "string", "description": "ID of the scene to retrieve"}
                },
                "required": ["scene_id"]
            }
        ),
        Tool(
            name="update_scene",
            description="Update fields of an existing scene definition. Use this to rename a scene, change its type, set a menu layout, update scripts, or change its world database. Only provided fields are changed.",
            inputSchema={
                "type": "object",
                "properties": {
                    "scene_id": {"type": "string", "description": "ID of the scene to update"},
                    "name": {"type": "string", "description": "New display name"},
                    "description": {"type": "string", "description": "Scene description"},
                    "sceneType": {"type": "string", "enum": ["world", "menu", "cutscene"], "description": "Change the scene type"},
                    "worldDatabase": {"type": "string", "description": "SQLite DB filename (world scenes)"},
                    "menuLayout": {"type": "object", "description": "Full menu layout JSON replacing any existing layout"},
                    "onEnterScript": {"type": "string", "description": "Python script path to run on scene enter"},
                    "onExitScript": {"type": "string", "description": "Python script path to run on scene exit"},
                    "transitionStyle": {"type": "string", "enum": ["cut", "fade", "loading_screen"]}
                },
                "required": ["scene_id"]
            }
        ),
        Tool(
            name="remove_scene",
            description="Remove a scene definition from the manifest. Cannot remove the currently active scene.",
            inputSchema={
                "type": "object",
                "properties": {
                    "scene_id": {"type": "string", "description": "ID of the scene to remove"}
                },
                "required": ["scene_id"]
            }
        ),
        Tool(
            name="save_scene_manifest",
            description="Save the current scene manifest (all scene definitions) to a JSON file. Defaults to 'game.json'.",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Output file path (default: 'game.json')"}
                },
                "required": []
            }
        ),

        # ================================================================
        # Game Menu Element Control
        # ================================================================
        Tool(
            name="get_menu_element",
            description="Get the JSON definition of a named element in the currently-active menu scene layout. Returns the element's type, position, size, text, color, and action.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Element ID to retrieve"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="set_menu_element",
            description="Override properties of a named element in the active game menu. You can change visibility, text, and background color. Changes are applied immediately on the next rendered frame.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Element ID to modify"},
                    "visible": {"type": "boolean", "description": "Show or hide the element"},
                    "text": {"type": "string", "description": "New text for labels or buttons"},
                    "color": {
                        "type": "array", "items": {"type": "number"},
                        "description": "RGBA background color [r, g, b, a] in 0..1 range",
                        "minItems": 4, "maxItems": 4
                    }
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="add_menu_element",
            description="Add a new element (button, label, image, panel) to a panel in the active game menu layout.",
            inputSchema={
                "type": "object",
                "properties": {
                    "element": {
                        "type": "object",
                        "description": "Element definition. Required: type (button/label/image/panel), id, position [x,y], size [w,h]. Optional: text, font, text_color, color, color_hover, image, tint, animation, animation_delay, action {type, target}."
                    },
                    "panel_id": {"type": "string", "description": "Panel key to add into (e.g. 'main', 'options'). Defaults to the current panel."}
                },
                "required": ["element"]
            }
        ),
        Tool(
            name="remove_menu_element",
            description="Remove a named element from the active game menu layout.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Element ID to remove"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="open_menu_submenu",
            description="Push a named panel onto the menu navigation stack, showing it as the active submenu. Animations reset.",
            inputSchema={
                "type": "object",
                "properties": {
                    "panel_id": {"type": "string", "description": "Key of the panel in the layout's 'panels' object to show"}
                },
                "required": ["panel_id"]
            }
        ),
        Tool(
            name="close_menu_submenu",
            description="Pop the current submenu from the navigation stack, returning to the previous panel.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
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
        # Undo / Redo
        # ================================================================
        Tool(
            name="undo",
            description="Undo the last destructive voxel operation (fill_region, clear_region, spawn_template, build_structure, place_voxels_batch). Automatically captures state before each operation. Supports up to 20 levels of undo. Each undo pushes the current state to the redo stack.",
            inputSchema={
                "type": "object",
                "properties": {},
            }
        ),
        Tool(
            name="redo",
            description="Redo the last undone operation. Only available after an undo. Any new destructive operation clears the redo stack.",
            inputSchema={
                "type": "object",
                "properties": {},
            }
        ),
        Tool(
            name="get_undo_status",
            description="Get the current undo/redo stack status: depth, whether undo/redo is available, and a list of operations in each stack.",
            inputSchema={
                "type": "object",
                "properties": {},
            }
        ),

        # ================================================================
        # Placed Objects (furniture / structure tracking)
        # ================================================================
        Tool(
            name="list_placed_objects",
            description="List all placed objects (furniture, structures) tracked by the PlacedObjectManager. Returns id, template name, category, position, rotation, and bounding box for each.",
            inputSchema={
                "type": "object",
                "properties": {},
            }
        ),
        Tool(
            name="get_placed_object",
            description="Get detailed info about a specific placed object by its ID.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Placed object ID (e.g. 'test_chair_3')"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="remove_placed_object",
            description="Remove a placed object from the world. Clears all voxels in its bounding box and deletes the registry entry. Supports undo.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Placed object ID to remove"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="move_placed_object",
            description="Move a placed object to a new position. Clears voxels at old location and re-places the template at the new position. Only works for template-based objects (not structures).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Placed object ID to move"},
                    "position": {
                        "type": "object",
                        "description": "New world position",
                        "properties": {
                            "x": {"type": "integer"},
                            "y": {"type": "integer"},
                            "z": {"type": "integer"}
                        },
                        "required": ["x", "y", "z"]
                    }
                },
                "required": ["id", "position"]
            }
        ),
        Tool(
            name="rotate_placed_object",
            description="Rotate a placed object to a new rotation. Clears voxels and re-places with new rotation. Only works for template-based objects. Rotation: 0, 90, 180, 270 degrees.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Placed object ID to rotate"},
                    "rotation": {"type": "integer", "description": "New rotation in degrees (0, 90, 180, 270)"}
                },
                "required": ["id", "rotation"]
            }
        ),
        Tool(
            name="get_objects_at",
            description="Find all placed objects whose bounding box contains a given world position.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "World X coordinate"},
                    "y": {"type": "integer", "description": "World Y coordinate"},
                    "z": {"type": "integer", "description": "World Z coordinate"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="set_object_parent",
            description="Set or change the parent of a placed object, establishing an ownership hierarchy (e.g. chair belongs to tavern). Set parent_id to empty string to make it a root object. Prevents circular references.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Placed object ID to re-parent"},
                    "parent_id": {"type": "string", "description": "New parent object ID, or empty string to make root-level"}
                },
                "required": ["id", "parent_id"]
            }
        ),
        Tool(
            name="get_object_children",
            description="Get all direct children of a placed object. Pass empty id to get all root-level objects (objects with no parent).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Parent object ID (empty string for root-level objects)", "default": ""}
                },
            }
        ),
        Tool(
            name="get_object_tree",
            description="Get the full ownership tree rooted at an object, including all nested descendants. Returns a JSON tree with 'children' arrays at each level.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Root object ID for the tree"}
                },
                "required": ["id"]
            }
        ),

        # ================================================================
        # Dynamic Furniture
        # ================================================================
        Tool(
            name="activate_furniture",
            description="Activate a placed object as dynamic furniture. Removes static voxels from chunks and creates a physics-driven compound rigid body that can slide, tumble, and be knocked around. Optionally apply an impulse.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Placed object ID to activate"},
                    "impulse_x": {"type": "number", "description": "Impulse X component (default 0)"},
                    "impulse_y": {"type": "number", "description": "Impulse Y component (default 0)"},
                    "impulse_z": {"type": "number", "description": "Impulse Z component (default 0)"},
                    "contact_x": {"type": "number", "description": "Contact point X (default 0)"},
                    "contact_y": {"type": "number", "description": "Contact point Y (default 0)"},
                    "contact_z": {"type": "number", "description": "Contact point Z (default 0)"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="deactivate_furniture",
            description="Deactivate dynamic furniture back to static voxels. Places the template back into chunks at the current physics position (quantized to grid), or at the original position if restore_original is true.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Placed object ID to deactivate"},
                    "restore_original": {"type": "boolean", "description": "If true, restore at original position instead of current (default false)"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="list_dynamic_furniture",
            description="List all currently active dynamic furniture objects with their physics state (position, mass, sleep timer, grab state).",
            inputSchema={
                "type": "object",
                "properties": {},
            }
        ),
        Tool(
            name="shatter_furniture",
            description="Shatter an active dynamic furniture object into fragments. Large fragments (>= 4 voxels) become new compound rigid bodies; small fragments become GPU particle debris. Requires the object to be active (use activate_furniture first).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Placed object ID of active dynamic furniture"},
                    "force": {"type": "number", "description": "Impact force magnitude (default 100.0, must exceed bondStrength * 50)"},
                    "contact_x": {"type": "number", "description": "Contact point X (world space)"},
                    "contact_y": {"type": "number", "description": "Contact point Y (world space)"},
                    "contact_z": {"type": "number", "description": "Contact point Z (world space)"}
                },
                "required": ["id"]
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
        Tool(
            name="move_region",
            description="Move a voxel region to a new position. Captures all cubes/subcubes/microcubes, clears the source, optionally rotates, then places at the destination. Max 100k voxels. Great for moving furniture or objects.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x1": {"type": "integer", "description": "Source region first corner X"},
                    "y1": {"type": "integer", "description": "Source region first corner Y"},
                    "z1": {"type": "integer", "description": "Source region first corner Z"},
                    "x2": {"type": "integer", "description": "Source region opposite corner X"},
                    "y2": {"type": "integer", "description": "Source region opposite corner Y"},
                    "z2": {"type": "integer", "description": "Source region opposite corner Z"},
                    "dx": {"type": "integer", "description": "Destination X (new min corner)"},
                    "dy": {"type": "integer", "description": "Destination Y (new min corner)"},
                    "dz": {"type": "integer", "description": "Destination Z (new min corner)"},
                    "rotate": {"type": "integer", "description": "Rotation in degrees: 0, 90, 180, or 270 (clockwise around Y)", "default": 0}
                },
                "required": ["x1", "y1", "z1", "x2", "y2", "z2", "dx", "dy", "dz"]
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
        # Template Generation (BlockSmith AI)
        # ================================================================
        Tool(
            name="generate_template",
            description="Generate a voxel template from a text description using BlockSmith (LLM-powered). Creates a .voxel template file that can be spawned with spawn_template. If the template already exists, returns the cached version (use force=true to regenerate). Requires ANTHROPIC_API_KEY or PHYXEL_AI_API_KEY.",
            inputSchema={
                "type": "object",
                "properties": {
                    "prompt": {"type": "string", "description": "Text description of the object to generate (e.g., 'a wooden chair', 'medieval torch', 'tavern bar counter')"},
                    "name": {"type": "string", "description": "Template name (used as filename, no extension)"},
                    "material": {"type": "string", "description": "Default material for the model", "default": "Wood"},
                    "size": {"type": "number", "description": "Target size in world cubes (1.0 = one block)", "default": 3.0},
                    "force": {"type": "boolean", "description": "Force regeneration even if template already exists", "default": False},
                    "native": {"type": "boolean", "description": "Use native Phyxel C/S/M generation (no bbmodel/trimesh pipeline). Enables per-part materials and subcube detail.", "default": False},
                    "image": {"type": "string", "description": "Reference image (local path or URL). Enables prompt enhancement automatically."},
                    "enhance_prompt": {"type": "boolean", "description": "Run a pre-pass LLM call to expand the prompt into a detailed spatial breakdown before generation.", "default": False},
                    "auto_inspect": {"type": "boolean", "description": "After generation, automatically take multi-angle screenshots with the asset editor and return them inline.", "default": False}
                },
                "required": ["prompt", "name"]
            }
        ),
        Tool(
            name="search_templates",
            description="Search the generated template catalog by name or prompt keyword. Returns matching templates with their metadata (prompt, material, size, primitive counts). Only searches AI-generated templates, not hand-crafted ones.",
            inputSchema={
                "type": "object",
                "properties": {
                    "query": {"type": "string", "description": "Search term to match against template names and generation prompts"}
                },
                "required": ["query"]
            }
        ),
        Tool(
            name="list_generated_templates",
            description="List all AI-generated templates from the catalog with their prompts, materials, sizes, and primitive counts.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        Tool(
            name="build_building",
            description="Generate an LLM-designed building using BlockSmith and spawn it in the world. "
                        "Produces multi-material structures (different materials for walls, floor, roof) with "
                        "door/window openings. Much higher visual quality than build_structure for buildings. "
                        "Buildings are cached — identical parameters return the cached template instantly. "
                        "Requires ANTHROPIC_API_KEY or PHYXEL_AI_API_KEY.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Template name for caching (e.g. 'tavern_medieval')"},
                    "position": {
                        "type": "object",
                        "description": "World position to place the building",
                        "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "z": {"type": "integer"}},
                        "required": ["x", "y", "z"]
                    },
                    "building_type": {"type": "string", "description": "Building type: house, tavern, tower, shop, temple, barn", "default": "house"},
                    "style": {"type": "string", "description": "Architectural style (medieval, elven, dwarven, gothic, rustic)", "default": "medieval"},
                    "width": {"type": "integer", "description": "Width in blocks (X axis)", "default": 10},
                    "depth": {"type": "integer", "description": "Depth in blocks (Z axis)", "default": 12},
                    "height": {"type": "integer", "description": "Interior height per story in blocks", "default": 4},
                    "stories": {"type": "integer", "description": "Number of stories", "default": 1},
                    "door_facing": {"type": "string", "description": "Which wall has the door: front, back, left, right", "default": "front"},
                    "windows": {"type": "integer", "description": "Windows per side wall per story", "default": 2},
                    "materials": {
                        "type": "object",
                        "description": "Material palette: {wall, floor, roof, trim, accent}",
                        "properties": {
                            "wall": {"type": "string"}, "floor": {"type": "string"},
                            "roof": {"type": "string"}, "trim": {"type": "string"},
                            "accent": {"type": "string"}
                        }
                    },
                    "rotation": {"type": "integer", "description": "Y-axis rotation: 0, 90, 180, 270", "default": 0, "enum": [0, 90, 180, 270]},
                    "extra_notes": {"type": "string", "description": "Additional creative direction for the LLM"},
                    "force": {"type": "boolean", "description": "Force regeneration of cached template", "default": False},
                },
                "required": ["name", "position"]
            }
        ),

        # ================================================================
        # Template Creation
        # ================================================================
        Tool(
            name="save_template",
            description="Save a voxel region as a reusable .voxel template file. Scans all cubes in the bounding box, converts to relative coordinates, and writes to resources/templates/<name>.voxel. The template is immediately available for spawn_template. Max 100k voxels.",
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
                    "animFile": {"type": "string", "description": "Animation file (default: resources/animated_characters/humanoid.anim)", "default": "resources/animated_characters/humanoid.anim"},
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
        Tool(
            name="get_npc_appearance",
            description="Get the current appearance (colors and proportions) of an NPC. Returns heightScale, bulkScale, headScale, armLengthScale, legLengthScale, torsoLengthScale, shoulderWidthScale, and color values.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="set_npc_appearance",
            description=(
                "Set or update appearance (colors and proportions) of an existing NPC. "
                "Only provided fields are changed; others keep their current values. "
                "Proportion changes rebuild the character's skeleton in real-time. "
                "Fields: heightScale (0.4-1.6), bulkScale (0.5-1.8), headScale (0.6-1.6), "
                "armLengthScale (0.5-1.5), legLengthScale (0.5-1.5), torsoLengthScale (0.6-1.5), "
                "shoulderWidthScale (0.5-1.6), "
                "skinColor/torsoColor/armColor/legColor ({r,g,b,a} 0.0-1.0)."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"},
                    "appearance": {
                        "type": "object",
                        "description": "Partial appearance object — only include fields you want to change",
                        "properties": {
                            "heightScale": {"type": "number"},
                            "bulkScale": {"type": "number"},
                            "headScale": {"type": "number"},
                            "armLengthScale": {"type": "number"},
                            "legLengthScale": {"type": "number"},
                            "torsoLengthScale": {"type": "number"},
                            "shoulderWidthScale": {"type": "number"},
                            "skinColor": {"type": "object", "properties": {"r": {"type": "number"}, "g": {"type": "number"}, "b": {"type": "number"}, "a": {"type": "number"}}},
                            "torsoColor": {"type": "object", "properties": {"r": {"type": "number"}, "g": {"type": "number"}, "b": {"type": "number"}, "a": {"type": "number"}}},
                            "armColor": {"type": "object", "properties": {"r": {"type": "number"}, "g": {"type": "number"}, "b": {"type": "number"}, "a": {"type": "number"}}},
                            "legColor": {"type": "object", "properties": {"r": {"type": "number"}, "g": {"type": "number"}, "b": {"type": "number"}, "a": {"type": "number"}}}
                        }
                    }
                },
                "required": ["name", "appearance"]
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
                "Load a complete game from a JSON definition. Supports two formats:\n\n"
                "SINGLE-SCENE: Provide world, structures, player, npcs, camera, story keys directly. "
                "Immediately generates and populates the world.\n\n"
                "MULTI-SCENE: Provide a 'scenes' array. Each scene has: id, name, sceneType "
                "(world|menu|cutscene), worldDatabase, menuLayout (for menu scenes), and a definition "
                "object (same format as single-scene). The engine loads the scene manifest and transitions "
                "to the startScene (or first scene). Menu-type scenes display the GameMenuRenderer. "
                "Use transition_scene to move between scenes. "
                "See samples/game_definitions/menu_demo.json for a complete example."
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
                        "description": "Player entity. type: physics|spider|animated|active|hybrid. position: {x,y,z}. Optional: id, animFile.",
                        "properties": {
                            "type": {"type": "string", "enum": ["physics", "spider", "animated", "active", "hybrid"]},
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
                    "animFile": {"type": "string", "description": "Animation file (default: resources/animated_characters/humanoid.anim)"},
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
        # GAME STATE (Pause, Health, Respawn, Music, Save/Load, Objectives)
        # ================================================================

        Tool(
            name="toggle_pause",
            description="Toggle or set the game pause state. When paused, simulation freezes but API commands still work.",
            inputSchema={
                "type": "object",
                "properties": {
                    "paused": {"type": "boolean", "description": "Set pause state explicitly (omit to toggle)"}
                }
            }
        ),
        Tool(
            name="get_pause_state",
            description="Get the current pause state of the game.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="get_player_health",
            description="Get the player's current health, max health, and respawn state.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="damage_player",
            description="Deal damage to the player. If health reaches 0, triggers death sequence.",
            inputSchema={
                "type": "object",
                "properties": {
                    "amount": {"type": "number", "description": "Damage amount (default: 10)"}
                }
            }
        ),
        Tool(
            name="heal_player",
            description="Heal the player by a specified amount.",
            inputSchema={
                "type": "object",
                "properties": {
                    "amount": {"type": "number", "description": "Heal amount (default: 10)"}
                }
            }
        ),
        Tool(
            name="kill_player",
            description="Instantly kill the player, triggering death sequence and respawn timer.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="revive_player",
            description="Revive the player from death and respawn at spawn point.",
            inputSchema={
                "type": "object",
                "properties": {
                    "healthPercent": {"type": "number", "description": "Health percentage to revive with (0.0-1.0, default: 1.0)"}
                }
            }
        ),
        Tool(
            name="get_respawn_state",
            description="Get respawn system state: spawn point, respawn delay, death count, death timer.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="set_spawn_point",
            description="Set the player's respawn point.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "number", "description": "Spawn X coordinate"},
                    "y": {"type": "number", "description": "Spawn Y coordinate"},
                    "z": {"type": "number", "description": "Spawn Z coordinate"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="force_respawn",
            description="Force an immediate respawn, reviving the player at full health at the spawn point.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="get_music_state",
            description="Get the current music playlist state: tracks, current track, playing status, volume, mode.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="control_music",
            description="Control the background music playlist.",
            inputSchema={
                "type": "object",
                "properties": {
                    "action": {
                        "type": "string",
                        "enum": ["play", "stop", "next", "add_track", "remove_track", "clear", "set_volume", "set_mode"],
                        "description": "Music control action"
                    },
                    "path": {"type": "string", "description": "Track file path (for add_track/remove_track)"},
                    "volume": {"type": "number", "description": "Volume 0.0-1.0 (for set_volume)"},
                    "mode": {"type": "string", "enum": ["sequential", "shuffle"], "description": "Playlist mode (for set_mode)"}
                },
                "required": ["action"]
            }
        ),
        Tool(
            name="save_player",
            description="Save the player's state (camera, health, spawn point, inventory) to the world database.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="load_player",
            description="Load the player's saved state from the world database.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="get_objectives",
            description="Get all objectives (quests/tasks) with their status.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="add_objective",
            description="Add a new objective/quest to the tracker. Shows in the HUD.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Unique objective ID"},
                    "title": {"type": "string", "description": "Short title displayed in HUD"},
                    "description": {"type": "string", "description": "Longer description"},
                    "category": {"type": "string", "description": "Category: main, side, exploration (default: main)"},
                    "priority": {"type": "integer", "description": "Priority (higher = more important, default: 0)"},
                    "hidden": {"type": "boolean", "description": "If true, not shown in HUD (default: false)"}
                },
                "required": ["id", "title"]
            }
        ),
        Tool(
            name="complete_objective",
            description="Mark an objective as completed.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Objective ID to complete"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="fail_objective",
            description="Mark an objective as failed.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Objective ID to fail"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="remove_objective",
            description="Remove an objective from the tracker entirely.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Objective ID to remove"}
                },
                "required": ["id"]
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
            name="stop_engine",
            description=(
                "Stop the running engine process. Sends SIGTERM and waits up to 5 seconds "
                "for graceful shutdown; force-kills if it does not exit. "
                "Use before rebuild-and-relaunch cycles or to clean up after a test run."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "grace_seconds": {"type": "number", "description": "Seconds to wait for graceful exit before force-kill (default: 5)"}
                }
            }
        ),
        Tool(
            name="restart_engine",
            description=(
                "Stop the running engine then launch it again. "
                "Replays the same command-line args as the previous launch unless overridden. "
                "Useful after a build to pick up new binaries without manually stopping and starting. "
                "Always clears phyxel.log before relaunching so the new session starts with a clean log."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Build config to launch (default: same as previous launch, or Debug)"},
                    "args": {"type": "array", "items": {"type": "string"}, "description": "Command-line args (default: same as previous launch)"}
                }
            }
        ),
        Tool(
            name="clear_engine_logs",
            description=(
                "Truncate phyxel.log to empty. Call this before a test run or after stop_engine "
                "to prevent the log file from growing too large for get_engine_logs to process. "
                "restart_engine does this automatically; use clear_engine_logs when you want to "
                "reset logs without restarting."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="launch_asset_editor",
            description=(
                "Launch the engine in asset-editor mode to inspect a .voxel template on a clean "
                "Stone floor. The asset editor runs on a separate port (default 8091) so it "
                "can coexist with a running game engine on port 8090. "
                "Use inspect_template or critique_template afterwards to take screenshots and "
                "evaluate the model visually."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "template_path": {
                        "type": "string",
                        "description": "Relative or absolute path to the .voxel file to inspect"
                    },
                    "port": {
                        "type": "integer",
                        "description": "HTTP API port for the asset editor instance (default: 8091)"
                    },
                    "config": {
                        "type": "string",
                        "enum": ["Debug", "Release"],
                        "description": "Build configuration (default: Debug)"
                    },
                    "interaction_editor": {
                        "type": "boolean",
                        "description": "Launch in --interaction-editor mode instead of --asset-editor. Spawns a standing character next to the asset for sit/stand animation preview and calibration."
                    }
                },
                "required": ["template_path"]
            }
        ),
        Tool(
            name="close_asset_editor",
            description="Stop the asset editor process launched by launch_asset_editor.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="reload_asset_editor",
            description=(
                "Hot-reload the current .voxel template in the running asset editor without "
                "stopping/restarting the process. Useful after writing a new version of the "
                "template file to disk. Optionally supply a new path to switch templates."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {
                        "type": "string",
                        "description": "Optional new .voxel file path. Omit to reload the current template."
                    },
                    "port": {
                        "type": "integer",
                        "description": "Asset editor port (default 8091).",
                        "default": 8091
                    }
                }
            }
        ),
        Tool(
            name="inspect_template",
            description=(
                "Take multi-angle screenshots of a .voxel template in the asset editor and return "
                "them inline. Launches the asset editor automatically if needed. "
                "Returns screenshots from front, right, back-left, and top-down viewpoints, "
                "plus primitive counts from the template file header."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "template_name": {
                        "type": "string",
                        "description": "Template name (without extension) in resources/templates/"
                    },
                    "template_path": {
                        "type": "string",
                        "description": "Explicit path to .voxel file (overrides template_name)"
                    },
                    "angles": {
                        "type": "integer",
                        "description": "Number of viewpoints: 2 (front+right), 3 (+back-left), 4 (+top). Default: 4"
                    },
                    "port": {
                        "type": "integer",
                        "description": "Asset editor API port (default: 8091)"
                    },
                    "config": {
                        "type": "string",
                        "enum": ["Debug", "Release"],
                        "description": "Build config for auto-launch (default: Debug)"
                    },
                    "show_reference_character": {
                        "type": "boolean",
                        "description": "Show a humanoid reference character beside the asset for scale comparison (default: true)"
                    }
                }
            }
        ),
        Tool(
            name="critique_template",
            description=(
                "Visually inspect a .voxel template and return an AI critique. "
                "Takes multi-angle screenshots, then asks a vision-capable model to evaluate "
                "how well it matches the original prompt. Returns critique JSON plus all screenshots. "
                "Use refine_template to act on the critique automatically."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "template_name": {
                        "type": "string",
                        "description": "Template name (without extension) in resources/templates/"
                    },
                    "original_prompt": {
                        "type": "string",
                        "description": "The original generation prompt — used to evaluate match quality"
                    },
                    "critique_model": {
                        "type": "string",
                        "description": "Vision-capable model for critique (default: anthropic/claude-opus-4-5)"
                    },
                    "port": {
                        "type": "integer",
                        "description": "Asset editor API port (default: 8091)"
                    },
                    "config": {
                        "type": "string",
                        "enum": ["Debug", "Release"],
                        "description": "Build config for auto-launch (default: Debug)"
                    },
                    "show_reference_character": {
                        "type": "boolean",
                        "description": "Show a humanoid reference character beside the asset for scale comparison (default: true)"
                    }
                },
                "required": ["template_name", "original_prompt"]
            }
        ),
        Tool(
            name="refine_template",
            description=(
                "Iterative refinement loop: critique → regenerate → repeat until quality threshold "
                "is met or max rounds exhausted. Each round saves <name>_round_N.voxel; the best "
                "round is promoted to <name>.voxel. Uses native Phyxel C/S/M generation."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "template_name": {"type": "string", "description": "Template name (without .voxel extension)"},
                    "original_prompt": {"type": "string", "description": "Original generation prompt"},
                    "image": {"type": "string", "description": "Optional reference image (file path or URL) threaded through all rounds"},
                    "max_rounds": {"type": "integer", "description": "Maximum refinement rounds (default: 3)"},
                    "quality_threshold": {"type": "number", "description": "Stop when critique score >= this value 0-10 (default: 7)"},
                    "critique_model": {"type": "string", "description": "Vision model for critique (default: anthropic/claude-sonnet-4-6)"},
                    "generation_model": {"type": "string", "description": "Model for generation (default: anthropic/claude-sonnet-4-20250514)"},
                    "port": {"type": "integer", "description": "Asset editor API port (default: 8091)"},
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Build config for asset editor (default: Debug)"},
                    "show_reference_character": {
                        "type": "boolean",
                        "description": "Show a humanoid reference character beside the asset for scale comparison during each round (default: true)"
                    }
                },
                "required": ["template_name", "original_prompt"]
            }
        ),
        Tool(
            name="generate_asset",
            description=(
                "Full asset generation pipeline for game development: enhance prompt → generate → "
                "critique → refine loop → final verification. Produces a game-ready .voxel template "
                "with metadata (facing direction, bounding box, interaction points). "
                "Accepts an optional reference image for higher accuracy. "
                "Saves round snapshots and automatically promotes the best result. "
                "Use this as the single command to go from a description to a verified asset."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Output template name (no extension)"},
                    "prompt": {"type": "string", "description": "Description of the object to generate"},
                    "image": {"type": "string", "description": "Optional reference image (file path or HTTP/HTTPS URL) — dramatically improves quality for complex assets"},
                    "max_rounds": {"type": "integer", "description": "Max refinement rounds after initial generation (default: 3)"},
                    "quality_threshold": {"type": "number", "description": "Accept result when critique score >= this (0-10, default: 7)"},
                    "generation_model": {"type": "string", "description": "LLM for generation (default: anthropic/claude-sonnet-4-20250514)"},
                    "critique_model": {"type": "string", "description": "Vision LLM for critique (default: anthropic/claude-sonnet-4-6)"},
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Engine build config (default: Debug)"},
                    "port": {"type": "integer", "description": "Asset editor port (default: 8091)"}
                },
                "required": ["name", "prompt"]
            }
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
        # PROJECT MANAGEMENT (launcher / discovery)
        # ================================================================

        Tool(
            name="list_projects",
            description=(
                "List all game projects discovered in the PhyxelProjects directory "
                "(typically ~/Documents/PhyxelProjects). Returns project names, paths, "
                "last-opened timestamps, and metadata (has game.json, CMakeLists.txt, world DB)."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="create_project",
            description=(
                "Create a new empty game project. Scaffolds a minimal project directory "
                "with engine.json and resource folders in the PhyxelProjects directory. "
                "The project can then be opened with open_project or via the launcher UI."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Project name (used as directory name and window title)"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="open_project",
            description=(
                "Open/switch to a game project in the running engine. "
                "Loads the project's world database and game definition. "
                "The engine must be running."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Absolute path to the project directory"}
                },
                "required": ["path"]
            }
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

        # ================================================================
        # LIGHTING CONTROL
        # ================================================================

        Tool(
            name="get_inventory",
            description=(
                "Get the player's inventory state: all slots, hotbar contents, selected slot, "
                "and creative mode status. Hotbar is slots 0-8."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="give_item",
            description="Give items to the player's inventory. Stacks automatically.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material": {"type": "string", "description": "Material name (e.g. 'Stone', 'Wood', 'Metal')"},
                    "count": {"type": "integer", "description": "Number of items to give (default 1)", "default": 1}
                },
                "required": ["material"]
            }
        ),
        Tool(
            name="take_item",
            description="Remove items from the player's inventory.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material": {"type": "string", "description": "Material name to remove"},
                    "count": {"type": "integer", "description": "Number to remove (default 1)", "default": 1}
                },
                "required": ["material"]
            }
        ),
        Tool(
            name="select_hotbar_slot",
            description="Set the currently selected hotbar slot (0-8). This determines which material is used when placing voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "slot": {"type": "integer", "description": "Hotbar slot index (0-8)"}
                },
                "required": ["slot"]
            }
        ),
        Tool(
            name="set_inventory_slot",
            description="Set a specific inventory slot to a material and count. Omit 'material' to clear the slot.",
            inputSchema={
                "type": "object",
                "properties": {
                    "slot": {"type": "integer", "description": "Slot index (0-35)"},
                    "material": {"type": "string", "description": "Material name (omit to clear slot)"},
                    "count": {"type": "integer", "description": "Stack count (default 1)", "default": 1}
                },
                "required": ["slot"]
            }
        ),
        Tool(
            name="clear_inventory",
            description="Clear all items from the player's inventory.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="set_creative_mode",
            description="Toggle creative mode. In creative mode, items are never consumed when placing voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "enabled": {"type": "boolean", "description": "True for creative mode, false for survival"}
                },
                "required": ["enabled"]
            }
        ),

        # ================================================================
        # HEALTH / DAMAGE
        # ================================================================

        Tool(
            name="damage_entity",
            description="Deal damage to an entity. Returns actual damage dealt and updated health state.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "amount": {"type": "number", "description": "Damage amount"},
                    "source": {"type": "string", "description": "Source entity ID (optional)", "default": ""}
                },
                "required": ["id", "amount"]
            }
        ),
        Tool(
            name="heal_entity",
            description="Heal an entity. Returns actual amount healed and updated health state.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "amount": {"type": "number", "description": "Heal amount"}
                },
                "required": ["id", "amount"]
            }
        ),
        Tool(
            name="set_entity_health",
            description="Set entity health properties directly (health, maxHealth, invulnerable).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "health": {"type": "number", "description": "Set current health"},
                    "maxHealth": {"type": "number", "description": "Set max health"},
                    "invulnerable": {"type": "boolean", "description": "Set invulnerability"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="kill_entity",
            description="Instantly kill an entity (set health to 0).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="revive_entity",
            description="Revive a dead entity with optional health percentage (default 100%).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "healthPercent": {"type": "number", "description": "Health percentage to revive with (0.01-1.0, default 1.0)", "default": 1.0}
                },
                "required": ["id"]
            }
        ),

        # ================================================================
        # DAY/NIGHT CYCLE
        # ================================================================

        Tool(
            name="get_day_night",
            description=(
                "Get the current day/night cycle state: time of day (0-24h), sun direction/color, "
                "ambient strength, cycle enabled/paused, day length, and time scale."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="set_day_night",
            description=(
                "Configure the day/night cycle. Can set time of day, day number, enable/disable the cycle, "
                "pause it, adjust day length and time scale."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "timeOfDay": {"type": "number", "description": "Time of day in hours (0-24). 0=midnight, 6=dawn, 12=noon, 18=dusk"},
                    "dayNumber": {"type": "integer", "description": "Set the day counter (starts at 1)"},
                    "enabled": {"type": "boolean", "description": "Enable/disable the day/night cycle"},
                    "paused": {"type": "boolean", "description": "Pause/unpause the cycle"},
                    "dayLengthSeconds": {"type": "number", "description": "Real seconds for a full day cycle (default 600)"},
                    "timeScale": {"type": "number", "description": "Time speed multiplier (1.0 = normal)"}
                }
            }
        ),

        # ================================================================
        # ITEM REGISTRY
        # ================================================================

        Tool(
            name="list_items",
            description=(
                "List all registered item definitions. Returns items with their type, stats, "
                "equipment slot, damage, durability, and other properties."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="get_item",
            description="Get detailed information about a specific item by its ID.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Item ID (e.g. 'iron_sword', 'Stone')"}
                },
                "required": ["id"]
            }
        ),

        # ================================================================
        # EQUIPMENT & COMBAT
        # ================================================================

        Tool(
            name="get_equipment",
            description="Get the equipment loadout of an NPC entity. Shows all equipped slots, total damage, and reach.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entityId": {"type": "string", "description": "Entity ID of the NPC"}
                },
                "required": ["entityId"]
            }
        ),
        Tool(
            name="equip_item",
            description="Equip an item on an NPC entity. The item must be a Weapon, Tool, or Equippable type with a valid equipment slot. Automatically attaches weapon visual to the character's hand.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entityId": {"type": "string", "description": "Entity ID of the NPC"},
                    "itemId": {"type": "string", "description": "Item ID to equip (e.g. 'iron_sword')"}
                },
                "required": ["entityId", "itemId"]
            }
        ),
        Tool(
            name="unequip_item",
            description="Unequip an item from a specific slot on an NPC entity. Removes the weapon visual attachment.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entityId": {"type": "string", "description": "Entity ID of the NPC"},
                    "slot": {"type": "string", "description": "Equipment slot to unequip (MainHand, OffHand, Head, Chest, Legs, Feet)"}
                },
                "required": ["entityId", "slot"]
            }
        ),
        Tool(
            name="attack",
            description="Make an entity perform a combat attack. Uses equipped weapon stats for damage/reach. Returns list of hit entities with damage dealt.",
            inputSchema={
                "type": "object",
                "properties": {
                    "attackerId": {"type": "string", "description": "Entity ID of the attacker"},
                    "forward": {
                        "type": "object",
                        "description": "Attack direction vector (default: entity forward)",
                        "properties": {
                            "x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}
                        }
                    },
                    "coneAngle": {"type": "number", "description": "Attack cone angle in degrees (default 90)"},
                    "knockback": {"type": "number", "description": "Knockback force (default 2.0)"}
                },
                "required": ["attackerId"]
            }
        ),

        Tool(
            name="spawn_vfx",
            description=(
                "Spawn a lightweight voxel particle VFX burst at a world position. "
                "Stylized chunky glowing cubes that fly outward and fade (additive glow, no physics). "
                "Standalone — not wired into the spell/combat system. "
                "Effects: fireball (orange), magic_missile (violet), eldritch_blast (green), "
                "shield (blue dome-ish), heal (golden rising), spark (generic fallback)."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "effect": {
                        "type": "string",
                        "description": "Effect preset name",
                        "enum": ["fireball", "magic_missile", "eldritch_blast", "shield", "heal", "spark"],
                        "default": "fireball"
                    },
                    "x": {"type": "number", "description": "Burst X position"},
                    "y": {"type": "number", "description": "Burst Y position"},
                    "z": {"type": "number", "description": "Burst Z position"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="cast_vfx_projectile",
            description=(
                "Cast a travelling projectile VFX from one point to another (spell-style). "
                "The projectile flies from 'from' to 'to' with a glowing core and trail, then "
                "spawns a burst on arrival. 'fireball' = one fiery projectile that lights the room "
                "and explodes on impact; 'magic_missile' = a fan of homing violet darts. "
                "Standalone, not wired into combat."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "effect": {
                        "type": "string",
                        "description": "Projectile effect preset",
                        "enum": ["fireball", "magic_missile"],
                        "default": "fireball"
                    },
                    "from": {
                        "type": "object",
                        "description": "Origin (caster position)",
                        "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}
                    },
                    "to": {
                        "type": "object",
                        "description": "Target position",
                        "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}
                    }
                },
                "required": ["from", "to"]
            }
        ),
        Tool(
            name="cast_vfx_beam",
            description=(
                "Fire a sustained beam VFX from one point to another (spell-style). "
                "A flickering line of glowing cubes from 'from' to 'to' with a bright impact node "
                "and a transient light at the target, lasting a short duration. 'eldritch_blast' = "
                "warlock green beam. Standalone, not wired into combat."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "effect": {
                        "type": "string",
                        "description": "Beam effect preset",
                        "enum": ["eldritch_blast"],
                        "default": "eldritch_blast"
                    },
                    "from": {
                        "type": "object",
                        "description": "Origin (caster position)",
                        "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}
                    },
                    "to": {
                        "type": "object",
                        "description": "Target position",
                        "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}
                    }
                },
                "required": ["from", "to"]
            }
        ),
        Tool(
            name="cast_vfx_field",
            description=(
                "Raise a sustained field/shell VFX at a center position (spell-style). "
                "A shimmering shell of glowing cubes on a sphere around the point, persisting "
                "for a few seconds with a transient light. 'shield' = protective blue energy bubble. "
                "Standalone, not wired into combat."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "effect": {
                        "type": "string",
                        "description": "Field effect preset",
                        "enum": ["shield"],
                        "default": "shield"
                    },
                    "x": {"type": "number", "description": "Center X"},
                    "y": {"type": "number", "description": "Center Y"},
                    "z": {"type": "number", "description": "Center Z"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="list_lights",
            description=(
                "List all lights in the scene (point lights, spot lights) and the ambient light strength. "
                "Returns light IDs, positions, colors, intensities, radii, and enabled state."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="add_point_light",
            description="Add a point light to the scene. Returns the light ID. Max 32 point lights.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "number", "description": "Light X position"},
                    "y": {"type": "number", "description": "Light Y position"},
                    "z": {"type": "number", "description": "Light Z position"},
                    "color": {
                        "type": "object",
                        "description": "Light color (default white)",
                        "properties": {
                            "r": {"type": "number", "default": 1.0},
                            "g": {"type": "number", "default": 1.0},
                            "b": {"type": "number", "default": 1.0}
                        }
                    },
                    "intensity": {"type": "number", "description": "Light intensity (default 1.0)"},
                    "radius": {"type": "number", "description": "Light radius in voxel units (default 10.0)"},
                    "enabled": {"type": "boolean", "description": "Whether the light is enabled (default true)"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="add_spot_light",
            description=(
                "Add a spot light to the scene. Returns the light ID. Max 16 spot lights. "
                "Direction (dx, dy, dz) is a normalized vector. Cone angles are cosine values "
                "(inner_cone=0.9 ≈ 25° half-angle, outer_cone=0.8 ≈ 37° half-angle)."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "number", "description": "Light X position"},
                    "y": {"type": "number", "description": "Light Y position"},
                    "z": {"type": "number", "description": "Light Z position"},
                    "dx": {"type": "number", "description": "Direction X (default 0)", "default": 0},
                    "dy": {"type": "number", "description": "Direction Y (default -1, pointing down)", "default": -1},
                    "dz": {"type": "number", "description": "Direction Z (default 0)", "default": 0},
                    "color": {
                        "type": "object",
                        "description": "Light color (default white)",
                        "properties": {
                            "r": {"type": "number", "default": 1.0},
                            "g": {"type": "number", "default": 1.0},
                            "b": {"type": "number", "default": 1.0}
                        }
                    },
                    "intensity": {"type": "number", "description": "Light intensity (default 1.0)"},
                    "radius": {"type": "number", "description": "Light radius (default 20.0)"},
                    "inner_cone": {"type": "number", "description": "Inner cone cosine (default 0.9)"},
                    "outer_cone": {"type": "number", "description": "Outer cone cosine (default 0.8)"},
                    "enabled": {"type": "boolean", "description": "Whether enabled (default true)"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="remove_light",
            description="Remove a point or spot light by its ID.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "description": "The light ID to remove"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="update_light",
            description=(
                "Update properties of an existing light (point or spot). "
                "Only specify the fields you want to change. The light type is auto-detected by ID."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "description": "The light ID to update"},
                    "x": {"type": "number", "description": "New X position"},
                    "y": {"type": "number", "description": "New Y position"},
                    "z": {"type": "number", "description": "New Z position"},
                    "dx": {"type": "number", "description": "New direction X (spot lights only)"},
                    "dy": {"type": "number", "description": "New direction Y (spot lights only)"},
                    "dz": {"type": "number", "description": "New direction Z (spot lights only)"},
                    "color": {
                        "type": "object",
                        "properties": {
                            "r": {"type": "number"},
                            "g": {"type": "number"},
                            "b": {"type": "number"}
                        }
                    },
                    "intensity": {"type": "number"},
                    "radius": {"type": "number"},
                    "inner_cone": {"type": "number", "description": "Spot light inner cone cosine"},
                    "outer_cone": {"type": "number", "description": "Spot light outer cone cosine"},
                    "enabled": {"type": "boolean"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="set_ambient_light",
            description="Set the global ambient light strength (0.0 = pitch black, 1.0 = normal, 2.0 = maximum).",
            inputSchema={
                "type": "object",
                "properties": {
                    "strength": {"type": "number", "description": "Ambient light strength (0.0 to 2.0)"}
                },
                "required": ["strength"]
            }
        ),

        # ================================================================
        # AUDIO CONTROL
        # ================================================================

        Tool(
            name="list_sounds",
            description="List available sound files and audio channels.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="play_sound",
            description=(
                "Play a sound effect. Specify just the filename (e.g. 'click.wav'). "
                "For 3D positional audio, include x/y/z coordinates. "
                "Channels: Master, SFX (default), Music, Voice."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "file": {"type": "string", "description": "Sound filename (e.g. 'click.wav', 'whoosh.wav')"},
                    "volume": {"type": "number", "description": "Volume 0.0 to 1.0 (default 1.0)"},
                    "channel": {
                        "type": "string",
                        "description": "Audio channel",
                        "enum": ["Master", "SFX", "Music", "Voice"]
                    },
                    "x": {"type": "number", "description": "3D position X (omit for 2D sound)"},
                    "y": {"type": "number", "description": "3D position Y"},
                    "z": {"type": "number", "description": "3D position Z"}
                },
                "required": ["file"]
            }
        ),
        Tool(
            name="set_volume",
            description="Set the volume for an audio channel (Master, SFX, Music, Voice).",
            inputSchema={
                "type": "object",
                "properties": {
                    "channel": {
                        "type": "string",
                        "description": "Audio channel to adjust",
                        "enum": ["Master", "SFX", "Music", "Voice"]
                    },
                    "volume": {"type": "number", "description": "Volume level (0.0 to 1.0)"}
                },
                "required": ["channel", "volume"]
            }
        ),

        # ================================================================
        # Custom UI Menu Management
        # ================================================================
        Tool(
            name="list_menus",
            description="List all registered custom UI menu screens and their visibility state.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="create_menu",
            description=(
                "Create a custom UI menu from a JSON definition. The menu is added as a named screen. "
                "Definition format: {\"id\": \"menu_id\", \"title\": \"Title\", \"anchor\": \"Center\", "
                "\"size\": [400, 500], \"children\": [{\"type\": \"label\", \"id\": \"lbl\", \"text\": \"Hello\", \"isTitle\": true}, "
                "{\"type\": \"slider\", \"id\": \"fov\", \"label\": \"FOV \", \"value\": 70, \"min\": 30, \"max\": 120, \"size\": [380, 32]}, "
                "{\"type\": \"checkbox\", \"id\": \"vsync\", \"label\": \"VSync\", \"checked\": false, \"size\": [380, 32]}, "
                "{\"type\": \"dropdown\", \"id\": \"quality\", \"label\": \"Quality \", \"options\": [\"Low\",\"Medium\",\"High\"], \"selected\": 1, \"size\": [380, 40]}, "
                "{\"type\": \"button\", \"id\": \"back\", \"text\": \"Close\", \"size\": [380, 40]}]}"
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Unique name for the menu screen (e.g. 'game_menu')"},
                    "definition": {
                        "type": "object",
                        "description": "Menu definition JSON with id, title, anchor, size, children array of widgets"
                    }
                },
                "required": ["name", "definition"]
            }
        ),
        Tool(
            name="show_menu",
            description="Show (make visible) a previously created custom UI menu screen.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the menu screen to show"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="hide_menu",
            description="Hide a custom UI menu screen.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the menu screen to hide"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="toggle_menu",
            description="Toggle the visibility of a custom UI menu screen.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the menu screen to toggle"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="remove_menu",
            description="Remove a custom UI menu screen entirely.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the menu screen to remove"}
                },
                "required": ["name"]
            }
        ),

        # ================================================================
        # Bulk Operations (fast chunk-level ops)
        # ================================================================
        Tool(
            name="clear_chunk",
            description="Instantly clear ALL voxels in a chunk. Much faster than clear_region for whole chunks. Takes chunk coordinates (not world coordinates).",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "Chunk X coordinate (world_x / 32)"},
                    "y": {"type": "integer", "description": "Chunk Y coordinate (world_y / 32)"},
                    "z": {"type": "integer", "description": "Chunk Z coordinate (world_z / 32)"},
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="rebuild_physics",
            description="Force rebuild physics collision shapes. Optionally target a specific chunk. Useful after bulk modifications.",
            inputSchema={
                "type": "object",
                "properties": {
                    "chunk": {
                        "type": "object",
                        "description": "Optional: rebuild only this chunk (chunk coordinates)",
                        "properties": {
                            "x": {"type": "integer"},
                            "y": {"type": "integer"},
                            "z": {"type": "integer"},
                        }
                    }
                },
                "required": []
            }
        ),
        Tool(
            name="clear_all_entities",
            description="Remove ALL entities from the world (player, NPCs, all characters). Use before reload_game_definition to start fresh.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="reload_game_definition",
            description="Destructively reload a game definition: clears all entities, NPCs, dialogue, and story, then loads the provided definition fresh. Unlike load_game_definition which is additive, this replaces everything.",
            inputSchema={
                "type": "object",
                "properties": {
                    "definition": {
                        "type": "string",
                        "description": "Complete game definition as a JSON string"
                    }
                },
                "required": ["definition"]
            }
        ),
        Tool(
            name="get_terrain_height",
            description="Query the surface Y coordinate at a given (x, z) world position. Returns the highest voxel Y and the spawn Y (surface + 1). Useful for placing entities on terrain.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "World X coordinate"},
                    "z": {"type": "integer", "description": "World Z coordinate"},
                    "max_y": {"type": "integer", "description": "Maximum Y to search (default: 255)"},
                    "min_y": {"type": "integer", "description": "Minimum Y to search (default: 0)"},
                },
                "required": ["x", "z"]
            }
        ),

        # ================================================================
        # Animation Control
        # ================================================================
        Tool(
            name="list_entity_animations",
            description="List all animation clips available on an animated entity. Returns clip names, durations, and speeds.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID (e.g. 'npc_Guard', 'player')"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="play_entity_animation",
            description="Play a named animation clip on an animated entity. Puts the entity into Preview mode and plays the clip.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "animation": {"type": "string", "description": "Animation clip name (e.g. 'walk', 'run', 'idle')"}
                },
                "required": ["id", "animation"]
            }
        ),
        Tool(
            name="get_animation_state",
            description="Get the current animation state of an entity: FSM state, current clip, progress, duration, and blend duration.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="set_animation_state",
            description="Set the FSM state of an animated entity (e.g. 'Idle', 'Walk', 'Run', 'Jump', 'Attack', 'Crouch', 'Preview').",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "state": {"type": "string", "description": "State name: Idle, Walk, Run, Jump, Fall, Land, Crouch, CrouchIdle, CrouchWalk, StandUp, Attack, TurnLeft, TurnRight, StrafeLeft, StrafeRight, Preview, etc."}
                },
                "required": ["id", "state"]
            }
        ),
        Tool(
            name="set_blend_duration",
            description="Set the crossfade blend duration (seconds) for animation transitions on an entity.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "duration": {"type": "number", "description": "Blend duration in seconds (default is 0.2)"}
                },
                "required": ["id", "duration"]
            }
        ),
        Tool(
            name="reload_entity_animation",
            description="Hot-reload animation clips from a .anim file on an entity. Preserves skeleton and model, replaces all animation data.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "animFile": {"type": "string", "description": "Path to .anim file (relative to engine root or absolute)"}
                },
                "required": ["id", "animFile"]
            }
        ),
        Tool(
            name="seek_animation",
            description="Pause an entity's animation and scrub to a specific normalised time [0.0–1.0] within the current clip. Use for storyboard-style visual review: 0.0=start, 0.5=midpoint, 1.0=end. Call resume_animation to unpause.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id":   {"type": "string", "description": "Entity ID (e.g. 'player', 'npc_Guard')"},
                    "time": {"type": "number", "description": "Normalised time within the current clip [0.0, 1.0]"}
                },
                "required": ["id", "time"]
            }
        ),
        Tool(
            name="resume_animation",
            description="Unpause an entity's animation after seek_animation. The clip resumes from the scrubbed position.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="get_bone_positions",
            description="Get world-space bone AABBs for an animated character. Returns center position and half-extents for each named bone. Use during SittingIdle to check pelvis height vs seat surface and foot height vs floor.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID (e.g. 'player', 'npc_Guard')"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="get_character_design_constraints",
            description="Return pre-measured furniture sizing targets for a character archetype. Includes popliteal height (ideal seat top), achievable subcube seat height, seat depth/width minimums, and backrest height ranges. Use this BEFORE designing any seating asset to ensure correct scale.",
            inputSchema={
                "type": "object",
                "properties": {
                    "archetype": {"type": "string", "description": "Character archetype key (default: 'humanoid_normal')"}
                }
            }
        ),
        Tool(
            name="sit_character",
            description="Sit an animated entity at a placed object's named interaction point. Looks up the seat's world position and facing (rotation-corrected), applies any existing calibration profile, then drives the stand_to_sit → sitting_idle animation sequence.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entity_id": {"type": "string", "description": "Entity ID (e.g. 'player', 'npc_Guard')"},
                    "object_id": {"type": "string", "description": "Placed object ID (e.g. 'chair_wood_1')"},
                    "point_id":  {"type": "string", "description": "Interaction point ID on the object (default: 'seat_0')"}
                },
                "required": ["entity_id", "object_id"]
            }
        ),
        Tool(
            name="stand_up_character",
            description="Stand an entity up from a seated state, driving the sit_to_stand → idle animation sequence.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entity_id": {"type": "string", "description": "Entity ID (e.g. 'player', 'npc_Guard')"}
                },
                "required": ["entity_id"]
            }
        ),
        Tool(
            name="set_interaction_profile",
            description="Create or update a calibration profile for a specific (archetype, template, interaction point) combination. Offsets are in template-local space — the engine rotates them automatically at runtime. Saved to resources/interactions/<archetype>.json.",
            inputSchema={
                "type": "object",
                "properties": {
                    "archetype":            {"type": "string", "description": "Character archetype (e.g. 'humanoid_normal')"},
                    "template_name":        {"type": "string", "description": "Template name (e.g. 'chair_wood', 'wooden_throne')"},
                    "point_id":             {"type": "string", "description": "Interaction point ID (e.g. 'seat_0')"},
                    "sit_down_offset":      {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "[x, y, z] foot-snap offset for the stand_to_sit transition (template-local)"},
                    "sitting_idle_offset":  {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "[x, y, z] foot-snap offset for the seated idle loop (template-local)"},
                    "sit_stand_up_offset":  {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "[x, y, z] foot-snap offset for the sit_to_stand transition (template-local)"},
                    "sit_blend_duration":   {"type": "number", "description": "Animation crossfade duration in seconds (0 = instant)"},
                    "seat_height_offset":   {"type": "number", "description": "Additional Y offset on the seat anchor position"}
                },
                "required": ["template_name"]
            }
        ),
        Tool(
            name="get_interaction_profile",
            description="Retrieve the current calibration profile for a (archetype, template, interaction point) combination. Returns found=false if no profile exists yet.",
            inputSchema={
                "type": "object",
                "properties": {
                    "archetype":     {"type": "string", "description": "Character archetype (default: 'humanoid_normal')"},
                    "template_name": {"type": "string", "description": "Template name (e.g. 'chair_wood')"},
                    "point_id":      {"type": "string", "description": "Interaction point ID (default: 'seat_0')"}
                },
                "required": ["template_name"]
            }
        ),
        Tool(
            name="ie_sit_preview",
            description="Trigger the 'Sit Down' preview in the interaction editor (--interaction-editor mode only). Drives the character through the stand_to_sit → sitting_idle animation sequence using the current seat anchor and profile offsets.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="ie_stand_preview",
            description="Reset the interaction editor preview — stands the character back up and returns to the standing idle state. Use after ie_sit_preview to prepare for another sit cycle.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="ie_preview_state",
            description="Get the current interaction editor preview state: 'standing', 'sitting_down', 'sitting_idle', or 'standing_up'. Also returns the character's world position.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="validate_ie_pose",
            description=(
                "Run a bone-vs-voxel AABB intersection check for the character's current pose in the "
                "interaction editor. Tests every character bone against every voxel in the asset and "
                "reports any penetrations. Returns valid=true only when no 'error'-severity violations "
                "exist. Severity: 'error' for trunk/core bones (Hips, Spine, UpLeg, Leg, Neck, Head), "
                "'warning' for extremities (arms, hands, feet). MUST be called after ie_sit_preview "
                "before accepting a calibration as correct — visual inspection alone cannot catch "
                "axis-parallel penetrations."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="seek_ie_animation",
            description=(
                "Pause the interaction editor character at a specific point in a sitting animation clip. "
                "After calling this, the character is frozen at that exact pose and orbit_screenshots can "
                "capture it visually. Call ie_resume_animation to resume playback. "
                "clip_name: 'stand_to_sit', 'sitting_idle', or 'sit_to_stand'. "
                "normalized_time: 0.0 = start of clip, 1.0 = end of clip."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "clip_name": {"type": "string", "description": "Animation clip name"},
                    "normalized_time": {"type": "number", "description": "0.0 to 1.0"}
                },
                "required": ["clip_name", "normalized_time"]
            }
        ),
        Tool(
            name="ie_resume_animation",
            description="Resume animation playback in the interaction editor after a seek_ie_animation pause.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="validate_ie_animation",
            description=(
                "Run a multi-frame bone-vs-voxel AABB intersection check across all three sitting "
                "animation clips (stand_to_sit, sitting_idle, sit_to_stand). Samples bone positions "
                "at N evenly-spaced time steps per clip and tests each against every voxel in the "
                "asset. Returns per-clip summaries (error/warning counts, worst penetration depth) "
                "and a full violation list tagged with clip name, normalized time, bone name, and "
                "penetration vector. Use this after validate_ie_pose to catch transient violations "
                "that only occur mid-animation. A clean result requires has_errors=false across all "
                "clips."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "samples": {
                        "type": "integer",
                        "description": "Number of time samples per clip (default: 30). More samples = finer coverage but slower."
                    }
                }
            }
        ),
        Tool(
            name="get_npc_blackboard",
            description="Get the AI blackboard state of a BehaviorTree-driven NPC. Shows all key-value pairs used by the behavior tree for decision-making.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="get_npc_perception",
            description="Get the perception state of a BehaviorTree-driven NPC. Shows visible entities, heard entities, threats, and memory entries.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="set_npc_blackboard",
            description="Set a key-value pair on a BehaviorTree-driven NPC's blackboard. Supports bool, int, float, string, and vec3 ({x,y,z}) values.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"},
                    "key": {"type": "string", "description": "Blackboard key"},
                    "value": {"description": "Value to set (bool, number, string, or {x,y,z} object)"}
                },
                "required": ["name", "key", "value"]
            }
        ),
        Tool(
            name="get_locations",
            description="Get all registered world locations (taverns, guard posts, homes, etc.). Each location has an id, name, position, radius, and type.",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        ),
        Tool(
            name="add_location",
            description="Register a named world location for NPC navigation. Types: Home, Work, Tavern, Market, Temple, Farm, GuardPost, Wilderness, Custom.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Unique location ID"},
                    "name": {"type": "string", "description": "Display name"},
                    "x": {"type": "number", "description": "World X position"},
                    "y": {"type": "number", "description": "World Y position"},
                    "z": {"type": "number", "description": "World Z position"},
                    "radius": {"type": "number", "description": "Arrival radius (default 3.0)"},
                    "type": {"type": "string", "description": "Location type (Home, Work, Tavern, Market, Temple, Farm, GuardPost, Wilderness, Custom)"}
                },
                "required": ["id", "name", "x", "y", "z"]
            }
        ),
        Tool(
            name="remove_location",
            description="Remove a registered world location by its ID.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Location ID to remove"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="get_npc_schedule",
            description="Get the daily schedule of a Scheduled-behavior NPC. Shows time slots, activities, and target locations.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="set_npc_schedule",
            description="Set or replace the daily schedule of a Scheduled-behavior NPC. Provide a schedule JSON with entries, or a role name to use a built-in default (guard, merchant, farmer, innkeeper).",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"},
                    "role": {"type": "string", "description": "Use a built-in schedule (guard, merchant, farmer, innkeeper)"},
                    "schedule": {"type": "object", "description": "Custom schedule JSON with 'entries' array"}
                },
                "required": ["name"]
            }
        ),
        # --- Social Simulation ---
        Tool(
            name="get_npc_needs",
            description="Get an NPC's current needs (Hunger, Rest, Social, Safety, Entertainment, Comfort). Each need has a value (0-100), decay rate, and urgency threshold.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="set_npc_needs",
            description="Set an NPC's needs. Provide either a full needs JSON or a single type+value pair.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"},
                    "type": {"type": "string", "description": "Need type: Hunger, Rest, Social, Safety, Entertainment, Comfort"},
                    "value": {"type": "number", "description": "Need value (0-100)"},
                    "needs": {"type": "array", "description": "Full needs array to replace all needs"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="get_npc_relationships",
            description="Get an NPC's relationships with other NPCs (trust, affection, respect, fear, disposition). If no name given, returns all relationships.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name (omit for all relationships)"}
                }
            }
        ),
        Tool(
            name="set_npc_relationship",
            description="Set a directed relationship from one NPC to another. Trust/affection/respect range -1 to 1, fear 0 to 1.",
            inputSchema={
                "type": "object",
                "properties": {
                    "from": {"type": "string", "description": "Source NPC name"},
                    "to": {"type": "string", "description": "Target NPC name"},
                    "trust": {"type": "number", "description": "Trust level (-1 to 1)"},
                    "affection": {"type": "number", "description": "Affection level (-1 to 1)"},
                    "respect": {"type": "number", "description": "Respect level (-1 to 1)"},
                    "fear": {"type": "number", "description": "Fear level (0 to 1)"},
                    "label": {"type": "string", "description": "Label (friend, rival, mentor, etc.)"}
                },
                "required": ["from", "to"]
            }
        ),
        Tool(
            name="apply_npc_interaction",
            description="Apply a social interaction between two NPCs. Modifies their relationship based on interaction type and intensity. Types: Greeting, Conversation, Trade, Gift, Insult, Attack, Help, Betray, Gossip, Witnessed.",
            inputSchema={
                "type": "object",
                "properties": {
                    "from": {"type": "string", "description": "Initiating NPC name"},
                    "to": {"type": "string", "description": "Target NPC name"},
                    "type": {"type": "string", "description": "Interaction type"},
                    "intensity": {"type": "number", "description": "Intensity multiplier (default 1.0, max 2.0)"}
                },
                "required": ["from", "to", "type"]
            }
        ),
        Tool(
            name="get_npc_worldview",
            description="Get an NPC's subjective worldview: beliefs, opinions, and observations. Also returns a context summary string useful for LLM prompts.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="set_npc_belief",
            description="Set a belief in an NPC's worldview. Beliefs are key-value pairs with confidence levels. Different NPCs can hold contradictory beliefs.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"},
                    "key": {"type": "string", "description": "Belief key (e.g. 'blacksmith_location', 'king_is_alive')"},
                    "value": {"type": "string", "description": "Belief value"},
                    "confidence": {"type": "number", "description": "Confidence 0.0-1.0 (default 1.0)"}
                },
                "required": ["name", "key", "value"]
            }
        ),
        Tool(
            name="set_npc_opinion",
            description="Set an NPC's opinion about a subject (entity, faction, concept). Sentiment ranges from -1 (hate) to 1 (love).",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"},
                    "subject": {"type": "string", "description": "What the opinion is about"},
                    "sentiment": {"type": "number", "description": "Sentiment -1.0 to 1.0"},
                    "reason": {"type": "string", "description": "Why the NPC holds this opinion"}
                },
                "required": ["name", "subject", "sentiment"]
            }
        ),

        # --- AI / LLM ---
        Tool(
            name="get_ai_status",
            description="Get AI/LLM configuration status, provider, model, and token usage statistics.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="configure_ai",
            description="Update AI/LLM configuration (provider, model, API key). Changes take effect immediately.",
            inputSchema={
                "type": "object",
                "properties": {
                    "provider": {"type": "string", "description": "LLM provider: anthropic, openai, or ollama"},
                    "model": {"type": "string", "description": "Model name (empty for provider default)"},
                    "api_key": {"type": "string", "description": "API key for cloud providers"},
                    "max_tokens": {"type": "integer", "description": "Max response tokens (default 1024)"},
                    "temperature": {"type": "number", "description": "Temperature 0.0-2.0 (default 0.8)"}
                },
                "required": []
            }
        ),
        Tool(
            name="start_ai_conversation",
            description="Start an AI-driven conversation with an NPC using the LLM. The NPC will greet the player automatically.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name to start conversation with"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="send_ai_message",
            description="Send a player message to the active AI conversation. The NPC will respond via LLM.",
            inputSchema={
                "type": "object",
                "properties": {
                    "message": {"type": "string", "description": "Player's message text"}
                },
                "required": ["message"]
            }
        ),

        # ================================================================
        # D&D RPG — Stateless dice / lookup tools (no engine required)
        # ================================================================
        Tool(
            name="roll_dice",
            description="Roll dice using D&D notation (e.g. '2d6+3', '1d20', '4d6kh3'). Supports advantage/disadvantage. Returns roll results with breakdown.",
            inputSchema={
                "type": "object",
                "properties": {
                    "expression": {"type": "string", "description": "Dice expression e.g. '2d6+3', '1d20', '1d8'"},
                    "advantage": {"type": "boolean", "description": "Roll with advantage (roll twice, take higher)"},
                    "disadvantage": {"type": "boolean", "description": "Roll with disadvantage (roll twice, take lower)"},
                    "count": {"type": "integer", "description": "Number of independent rolls (default 1)"}
                },
                "required": ["expression"]
            }
        ),
        Tool(
            name="check_dc",
            description="Check if a D20 roll meets a Difficulty Class (DC). Returns pass/fail with roll details.",
            inputSchema={
                "type": "object",
                "properties": {
                    "dc": {"type": "integer", "description": "Difficulty Class to beat"},
                    "bonus": {"type": "integer", "description": "Bonus to add to the d20 roll (ability mod + proficiency, etc.)"},
                    "advantage": {"type": "boolean", "description": "Roll with advantage"},
                    "disadvantage": {"type": "boolean", "description": "Roll with disadvantage"}
                },
                "required": ["dc"]
            }
        ),

        # ================================================================
        # D&D RPG — Party management (engine required)
        # ================================================================
        Tool(
            name="get_party",
            description="Get the current D&D party state: members, levels, alive status, leader, total/average level.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="add_party_member",
            description="Add an entity to the D&D party.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entity_id": {"type": "string", "description": "Entity ID to add"},
                    "name": {"type": "string", "description": "Display name (defaults to entity_id)"},
                    "level": {"type": "integer", "description": "Character level 1-20 (default 1)"}
                },
                "required": ["entity_id"]
            }
        ),
        Tool(
            name="remove_party_member",
            description="Remove an entity from the D&D party.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entity_id": {"type": "string", "description": "Entity ID to remove"}
                },
                "required": ["entity_id"]
            }
        ),

        # ================================================================
        # D&D RPG — Combat / Initiative (engine required)
        # ================================================================
        Tool(
            name="get_combat_state",
            description="Get D&D initiative tracker state: whether combat is active, current round, whose turn it is, full turn order.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="start_combat",
            description="Start D&D combat. Rolls initiative for all participants and sorts turn order.",
            inputSchema={
                "type": "object",
                "properties": {
                    "participants": {
                        "type": "array",
                        "description": "List of combatants with initiative bonuses",
                        "items": {
                            "type": "object",
                            "properties": {
                                "entity_id": {"type": "string"},
                                "initiative_bonus": {"type": "integer"}
                            },
                            "required": ["entity_id"]
                        }
                    }
                },
                "required": []
            }
        ),
        Tool(
            name="next_combat_turn",
            description="Advance to the next turn in D&D combat. Returns who goes next and the current round number.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="end_combat",
            description="End D&D combat and reset the initiative tracker.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="set_initiative",
            description="Manually set an entity's initiative value in the turn order.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entity_id": {"type": "string", "description": "Entity ID"},
                    "value": {"type": "integer", "description": "Initiative value"}
                },
                "required": ["entity_id", "value"]
            }
        ),

        # ================================================================
        # D&D RPG — World Calendar (engine required)
        # ================================================================
        Tool(
            name="get_world_date",
            description="Get the current in-game calendar date: day, month, year, season, moon phase, day of week, holidays.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="advance_world_date",
            description="Advance the in-game calendar by a number of days.",
            inputSchema={
                "type": "object",
                "properties": {
                    "days": {"type": "integer", "description": "Number of days to advance (default 1)"}
                },
                "required": []
            }
        ),
        Tool(
            name="set_world_date",
            description="Set the in-game calendar to a specific total day number (day 1 = 1st Deepwinter, Year 1).",
            inputSchema={
                "type": "object",
                "properties": {
                    "total_days": {"type": "integer", "description": "Total elapsed days from epoch"}
                },
                "required": ["total_days"]
            }
        ),

        # ================================================================
        # D&D RPG — Campaign Journal (engine required)
        # ================================================================
        Tool(
            name="add_journal_entry",
            description="Add an entry to the campaign journal. Useful for recording session notes, world events, quest updates, and discoveries.",
            inputSchema={
                "type": "object",
                "properties": {
                    "title": {"type": "string", "description": "Entry title"},
                    "content": {"type": "string", "description": "Entry body text"},
                    "type": {
                        "type": "string",
                        "description": "Entry type: SessionNote, WorldEvent, CharacterEvent, QuestUpdate, Discovery",
                        "enum": ["SessionNote", "WorldEvent", "CharacterEvent", "QuestUpdate", "Discovery"]
                    },
                    "day": {"type": "integer", "description": "In-game day number (defaults to current world date)"},
                    "tags": {"type": "array", "items": {"type": "string"}, "description": "Searchable tags"}
                },
                "required": ["title"]
            }
        ),
        Tool(
            name="get_journal_entries",
            description="Query campaign journal entries. Filter by type, tag, day, or full-text search.",
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {"type": "string", "description": "Filter by entry type: SessionNote, WorldEvent, CharacterEvent, QuestUpdate, Discovery"},
                    "tag": {"type": "string", "description": "Filter by tag"},
                    "day": {"type": "integer", "description": "Filter by in-game day number"},
                    "search": {"type": "string", "description": "Full-text search across title and content"}
                },
                "required": []
            }
        ),
        Tool(
            name="remove_journal_entry",
            description="Remove a campaign journal entry by ID.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "description": "Journal entry ID"}
                },
                "required": ["id"]
            }
        ),
    ]


_VISUAL_TOOLS   = {"screenshot", "get_visual_diagnostic"}
_ORBIT_TOOLS    = {"orbit_screenshots"}
# Tools that build and return list[TextContent] directly (skip json.dumps wrapping)
_CONTENT_TOOLS  = {"inspect_template", "critique_template", "refine_template", "generate_asset"}


@server.call_tool()
async def call_tool(name: str, arguments: dict) -> list:
    """Route tool calls to the engine HTTP API."""
    logger.info(f"Tool call: {name}({json.dumps(arguments, default=str)[:200]})")

    result = await _dispatch_tool(name, arguments)

    if name in _VISUAL_TOOLS:
        return _build_visual_response(result)

    if name in _ORBIT_TOOLS:
        return _build_orbit_response(result)

    if name in _CONTENT_TOOLS:
        return result  # already list[TextContent | ImageContent]

    return [TextContent(type="text", text=json.dumps(result, indent=2))]


# Tools that are safe to call without a project loaded
_NO_PROJECT_TOOLS = {
    "engine_status", "engine_running", "build_project", "launch_engine",
    "stop_engine", "restart_engine", "clear_engine_logs",
    "project_info", "list_projects", "create_project", "open_project",
    "package_game",
    # Diagnostics — work in any engine mode (asset editor, anim editor, no project)
    "screenshot", "get_visual_diagnostic", "get_engine_logs",
    "get_render_stats", "set_log_level", "set_debug_overlay", "orbit_screenshots",
    # Asset generation pipeline — purely Python-side or uses asset editor (port 8091), no project needed
    "generate_template", "generate_asset",
    "launch_asset_editor", "close_asset_editor", "reload_asset_editor",
    "inspect_template", "critique_template", "refine_template",
    "list_generated_templates", "search_templates",
    # D&D stateless tools — no engine needed
    "roll_dice", "check_dc",
}


async def _check_project_loaded() -> dict | None:
    """Check if a game project is loaded. Returns None if OK, or an error dict."""
    try:
        result = await api_get("/api/project/info")
        if "error" in result:
            return {
                "error": "No game project is loaded.",
                "hint": (
                    "The engine is running but no project is open. "
                    "You must either:\n"
                    "  1. Launch the engine with --project flag: "
                    "phyxel.exe --project C:\\Users\\jack\\Documents\\PhyxelProjects\\YourProject\n"
                    "  2. Use the 'open_project' tool to open an existing project\n"
                    "  3. Use the 'create_project' tool to create a new project, then open it\n\n"
                    "Without a project loaded, most tools will not work (no world, no entities, no chunks)."
                ),
                "available_projects": "Use the 'list_projects' tool to see available projects."
            }
        return None
    except Exception:
        return None  # Can't reach engine — other errors will surface naturally


async def _dispatch_tool(name: str, args: dict) -> dict:
    """Dispatch a tool call to the appropriate API endpoint."""

    # Check that a project is loaded for tools that need one
    if name not in _NO_PROJECT_TOOLS:
        project_err = await _check_project_loaded()
        if project_err is not None:
            return project_err

    # --- Status & State ---
    if name == "engine_status":
        status = await api_get("/api/status")
        # Enrich with project info so the caller knows the full state
        project = await api_get("/api/project/info")
        if "error" not in project:
            status["project"] = project
        else:
            status["project"] = None
            status["project_warning"] = (
                "No game project is loaded. The engine is in project-selector mode. "
                "Use 'launch_engine' with args: [\"--project\", \"<path>\"] "
                "or call 'open_project' to load a project before using other tools."
            )
        return status

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
        body = {
            "name": args["name"],
            "position": {"x": args["x"], "y": args["y"], "z": args["z"]},
            "static": args.get("static", True)
        }
        if "rotation" in args:
            body["rotation"] = args["rotation"]
        if "parent_id" in args:
            body["parent_id"] = args["parent_id"]
        return await api_post("/api/world/template", body)

    # --- Structures ---
    elif name == "build_structure":
        return await api_post("/api/structure/build", args)

    elif name == "list_structure_types":
        return await api_get("/api/structure/types")

    # --- Doors ---
    elif name == "register_door":
        body: dict[str, Any] = {
            "placed_object_id": args["placed_object_id"],
            "template_name": args["template_name"],
        }
        if "base_rotation" in args:
            body["base_rotation"] = args["base_rotation"]
        if "open_angle" in args:
            body["open_angle"] = args["open_angle"]
        if "swing_speed" in args:
            body["swing_speed"] = args["swing_speed"]
        if "hinge" in args:
            body["hinge"] = args["hinge"]
        if "thickness" in args:
            body["thickness"] = args["thickness"]
        return await api_post("/api/door/register", body)

    elif name == "toggle_door":
        return await api_post("/api/door/toggle", {"placed_object_id": args["placed_object_id"]})

    elif name == "open_door":
        return await api_post("/api/door/open", {"placed_object_id": args["placed_object_id"]})

    elif name == "close_door":
        return await api_post("/api/door/close", {"placed_object_id": args["placed_object_id"]})

    elif name == "list_doors":
        return await api_get("/api/doors")

    elif name == "set_door_lock":
        body = {
            "placed_object_id": args["placed_object_id"],
            "locked": args["locked"],
        }
        if "key_item_id" in args:
            body["key_item_id"] = args["key_item_id"]
        return await api_post("/api/door/lock", body)

    elif name == "unregister_door":
        return await api_post("/api/door/unregister", {"placed_object_id": args["placed_object_id"]})

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
        return await api_post_async("/api/world/fill", body)

    # --- Materials ---
    elif name == "list_materials":
        return await api_get("/api/materials")

    elif name == "add_material":
        body = {"name": args["name"]}
        if "emissive" in args:
            body["emissive"] = args["emissive"]
        if "physics" in args:
            body["physics"] = args["physics"]
        return await api_post("/api/materials/add", body)

    elif name == "remove_material":
        return await api_post("/api/materials/remove", {"name": args["name"]})

    elif name == "save_materials":
        body = {}
        if "path" in args:
            body["path"] = args["path"]
        return await api_post("/api/materials/save", body)

    elif name == "reload_atlas":
        return await api_post("/api/atlas/reload", {})

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

    # --- Screenshot / Visual Diagnostics ---
    elif name == "screenshot":
        return await api_get("/api/screenshot")

    elif name == "get_visual_diagnostic":
        return await _get_visual_diagnostic(args)

    elif name == "orbit_screenshots":
        return await _orbit_screenshots(args)

    elif name == "set_debug_overlay":
        overlay = args.get("overlay", "none")
        return await _set_overlay(overlay)

    elif name == "get_engine_logs":
        return _read_engine_logs(
            lines=args.get("lines", 50),
            module=args.get("module"),
            level=args.get("level")
        )

    elif name == "set_log_level":
        return await api_post("/api/logs/level", {
            "module": args.get("module", "global"),
            "level":  args.get("level", "info")
        })

    elif name == "get_render_stats":
        return await api_get("/api/render/stats")

    # --- Region Scan ---
    elif name == "scan_region":
        return await api_get("/api/world/scan", {
            "x1": str(args["x1"]), "y1": str(args["y1"]), "z1": str(args["z1"]),
            "x2": str(args["x2"]), "y2": str(args["y2"]), "z2": str(args["z2"])
        })

    # --- Region Clear ---
    elif name == "clear_region":
        return await api_post_async("/api/world/clear", {
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    # --- World Save ---
    elif name == "save_world":
        body: dict[str, Any] = {}
        if "all" in args:
            body["all"] = args["all"]
        return await api_post("/api/world/save", body)

    # --- Scene Management ---
    elif name == "list_scenes":
        return await api_get("/api/scenes")

    elif name == "get_active_scene":
        return await api_get("/api/scene/active")

    elif name == "transition_scene":
        return await api_post("/api/scene/transition", {
            "scene_id": args["scene_id"]
        })

    elif name == "add_scene":
        return await api_post("/api/scene/add", args)

    elif name == "get_scene":
        return await api_get(f"/api/scene/{args['scene_id']}")

    elif name == "update_scene":
        return await api_post("/api/scene/update", args)

    elif name == "remove_scene":
        return await api_post("/api/scene/remove", {
            "scene_id": args["scene_id"]
        })

    elif name == "save_scene_manifest":
        body: dict[str, Any] = {}
        if "path" in args:
            body["path"] = args["path"]
        return await api_post("/api/scene/manifest/save", body)

    # --- Game Menu Element Control ---
    elif name == "get_menu_element":
        return await api_get(f"/api/menu/element/{args['id']}")

    elif name == "set_menu_element":
        return await api_post("/api/menu/element/set", args)

    elif name == "add_menu_element":
        return await api_post("/api/menu/element/add", args)

    elif name == "remove_menu_element":
        return await api_post("/api/menu/element/remove", args)

    elif name == "open_menu_submenu":
        return await api_post("/api/menu/submenu/open", args)

    elif name == "close_menu_submenu":
        return await api_post("/api/menu/submenu/close", {})

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

    # --- Undo / Redo ---
    elif name == "undo":
        return await api_post("/api/undo", {})

    elif name == "redo":
        return await api_post("/api/redo", {})

    elif name == "get_undo_status":
        return await api_get("/api/undo/status")

    # --- Placed Objects ---
    elif name == "list_placed_objects":
        return await api_get("/api/placed_objects")

    elif name == "get_placed_object":
        return await api_get("/api/placed_object", {"id": args["id"]})

    elif name == "remove_placed_object":
        return await api_post("/api/placed_object/remove", {"id": args["id"]})

    elif name == "move_placed_object":
        return await api_post("/api/placed_object/move", {
            "id": args["id"],
            "position": args["position"]
        })

    elif name == "rotate_placed_object":
        return await api_post("/api/placed_object/rotate", {
            "id": args["id"],
            "rotation": args["rotation"]
        })

    elif name == "get_objects_at":
        return await api_get("/api/placed_objects/at", {
            "x": str(args["x"]), "y": str(args["y"]), "z": str(args["z"])
        })

    elif name == "set_object_parent":
        return await api_post("/api/placed_object/set_parent", {
            "id": args["id"],
            "parent_id": args["parent_id"]
        })

    elif name == "get_object_children":
        return await api_get("/api/placed_object/children", {
            "id": args.get("id", "")
        })

    elif name == "get_object_tree":
        return await api_get("/api/placed_object/tree", {
            "id": args["id"]
        })

    # --- Dynamic Furniture ---
    elif name == "activate_furniture":
        body = {"id": args["id"]}
        for key in ("impulse_x", "impulse_y", "impulse_z",
                     "contact_x", "contact_y", "contact_z"):
            if key in args:
                body[key] = args[key]
        return await api_post("/api/furniture/activate", body)

    elif name == "deactivate_furniture":
        body = {"id": args["id"]}
        if "restore_original" in args:
            body["restore_original"] = args["restore_original"]
        return await api_post("/api/furniture/deactivate", body)

    elif name == "list_dynamic_furniture":
        return await api_get("/api/furniture/list")

    elif name == "shatter_furniture":
        body = {"id": args["id"]}
        if "force" in args:
            body["force"] = args["force"]
        if "contact_x" in args:
            body["contact_x"] = args["contact_x"]
        if "contact_y" in args:
            body["contact_y"] = args["contact_y"]
        if "contact_z" in args:
            body["contact_z"] = args["contact_z"]
        return await api_post("/api/furniture/shatter", body)

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

    elif name == "move_region":
        body = {
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"],
            "dx": args["dx"], "dy": args["dy"], "dz": args["dz"]
        }
        if "rotate" in args:
            body["rotate"] = args["rotate"]
        return await api_post("/api/world/move_region", body)

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
        return await api_post_async("/api/world/generate", body)

    # --- Template Generation (BlockSmith) ---
    elif name == "generate_template":
        import subprocess as _sp
        # Enhance prompt in-process (avoids litellm hanging in child process)
        gen_prompt = args["prompt"]
        if args.get("enhance_prompt", False) or args.get("image"):
            _ep_prompt = gen_prompt
            _ep_model  = args.get("model", "anthropic/claude-sonnet-4-20250514")
            _ep_image  = args.get("image")
            gen_prompt = await asyncio.get_event_loop().run_in_executor(
                None, lambda: _enhance_prompt_inprocess(_ep_prompt, _ep_model, _ep_image)
            )
        cmd = [
            sys.executable,
            os.path.join(os.path.dirname(os.path.dirname(__file__)), "..", "tools", "blocksmith_generate.py"),
            gen_prompt,
            "--name", args["name"],
            "--material", args.get("material", "Wood"),
            "--size", str(args.get("size", 3.0)),
            "--json",
        ]
        if args.get("force", False):
            cmd.append("--force")
        if args.get("native", False):
            cmd.append("--native")
        if args.get("image"):
            cmd += ["--image", args["image"]]
        # Note: --enhance-prompt NOT passed — already enhanced above
        result = _sp.run(cmd, capture_output=True, text=True, timeout=180)
        if result.returncode != 0:
            return {"success": False, "error": result.stderr.strip() or "Generation failed"}
        try:
            gen_result = json.loads(result.stdout.strip().split("\n")[-1])
        except json.JSONDecodeError:
            return {"success": False, "error": f"Failed to parse output: {result.stdout}"}

        if args.get("auto_inspect", False) and gen_result.get("success"):
            inspect_content = await _inspect_template({
                "template_name": args["name"],
            })
            return [TextContent(type="text", text=json.dumps(gen_result))] + inspect_content

        return gen_result

    elif name == "search_templates":
        catalog_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "..", "resources", "templates", "template_catalog.json")
        if not os.path.exists(catalog_path):
            return {"results": [], "message": "No catalog found"}
        with open(catalog_path, "r", encoding="utf-8") as f:
            catalog = json.load(f)
        query = args["query"].lower()
        results = []
        for name_key, info in catalog.items():
            if query in name_key.lower() or query in info.get("prompt", "").lower():
                results.append({"name": name_key, **info})
        return {"results": results, "count": len(results)}

    elif name == "list_generated_templates":
        catalog_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "..", "resources", "templates", "template_catalog.json")
        if not os.path.exists(catalog_path):
            return {"templates": [], "count": 0}
        with open(catalog_path, "r", encoding="utf-8") as f:
            catalog = json.load(f)
        templates = [{"name": k, **v} for k, v in catalog.items()]
        return {"templates": templates, "count": len(templates)}

    elif name == "build_building":
        import subprocess as _sp
        # Step 1: Generate building template via blocksmith_generate.py --building
        tools_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), "..", "tools")
        cmd = [
            sys.executable,
            os.path.join(tools_dir, "blocksmith_generate.py"),
            args.get("extra_notes", ""),
            "--name", args["name"],
            "--building",
            "--building-type", args.get("building_type", "house"),
            "--style", args.get("style", "medieval"),
            "--width", str(args.get("width", 10)),
            "--depth", str(args.get("depth", 12)),
            "--height", str(args.get("height", 4)),
            "--stories", str(args.get("stories", 1)),
            "--door-facing", args.get("door_facing", "front"),
            "--windows", str(args.get("windows", 2)),
            "--json",
        ]
        if args.get("materials"):
            cmd.extend(["--materials", json.dumps(args["materials"])])
        if args.get("force", False):
            cmd.append("--force")

        gen_result = _sp.run(cmd, capture_output=True, text=True, timeout=180)
        if gen_result.returncode != 0:
            return {"success": False, "error": gen_result.stderr.strip() or "Building generation failed"}
        try:
            gen_info = json.loads(gen_result.stdout.strip().split("\n")[-1])
        except json.JSONDecodeError:
            return {"success": False, "error": f"Failed to parse output: {gen_result.stdout}"}
        if not gen_info.get("success"):
            return gen_info

        # Step 2: Spawn the template in the world
        pos = args["position"]
        rotation = args.get("rotation", 0)
        spawn_body = {
            "name": args["name"],
            "x": pos["x"],
            "y": pos["y"],
            "z": pos["z"],
            "dynamic": False,
        }
        if rotation:
            spawn_body["rotation"] = rotation
        spawn_result = await api_post("/api/world/template", spawn_body)

        return {
            "success": True,
            "template": gen_info,
            "spawn": spawn_result,
            "building_type": args.get("building_type", "house"),
            "style": args.get("style", "medieval"),
            "position": pos,
            "rotation": rotation,
            "cached": gen_info.get("cached", False),
        }

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

    elif name == "get_npc_appearance":
        return await api_get(f"/api/npc/appearance?name={args['name']}")

    elif name == "set_npc_appearance":
        return await api_post("/api/npc/appearance", {
            "name": args["name"], "appearance": args["appearance"]
        })

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
        return await api_post_async("/api/game/load_definition", args)

    elif name == "export_game_definition":
        return await api_get("/api/game/export_definition")

    elif name == "validate_game_definition":
        definition = args.get("definition", args)
        return await api_post("/api/game/validate_definition", definition)

    elif name == "create_game_npc":
        return await api_post("/api/game/create_npc", args)

    # --- Game State (Pause, Health, Respawn, Music, Save/Load, Objectives) ---

    elif name == "toggle_pause":
        body = {}
        if "paused" in args:
            body["paused"] = args["paused"]
        return await api_post("/api/game/pause", body)

    elif name == "get_pause_state":
        return await api_get("/api/game/pause")

    elif name == "get_player_health":
        return await api_get("/api/game/health")

    elif name == "damage_player":
        return await api_post("/api/game/health", {
            "action": "damage", "amount": args.get("amount", 10)
        })

    elif name == "heal_player":
        return await api_post("/api/game/health", {
            "action": "heal", "amount": args.get("amount", 10)
        })

    elif name == "kill_player":
        return await api_post("/api/game/health", {"action": "kill"})

    elif name == "revive_player":
        body: dict[str, Any] = {"action": "revive"}
        if "healthPercent" in args:
            body["healthPercent"] = args["healthPercent"]
        return await api_post("/api/game/health", body)

    elif name == "get_respawn_state":
        return await api_get("/api/game/respawn")

    elif name == "set_spawn_point":
        return await api_post("/api/game/respawn", {
            "spawn_point": {"x": args["x"], "y": args["y"], "z": args["z"]}
        })

    elif name == "force_respawn":
        return await api_post("/api/game/respawn", {"force_respawn": True})

    elif name == "get_music_state":
        return await api_get("/api/game/music")

    elif name == "control_music":
        body = {"action": args["action"]}
        if "path" in args:
            body["path"] = args["path"]
        if "volume" in args:
            body["volume"] = args["volume"]
        if "mode" in args:
            body["mode"] = args["mode"]
        return await api_post("/api/game/music", body)

    elif name == "save_player":
        return await api_post("/api/game/save", {})

    elif name == "load_player":
        return await api_post("/api/game/load", {})

    elif name == "get_objectives":
        return await api_get("/api/game/objectives")

    elif name == "add_objective":
        body = {"action": "add", "id": args["id"], "title": args["title"]}
        if "description" in args:
            body["description"] = args["description"]
        if "category" in args:
            body["category"] = args["category"]
        if "priority" in args:
            body["priority"] = args["priority"]
        if "hidden" in args:
            body["hidden"] = args["hidden"]
        return await api_post("/api/game/objectives", body)

    elif name == "complete_objective":
        return await api_post("/api/game/objectives", {
            "action": "complete", "id": args["id"]
        })

    elif name == "fail_objective":
        return await api_post("/api/game/objectives", {
            "action": "fail", "id": args["id"]
        })

    elif name == "remove_objective":
        return await api_post("/api/game/objectives", {
            "action": "remove", "id": args["id"]
        })

    # --- Project Lifecycle ---

    elif name == "build_project":
        return await _build_project(args)

    elif name == "launch_engine":
        return await _launch_engine(args)

    elif name == "stop_engine":
        return await _stop_engine(float(args.get("grace_seconds", 5.0)))

    elif name == "restart_engine":
        return await _restart_engine(args)

    elif name == "clear_engine_logs":
        return _clear_engine_logs()

    elif name == "launch_asset_editor":
        return await _launch_asset_editor(args)

    elif name == "close_asset_editor":
        return await _close_asset_editor()

    elif name == "reload_asset_editor":
        return await _reload_asset_editor(args)

    elif name == "inspect_template":
        return await _inspect_template(args)

    elif name == "critique_template":
        return await _critique_template(args)

    elif name == "refine_template":
        return await _refine_template(args)

    elif name == "generate_asset":
        return await _generate_asset(args)

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

    # --- Project Management ---

    elif name == "list_projects":
        return await api_get("/api/projects/list")

    elif name == "create_project":
        return await api_post("/api/projects/create", {"name": args["name"]})

    elif name == "open_project":
        return await api_post("/api/projects/open", {"path": args["path"]})

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

    # --- Inventory ---
    elif name == "get_inventory":
        return await api_get("/api/inventory")

    elif name == "give_item":
        body = {"material": args["material"], "count": args.get("count", 1)}
        return await api_post("/api/inventory/give", body)

    elif name == "take_item":
        body = {"material": args["material"], "count": args.get("count", 1)}
        return await api_post("/api/inventory/take", body)

    elif name == "select_hotbar_slot":
        return await api_post("/api/inventory/select", {"slot": args["slot"]})

    elif name == "set_inventory_slot":
        body: dict[str, Any] = {"slot": args["slot"]}
        if "material" in args:
            body["material"] = args["material"]
            body["count"] = args.get("count", 1)
        return await api_post("/api/inventory/set_slot", body)

    elif name == "clear_inventory":
        return await api_post("/api/inventory/clear", {})

    elif name == "set_creative_mode":
        return await api_post("/api/inventory/creative", {"enabled": args["enabled"]})

    # --- Health / Damage ---
    elif name == "damage_entity":
        body = {"id": args["id"], "amount": args["amount"]}
        if "source" in args:
            body["source"] = args["source"]
        return await api_post("/api/entity/damage", body)

    elif name == "heal_entity":
        return await api_post("/api/entity/heal", {"id": args["id"], "amount": args["amount"]})

    elif name == "set_entity_health":
        body: dict[str, Any] = {"id": args["id"]}
        if "health" in args:
            body["health"] = args["health"]
        if "maxHealth" in args:
            body["maxHealth"] = args["maxHealth"]
        if "invulnerable" in args:
            body["invulnerable"] = args["invulnerable"]
        return await api_post("/api/entity/set_health", body)

    elif name == "kill_entity":
        return await api_post("/api/entity/kill", {"id": args["id"]})

    elif name == "revive_entity":
        body = {"id": args["id"]}
        if "healthPercent" in args:
            body["healthPercent"] = args["healthPercent"]
        return await api_post("/api/entity/revive", body)

    # --- Day/Night Cycle ---
    elif name == "get_day_night":
        return await api_get("/api/daynight")

    elif name == "set_day_night":
        body: dict[str, Any] = {}
        for key in ("timeOfDay", "dayNumber", "enabled", "paused", "dayLengthSeconds", "timeScale"):
            if key in args:
                body[key] = args[key]
        return await api_post("/api/daynight/set", body)

    # --- Item Registry ---
    elif name == "list_items":
        return await api_get("/api/items")

    elif name == "get_item":
        item_id = args["id"]
        return await api_get(f"/api/items/{item_id}")

    # --- Equipment & Combat ---
    elif name == "get_equipment":
        entity_id = args["entityId"]
        return await api_get(f"/api/entity/{entity_id}/equipment")

    elif name == "equip_item":
        entity_id = args["entityId"]
        return await api_post(f"/api/entity/{entity_id}/equip", {"itemId": args["itemId"]})

    elif name == "unequip_item":
        entity_id = args["entityId"]
        return await api_post(f"/api/entity/{entity_id}/unequip", {"slot": args["slot"]})

    elif name == "attack":
        body: dict[str, Any] = {"attackerId": args["attackerId"]}
        if "forward" in args:
            body["forward"] = args["forward"]
        if "coneAngle" in args:
            body["coneAngle"] = args["coneAngle"]
        if "knockback" in args:
            body["knockback"] = args["knockback"]
        return await api_post("/api/combat/attack", body)

    # --- Lighting ---
    elif name == "list_lights":
        return await api_get("/api/lights")

    elif name == "add_point_light":
        body: dict[str, Any] = {"x": args["x"], "y": args["y"], "z": args["z"]}
        if "color" in args:
            body["color"] = args["color"]
        if "intensity" in args:
            body["intensity"] = args["intensity"]
        if "radius" in args:
            body["radius"] = args["radius"]
        if "enabled" in args:
            body["enabled"] = args["enabled"]
        return await api_post("/api/light/point/add", body)

    elif name == "add_spot_light":
        body: dict[str, Any] = {"x": args["x"], "y": args["y"], "z": args["z"]}
        for key in ("dx", "dy", "dz", "intensity", "radius", "inner_cone", "outer_cone", "enabled"):
            if key in args:
                body[key] = args[key]
        if "color" in args:
            body["color"] = args["color"]
        return await api_post("/api/light/spot/add", body)

    elif name == "remove_light":
        return await api_post("/api/light/remove", {"id": args["id"]})

    elif name == "update_light":
        body: dict[str, Any] = {"id": args["id"]}
        for key in ("x", "y", "z", "dx", "dy", "dz", "intensity", "radius",
                     "inner_cone", "outer_cone", "enabled", "color"):
            if key in args:
                body[key] = args[key]
        return await api_post("/api/light/update", body)

    elif name == "set_ambient_light":
        return await api_post("/api/ambient", {"strength": args["strength"]})

    # --- VFX ---
    elif name == "spawn_vfx":
        body: dict[str, Any] = {
            "effect": args.get("effect", "fireball"),
            "x": args["x"], "y": args["y"], "z": args["z"],
        }
        return await api_post("/api/vfx/spawn", body)

    elif name == "cast_vfx_projectile":
        body: dict[str, Any] = {
            "effect": args.get("effect", "fireball"),
            "from": args["from"],
            "to": args["to"],
        }
        return await api_post("/api/vfx/projectile", body)

    elif name == "cast_vfx_beam":
        body: dict[str, Any] = {
            "effect": args.get("effect", "eldritch_blast"),
            "from": args["from"],
            "to": args["to"],
        }
        return await api_post("/api/vfx/beam", body)

    elif name == "cast_vfx_field":
        body: dict[str, Any] = {
            "effect": args.get("effect", "shield"),
            "x": args["x"], "y": args["y"], "z": args["z"],
        }
        return await api_post("/api/vfx/field", body)

    # --- Audio ---
    elif name == "list_sounds":
        return await api_get("/api/audio/sounds")

    elif name == "play_sound":
        body: dict[str, Any] = {"file": args["file"]}
        for key in ("volume", "channel", "x", "y", "z"):
            if key in args:
                body[key] = args[key]
        return await api_post("/api/audio/play", body)

    elif name == "set_volume":
        return await api_post("/api/audio/volume", {
            "channel": args["channel"],
            "volume": args["volume"]
        })

    # --- Custom UI Menu Management ---
    elif name == "list_menus":
        return await api_get("/api/ui/menus")

    elif name == "create_menu":
        return await api_post("/api/ui/menu", {
            "name": args["name"],
            "definition": args["definition"]
        })

    elif name == "show_menu":
        return await api_post("/api/ui/menu/show", {"name": args["name"]})

    elif name == "hide_menu":
        return await api_post("/api/ui/menu/hide", {"name": args["name"]})

    elif name == "toggle_menu":
        return await api_post("/api/ui/menu/toggle", {"name": args["name"]})

    elif name == "remove_menu":
        return await api_post("/api/ui/menu/remove", {"name": args["name"]})

    # --- Bulk Operations ---
    elif name == "clear_chunk":
        return await api_post("/api/world/clear_chunk", {
            "x": args["x"], "y": args["y"], "z": args["z"]
        })

    elif name == "rebuild_physics":
        body = {}
        if "chunk" in args:
            body["chunk"] = args["chunk"]
        return await api_post("/api/world/rebuild_physics", body)

    elif name == "clear_all_entities":
        return await api_post("/api/entities/clear", {})

    elif name == "reload_game_definition":
        return await api_post("/api/game/reload", {
            "definition": args["definition"]
        })

    elif name == "get_terrain_height":
        params = {"x": str(args["x"]), "z": str(args["z"])}
        if "max_y" in args:
            params["max_y"] = str(args["max_y"])
        if "min_y" in args:
            params["min_y"] = str(args["min_y"])
        return await api_get("/api/world/terrain_height", params)

    # --- Animation Control ---
    elif name == "list_entity_animations":
        return await api_get("/api/animation/list", {"id": args["id"]})

    elif name == "play_entity_animation":
        return await api_post("/api/animation/play", {
            "id": args["id"],
            "animation": args["animation"]
        })

    elif name == "get_animation_state":
        return await api_get("/api/animation/state", {"id": args["id"]})

    elif name == "set_animation_state":
        return await api_post("/api/animation/state", {
            "id": args["id"],
            "state": args["state"]
        })

    elif name == "set_blend_duration":
        return await api_post("/api/animation/blend", {
            "id": args["id"],
            "duration": args["duration"]
        })

    elif name == "reload_entity_animation":
        return await api_post("/api/animation/reload", {
            "id": args["id"],
            "animFile": args["animFile"]
        })

    elif name == "seek_animation":
        return await api_post("/api/animation/seek", {
            "id": args["id"],
            "time": args["time"]
        })

    elif name == "resume_animation":
        return await api_post("/api/animation/resume", {"id": args["id"]})

    elif name == "get_bone_positions":
        return await api_get(f"/api/entity/{args['id']}/bones")

    elif name == "get_character_design_constraints":
        archetype = args.get("archetype", "humanoid_normal")
        return await api_get(f"/api/character/design_constraints?archetype={archetype}")

    elif name == "sit_character":
        body = {"entity_id": args["entity_id"], "object_id": args["object_id"]}
        if "point_id" in args:
            body["point_id"] = args["point_id"]
        return await api_post("/api/interaction/sit", body)

    elif name == "stand_up_character":
        return await api_post("/api/interaction/stand_up", {"entity_id": args["entity_id"]})

    elif name == "set_interaction_profile":
        body = {
            "archetype":    args.get("archetype", "humanoid_normal"),
            "template_name": args["template_name"],
            "point_id":     args.get("point_id", "seat_0"),
        }
        for key in ("sit_down_offset", "sitting_idle_offset", "sit_stand_up_offset",
                    "sit_blend_duration", "seat_height_offset"):
            if key in args:
                body[key] = args[key]
        return await api_post("/api/interaction/profile", body)

    elif name == "get_interaction_profile":
        return await api_get("/api/interaction/profile", {
            "archetype":     args.get("archetype", "humanoid_normal"),
            "template_name": args["template_name"],
            "point_id":      args.get("point_id", "seat_0"),
        })

    elif name == "ie_sit_preview":
        return await api_post("/api/interaction/ie/sit", {})

    elif name == "ie_stand_preview":
        return await api_post("/api/interaction/ie/stand", {})

    elif name == "ie_preview_state":
        return await api_get("/api/interaction/ie/state")

    elif name == "seek_ie_animation":
        return await api_post("/api/interaction/ie/seek", {
            "clip_name": args["clip_name"],
            "normalized_time": args["normalized_time"]
        })

    elif name == "ie_resume_animation":
        return await api_post("/api/interaction/ie/resume", {})

    elif name == "validate_ie_pose":
        return await api_get("/api/interaction/ie/validate")

    elif name == "validate_ie_animation":
        samples = args.get("samples", 30)
        return await api_get(f"/api/interaction/ie/validate_animation?samples={samples}")

    elif name == "get_npc_blackboard":
        return await api_get(f"/api/npc/{args['name']}/blackboard")

    elif name == "get_npc_perception":
        return await api_get(f"/api/npc/{args['name']}/perception")

    elif name == "set_npc_blackboard":
        return await api_post(f"/api/npc/{args['name']}/blackboard", {
            "key": args["key"],
            "value": args["value"]
        })

    elif name == "get_locations":
        return await api_get("/api/locations")

    elif name == "add_location":
        return await api_post("/api/locations", args)

    elif name == "remove_location":
        return await api_post("/api/locations/remove", args)

    elif name == "get_npc_schedule":
        return await api_get(f"/api/npc/{args['name']}/schedule")

    elif name == "set_npc_schedule":
        payload = {}
        if "role" in args:
            payload["role"] = args["role"]
        if "schedule" in args:
            payload["schedule"] = args["schedule"]
        return await api_post(f"/api/npc/{args['name']}/schedule", payload)

    # --- Social Simulation ---
    elif name == "get_npc_needs":
        return await api_get(f"/api/npc/{args['name']}/needs")

    elif name == "set_npc_needs":
        payload = {}
        if "type" in args:
            payload["type"] = args["type"]
        if "value" in args:
            payload["value"] = args["value"]
        if "needs" in args:
            payload["needs"] = args["needs"]
        return await api_post(f"/api/npc/{args['name']}/needs", payload)

    elif name == "get_npc_relationships":
        npc_name = args.get("name", "")
        if npc_name:
            return await api_get(f"/api/npc/{npc_name}/relationships")
        else:
            return await api_get("/api/relationships")

    elif name == "set_npc_relationship":
        return await api_post("/api/npc/relationship", args)

    elif name == "apply_npc_interaction":
        return await api_post("/api/npc/interaction", args)

    elif name == "get_npc_worldview":
        return await api_get(f"/api/npc/{args['name']}/worldview")

    elif name == "set_npc_belief":
        payload = {"key": args["key"], "value": args["value"]}
        if "confidence" in args:
            payload["confidence"] = args["confidence"]
        return await api_post(f"/api/npc/{args['name']}/belief", payload)

    elif name == "set_npc_opinion":
        payload = {"subject": args["subject"], "sentiment": args["sentiment"]}
        if "reason" in args:
            payload["reason"] = args["reason"]
        return await api_post(f"/api/npc/{args['name']}/opinion", payload)

    # --- AI / LLM ---
    elif name == "get_ai_status":
        return await api_get("/api/ai/status")

    elif name == "configure_ai":
        return await api_post("/api/ai/configure", args)

    elif name == "start_ai_conversation":
        return await api_post("/api/ai/conversation/start", args)

    elif name == "send_ai_message":
        return await api_post("/api/ai/conversation/send", args)

    # -----------------------------------------------------------------------
    # D&D RPG — Stateless dice tools (no engine required)
    # -----------------------------------------------------------------------
    elif name == "roll_dice":
        return _rpg_roll_dice(args)

    elif name == "check_dc":
        return _rpg_check_dc(args)

    # -----------------------------------------------------------------------
    # D&D RPG — Party
    # -----------------------------------------------------------------------
    elif name == "get_party":
        return await api_get("/api/rpg/party")

    elif name == "add_party_member":
        body: dict[str, Any] = {"entity_id": args["entity_id"]}
        if "name" in args:
            body["name"] = args["name"]
        if "level" in args:
            body["level"] = args["level"]
        return await api_post("/api/rpg/party/add", body)

    elif name == "remove_party_member":
        return await api_post("/api/rpg/party/remove", {"entity_id": args["entity_id"]})

    # -----------------------------------------------------------------------
    # D&D RPG — Combat / Initiative
    # -----------------------------------------------------------------------
    elif name == "get_combat_state":
        return await api_get("/api/rpg/combat/state")

    elif name == "start_combat":
        body = {}
        if "participants" in args:
            body["participants"] = args["participants"]
        return await api_post("/api/rpg/combat/start", body)

    elif name == "next_combat_turn":
        return await api_post("/api/rpg/combat/next_turn", {})

    elif name == "end_combat":
        return await api_post("/api/rpg/combat/end", {})

    elif name == "set_initiative":
        return await api_post("/api/rpg/combat/set_initiative", {
            "entity_id": args["entity_id"],
            "value": args["value"]
        })

    # -----------------------------------------------------------------------
    # D&D RPG — World Calendar
    # -----------------------------------------------------------------------
    elif name == "get_world_date":
        return await api_get("/api/rpg/world/date")

    elif name == "advance_world_date":
        return await api_post("/api/rpg/world/advance_date", {"days": args.get("days", 1)})

    elif name == "set_world_date":
        return await api_post("/api/rpg/world/set_date", {"total_days": args["total_days"]})

    # -----------------------------------------------------------------------
    # D&D RPG — Campaign Journal
    # -----------------------------------------------------------------------
    elif name == "add_journal_entry":
        body = {
            "title":   args["title"],
            "content": args.get("content", ""),
            "type":    args.get("type", "WorldEvent"),
        }
        if "day" in args:
            body["day"] = args["day"]
        if "tags" in args:
            body["tags"] = args["tags"]
        return await api_post("/api/rpg/journal/add", body)

    elif name == "get_journal_entries":
        body: dict[str, Any] = {}
        for key in ("type", "tag", "day", "search"):
            if key in args:
                body[key] = args[key]
        return await api_post("/api/rpg/journal/entries", body)

    elif name == "remove_journal_entry":
        return await api_post("/api/rpg/journal/remove", {"id": args["id"]})

    else:
        return {"error": f"Unknown tool: {name}"}


# ============================================================================
# D&D RPG — Stateless Python helpers (no engine required)
# ============================================================================

def _roll_die(sides: int) -> int:
    """Roll a single die with the given number of sides."""
    return random.randint(1, sides)


def _parse_and_roll(expression: str) -> dict:
    """
    Parse a dice expression like '2d6+3' or '1d20' and roll it.
    Returns {"dice": [...], "modifier": int, "total": int, "expression": str}.
    Supports basic NdS[+/-M] format only.
    """
    import re
    expr = expression.strip().lower().replace(" ", "")
    m = re.fullmatch(r"(\d+)d(\d+)([+-]\d+)?", expr)
    if not m:
        # Try bare die like 'd20'
        m2 = re.fullmatch(r"d(\d+)([+-]\d+)?", expr)
        if m2:
            count, sides, mod_str = 1, int(m2.group(1)), m2.group(2) or "+0"
        else:
            return {"error": f"Cannot parse dice expression: {expression}"}
    else:
        count, sides, mod_str = int(m.group(1)), int(m.group(2)), m.group(3) or "+0"

    if sides < 2 or count < 1 or count > 100:
        return {"error": "Invalid dice parameters"}

    modifier = int(mod_str)
    rolls = [_roll_die(sides) for _ in range(count)]
    total = sum(rolls) + modifier
    return {
        "expression": expression,
        "dice":       rolls,
        "modifier":   modifier,
        "total":      total,
        "die_type":   f"d{sides}",
        "count":      count
    }


def _rpg_roll_dice(args: dict) -> dict:
    """Stateless dice roller — implements the roll_dice MCP tool."""
    expression  = args.get("expression", "1d20")
    advantage   = args.get("advantage", False)
    disadvantage = args.get("disadvantage", False)
    count       = max(1, int(args.get("count", 1)))

    if count > 1:
        results = [_parse_and_roll(expression) for _ in range(count)]
        if any("error" in r for r in results):
            return results[0]  # return first error
        return {"rolls": results, "expression": expression, "count": count}

    if advantage and not disadvantage:
        r1 = _parse_and_roll(expression)
        r2 = _parse_and_roll(expression)
        if "error" in r1:
            return r1
        chosen = r1 if r1["total"] >= r2["total"] else r2
        return {**chosen, "advantage": True, "other_roll": r1["total"] if chosen is r2 else r2["total"]}

    if disadvantage and not advantage:
        r1 = _parse_and_roll(expression)
        r2 = _parse_and_roll(expression)
        if "error" in r1:
            return r1
        chosen = r1 if r1["total"] <= r2["total"] else r2
        return {**chosen, "disadvantage": True, "other_roll": r1["total"] if chosen is r2 else r2["total"]}

    return _parse_and_roll(expression)


def _rpg_check_dc(args: dict) -> dict:
    """Stateless DC check — implements the check_dc MCP tool."""
    dc          = int(args.get("dc", 10))
    bonus       = int(args.get("bonus", 0))
    advantage   = args.get("advantage", False)
    disadvantage = args.get("disadvantage", False)

    roll_args = {"expression": "1d20", "advantage": advantage, "disadvantage": disadvantage}
    roll = _rpg_roll_dice(roll_args)
    if "error" in roll:
        return roll

    d20 = roll["dice"][0]
    total = d20 + bonus
    passed = total >= dc
    is_nat20 = d20 == 20
    is_nat1  = d20 == 1

    return {
        "passed":       passed,
        "d20_roll":     d20,
        "bonus":        bonus,
        "total":        total,
        "dc":           dc,
        "margin":       total - dc,
        "natural_20":   is_nat20,
        "natural_1":    is_nat1,
        "advantage":    advantage,
        "disadvantage": disadvantage,
    }


# ============================================================================
# Project Lifecycle Helpers
# ============================================================================

# Resolve project root (parent of scripts/mcp/)
PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent

# Load .env from repo root (gitignored local config)
_env_file = PROJECT_ROOT / ".env"
if _env_file.exists():
    for _line in _env_file.read_text().splitlines():
        _line = _line.strip()
        if _line and not _line.startswith("#") and "=" in _line:
            _k, _, _v = _line.partition("=")
            os.environ.setdefault(_k.strip(), _v.strip())

# Propagate PHYXEL_AI_API_KEY → ANTHROPIC_API_KEY (blocksmith alias)
if os.environ.get("PHYXEL_AI_API_KEY") and not os.environ.get("ANTHROPIC_API_KEY"):
    os.environ["ANTHROPIC_API_KEY"] = os.environ["PHYXEL_AI_API_KEY"]


def _embed_screenshot(path_str: str) -> ImageContent | None:
    """Read a screenshot PNG from disk and return an MCP ImageContent block.

    The engine writes screenshots relative to PROJECT_ROOT (e.g. "screenshots/foo.png").
    Tries the path as-is first, then relative to PROJECT_ROOT.
    """
    candidates = [Path(path_str), PROJECT_ROOT / path_str]
    for p in candidates:
        if p.exists():
            try:
                data = base64.standard_b64encode(p.read_bytes()).decode("ascii")
                return ImageContent(type="image", data=data, mimeType="image/png")
            except Exception as e:
                logger.warning(f"Failed to embed screenshot {p}: {e}")
                return None
    logger.warning(f"Screenshot file not found: {path_str}")
    return None


def _build_visual_response(result: dict) -> list:
    """Build MCP content list for a result that may contain a screenshot path."""
    content: list = [TextContent(type="text", text=json.dumps(result, indent=2))]
    path_str = (
        result.get("path")
        or (result.get("screenshot") or {}).get("path")
    )
    if path_str:
        img = _embed_screenshot(path_str)
        if img:
            content.append(img)
    return content


def _build_orbit_response(result: dict) -> list:
    """Build MCP content list for orbit_screenshots — one ImageContent per view."""
    # Summary text first (strip the bulky base64 blobs from the text block)
    summary = {k: v for k, v in result.items() if k != "screenshots"}
    summary["views_captured"] = [s["view"] for s in result.get("screenshots", [])]
    content: list = [TextContent(type="text", text=json.dumps(summary, indent=2))]
    for shot in result.get("screenshots", []):
        path_str = shot.get("path", "")
        if path_str:
            img = _embed_screenshot(path_str)
            if img:
                # Prepend a label so Claude knows which angle each image is
                content.append(TextContent(type="text", text=f"[{shot['view'].upper()} view]"))
                content.append(img)
    return content

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


_LEVEL_ORDER = {"trace": 0, "debug": 1, "info": 2, "warn": 3, "error": 4, "fatal": 5}


def _read_engine_logs(lines: int = 50, module: str | None = None, level: str | None = None) -> dict:
    """Read tail of phyxel.log, optionally filtered by module name and minimum log level."""
    log_path = PROJECT_ROOT / "phyxel.log"
    if not log_path.exists():
        return {"error": "phyxel.log not found", "searched": str(log_path)}

    try:
        content = log_path.read_text(encoding="utf-8", errors="replace")
        all_lines = content.splitlines()
        total = len(all_lines)

        min_level = _LEVEL_ORDER.get((level or "").lower(), -1)

        filtered = []
        for line in all_lines:
            if module and module.lower() not in line.lower():
                continue
            if min_level >= 0:
                matched_level = next(
                    (v for k, v in _LEVEL_ORDER.items() if f"[{k.upper()}]" in line.upper()),
                    -1
                )
                if matched_level < min_level:
                    continue
            filtered.append(line)

        result_lines = filtered[-lines:]
        return {
            "log_file":      str(log_path),
            "total_lines":   total,
            "matched_lines": len(filtered),
            "returned":      len(result_lines),
            "filters":       {"module": module, "level": level},
            "content":       "\n".join(result_lines)
        }
    except Exception as e:
        return {"error": f"Failed to read log: {e}"}


_OVERLAY_MODES = {
    "none":      (-1, False),
    "wireframe": (0,  True),
    "normals":   (1,  True),
    "hierarchy": (2,  True),
    "uv":        (3,  True),
    "emissive":  (4,  True),
}


async def _set_overlay(overlay: str) -> dict:
    mode, enabled = _OVERLAY_MODES.get(overlay, (-1, False))
    return await api_post("/api/debug/overlay", {"enabled": enabled, "mode": mode})


async def _orbit_screenshots(args: dict) -> dict:
    """Capture 6-angle orbit screenshots around a world position."""
    payload: dict = {
        "x":      float(args.get("x", 0)),
        "y":      float(args.get("y", 16)),
        "z":      float(args.get("z", 0)),
        "radius": float(args.get("radius", 4)),
    }
    if "views" in args:
        payload["views"] = args["views"]

    port = int(args.get("port", 8090))
    result = await _api_post_port("/api/orbit-screenshots", payload, port, timeout=120.0)

    if "error" in result:
        return result

    return {
        "success":     result.get("success", False),
        "target":      result.get("target", {}),
        "radius":      result.get("radius", 4),
        "screenshots": result.get("screenshots", []),
        "count":       len(result.get("screenshots", [])),
    }


async def _get_visual_diagnostic(args: dict) -> dict:
    """Capture screenshot + world state + render stats + recent logs in one call."""
    overlay = args.get("overlay", "none")
    log_module = args.get("log_module")   # optional module filter for logs
    log_lines  = args.get("log_lines", 40)

    # Save current overlay state so we can restore it
    prev = await api_get("/api/debug/overlay")

    # Activate requested overlay
    if overlay != "none":
        await _set_overlay(overlay)
    elif prev.get("enabled"):
        await api_post("/api/debug/overlay", {"enabled": False, "mode": 0})

    # Capture screenshot, camera, state, and render stats in parallel
    shot, camera, state, render_stats = await asyncio.gather(
        api_get("/api/screenshot"),
        api_get("/api/camera"),
        api_get("/api/state"),
        api_get("/api/render/stats"),
    )

    # Restore overlay state
    await api_post("/api/debug/overlay", {
        "enabled": prev.get("enabled", False),
        "mode":    prev.get("mode", 0),
    })

    # Read logs synchronously (local file read, no await needed)
    logs = _read_engine_logs(lines=log_lines, module=log_module, level="debug")

    return {
        "screenshot":    shot,
        "camera":        camera,
        "world":         state,
        "render_stats":  render_stats,
        "recent_logs":   logs,
        "overlay_used":  overlay,
        "path":          shot.get("path"),
    }


_engine_process = None
_engine_launch_args: dict = {}  # remembered so restart can replay them
_asset_editor_process = None
_asset_editor_port: int = 8091


async def _launch_asset_editor(args: dict) -> dict:
    """Launch the engine in asset-editor mode on a separate port."""
    global _asset_editor_process, _asset_editor_port

    template_path = args.get("template_path", "")
    port = int(args.get("port", 8091))
    config = args.get("config", "Debug")
    interaction_editor = bool(args.get("interaction_editor", False))

    # Resolve template path
    abs_path = Path(template_path)
    if not abs_path.is_absolute():
        abs_path = PROJECT_ROOT / template_path
    if not abs_path.exists():
        # Try resources/templates/
        candidate = PROJECT_ROOT / "resources" / "templates" / template_path
        if candidate.exists():
            abs_path = candidate
        elif (candidate.with_suffix(".voxel")).exists():
            abs_path = candidate.with_suffix(".voxel")
        else:
            return {"error": f"Template not found: {template_path}"}

    # Kill any existing asset editor — tracked or orphaned from a previous MCP session
    await _kill_process_on_port(port)

    exe_path = PROJECT_ROOT / "build" / "editor" / config / "phyxel.exe"
    if not exe_path.exists():
        exe_path = PROJECT_ROOT / "build" / "game" / config / "phyxel.exe"
    if not exe_path.exists():
        return {"error": "Engine executable not found. Build the project first."}

    editor_flag = "--interaction-editor" if interaction_editor else "--asset-editor"
    cmd = [str(exe_path), editor_flag, str(abs_path), "--port", str(port)]
    try:
        _asset_editor_process = subprocess.Popen(
            cmd,
            cwd=str(PROJECT_ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        _asset_editor_port = port
    except Exception as e:
        return {"error": f"Failed to launch asset editor: {e}"}

    # Poll /api/status until ready (up to 45s — engine startup takes ~30s on this machine)
    import time
    deadline = time.monotonic() + 45.0
    while time.monotonic() < deadline:
        await asyncio.sleep(1.0)
        if _asset_editor_process.poll() is not None:
            return {"error": "Asset editor process exited unexpectedly"}
        try:
            async with httpx.AsyncClient(base_url=f"http://localhost:{port}", timeout=2.0) as c:
                resp = await c.get("/api/status")
                if resp.status_code == 200:
                    return {
                        "success": True,
                        "pid": _asset_editor_process.pid,
                        "port": port,
                        "template": str(abs_path),
                    }
        except Exception:
            pass

    return {"error": "Asset editor did not become responsive within 15s"}


async def _kill_process_on_port(port: int) -> None:
    """Kill any process (tracked or orphaned) listening on the given TCP port."""
    # Kill the tracked process if it's on this port
    global _asset_editor_process
    if _asset_editor_process and _asset_editor_process.poll() is None:
        _asset_editor_process.terminate()
        await asyncio.sleep(1.0)
        if _asset_editor_process.poll() is None:
            _asset_editor_process.kill()
        _asset_editor_process = None

    # Also kill any orphaned process on this port (survives MCP server restarts)
    try:
        result = subprocess.run(
            ["netstat", "-ano", "-p", "TCP"],
            capture_output=True, text=True, timeout=5,
        )
        for line in result.stdout.splitlines():
            if f":{port} " in line and "LISTENING" in line:
                parts = line.split()
                pid = int(parts[-1])
                if pid > 0:
                    subprocess.run(["taskkill", "/F", "/PID", str(pid)],
                                   capture_output=True, timeout=5)
                    await asyncio.sleep(2.0)  # Let the OS release the port
                break
    except Exception:
        pass


async def _close_asset_editor() -> dict:
    """Stop the asset editor process (tracked or orphaned on the known port)."""
    global _asset_editor_process
    if _asset_editor_process is None or _asset_editor_process.poll() is not None:
        _asset_editor_process = None
        # Still try to kill any orphaned process on the default port
        await _kill_process_on_port(_asset_editor_port)
        return {"success": True, "message": "Asset editor was not running (checked for orphans)"}
    pid = _asset_editor_process.pid
    await _kill_process_on_port(_asset_editor_port)
    return {"success": True, "message": f"Asset editor (pid {pid}) stopped"}


async def _reload_asset_editor(args: dict) -> dict:
    """Hot-reload the template in the running asset editor (no process restart)."""
    port = int(args.get("port", 8091))
    body: dict = {}
    if "path" in args:
        body["path"] = args["path"]
    result = await _api_post_port("/api/asset-editor/reload", body, port, timeout=30.0)
    return result


async def _api_get_port(path: str, port: int) -> dict:
    """api_get but targets the given port instead of the default 8090."""
    try:
        async with httpx.AsyncClient(base_url=f"http://localhost:{port}", timeout=10.0) as c:
            resp = await c.get(path)
            resp.raise_for_status()
            return resp.json()
    except Exception as e:
        return {"error": str(e)}


async def _api_post_port(path: str, body: dict, port: int, timeout: float = 10.0) -> dict:
    """api_post but targets the given port."""
    try:
        async with httpx.AsyncClient(base_url=f"http://localhost:{port}", timeout=timeout) as c:
            resp = await c.post(path, json=body)
            resp.raise_for_status()
            return resp.json()
    except Exception as e:
        return {"error": str(e)}


async def _inspect_template(args: dict) -> list:
    """Take multi-angle screenshots in the asset editor and return them inline."""
    global _asset_editor_process, _asset_editor_port

    port   = int(args.get("port", _asset_editor_port or 8091))
    angles = int(args.get("angles", 4))
    config = args.get("config", "Debug")
    show_reference_character = args.get("show_reference_character", True)

    # Resolve template path
    template_path = None
    if args.get("template_path"):
        template_path = args["template_path"]
    elif args.get("template_name"):
        name = args["template_name"]
        p = PROJECT_ROOT / "resources" / "templates" / (name + ".voxel")
        template_path = str(p) if p.exists() else name

    if template_path is None:
        return [TextContent(type="text", text="ERROR: provide template_name or template_path")]

    # Ensure asset editor is running with this template
    editor_ready = (
        _asset_editor_process is not None and _asset_editor_process.poll() is None
    )
    if not editor_ready:
        launch_result = await _launch_asset_editor({
            "template_path": template_path, "port": port, "config": config
        })
        if "error" in launch_result:
            return [TextContent(type="text", text=f"ERROR: {launch_result['error']}")]

    # Camera orbit positions around template origin (13, 16, 13)
    # (x, y, z, yaw_deg, pitch_deg)  — asset editor camera JSON uses yaw/pitch in degrees
    camera_views = [
        {"pos": [13, 20, 28], "yaw": 180, "pitch": -25, "label": "front"},
        {"pos": [28, 20, 13], "yaw": 90, "pitch": -25, "label": "right"},
        {"pos": [2, 22, 2],   "yaw": -45, "pitch": -30, "label": "back-left"},
        {"pos": [13, 35, 13], "yaw": 180, "pitch": -75, "label": "top"},
    ][:angles]

    if show_reference_character:
        await _set_ref_character(True, port)

    screenshots = []
    for view in camera_views:
        cam_body = {
            "position": view["pos"],
            "yaw": view["yaw"],
            "pitch": view["pitch"],
        }
        await _api_post_port("/api/camera", cam_body, port)
        await asyncio.sleep(0.4)  # let render settle
        shot = await _api_get_port("/api/screenshot", port)
        shot_path = shot.get("path") if isinstance(shot, dict) else None
        img = _embed_screenshot(shot_path) if shot_path else None
        screenshots.append((view["label"], img))

    if show_reference_character:
        await _set_ref_character(False, port)

    # Read metadata from the .voxel file via blocksmith parser
    abs_voxel = Path(template_path)
    if not abs_voxel.is_absolute():
        abs_voxel = PROJECT_ROOT / template_path
    if not abs_voxel.exists():
        abs_voxel = PROJECT_ROOT / "resources" / "templates" / (args.get("template_name", "") + ".voxel")

    meta: dict = {}
    if abs_voxel.exists():
        try:
            import sys as _sys
            _bs_path = str(PROJECT_ROOT / "external" / "blocksmith")
            if _bs_path not in _sys.path:
                _sys.path.insert(0, _bs_path)
            from blocksmith.generators.phyxel_parser import read_voxel_metadata
            meta = read_voxel_metadata(str(abs_voxel))
        except Exception:
            # Fallback: count manually without metadata
            for line in abs_voxel.read_text().splitlines():
                s = line.strip()
                if s.startswith("C "):
                    meta.setdefault("primitive_counts", {}).setdefault("cubes", 0)
                    meta["primitive_counts"]["cubes"] += 1
                elif s.startswith("S "):
                    meta.setdefault("primitive_counts", {}).setdefault("subcubes", 0)
                    meta["primitive_counts"]["subcubes"] += 1
                elif s.startswith("M "):
                    meta.setdefault("primitive_counts", {}).setdefault("microcubes", 0)
                    meta["primitive_counts"]["microcubes"] += 1

    counts = meta.get("primitive_counts", {})
    cubes      = counts.get("cubes", 0)
    subcubes   = counts.get("subcubes", 0)
    microcubes = counts.get("microcubes", 0)
    bounds     = meta.get("bounds", {})
    facing_yaw = meta.get("facing_yaw", None)
    ipoints    = meta.get("interaction_points", [])

    summary_lines = [
        f"Template: {abs_voxel.name}",
        f"Primitives: {cubes}C + {subcubes}S + {microcubes}M = {cubes + subcubes + microcubes} total",
    ]
    if bounds:
        summary_lines.append(f"Bounds: {bounds.get('w',0)}W × {bounds.get('h',0)}H × {bounds.get('d',0)}D cubes")
    if facing_yaw is not None:
        import math as _math
        facing_deg = _math.degrees(facing_yaw)
        summary_lines.append(f"Facing: {facing_yaw:.4f} rad ({facing_deg:.1f}°)  [0=+Z front, π=−Z back]")
    if ipoints:
        summary_lines.append(f"Interaction points ({len(ipoints)}):")
        for ip in ipoints:
            loc = ip.get("local", [0, 0, 0])
            summary_lines.append(
                f"  • {ip['point_id']} ({ip['type']})  "
                f"local=[{loc[0]:.2f}, {loc[1]:.2f}, {loc[2]:.2f}]  "
                f"yaw={ip['facing_yaw']:.4f}  groups={','.join(ip.get('groups', ['*']))}"
            )
    else:
        summary_lines.append("Interaction points: none")
    summary_lines.append(f"Screenshots ({len(screenshots)} angles):")

    content = [TextContent(type="text", text="\n".join(summary_lines))]
    for label, img in screenshots:
        content.append(TextContent(type="text", text=f"\n--- {label} ---"))
        if img:
            content.append(img)
        else:
            content.append(TextContent(type="text", text="(screenshot unavailable)"))
    return content


def _enhance_prompt_inprocess(prompt: str, model: str, image: str | None = None) -> str:
    """Run prompt enhancement directly (no subprocess) — avoids litellm hanging in child processes.

    Adds external/blocksmith and tools/ to sys.path so prompt_enhancer can import LLMClient.
    Falls back to original prompt on any error.
    """
    import sys as _sys
    bs_path  = str(PROJECT_ROOT / "external" / "blocksmith")
    tools_path = str(PROJECT_ROOT / "tools")
    for p in (bs_path, tools_path):
        if p not in _sys.path:
            _sys.path.insert(0, p)
    try:
        from asset_pipeline.prompt_enhancer import enhance_prompt as _ep
        return _ep(prompt, image=image, model=model)
    except Exception as e:
        return prompt  # non-fatal fallback


def _run_blocksmith_native(
    name: str,
    prompt: str,
    model: str,
    templates_dir,
    revision_notes: list | None = None,
    image: str | None = None,
    enhance_prompt: bool = False,
) -> dict:
    """Run blocksmith_generate.py --native in a subprocess.

    On refinement rounds, revision_notes are appended as a structured
    '## Revision Notes' block so the generator can address specific issues.
    The original prompt is always preserved as the base description.
    """
    import sys as _sys, json as _json

    # Append revision notes as a structured block the generator reads
    full_prompt = prompt
    if revision_notes:
        notes_block = "\n\n## Revision Notes\n" + "\n".join(f"- {n}" for n in revision_notes)
        full_prompt = prompt + notes_block

    script = PROJECT_ROOT / "tools" / "blocksmith_generate.py"
    cmd = [
        _sys.executable, str(script),
        full_prompt,
        "--name", name,
        "--model", model,
        "--native",
        "--force",
        "--output-dir", str(templates_dir),
        "--json",
    ]
    if image:
        cmd += ["--image", image]
    if enhance_prompt:
        cmd += ["--enhance-prompt"]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=str(PROJECT_ROOT), timeout=600)
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "blocksmith generation timed out after 10 minutes"}
    if result.returncode != 0:
        return {"success": False, "error": result.stderr or result.stdout}
    try:
        return _json.loads(result.stdout.strip())
    except Exception:
        return {"success": False, "error": f"JSON parse error: {result.stdout}"}


async def _set_ref_character(visible: bool, port: int) -> None:
    """Show or hide the humanoid reference character in the asset editor."""
    try:
        await _api_post_port("/api/asset-editor/ref-character", {"visible": visible}, port)
        await asyncio.sleep(0.5)  # let the character spawn/despawn before capturing
    except Exception:
        pass  # non-fatal — orbit continues without the character


async def _orbit_for_critique(template_path: str, port: int, config: str, show_reference_character: bool = True) -> tuple[list, list]:
    """
    Ensure asset editor is running on `port` with `template_path`, then capture
    6-angle orbit screenshots.  Returns (mcp_content_list, image_parts_for_llm).
    """
    # Ensure the asset editor is up with this template
    editor_up = (
        _asset_editor_process is not None and _asset_editor_process.poll() is None
    )
    if not editor_up:
        launch = await _launch_asset_editor(
            {"template_path": template_path, "port": port, "config": config}
        )
        if "error" in launch:
            return [TextContent(type="text", text=f"ERROR launching asset editor: {launch['error']}")], []
        await asyncio.sleep(1.0)  # extra settle time after launch

    # Optionally show a humanoid reference character for scale context
    if show_reference_character:
        await _set_ref_character(True, port)

    # Use orbit_screenshots endpoint (same binary, available on any port)
    orbit_result = await _api_post_port(
        "/api/orbit-screenshots",
        {"x": 16, "y": 20, "z": 16, "radius": 6},
        port,
        timeout=120.0,
    )

    # Hide the reference character after capture so it doesn't persist
    if show_reference_character:
        await _set_ref_character(False, port)

    if "error" in orbit_result:
        return [TextContent(type="text", text=f"ERROR capturing orbit: {orbit_result['error']}")], []

    # Build content list and image parts for the LLM
    content: list = []
    image_parts: list = []
    for shot in orbit_result.get("screenshots", []):
        path_str = shot.get("path", "")
        label = shot.get("view", "").upper()
        img = _embed_screenshot(path_str) if path_str else None
        if img:
            content.append(TextContent(type="text", text=f"[{label}]"))
            content.append(img)
            image_parts.append({
                "type": "image_url",
                "image_url": {"url": f"data:{img.mimeType};base64,{img.data}"},
            })
    return content, image_parts


async def _critique_template(args: dict) -> list:
    """Visually critique a template: 6-angle orbit + vision LLM evaluation."""
    import json as _json

    template_name          = args.get("template_name", "")
    original_prompt        = args.get("original_prompt", "")
    critique_model         = args.get("critique_model", "anthropic/claude-sonnet-4-6")
    port                   = int(args.get("port", _asset_editor_port or 8091))
    config                 = args.get("config", "Debug")
    show_reference_character = args.get("show_reference_character", True)

    # Resolve template path
    voxel_path = PROJECT_ROOT / "resources" / "templates" / f"{template_name}.voxel"
    if not voxel_path.exists():
        return [TextContent(type="text", text=f"ERROR: {voxel_path} not found")]

    # Read metadata for context
    meta_text = ""
    try:
        _bs_path = str(PROJECT_ROOT / "external" / "blocksmith")
        import sys as _sys
        if _bs_path not in _sys.path:
            _sys.path.insert(0, _bs_path)
        from blocksmith.generators.phyxel_parser import read_voxel_metadata
        import math as _math
        m = read_voxel_metadata(str(voxel_path))
        b = m.get("bounds", {})
        fy = m.get("facing_yaw", 0.0)
        ips = m.get("interaction_points", [])
        counts = m.get("primitive_counts", {})
        ip_lines = "\n".join(
            f"  {ip['point_id']} ({ip['type']}) at {ip['local']}, yaw={ip['facing_yaw']:.3f}"
            for ip in ips
        ) or "  none"
        meta_text = (
            f"\nTemplate metadata:\n"
            f"  Primitives: {counts.get('cubes',0)}C + {counts.get('subcubes',0)}S + {counts.get('microcubes',0)}M\n"
            f"  Bounds: {b.get('w',0)}W × {b.get('h',0)}H × {b.get('d',0)}D cubes\n"
            f"  Facing: {_math.degrees(fy):.1f}° (0°=front faces +Z)\n"
            f"  Interaction points:\n{ip_lines}"
        )
    except Exception:
        pass

    # Capture 6-angle orbit (with reference character for scale context)
    orbit_content, image_parts = await _orbit_for_critique(str(voxel_path), port, config, show_reference_character)
    if not image_parts:
        return orbit_content + [TextContent(type="text", text="ERROR: No screenshots for critique")]

    # Ask the vision LLM
    critique_text = ""
    try:
        import litellm
        user_text = (
            f'Original prompt: "{original_prompt}"\n'
            f"{meta_text}\n\n"
            "The 6 screenshots above show NORTH (front), SOUTH (back), EAST, WEST, TOP, and ISO views.\n\n"
            "SCALE REFERENCE: 1 cube = 1 meter. Standard humanoid character is ~2 cubes (2m) tall.\n"
            "Humanoid furniture proportions: chair seat at ~0.67m, throne seat at ~0.67-1.0m, \n"
            "chair total height ≤1.3m, throne total height ≤3m, table ≤1.0m tall.\n"
            "A throne with seat higher than 1.0 cube, or total height over 3 cubes, is too large.\n\n"
            "Evaluate the model against the prompt. Return ONLY valid JSON:\n"
            "{\n"
            '  "overall_quality": <0-10>,\n'
            '  "issues": ["specific visual problems — be precise about which part and what\'s wrong"],\n'
            '  "scale_issues": ["any parts that are too large or too small for a standard humanoid character"],\n'
            '  "interaction_point_issues": ["problems with interaction point placement or facing — or empty list if ok"],\n'
            '  "suggestions": ["actionable geometry/material/proportion changes for the next generation round"]\n'
            "}\n\n"
            "Scoring guide: 0-3 = unrecognisable, 4-5 = roughly correct shape but major issues, "
            "6-7 = recognisable with minor issues, 8-9 = good game asset, 10 = excellent. "
            "Deduct 2 points if furniture is badly out of scale for a humanoid character."
        )
        messages = [{
            "role": "user",
            "content": [{"type": "text", "text": user_text}, *image_parts],
        }]
        response = await asyncio.get_event_loop().run_in_executor(
            None,
            lambda: litellm.completion(
                model=critique_model, messages=messages, max_tokens=1000, temperature=0.2,
                timeout=120,
            )
        )
        critique_text = response.choices[0].message.content.strip()
    except Exception as e:
        critique_text = f'{{"overall_quality": 0, "issues": ["Critique failed: {e}"], "interaction_point_issues": [], "suggestions": []}}'

    return orbit_content + [TextContent(type="text", text=f"\n=== Critique ===\n{critique_text}")]


async def _refine_template(args: dict) -> list:
    """Iterative critique → regenerate loop with structured revision notes."""
    import json as _json, shutil

    template_name            = args.get("template_name", "")
    original_prompt          = args.get("original_prompt", "")
    image                    = args.get("image")
    max_rounds               = int(args.get("max_rounds", 3))
    quality_threshold        = float(args.get("quality_threshold", 7.0))
    critique_model           = args.get("critique_model", "anthropic/claude-sonnet-4-6")
    generation_model         = args.get("generation_model", "anthropic/claude-sonnet-4-20250514")
    port                     = int(args.get("port", _asset_editor_port or 8091))
    config                   = args.get("config", "Debug")
    show_reference_character = args.get("show_reference_character", True)

    templates_dir = PROJECT_ROOT / "resources" / "templates"
    base_path     = templates_dir / f"{template_name}.voxel"

    best_quality    = 0.0
    best_round_path = base_path
    all_content     = []

    for round_num in range(1, max_rounds + 1):
        all_content.append(TextContent(
            type="text",
            text=f"\n{'='*44}\nRound {round_num}/{max_rounds}\n{'='*44}"
        ))

        # Critique current version
        critique_content = await _critique_template({
            "template_name":  template_name,
            "original_prompt": original_prompt,
            "critique_model": critique_model,
            "port": port, "config": config,
            "show_reference_character": show_reference_character,
        })
        all_content.extend(critique_content)

        # Parse quality + issues from the critique JSON block
        quality = 0.0
        issues: list[str] = []
        suggestions: list[str] = []
        ip_issues: list[str] = []
        scale_issues: list[str] = []
        for item in reversed(critique_content):
            if hasattr(item, "text") and "overall_quality" in item.text:
                try:
                    start = item.text.find("{")
                    end   = item.text.rfind("}") + 1
                    cj    = _json.loads(item.text[start:end])
                    quality      = float(cj.get("overall_quality", 0))
                    issues       = cj.get("issues", [])
                    suggestions  = cj.get("suggestions", [])
                    ip_issues    = cj.get("interaction_point_issues", [])
                    scale_issues = cj.get("scale_issues", [])
                except Exception:
                    pass
                break

        all_content.append(TextContent(type="text", text=f"Score: {quality}/10"))

        # Save round snapshot
        round_path = templates_dir / f"{template_name}_round_{round_num}.voxel"
        if base_path.exists():
            shutil.copy2(str(base_path), str(round_path))
            if quality > best_quality:
                best_quality    = quality
                best_round_path = round_path

        if quality >= quality_threshold:
            all_content.append(TextContent(
                type="text",
                text=f"Quality threshold met ({quality:.1f} >= {quality_threshold}). Done."
            ))
            break

        if round_num == max_rounds:
            all_content.append(TextContent(type="text", text="Max rounds reached."))
            break

        # Build revision notes from scale issues (highest priority) + issues + suggestions + IP issues
        revision_notes = []
        if scale_issues:
            revision_notes += [f"[Scale] {n}" for n in scale_issues]
        revision_notes += issues + suggestions
        if ip_issues:
            revision_notes += [f"[Interaction point] {n}" for n in ip_issues]

        all_content.append(TextContent(
            type="text",
            text=f"Regenerating with {len(revision_notes)} revision notes..."
        ))

        # Close and relaunch the asset editor after regeneration
        await _close_asset_editor()

        regen = await asyncio.get_event_loop().run_in_executor(
            None,
            lambda: _run_blocksmith_native(
                template_name, original_prompt, generation_model, templates_dir,
                revision_notes=revision_notes, image=image, enhance_prompt=False,
            )
        )
        if not regen.get("success"):
            all_content.append(TextContent(type="text", text=f"Regeneration failed: {regen.get('error')}"))
            break

        counts = f"{regen.get('cubes',0)}C + {regen.get('subcubes',0)}S + {regen.get('microcubes',0)}M"
        all_content.append(TextContent(type="text", text=f"Generated: {counts}"))

        await _launch_asset_editor({"template_path": str(base_path), "port": port, "config": config})

    # Promote best round to canonical path
    if best_round_path != base_path and best_round_path.exists():
        shutil.copy2(str(best_round_path), str(base_path))
        all_content.append(TextContent(
            type="text",
            text=f"Best result (score={best_quality:.1f}) promoted to {base_path.name}"
        ))

    return all_content


async def _generate_asset(args: dict) -> list:
    """Full pipeline: enhance → generate → refine loop → final orbit verification."""
    import json as _json, shutil

    name              = args.get("name", "")
    prompt            = args.get("prompt", "")
    image             = args.get("image")
    max_rounds        = int(args.get("max_rounds", 3))
    quality_threshold = float(args.get("quality_threshold", 7.0))
    generation_model  = args.get("generation_model", "anthropic/claude-sonnet-4-20250514")
    critique_model    = args.get("critique_model", "anthropic/claude-sonnet-4-6")
    config            = args.get("config", "Debug")
    port              = int(args.get("port", 8091))

    templates_dir = PROJECT_ROOT / "resources" / "templates"
    all_content: list = []

    # Close any lingering asset editor from a previous session before starting
    await _kill_process_on_port(port)

    # --- Step 1: Initial generation ---
    all_content.append(TextContent(type="text", text=(
        f"=== generate_asset: {name} ===\n"
        f"Prompt: {prompt}\n"
        f"Image: {image or 'none'}\n"
        f"Max rounds: {max_rounds}  Threshold: {quality_threshold}/10"
    )))

    # Enhance prompt in-process (litellm works in MCP server context; subprocess hangs)
    all_content.append(TextContent(type="text", text="Enhancing prompt..."))
    enhanced_prompt = await asyncio.get_event_loop().run_in_executor(
        None, lambda: _enhance_prompt_inprocess(prompt, generation_model, image)
    )
    if enhanced_prompt != prompt:
        all_content.append(TextContent(type="text", text=f"Enhanced ({len(enhanced_prompt)} chars)"))

    gen = await asyncio.get_event_loop().run_in_executor(
        None,
        lambda: _run_blocksmith_native(
            name, enhanced_prompt, generation_model, templates_dir,
            revision_notes=None, image=image, enhance_prompt=False,
        )
    )
    if not gen.get("success"):
        all_content.append(TextContent(type="text", text=f"ERROR: Initial generation failed: {gen.get('error')}"))
        return all_content

    counts = f"{gen.get('cubes',0)}C + {gen.get('subcubes',0)}S + {gen.get('microcubes',0)}M"
    all_content.append(TextContent(type="text", text=f"Initial generation: {counts}"))

    # --- Steps 2–4: Editor-dependent work — always close editor when done ---
    try:
        # Step 2: Refinement loop
        await _launch_asset_editor({
            "template_path": str(templates_dir / f"{name}.voxel"),
            "port": port, "config": config,
        })

        refine_content = await _refine_template({
            "template_name":          name,
            "original_prompt":        prompt,
            "image":                  image,
            "max_rounds":             max_rounds,
            "quality_threshold":      quality_threshold,
            "generation_model":       generation_model,
            "critique_model":         critique_model,
            "port":                   port,
            "config":                 config,
            "show_reference_character": True,
        })
        all_content.extend(refine_content)

        # Step 3: Final verification orbit (always with reference character)
        all_content.append(TextContent(type="text", text="\n=== Final verification ==="))
        final_orbit, _ = await _orbit_for_critique(
            str(templates_dir / f"{name}.voxel"), port, config, show_reference_character=True
        )
        all_content.extend(final_orbit)

        # Step 4: Metadata summary
        voxel_path = templates_dir / f"{name}.voxel"
        try:
            _bs_path = str(PROJECT_ROOT / "external" / "blocksmith")
            import sys as _sys, math as _math
            if _bs_path not in _sys.path:
                _sys.path.insert(0, _bs_path)
            from blocksmith.generators.phyxel_parser import read_voxel_metadata
            m = read_voxel_metadata(str(voxel_path))
            b  = m.get("bounds", {})
            fy = m.get("facing_yaw", 0.0)
            ips = m.get("interaction_points", [])
            c   = m.get("primitive_counts", {})
            ip_lines = "\n".join(
                f"  • {ip['point_id']} ({ip['type']}) at {ip['local']}, yaw={ip['facing_yaw']:.3f}"
                for ip in ips
            ) or "  none (add via asset editor if needed)"
            summary = (
                f"\n--- Asset ready: {name}.voxel ---\n"
                f"Primitives : {c.get('cubes',0)}C + {c.get('subcubes',0)}S + {c.get('microcubes',0)}M\n"
                f"Bounds     : {b.get('w',0)}W × {b.get('h',0)}H × {b.get('d',0)}D cubes\n"
                f"Facing     : {_math.degrees(fy):.1f}° (0°=+Z front)\n"
                f"Interaction points:\n{ip_lines}"
            )
            all_content.append(TextContent(type="text", text=summary))
        except Exception as e:
            all_content.append(TextContent(type="text", text=f"Metadata read error: {e}"))

    finally:
        # Always clean up the asset editor regardless of success or error
        await _close_asset_editor()

    return all_content


def _clear_engine_logs() -> dict:
    """Truncate phyxel.log to zero bytes."""
    log_path = PROJECT_ROOT / "phyxel.log"
    try:
        if log_path.exists():
            log_path.write_text("", encoding="utf-8")
            return {"success": True, "cleared": str(log_path)}
        return {"success": True, "message": "phyxel.log did not exist — nothing to clear"}
    except Exception as e:
        return {"success": False, "error": str(e)}


async def _stop_engine(grace_seconds: float = 5.0) -> dict:
    """Terminate the engine process if it is running."""
    global _engine_process
    if _engine_process is None or _engine_process.poll() is not None:
        return {"success": True, "message": "Engine was not running"}

    pid = _engine_process.pid
    _engine_process.terminate()
    import time
    deadline = time.monotonic() + grace_seconds
    while time.monotonic() < deadline:
        if _engine_process.poll() is not None:
            _engine_process = None
            return {"success": True, "message": f"Engine (pid {pid}) stopped gracefully"}
        await asyncio.sleep(0.2)

    # Force-kill if still alive after grace period
    try:
        _engine_process.kill()
        _engine_process.wait(timeout=3)
    except Exception:
        pass
    _engine_process = None
    return {"success": True, "message": f"Engine (pid {pid}) force-killed after {grace_seconds}s"}


async def _restart_engine(args: dict) -> dict:
    """Stop the running engine then launch it again with the same (or new) args."""
    stop_result = await _stop_engine()
    if not stop_result.get("success"):
        return stop_result
    _clear_engine_logs()
    await asyncio.sleep(1.0)  # brief pause so ports are released
    launch_args = {**_engine_launch_args, **args}  # caller can override any field
    return await _launch_engine(launch_args)


async def _launch_engine(args: dict) -> dict:
    """Launch the engine executable as a background process."""
    global _engine_process, _engine_launch_args
    config = args.get("config", "Debug")
    extra_args = args.get("args", [])

    # Check if already running
    if _engine_process and _engine_process.poll() is None:
        return {"success": True, "message": "Engine already running", "pid": _engine_process.pid}

    exe_path = PROJECT_ROOT / "build" / "editor" / config / "phyxel.exe"
    if not exe_path.exists():
        # Try old path (pre-rename)
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
        _engine_launch_args = dict(args)  # remember for restart
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

    info: dict[str, Any] = {
        "process_alive": process_alive,
        "api_responsive": api_ok,
        "pid": _engine_process.pid if process_alive else None,
    }

    # Check if a project is loaded
    if api_ok:
        try:
            project = await api_get("/api/project/info")
            if "error" not in project:
                info["project_loaded"] = True
                info["project_dir"] = project.get("project_dir", "")
            else:
                info["project_loaded"] = False
                info["project_warning"] = (
                    "Engine is running but NO PROJECT is loaded. "
                    "The engine is showing the project-selector screen. "
                    "Launch with --project <dir> or use 'open_project' to load a project."
                )
        except Exception:
            pass

    return info


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
