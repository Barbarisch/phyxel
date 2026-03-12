#!/usr/bin/env python3
"""
Phyxel Dialogue & NPC Test Script
==================================
Run this while the game (phyxel.exe) is running to test the dialogue system,
speech bubbles, NPC spawning, and interaction prompt.

Usage:
    python scripts/test_dialogue.py [command]

Commands:
    setup       Spawn NPCs with dialogue trees near camera (default)
    dialogue    Start a dialogue with a standalone speaker
    bubble      Show speech bubbles above NPCs
    cleanup     Remove all test NPCs
    status      Check engine status and list NPCs
    all         Run full test sequence with pauses

Requirements:
    pip install httpx
"""

import sys
import time
import json
import httpx

API = "http://localhost:8090"
TIMEOUT = 10.0


def api_get(path: str) -> dict:
    try:
        r = httpx.get(f"{API}{path}", timeout=TIMEOUT)
        return r.json()
    except httpx.ConnectError:
        print(f"ERROR: Engine not running. Start phyxel.exe first.")
        sys.exit(1)


def api_post(path: str, body: dict) -> dict:
    try:
        r = httpx.post(f"{API}{path}", json=body, timeout=TIMEOUT)
        return r.json()
    except httpx.ConnectError:
        print(f"ERROR: Engine not running. Start phyxel.exe first.")
        sys.exit(1)


def get_camera_pos() -> tuple[float, float, float]:
    cam = api_get("/api/camera")
    pos = cam.get("position", {})
    return pos.get("x", 16), pos.get("y", 20), pos.get("z", 16)


# ============================================================================
# Test Scenarios
# ============================================================================

