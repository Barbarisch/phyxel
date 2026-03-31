"""NPC patrol & look-around demo for FOV cone visualization.

Run with the engine already up and F5 pressed to see debug cones.
Usage: python scripts/npc_patrol_demo.py
"""

import math
import time
import requests

API = "http://localhost:8090/api"
DT = 0.05  # 20 Hz update rate


def api(method, path, json=None):
    try:
        r = getattr(requests, method)(f"{API}{path}", json=json, timeout=2)
        return r.json()
    except Exception:
        return None


def set_rotation_yaw(entity_id: str, yaw_deg: float):
    """Set entity rotation from a yaw angle (degrees). 0 = +Z, 90 = +X."""
    yaw_rad = math.radians(yaw_deg)
    half = yaw_rad / 2.0
    # Rotation around Y axis: quat(cos(half), 0, sin(half), 0)
    w = math.cos(half)
    y = math.sin(half)
    api("post", "/entity/update", {
        "id": entity_id,
        "rotation": {"w": w, "x": 0, "y": y, "z": 0}
    })


def move_entity(entity_id: str, x: float, y: float, z: float):
    api("post", "/entity/move", {"id": entity_id, "position": {"x": x, "y": y, "z": z}})


def lerp(a, b, t):
    return a + (b - a) * t


def angle_toward(fx, fz, tx, tz):
    """Yaw angle (degrees) from (fx,fz) looking toward (tx,tz). 0 = +Z."""
    dx = tx - fx
    dz = tz - fz
    return math.degrees(math.atan2(dx, dz))


def main():
    print("NPC Patrol Demo — press Ctrl+C to stop")
    print("Make sure engine is running and F5 is toggled for debug viz\n")

    # Check engine
    status = api("get", "/status")
    if not status or status.get("status") != "ok":
        print("Engine not responding at localhost:8090")
        return

    # Switch both NPCs to behavior_tree (enables FOV cone)
    for name in ["Blacksmith", "Herbalist"]:
        api("post", "/npc/behavior", {"name": name, "behavior": "behavior_tree"})
    print("Both NPCs set to behavior_tree mode")

    # Blacksmith: patrols a square path at ground level
    smith_waypoints = [
        (10, 17, 13),
        (10, 17, 19),
        (16, 17, 19),
        (16, 17, 13),
    ]

    # Herbalist: patrols a triangle, different pace
    herb_waypoints = [
        (20, 17, 13),
        (24, 17, 19),
        (18, 17, 19),
    ]

    smith_wp_idx = 0
    herb_wp_idx = 0
    smith_progress = 0.0
    herb_progress = 0.0
    smith_speed = 0.4   # progress per second (0..1 between waypoints)
    herb_speed = 0.3

    # Current positions (start at first waypoint)
    sx, sy, sz = smith_waypoints[0]
    hx, hy, hz = herb_waypoints[0]
    move_entity("npc_Blacksmith", sx, sy, sz)
    move_entity("npc_Herbalist", hx, hy, hz)

    # Idle look-around state (used when arriving at waypoint)
    smith_idle_timer = 0.0
    herb_idle_timer = 0.0
    IDLE_DURATION = 2.0  # seconds to look around at each waypoint
    smith_idle_yaw_start = 0.0
    herb_idle_yaw_start = 0.0

    print("Patrolling... watch the FOV cones move!")

    try:
        while True:
            t0 = time.time()

            # --- Blacksmith ---
            if smith_idle_timer > 0:
                # Look-around: sweep yaw left and right
                smith_idle_timer -= DT
                sweep = math.sin(smith_idle_timer * 3.0) * 45  # +/- 45 degrees
                set_rotation_yaw("npc_Blacksmith", smith_idle_yaw_start + sweep)
            else:
                # Move toward next waypoint
                src = smith_waypoints[smith_wp_idx]
                dst = smith_waypoints[(smith_wp_idx + 1) % len(smith_waypoints)]
                smith_progress += smith_speed * DT

                if smith_progress >= 1.0:
                    # Arrived — start idle look-around
                    smith_progress = 0.0
                    smith_wp_idx = (smith_wp_idx + 1) % len(smith_waypoints)
                    sx, sy, sz = dst
                    move_entity("npc_Blacksmith", sx, sy, sz)
                    smith_idle_timer = IDLE_DURATION
                    smith_idle_yaw_start = angle_toward(
                        dst[0], dst[2],
                        smith_waypoints[(smith_wp_idx + 1) % len(smith_waypoints)][0],
                        smith_waypoints[(smith_wp_idx + 1) % len(smith_waypoints)][2],
                    )
                    set_rotation_yaw("npc_Blacksmith", smith_idle_yaw_start)
                else:
                    sx = lerp(src[0], dst[0], smith_progress)
                    sz = lerp(src[2], dst[2], smith_progress)
                    sy = src[1]
                    move_entity("npc_Blacksmith", sx, sy, sz)
                    yaw = angle_toward(src[0], src[2], dst[0], dst[2])
                    set_rotation_yaw("npc_Blacksmith", yaw)

            # --- Herbalist ---
            if herb_idle_timer > 0:
                herb_idle_timer -= DT
                sweep = math.sin(herb_idle_timer * 2.5) * 60  # wider sweep
                set_rotation_yaw("npc_Herbalist", herb_idle_yaw_start + sweep)
            else:
                src = herb_waypoints[herb_wp_idx]
                dst = herb_waypoints[(herb_wp_idx + 1) % len(herb_waypoints)]
                herb_progress += herb_speed * DT

                if herb_progress >= 1.0:
                    herb_progress = 0.0
                    herb_wp_idx = (herb_wp_idx + 1) % len(herb_waypoints)
                    hx, hy, hz = dst
                    move_entity("npc_Herbalist", hx, hy, hz)
                    herb_idle_timer = IDLE_DURATION
                    herb_idle_yaw_start = angle_toward(
                        dst[0], dst[2],
                        herb_waypoints[(herb_wp_idx + 1) % len(herb_waypoints)][0],
                        herb_waypoints[(herb_wp_idx + 1) % len(herb_waypoints)][2],
                    )
                    set_rotation_yaw("npc_Herbalist", herb_idle_yaw_start)
                else:
                    hx = lerp(src[0], dst[0], herb_progress)
                    hz = lerp(src[2], dst[2], herb_progress)
                    hy = src[1]
                    move_entity("npc_Herbalist", hx, hy, hz)
                    yaw = angle_toward(src[0], src[2], dst[0], dst[2])
                    set_rotation_yaw("npc_Herbalist", yaw)

            elapsed = time.time() - t0
            sleep_time = max(0, DT - elapsed)
            time.sleep(sleep_time)

    except KeyboardInterrupt:
        print("\nStopped. NPCs will stay at their last position.")


if __name__ == "__main__":
    main()
