"""
Phyxel MCP Extension — Engine API Tools for Goose AI Agents

This MCP (Model Context Protocol) server exposes phyxel engine capabilities
as tools that AI agents can call. It runs as a stdio-based MCP extension
that goose-server launches and communicates with.

Tool Categories:
  - World: get_nearby_entities, get_world_state, get_player_info
  - Movement: move_to, navigate_path
  - Dialog: say_dialog
  - Combat: attack
  - Animation: play_animation, emote
  - Quest: get_quest_state, set_quest_state
  - Spawning: spawn_entity
  - Events: trigger_event

Each tool call produces a JSON command that gets queued back to the
engine's AICommandQueue for execution in the next frame.
"""

import json
import sys
import os
import logging
import asyncio
from typing import Any, Optional

# Configure logging to stderr (stdout is used for MCP protocol)
logging.basicConfig(
    stream=sys.stderr,
    level=logging.DEBUG,
    format="[PhyxelMCP] %(levelname)s: %(message)s"
)
logger = logging.getLogger("phyxel_mcp")

# ============================================================================
# Engine State Store
# In a full implementation, this would communicate with the engine via
# shared memory or HTTP callback. For now, it maintains a local snapshot
# that the engine updates periodically.
# ============================================================================

class EngineState:
    """Holds a snapshot of the engine state, updated by the game loop."""
    
    def __init__(self):
        self.entities: dict[str, dict] = {}
        self.quest_states: dict[str, dict] = {}
        self.world_regions: dict[str, dict] = {}
        self.player_info: dict = {}
        
    def update_entity(self, entity_id: str, data: dict):
        self.entities[entity_id] = data
        
    def get_entity(self, entity_id: str) -> Optional[dict]:
        return self.entities.get(entity_id)
    
    def get_nearby_entities(self, x: float, y: float, z: float, 
                            radius: float) -> list[dict]:
        """Find entities within radius of the given position."""
        nearby = []
        for eid, data in self.entities.items():
            pos = data.get("position", {})
            ex, ey, ez = pos.get("x", 0), pos.get("y", 0), pos.get("z", 0)
            dist = ((ex - x)**2 + (ey - y)**2 + (ez - z)**2) ** 0.5
            if dist <= radius:
                nearby.append({**data, "entity_id": eid, "distance": round(dist, 2)})
        return nearby


# Global engine state (updated via engine callbacks)
engine_state = EngineState()

# Command queue — commands to send back to the engine
command_queue: list[dict] = []

def queue_command(cmd_type: str, data: dict) -> dict:
    """Queue a command for the engine and return a confirmation."""
    cmd = {"type": cmd_type, **data}
    command_queue.append(cmd)
    logger.debug(f"Queued command: {cmd_type} -> {json.dumps(data)}")
    return {"status": "queued", "command": cmd_type}


# ============================================================================
# MCP Protocol Implementation (stdio JSON-RPC)
# ============================================================================

