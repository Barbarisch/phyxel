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
import httpx
from typing import Any

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
# HTTP Client
# ============================================================================

http_client = httpx.Client(base_url=ENGINE_API_URL, timeout=ENGINE_API_TIMEOUT)


def api_get(path: str, params: dict | None = None) -> dict:
    """GET request to the engine API. Returns parsed JSON."""
    try:
        resp = http_client.get(path, params=params)
        resp.raise_for_status()
        return resp.json()
    except httpx.ConnectError:
        return {"error": f"Engine not running. Start phyxel.exe first. (tried {ENGINE_API_URL}{path})"}
    except Exception as e:
        return {"error": str(e)}


def api_post(path: str, body: dict) -> dict:
    """POST request to the engine API. Returns parsed JSON."""
    try:
        resp = http_client.post(path, json=body)
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
    ]


@server.call_tool()
async def call_tool(name: str, arguments: dict) -> list[TextContent]:
    """Route tool calls to the engine HTTP API."""
    logger.info(f"Tool call: {name}({json.dumps(arguments, default=str)[:200]})")

    result = _dispatch_tool(name, arguments)

    return [TextContent(type="text", text=json.dumps(result, indent=2))]


def _dispatch_tool(name: str, args: dict) -> dict:
    """Dispatch a tool call to the appropriate API endpoint."""

    # --- Status & State ---
    if name == "engine_status":
        return api_get("/api/status")

    elif name == "get_world_state":
        return api_get("/api/state")

    elif name == "get_camera":
        return api_get("/api/camera")

    # --- Entity Management ---
    elif name == "list_entities":
        return api_get("/api/entities")

    elif name == "get_entity":
        return api_get(f"/api/entity/{args['id']}")

    elif name == "spawn_entity":
        body: dict[str, Any] = {
            "type": args["type"],
            "position": {"x": args["x"], "y": args["y"], "z": args["z"]}
        }
        if "id" in args:
            body["id"] = args["id"]
        if "animFile" in args:
            body["animFile"] = args["animFile"]
        return api_post("/api/entity/spawn", body)

    elif name == "move_entity":
        return api_post("/api/entity/move", {
            "id": args["id"],
            "position": {"x": args["x"], "y": args["y"], "z": args["z"]}
        })

    elif name == "remove_entity":
        return api_post("/api/entity/remove", {"id": args["id"]})

    # --- Voxel World ---
    elif name == "place_voxel":
        body = {"x": args["x"], "y": args["y"], "z": args["z"]}
        if "material" in args:
            body["material"] = args["material"]
        return api_post("/api/world/voxel", body)

    elif name == "remove_voxel":
        return api_post("/api/world/voxel/remove", {
            "x": args["x"], "y": args["y"], "z": args["z"]
        })

    elif name == "query_voxel":
        return api_get("/api/world/voxel", {
            "x": str(args["x"]), "y": str(args["y"]), "z": str(args["z"])
        })

    elif name == "place_voxels_batch":
        return api_post("/api/world/voxel/batch", {"voxels": args["voxels"]})

    # --- Templates ---
    elif name == "list_templates":
        return api_get("/api/templates")

    elif name == "spawn_template":
        return api_post("/api/world/template", {
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
        return api_post("/api/camera", body)

    # --- Scripting ---
    elif name == "run_script":
        return api_post("/api/script", {"code": args["code"]})

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
        return api_post("/api/world/fill", body)

    # --- Materials ---
    elif name == "list_materials":
        return api_get("/api/materials")

    # --- Chunk Info ---
    elif name == "get_chunk_info":
        return api_get("/api/world/chunks")

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
        return api_post("/api/entity/update", body)

    # --- Screenshot ---
    elif name == "screenshot":
        return api_get("/api/screenshot")

    # --- Region Scan ---
    elif name == "scan_region":
        return api_get("/api/world/scan", {
            "x1": str(args["x1"]), "y1": str(args["y1"]), "z1": str(args["z1"]),
            "x2": str(args["x2"]), "y2": str(args["y2"]), "z2": str(args["z2"])
        })

    # --- Region Clear ---
    elif name == "clear_region":
        return api_post("/api/world/clear", {
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    # --- World Save ---
    elif name == "save_world":
        body: dict[str, Any] = {}
        if "all" in args:
            body["all"] = args["all"]
        return api_post("/api/world/save", body)

    # --- Event Polling ---
    elif name == "poll_events":
        params = {}
        if "cursor" in args:
            params["since"] = str(args["cursor"])
        return api_get("/api/events", params)

    else:
        return {"error": f"Unknown tool: {name}"}


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