def cmd_setup():
    """Spawn test NPCs with dialogue trees near the camera."""
    cx, cy, cz = get_camera_pos()
    print(f"Camera at ({cx:.0f}, {cy:.0f}, {cz:.0f})")

    # --- NPC 1: Guard with branching dialogue ---
    guard_pos = {"x": cx + 5, "y": cy - 2, "z": cz + 5}
    print(f"\nSpawning Guard at {guard_pos}...")
    r = api_post("/api/npc/spawn", {
        "name": "Guard",
        "animFile": "character.anim",
        "position": guard_pos,
        "behavior": "idle"
    })
    print(f"  -> {r}")

    guard_tree = {
        "id": "guard_dialogue",
        "startNodeId": "greet",
        "nodes": [
            {
                "id": "greet",
                "speaker": "Guard",
                "text": "Halt! State your business in this town, traveler.",
                "emotion": "stern",
                "nextNodeId": "",
                "choices": [
                    {"text": "I'm just passing through.", "targetNodeId": "passing"},
                    {"text": "I heard there's treasure nearby.", "targetNodeId": "treasure"},
                    {"text": "None of your business!", "targetNodeId": "rude"}
                ]
            },
            {
                "id": "passing",
                "speaker": "Guard",
                "text": "Very well. Keep to the main road and don't cause trouble. The inn is just ahead if you need rest.",
                "emotion": "neutral",
                "nextNodeId": "farewell",
                "choices": []
            },
            {
                "id": "treasure",
                "speaker": "Guard",
                "text": "Treasure? Ha! Every fool and their dog comes here looking for the old king's gold. It's just a legend...",
                "emotion": "amused",
                "nextNodeId": "treasure2",
                "choices": []
            },
            {
                "id": "treasure2",
                "speaker": "Guard",
                "text": "...though between you and me, I've heard strange noises from the caves to the north at night.",
                "emotion": "whisper",
                "nextNodeId": "",
                "choices": [
                    {"text": "Tell me more about the caves.", "targetNodeId": "caves"},
                    {"text": "Thanks for the tip!", "targetNodeId": "farewell"}
                ]
            },
            {
                "id": "caves",
                "speaker": "Guard",
                "text": "Head north past the old oak tree. But be warned — not everyone who goes in comes back out. Take a torch, at least.",
                "emotion": "serious",
                "nextNodeId": "farewell",
                "choices": []
            },
            {
                "id": "rude",
                "speaker": "Guard",
                "text": "Watch your tongue! I've thrown bigger folk than you in the stocks. Move along before I change my mind.",
                "emotion": "angry",
                "nextNodeId": "",
                "choices": []
            },
            {
                "id": "farewell",
                "speaker": "Guard",
                "text": "Safe travels, stranger.",
                "emotion": "neutral",
                "nextNodeId": "",
                "choices": []
            }
        ]
    }

    print("Setting Guard dialogue tree...")
    r = api_post("/api/npc/dialogue", {"name": "Guard", "tree": guard_tree})
    print(f"  -> {r}")

    # --- NPC 2: Merchant with patrol ---
    merchant_pos = {"x": cx - 5, "y": cy - 2, "z": cz + 3}
    wp1 = {"x": cx - 5, "y": cy - 2, "z": cz + 3}
    wp2 = {"x": cx - 5, "y": cy - 2, "z": cz + 10}
    print(f"\nSpawning Merchant at {merchant_pos} (patrol)...")
    r = api_post("/api/npc/spawn", {
        "name": "Merchant",
        "animFile": "character_female.anim",
        "position": merchant_pos,
        "behavior": "patrol",
        "waypoints": [wp1, wp2],
        "walkSpeed": 1.5,
        "waitTime": 3.0
    })
    print(f"  -> {r}")

    merchant_tree = {
        "id": "merchant_dialogue",
        "startNodeId": "hello",
        "nodes": [
            {
                "id": "hello",
                "speaker": "Merchant",
                "text": "Welcome, welcome! Finest goods in all the land! What catches your eye?",
                "emotion": "cheerful",
                "nextNodeId": "",
                "choices": [
                    {"text": "What do you sell?", "targetNodeId": "wares"},
                    {"text": "Just looking.", "targetNodeId": "browse"}
                ]
            },
            {
                "id": "wares",
                "speaker": "Merchant",
                "text": "Potions, scrolls, enchanted trinkets — you name it! My latest shipment includes a rare healing crystal from the eastern mountains.",
                "emotion": "excited",
                "nextNodeId": "buy",
                "choices": []
            },
            {
                "id": "buy",
                "speaker": "Merchant",
                "text": "So, interested in anything?",
                "emotion": "hopeful",
                "nextNodeId": "",
                "choices": [
                    {"text": "I'll take the crystal!", "targetNodeId": "sold"},
                    {"text": "Maybe next time.", "targetNodeId": "bye"}
                ]
            },
            {
                "id": "sold",
                "speaker": "Merchant",
                "text": "Excellent taste! That'll be 50 gold. Pleasure doing business!",
                "emotion": "happy",
                "nextNodeId": "",
                "choices": []
            },
            {
                "id": "browse",
                "speaker": "Merchant",
                "text": "Take your time! Everything's on display. Let me know if anything catches your fancy.",
                "emotion": "friendly",
                "nextNodeId": "",
                "choices": []
            },
            {
                "id": "bye",
                "speaker": "Merchant",
                "text": "No worries! Come back anytime. I'll save the good stuff for you!",
                "emotion": "wink",
                "nextNodeId": "",
                "choices": []
            }
        ]
    }

    print("Setting Merchant dialogue tree...")
    r = api_post("/api/npc/dialogue", {"name": "Merchant", "tree": merchant_tree})
    print(f"  -> {r}")

    print("\n=== SETUP COMPLETE ===")
    print("Walk up to an NPC and press E to interact!")
    print("  Enter  = advance dialogue / skip typewriter")
    print("  1-4    = select a choice")
    print("  Esc    = end conversation")


def cmd_dialogue():
    """Start a standalone dialogue (no NPC entity needed)."""
    tree = {
        "id": "narrator",
        "startNodeId": "intro",
        "nodes": [
            {
                "id": "intro",
                "speaker": "Narrator",
                "text": "The world stretches out before you. Voxel mountains rise in the distance, their peaks catching the last light of day.",
                "emotion": "",
                "nextNodeId": "choice",
                "choices": []
            },
            {
                "id": "choice",
                "speaker": "Narrator",
                "text": "A crossroads lies ahead. Which path do you choose?",
                "emotion": "",
                "nextNodeId": "",
                "choices": [
                    {"text": "The mountain path", "targetNodeId": "mountain"},
                    {"text": "The forest trail", "targetNodeId": "forest"},
                    {"text": "The river road", "targetNodeId": "river"}
                ]
            },
            {
                "id": "mountain",
                "speaker": "Narrator",
                "text": "You climb higher. The air thins. Ahead, the ruins of an ancient fortress emerge from the mist.",
                "emotion": "",
                "nextNodeId": "",
                "choices": []
            },
            {
                "id": "forest",
                "speaker": "Narrator",
                "text": "The canopy closes overhead. Strange glowing mushrooms light your way. Something watches from the shadows.",
                "emotion": "",
                "nextNodeId": "",
                "choices": []
            },
            {
                "id": "river",
                "speaker": "Narrator",
                "text": "The river rushes beside you. A small boat is tied to the dock. Perhaps it could take you downstream to the village.",
                "emotion": "",
                "nextNodeId": "",
                "choices": []
            }
        ]
    }

    print("Starting narrator dialogue...")
    r = api_post("/api/dialogue/start", {"npc": "Narrator", "tree": tree})
    print(f"  -> {r}")
    print("Use Enter to advance, 1-3 for choices, Esc to close.")