class MCPServer:
    """
    Minimal MCP server implementation using stdio transport.
    
    Implements the MCP protocol for tool serving:
    - initialize / initialized
    - tools/list
    - tools/call
    """
    
    def __init__(self):
        self.tools = self._register_tools()
        self.initialized = False
        
    def _register_tools(self) -> dict[str, dict]:
        """Register all available tools with their schemas."""
        return {
            # ================================================================
            # World Query Tools
            # ================================================================
            "get_nearby_entities": {
                "description": "Get entities near a position. Returns entity IDs, types, positions, distances, and factions.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "entity_id": {
                            "type": "string",
                            "description": "The entity whose surroundings to scan. If provided, uses that entity's position as center."
                        },
                        "x": {"type": "number", "description": "Center X coordinate (used if entity_id not provided)"},
                        "y": {"type": "number", "description": "Center Y coordinate"},
                        "z": {"type": "number", "description": "Center Z coordinate"},
                        "radius": {"type": "number", "description": "Search radius in world units", "default": 10.0}
                    },
                    "required": ["radius"]
                },
                "handler": self._handle_get_nearby_entities
            },
            
            "get_world_state": {
                "description": "Get the state of a world region including terrain, weather, time of day, and ambient conditions.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "region": {
                            "type": "string",
                            "description": "Region identifier (e.g., 'eastern_gate', 'market_square')"
                        }
                    },
                    "required": ["region"]
                },
                "handler": self._handle_get_world_state
            },
            
            "get_player_info": {
                "description": "Get information about the player character including position, health, inventory, quest log, and reputation.",
                "inputSchema": {
                    "type": "object",
                    "properties": {},
                    "required": []
                },
                "handler": self._handle_get_player_info
            },
            
            # ================================================================
            # Movement Tools
            # ================================================================
            "move_to": {
                "description": "Move an entity to a target position. The entity will walk/run toward the destination.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "entity_id": {"type": "string", "description": "ID of the entity to move"},
                        "x": {"type": "number", "description": "Target X coordinate"},
                        "y": {"type": "number", "description": "Target Y coordinate"},
                        "z": {"type": "number", "description": "Target Z coordinate"},
                        "speed": {"type": "number", "description": "Movement speed multiplier (0.5=walk, 1.0=normal, 2.0=run)", "default": 1.0}
                    },
                    "required": ["entity_id", "x", "y", "z"]
                },
                "handler": self._handle_move_to
            },
            
            "navigate_path": {
                "description": "Move an entity along a series of waypoints in order.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "entity_id": {"type": "string", "description": "ID of the entity to move"},
                        "waypoints": {
                            "type": "array",
                            "items": {
                                "type": "object",
                                "properties": {
                                    "x": {"type": "number"},
                                    "y": {"type": "number"},
                                    "z": {"type": "number"}
                                },
                                "required": ["x", "y", "z"]
                            },
                            "description": "Ordered list of positions to visit"
                        },
                        "speed": {"type": "number", "default": 1.0}
                    },
                    "required": ["entity_id", "waypoints"]
                },
                "handler": self._handle_navigate_path
            },
            
            # ================================================================
            # Dialog Tools
            # ================================================================
            "say_dialog": {
                "description": "Make an entity speak dialog text. This shows as a speech bubble or dialog box in-game.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "entity_id": {"type": "string", "description": "ID of the speaking entity"},
                        "text": {"type": "string", "description": "The dialog text to speak"},
                        "emotion": {
                            "type": "string",
                            "description": "Emotional tone of the dialog",
                            "enum": ["neutral", "happy", "sad", "angry", "scared", "surprised", "thoughtful", "sarcastic"]
                        }
                    },
                    "required": ["entity_id", "text"]
                },
                "handler": self._handle_say_dialog
            },
            
            # ================================================================
            # Combat Tools
            # ================================================================
            "attack": {
                "description": "Make an entity perform an attack action against a target.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "entity_id": {"type": "string", "description": "ID of the attacking entity"},
                        "target_id": {"type": "string", "description": "ID of the target entity"},
                        "skill_name": {
                            "type": "string",
                            "description": "Name of the attack skill to use",
                            "default": "basic_attack"
                        }
                    },
                    "required": ["entity_id", "target_id"]
                },
                "handler": self._handle_attack
            },
            
            # ================================================================
            # Animation Tools
            # ================================================================
            "play_animation": {
                "description": "Play a specific animation on an entity.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "entity_id": {"type": "string", "description": "ID of the entity"},
                        "animation_name": {
                            "type": "string",
                            "description": "Name of animation to play (e.g., 'idle', 'walk', 'attack_slash', 'wave')"
                        },
                        "loop": {"type": "boolean", "description": "Whether to loop the animation", "default": False}
                    },
                    "required": ["entity_id", "animation_name"]
                },
                "handler": self._handle_play_animation
            },
            
            "emote": {
                "description": "Make an entity perform an emote (a social/emotional gesture).",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "entity_id": {"type": "string", "description": "ID of the entity"},
                        "emote_type": {
                            "type": "string",
                            "description": "Type of emote to perform",
                            "enum": ["wave", "bow", "shrug", "nod", "shake_head", "point", "salute", "cheer", "cry", "laugh"]
                        }
                    },
                    "required": ["entity_id", "emote_type"]
                },
                "handler": self._handle_emote
            },
            
            # ================================================================
            # Quest Tools
            # ================================================================
            "get_quest_state": {
                "description": "Get the current state of a quest.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "quest_id": {"type": "string", "description": "Quest identifier"}
                    },
                    "required": ["quest_id"]
                },
                "handler": self._handle_get_quest_state
            },
            
            "set_quest_state": {
                "description": "Update the state of a quest. Use this to advance quest progress, complete objectives, or fail quests.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "quest_id": {"type": "string", "description": "Quest identifier"},
                        "state": {
                            "type": "string",
                            "description": "New quest state",
                            "enum": ["not_started", "active", "objective_complete", "completed", "failed"]
                        },
                        "detail": {"type": "string", "description": "Additional detail about the state change"}
                    },
                    "required": ["quest_id", "state"]
                },
                "handler": self._handle_set_quest_state
            },
            
            # ================================================================
            # Entity Spawning
            # ================================================================
            "spawn_entity": {
                "description": "Spawn a new entity in the world from a template.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "template": {
                            "type": "string",
                            "description": "Entity template name (e.g., 'guard', 'merchant', 'crate', 'torch')"
                        },
                        "x": {"type": "number", "description": "Spawn X coordinate"},
                        "y": {"type": "number", "description": "Spawn Y coordinate"},
                        "z": {"type": "number", "description": "Spawn Z coordinate"},
                        "entity_id": {"type": "string", "description": "Optional specific ID for the spawned entity"}
                    },
                    "required": ["template", "x", "y", "z"]
                },
                "handler": self._handle_spawn_entity
            },
            
            # ================================================================
            # Event System
            # ================================================================
            "trigger_event": {
                "description": "Trigger a game event. Events can affect the world, other NPCs, quests, or gameplay systems.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "event_name": {
                            "type": "string",
                            "description": "Event identifier (e.g., 'alarm_raised', 'gate_opened', 'storm_started')"
                        },
                        "payload": {
                            "type": "object",
                            "description": "Event-specific data",
                            "additionalProperties": True
                        }
                    },
                    "required": ["event_name"]
                },
                "handler": self._handle_trigger_event
            },
            
            # ================================================================
            # Inventory
            # ================================================================
            "get_inventory": {
                "description": "Get the inventory contents of an entity.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "entity_id": {"type": "string", "description": "ID of the entity"}
                    },
                    "required": ["entity_id"]
                },
                "handler": self._handle_get_inventory
            },
        }
    
    # ========================================================================
    # Tool Handlers
    # ========================================================================
    
    def _handle_get_nearby_entities(self, args: dict) -> dict:
        entity_id = args.get("entity_id")
        radius = args.get("radius", 10.0)
        
        if entity_id:
            entity = engine_state.get_entity(entity_id)
            if entity:
                pos = entity.get("position", {})
                x = pos.get("x", 0)
                y = pos.get("y", 0)
                z = pos.get("z", 0)
            else:
                return {"error": f"Entity '{entity_id}' not found"}
        else:
            x = args.get("x", 0)
            y = args.get("y", 0)
            z = args.get("z", 0)
        
        nearby = engine_state.get_nearby_entities(x, y, z, radius)
        # Filter out self
        if entity_id:
            nearby = [e for e in nearby if e.get("entity_id") != entity_id]
        
        return {"entities": nearby, "count": len(nearby)}
    
    def _handle_get_world_state(self, args: dict) -> dict:
        region = args["region"]
        state = engine_state.world_regions.get(region, {
            "region": region,
            "time_of_day": "day",
            "weather": "clear",
            "danger_level": "low",
            "description": f"The {region} area."
        })
        return state
    
    def _handle_get_player_info(self, args: dict) -> dict:
        return engine_state.player_info or {
            "position": {"x": 0, "y": 0, "z": 0},
            "health": 100,
            "faction": "player",
            "active_quests": []
        }
    
    def _handle_move_to(self, args: dict) -> dict:
        return queue_command("move_to", {
            "entity_id": args["entity_id"],
            "x": args["x"],
            "y": args["y"],
            "z": args["z"],
            "speed": args.get("speed", 1.0)
        })
    
    def _handle_navigate_path(self, args: dict) -> dict:
        # Send first waypoint as immediate move, queue rest
        waypoints = args["waypoints"]
        entity_id = args["entity_id"]
        speed = args.get("speed", 1.0)
        
        for wp in waypoints:
            queue_command("move_to", {
                "entity_id": entity_id,
                "x": wp["x"], "y": wp["y"], "z": wp["z"],
                "speed": speed
            })
        
        return {"status": "queued", "waypoint_count": len(waypoints)}
    
    def _handle_say_dialog(self, args: dict) -> dict:
        return queue_command("say_dialog", {
            "entity_id": args["entity_id"],
            "text": args["text"],
            "emotion": args.get("emotion", "neutral")
        })
    
    def _handle_attack(self, args: dict) -> dict:
        return queue_command("attack", {
            "entity_id": args["entity_id"],
            "target_id": args["target_id"],
            "skill_name": args.get("skill_name", "basic_attack")
        })
    
    def _handle_play_animation(self, args: dict) -> dict:
        return queue_command("play_animation", {
            "entity_id": args["entity_id"],
            "animation_name": args["animation_name"],
            "loop": args.get("loop", False)
        })
    
    def _handle_emote(self, args: dict) -> dict:
        return queue_command("emote", {
            "entity_id": args["entity_id"],
            "emote_type": args["emote_type"]
        })
    
    def _handle_get_quest_state(self, args: dict) -> dict:
        quest_id = args["quest_id"]
        return engine_state.quest_states.get(quest_id, {
            "quest_id": quest_id,
            "state": "not_started",
            "objectives": [],
            "description": ""
        })
    
    def _handle_set_quest_state(self, args: dict) -> dict:
        result = queue_command("set_quest_state", {
            "quest_id": args["quest_id"],
            "state": args["state"],
            "detail": args.get("detail", "")
        })
        # Also update local state
        engine_state.quest_states[args["quest_id"]] = {
            "quest_id": args["quest_id"],
            "state": args["state"],
            "detail": args.get("detail", "")
        }
        return result
    
    def _handle_spawn_entity(self, args: dict) -> dict:
        return queue_command("spawn_entity", {
            "template": args["template"],
            "x": args["x"],
            "y": args["y"],
            "z": args["z"],
            "entity_id": args.get("entity_id", "")
        })
    
    def _handle_trigger_event(self, args: dict) -> dict:
        return queue_command("trigger_event", {
            "event_name": args["event_name"],
            "payload": args.get("payload", {})
        })
    
    def _handle_get_inventory(self, args: dict) -> dict:
        entity_id = args["entity_id"]
        entity = engine_state.get_entity(entity_id)
        if entity:
            return {"entity_id": entity_id, "items": entity.get("inventory", [])}
        return {"entity_id": entity_id, "items": [], "note": "Entity not found or has no inventory"}
    
    # ========================================================================
    # MCP Protocol Handling
    # ========================================================================
    
    async def handle_request(self, request: dict) -> dict:
        """Handle a single JSON-RPC request."""
        method = request.get("method", "")
        req_id = request.get("id")
        params = request.get("params", {})
        
        logger.debug(f"Request: {method} (id={req_id})")
        
        if method == "initialize":
            return self._make_response(req_id, {
                "protocolVersion": "2024-11-05",
                "capabilities": {
                    "tools": {"listChanged": False}
                },
                "serverInfo": {
                    "name": "phyxel-engine",
                    "version": "0.1.0"
                }
            })
        
        elif method == "notifications/initialized":
            self.initialized = True
            return None  # No response for notifications
        
        elif method == "tools/list":
            tools_list = []
            for name, tool in self.tools.items():
                tools_list.append({
                    "name": name,
                    "description": tool["description"],
                    "inputSchema": tool["inputSchema"]
                })
            return self._make_response(req_id, {"tools": tools_list})
        
        elif method == "tools/call":
            tool_name = params.get("name", "")
            arguments = params.get("arguments", {})
            
            tool = self.tools.get(tool_name)
            if not tool:
                return self._make_error_response(req_id, -32601, 
                    f"Unknown tool: {tool_name}")
            
            try:
                result = tool["handler"](arguments)
                return self._make_response(req_id, {
                    "content": [{
                        "type": "text",
                        "text": json.dumps(result)
                    }]
                })
            except Exception as e:
                logger.error(f"Tool '{tool_name}' error: {e}")
                return self._make_error_response(req_id, -32000, str(e))
        
        elif method == "ping":
            return self._make_response(req_id, {})
        
        else:
            logger.warning(f"Unknown method: {method}")
            return self._make_error_response(req_id, -32601, 
                f"Method not found: {method}")
    
    def _make_response(self, req_id: Any, result: dict) -> dict:
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "result": result
        }
    
    def _make_error_response(self, req_id: Any, code: int, message: str) -> dict:
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "error": {"code": code, "message": message}
        }
    
    async def run_stdio(self):
        """Run the MCP server reading from stdin, writing to stdout."""
        logger.info("Phyxel MCP Extension starting (stdio transport)...")
        
        reader = asyncio.StreamReader()
        protocol = asyncio.StreamReaderProtocol(reader)
        await asyncio.get_event_loop().connect_read_pipe(lambda: protocol, sys.stdin.buffer)
        
        writer_transport, writer_protocol = await asyncio.get_event_loop().connect_write_pipe(
            asyncio.streams.FlowControlMixin, sys.stdout.buffer
        )
        writer = asyncio.StreamWriter(writer_transport, writer_protocol, None, asyncio.get_event_loop())
        
        buffer = b""
        
        while True:
            try:
                data = await reader.read(8192)
                if not data:
                    logger.info("stdin closed, shutting down")
                    break
                
                buffer += data
                
                # Process complete JSON-RPC messages
                # MCP uses newline-delimited JSON
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    
                    try:
                        request = json.loads(line.decode("utf-8"))
                    except json.JSONDecodeError as e:
                        logger.error(f"Invalid JSON: {e}")
                        continue
                    
                    response = await self.handle_request(request)
                    
                    if response is not None:
                        response_bytes = json.dumps(response).encode("utf-8") + b"\n"
                        writer.write(response_bytes)
                        await writer.drain()
                        
            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"Error in main loop: {e}")
                break
        
        logger.info("Phyxel MCP Extension stopped")


# ============================================================================
# Engine State Update API
# Called by the engine to keep the MCP extension's state in sync
# ============================================================================

def update_engine_state(state_json: str):
    """
    Called by the engine to update the MCP extension's state snapshot.
    
    Expected JSON format:
    {
        "entities": {
            "guard_01": {
                "position": {"x": 10, "y": 0, "z": 5},
                "type": "guard",
                "faction": "neutral",
                "health": 100,
                "state": "patrolling"
            },
            ...
        },
        "player": {
            "position": {"x": 0, "y": 0, "z": 0},
            "health": 100,
            ...
        },
        "quests": { ... },
        "world_regions": { ... }
    }
    """
    try:
        state = json.loads(state_json)
        
        if "entities" in state:
            engine_state.entities = state["entities"]
        if "player" in state:
            engine_state.player_info = state["player"]
        if "quests" in state:
            engine_state.quest_states = state["quests"]
        if "world_regions" in state:
            engine_state.world_regions = state["world_regions"]
            
        logger.debug(f"Engine state updated: {len(engine_state.entities)} entities")
    except Exception as e:
        logger.error(f"Failed to update engine state: {e}")


def get_pending_commands() -> str:
    """
    Called by the engine to retrieve and clear pending commands.
    Returns a JSON array of commands.
    """
    global command_queue
    commands = json.dumps(command_queue)
    command_queue.clear()
    return commands


# ============================================================================
# Entry Point
# ============================================================================

async def main():
    server = MCPServer()
    await server.run_stdio()

if __name__ == "__main__":
    asyncio.run(main())