def cmd_bubble():
    """Show speech bubbles above NPCs."""
    npcs = api_get("/api/npcs")
    npc_list = npcs.get("npcs", [])

    if not npc_list:
        print("No NPCs found. Run 'setup' first.")
        return

    messages = [
        "Hello there!",
        "Nice weather today.",
        "Have you heard the news?",
        "Watch your step!",
        "Voxels are wonderful.",
    ]

    for i, npc in enumerate(npc_list):
        name = npc["name"]
        entity_id = f"npc_{name}"
        msg = messages[i % len(messages)]
        print(f"Bubble on {name} (entity: {entity_id}): \"{msg}\"")
        r = api_post("/api/speech/say", {
            "entityId": entity_id,
            "text": msg,
            "duration": 5.0
        })
        print(f"  -> {r}")


def cmd_cleanup():
    """Remove all test NPCs."""
    for name in ["Guard", "Merchant"]:
        print(f"Removing {name}...")
        r = api_post("/api/npc/remove", {"name": name})
        print(f"  -> {r}")
    print("Cleanup complete.")


def cmd_status():
    """Check engine status and list NPCs."""
    print("=== Engine Status ===")
    status = api_get("/api/status")
    print(json.dumps(status, indent=2))

    print("\n=== NPCs ===")
    npcs = api_get("/api/npcs")
    print(json.dumps(npcs, indent=2))

    print("\n=== Camera ===")
    cam = api_get("/api/camera")
    pos = cam.get("position", {})
    print(f"  Position: ({pos.get('x', 0):.1f}, {pos.get('y', 0):.1f}, {pos.get('z', 0):.1f})")


def cmd_all():
    """Run full test sequence."""
    print("=" * 60)
    print("PHYXEL DIALOGUE SYSTEM - FULL TEST")
    print("=" * 60)

    print("\n--- Step 1: Check engine status ---")
    cmd_status()

    print("\n--- Step 2: Spawn NPCs with dialogues ---")
    cmd_setup()
    print("\nWaiting 2 seconds for NPCs to settle...")
    time.sleep(2)

    print("\n--- Step 3: Speech bubbles ---")
    cmd_bubble()
    print("\nBubbles should be visible above NPCs for 5 seconds.")
    print("Walk up to an NPC and press E to test dialogue.")
    time.sleep(3)

    print("\n--- Step 4: Standalone narrator dialogue ---")
    cmd_dialogue()

    print("\n" + "=" * 60)
    print("TEST COMPLETE")
    print("=" * 60)
    print("\nYou should see:")
    print("  1. Two NPCs (Guard + Merchant) near your position")
    print("  2. Speech bubbles above them (fading out)")
    print("  3. A narrator dialogue box at the bottom of the screen")
    print("  4. '[E] Interact' prompt when walking near an NPC")
    print("\nControls:")
    print("  Enter  = advance / skip typewriter")
    print("  1-4    = select choice")
    print("  Esc    = end conversation")
    print("  E      = interact with NPC (when near one)")
    print("\nRun 'python scripts/test_dialogue.py cleanup' when done.")


# ============================================================================
# CLI
# ============================================================================

COMMANDS = {
    "setup": cmd_setup,
    "dialogue": cmd_dialogue,
    "bubble": cmd_bubble,
    "cleanup": cmd_cleanup,
    "status": cmd_status,
    "all": cmd_all,
}

if __name__ == "__main__":
    cmd_name = sys.argv[1] if len(sys.argv) > 1 else "setup"

    if cmd_name in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)

    if cmd_name not in COMMANDS:
        print(f"Unknown command: {cmd_name}")
        print(f"Available: {', '.join(COMMANDS.keys())}")
        sys.exit(1)

    COMMANDS[cmd_name]()
