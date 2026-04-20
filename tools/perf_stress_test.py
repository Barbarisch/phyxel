#!/usr/bin/env python3
"""
Phyxel Engine — GPU & Bullet & VoxelDynamicsWorld Performance Stress Tester

Ramps up particle/object counts to find FPS breakpoints for GPU compute,
Bullet physics, and the custom VoxelDynamicsWorld systems.

Usage:
    python tools/perf_stress_test.py                    # Run all tests
    python tools/perf_stress_test.py --mode gpu         # GPU-only ramp
    python tools/perf_stress_test.py --mode bullet      # Bullet-only ramp
    python tools/perf_stress_test.py --mode voxel       # VoxelDynamicsWorld ramp
    python tools/perf_stress_test.py --mode mixed       # Mixed ramp
    python tools/perf_stress_test.py --mode scale       # Scale comparison
    python tools/perf_stress_test.py --mode sustained   # Sustained max load
    python tools/perf_stress_test.py --quick            # Fewer steps, faster
    python tools/perf_stress_test.py --settle 3         # Custom settle time (seconds)

Requires: Engine running at http://localhost:8090
"""

import argparse
import csv
import json
import os
import sys
import time
import urllib.request
import urllib.error
from datetime import datetime


BASE_URL = "http://localhost:8090"
SETTLE_TIME = 2.0       # seconds to wait after spawning before measuring
SAMPLE_COUNT = 10       # number of timing samples per measurement
SAMPLE_INTERVAL = 0.1   # seconds between samples


def api_get(path):
    """GET request to engine API, returns parsed JSON."""
    req = urllib.request.Request(BASE_URL + path)
    resp = urllib.request.urlopen(req, timeout=10)
    return json.loads(resp.read())


def api_post(path, body):
    """POST request to engine API, returns parsed JSON."""
    data = json.dumps(body).encode()
    req = urllib.request.Request(
        BASE_URL + path, data,
        headers={"Content-Type": "application/json"}
    )
    resp = urllib.request.urlopen(req, timeout=30)
    return json.loads(resp.read())


def check_engine():
    """Verify engine is running and API is responsive."""
    try:
        status = api_get("/api/status")
        return status.get("status") == "ok"
    except Exception:
        return False


def get_timing():
    """Fetch engine timing data."""
    return api_get("/api/debug/engine_timing")


def get_stats():
    """Fetch dynamic object counts."""
    return api_get("/api/debug/dynamic_stats")


def clear_dynamics():
    """Remove all Bullet + GPU particles."""
    return api_post("/api/debug/clear_dynamics", {})


def spawn_gpu(count, scale=1.0, x=32.0, y=20.0, z=32.0):
    """Spawn GPU particles."""
    return api_post("/api/debug/spawn_gpu_particle", {
        "x": x, "y": y, "z": z,
        "material": "Stone",
        "scale": scale,
        "count": count,
        "lifetime": 120.0,  # long lifetime so they don't expire during test
    })


def spawn_bullet(count, scale=1.0, x=32.0, y=20.0, z=32.0):
    """Spawn Bullet cubes."""
    return api_post("/api/debug/spawn_bullet_cube", {
        "x": x, "y": y, "z": z,
        "material": "Stone",
        "scale": scale,
        "count": count,
        "lifetime": 120.0,  # long lifetime so they don't expire during test
    })


def get_camera_pos():
    """Return (x, y, z) of the current camera position."""
    try:
        state = api_get("/api/world/state")
        cam = state.get("camera", {}).get("position", {})
        return cam.get("x", 32.0), cam.get("y", 20.0), cam.get("z", 32.0)
    except Exception:
        return 32.0, 20.0, 32.0


def spawn_voxel(count, scale=1.0, x=None, y=None, z=None, lifetime=10.0):
    """Spawn VoxelDynamicsWorld bodies near the camera."""
    if x is None or y is None or z is None:
        cx, cy, cz = get_camera_pos()
        x = cx if x is None else x
        y = (cy + 3.0) if y is None else y   # a few units above camera
        z = cz if z is None else z
    return api_post("/api/debug/spawn_voxel_body", {
        "x": x, "y": y, "z": z,
        "scale": scale,
        "mass": 1.0,
        "lifetime": lifetime,
        "count": count,
    })


def clear_voxel_bodies():
    """Remove all VoxelDynamicsWorld bodies."""
    return api_post("/api/debug/clear_voxel_bodies", {})


def measure(settle_time=SETTLE_TIME):
    """Wait for settling, then sample timing data multiple times.
    Returns dict with avg/min/max FPS and timing breakdown.
    """
    time.sleep(settle_time)

    samples = []
    for _ in range(SAMPLE_COUNT):
        try:
            t = get_timing()
            samples.append(t)
        except Exception as e:
            print(f"  Warning: timing sample failed: {e}")
        time.sleep(SAMPLE_INTERVAL)

    if not samples:
        return None

    fps_vals = [s["fps"] for s in samples if s.get("fps", 0) > 0]
    cpu_vals = [s["cpuFrameTime"] for s in samples]

    stats = get_stats()
    last = samples[-1]

    return {
        "fps_avg": sum(fps_vals) / len(fps_vals) if fps_vals else 0,
        "fps_min": min(fps_vals) if fps_vals else 0,
        "fps_max": max(fps_vals) if fps_vals else 0,
        "cpu_frame_ms": sum(cpu_vals) / len(cpu_vals) if cpu_vals else 0,
        "gpu_frame_ms": last.get("gpuFrameTime", 0),
        "draw_calls": last.get("drawCalls", 0),
        "visible_instances": last.get("visibleInstances", 0),
        "bullet_active": stats.get("bullet_active", 0),
        "bullet_total": stats.get("bullet_total", stats.get("bullet_active", 0)),
        "gpu_active": stats.get("gpu_active", 0),
        "physics_time": last.get("detailed", {}).get("physicsTime", 0),
    }


def screenshot(label):
    """Take a screenshot and return the path."""
    try:
        result = api_get("/api/screenshot")
        return result.get("path", "")
    except Exception:
        return ""


def print_header():
    """Print column headers for console output."""
    print(f"{'Step':<8} {'System':<8} {'Count':>7} {'Scale':>6} "
          f"{'FPS avg':>8} {'FPS min':>8} {'CPU ms':>8} "
          f"{'Bt act':>7} {'Bt tot':>7} {'GPU':>7} {'Draws':>6}")
    print("-" * 98)


def print_row(step, system, count, scale, m):
    """Print one measurement row."""
    print(f"{step:<8} {system:<8} {count:>7} {scale:>6.3f} "
          f"{m['fps_avg']:>8.1f} {m['fps_min']:>8.1f} {m['cpu_frame_ms']:>8.2f} "
          f"{m['bullet_active']:>7} {m['bullet_total']:>7} {m['gpu_active']:>7} {m['draw_calls']:>6}")


def find_breakpoints(rows):
    """Identify FPS breakpoints (where avg FPS drops below 60, 30, 15)."""
    breakpoints = {}
    thresholds = [60, 30, 15]
    for threshold in thresholds:
        for row in rows:
            if row["fps_avg"] < threshold and threshold not in breakpoints:
                breakpoints[threshold] = row
    return breakpoints


# =========================================================================
# Test modes
# =========================================================================

def test_gpu_ramp(quick=False, settle_time=SETTLE_TIME):
    """Ramp up GPU particle count and measure FPS at each level."""
    if quick:
        steps = [100, 500, 1000, 2000, 5000]
    else:
        steps = [100, 250, 500, 1000, 1500, 2000, 3000, 4000, 5000, 7500, 10000]

    print("\n=== GPU PARTICLE RAMP TEST ===\n")
    print_header()

    clear_dynamics()
    time.sleep(1)

    rows = []
    total_spawned = 0

    # Baseline (0 particles)
    m = measure(settle_time)
    if m:
        row = {"step": 0, "system": "gpu", "count": 0, "scale": 1.0, **m}
        rows.append(row)
        print_row(0, "gpu", 0, 1.0, m)

    for i, target in enumerate(steps, 1):
        to_spawn = target - total_spawned
        if to_spawn <= 0:
            continue

        # Spawn in batches of up to 2000
        remaining = to_spawn
        while remaining > 0:
            batch = min(remaining, 2000)
            spawn_gpu(batch)
            remaining -= batch
            total_spawned += batch

        m = measure(settle_time)
        if m:
            row = {"step": i, "system": "gpu", "count": target, "scale": 1.0, **m}
            rows.append(row)
            print_row(i, "gpu", target, 1.0, m)

    clear_dynamics()
    return rows


def test_bullet_ramp(quick=False, settle_time=SETTLE_TIME):
    """Ramp up Bullet object count and measure FPS at each level."""
    if quick:
        steps = [25, 50, 100, 200, 300]
    else:
        steps = [25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300]

    print("\n=== BULLET PHYSICS RAMP TEST ===\n")
    print_header()

    clear_dynamics()
    time.sleep(1)

    rows = []
    total_spawned = 0

    # Baseline
    m = measure(settle_time)
    if m:
        row = {"step": 0, "system": "bullet", "count": 0, "scale": 1.0, **m}
        rows.append(row)
        print_row(0, "bullet", 0, 1.0, m)

    for i, target in enumerate(steps, 1):
        to_spawn = target - total_spawned
        if to_spawn <= 0:
            continue

        spawn_bullet(to_spawn)
        total_spawned = target

        m = measure(settle_time)
        if m:
            row = {"step": i, "system": "bullet", "count": target, "scale": 1.0, **m}
            rows.append(row)
            print_row(i, "bullet", target, 1.0, m)

    clear_dynamics()
    return rows


def test_voxel_ramp(quick=False, settle_time=SETTLE_TIME):
    """Ramp up VoxelDynamicsWorld body count and measure FPS at each level."""
    if quick:
        steps = [50, 100, 200, 500, 1000]
    else:
        steps = [50, 100, 200, 300, 500, 750, 1000, 1500, 2000, 3000, 5000]

    print("\n=== VOXEL DYNAMICS WORLD RAMP TEST ===\n")
    print_header()

    clear_voxel_bodies()
    time.sleep(1)

    # Spawn near the player camera, a few units above to let them fall
    cx, cy, cz = get_camera_pos()
    spawn_x, spawn_y, spawn_z = cx, cy + 4.0, cz
    print(f"  Spawning near camera at ({spawn_x:.1f}, {spawn_y:.1f}, {spawn_z:.1f})\n")

    rows = []
    total_spawned = 0

    # Baseline
    m = measure(settle_time)
    if m:
        row = {"step": 0, "system": "voxel", "count": 0, "scale": 1.0, **m}
        rows.append(row)
        print_row(0, "voxel", 0, 1.0, m)

    for i, target in enumerate(steps, 1):
        to_spawn = target - total_spawned
        if to_spawn <= 0:
            continue

        # Spawn in batches of up to 500 to avoid HTTP timeout
        remaining = to_spawn
        while remaining > 0:
            batch = min(remaining, 500)
            spawn_voxel(batch, x=spawn_x, y=spawn_y, z=spawn_z, lifetime=10.0)
            remaining -= batch
            total_spawned += batch

        m = measure(settle_time)
        if m:
            row = {"step": i, "system": "voxel", "count": target, "scale": 1.0, **m}
            rows.append(row)
            print_row(i, "voxel", target, 1.0, m)

    clear_voxel_bodies()
    return rows


def test_mixed_ramp(quick=False, settle_time=SETTLE_TIME):
    """Fill Bullet to 50% cap, then ramp GPU particles."""
    if quick:
        gpu_steps = [100, 500, 1000, 2000, 5000]
    else:
        gpu_steps = [100, 250, 500, 1000, 2000, 3000, 5000, 7500, 10000]

    bullet_count = 150  # 50% of 300 cap

    print("\n=== MIXED RAMP TEST (Bullet=%d + GPU ramp) ===\n" % bullet_count)
    print_header()

    clear_dynamics()
    time.sleep(1)

    rows = []

    # Spawn bullet objects first
    spawn_bullet(bullet_count)
    m = measure(settle_time)
    if m:
        row = {"step": 0, "system": "mixed", "count": bullet_count, "scale": 1.0, **m}
        rows.append(row)
        print_row(0, "mixed", bullet_count, 1.0, m)

    total_gpu = 0
    for i, target in enumerate(gpu_steps, 1):
        to_spawn = target - total_gpu
        if to_spawn <= 0:
            continue

        remaining = to_spawn
        while remaining > 0:
            batch = min(remaining, 2000)
            spawn_gpu(batch)
            remaining -= batch
            total_gpu += batch

        m = measure(settle_time)
        if m:
            label = f"B{bullet_count}+G{target}"
            row = {"step": i, "system": "mixed", "count": bullet_count + target,
                   "scale": 1.0, **m}
            rows.append(row)
            print_row(i, "mixed", bullet_count + target, 1.0, m)

    clear_dynamics()
    return rows


def test_scale_comparison(quick=False, settle_time=SETTLE_TIME):
    """Compare FPS across scale tiers at the same object count."""
    scales = [1.0, 0.333, 0.111]
    scale_names = {1.0: "full", 0.333: "subcube", 0.111: "micro"}

    if quick:
        counts = [500, 2000]
    else:
        counts = [250, 500, 1000, 2000, 5000]

    print("\n=== SCALE COMPARISON TEST (GPU) ===\n")
    print_header()

    rows = []
    step = 0

    for count in counts:
        for scale in scales:
            clear_dynamics()
            time.sleep(1)

            remaining = count
            while remaining > 0:
                batch = min(remaining, 2000)
                spawn_gpu(batch, scale=scale)
                remaining -= batch

            m = measure(settle_time)
            if m:
                row = {"step": step, "system": scale_names[scale],
                       "count": count, "scale": scale, **m}
                rows.append(row)
                print_row(step, scale_names[scale], count, scale, m)
                step += 1

    clear_dynamics()
    return rows


def test_sustained(settle_time=SETTLE_TIME):
    """Max GPU particles held for 30 seconds, tracking FPS over time."""
    target = 10000
    duration = 30

    print(f"\n=== SUSTAINED LOAD TEST ({target} GPU particles, {duration}s) ===\n")
    print_header()

    clear_dynamics()
    time.sleep(1)

    # Spawn all particles
    remaining = target
    while remaining > 0:
        batch = min(remaining, 2000)
        spawn_gpu(batch)
        remaining -= batch

    time.sleep(settle_time)

    rows = []
    start = time.time()

    for i in range(duration):
        try:
            t = get_timing()
            stats = get_stats()
            m = {
                "fps_avg": t.get("fps", 0),
                "fps_min": t.get("fps", 0),
                "fps_max": t.get("fps", 0),
                "cpu_frame_ms": t.get("cpuFrameTime", 0),
                "gpu_frame_ms": t.get("gpuFrameTime", 0),
                "draw_calls": t.get("drawCalls", 0),
                "visible_instances": t.get("visibleInstances", 0),
                "bullet_active": stats.get("bullet_active", 0),
                "gpu_active": stats.get("gpu_active", 0),
                "physics_time": t.get("detailed", {}).get("physicsTime", 0),
            }
            row = {"step": i, "system": "sustained", "count": target,
                   "scale": 1.0, **m}
            rows.append(row)
            print_row(i, "sustain", target, 1.0, m)
        except Exception as e:
            print(f"  Warning: sample {i} failed: {e}")

        elapsed = time.time() - start
        next_sample = (i + 1) * 1.0
        if next_sample > elapsed:
            time.sleep(next_sample - elapsed)

    clear_dynamics()
    return rows


# =========================================================================
# Output
# =========================================================================

def write_csv(rows, filename):
    """Write results to CSV file."""
    if not rows:
        return
    fieldnames = ["step", "system", "count", "scale",
                  "fps_avg", "fps_min", "fps_max",
                  "cpu_frame_ms", "gpu_frame_ms",
                  "draw_calls", "visible_instances",
                  "bullet_active", "gpu_active", "physics_time"]
    with open(filename, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nResults written to: {filename}")


def write_summary(all_rows, filename):
    """Write JSON summary with breakpoints."""
    summary = {
        "timestamp": datetime.now().isoformat(),
        "total_measurements": len(all_rows),
        "tests": {},
    }

    # Group by system
    by_system = {}
    for row in all_rows:
        sys_name = row.get("system", "unknown")
        by_system.setdefault(sys_name, []).append(row)

    for sys_name, rows in by_system.items():
        bp = find_breakpoints(rows)
        test_summary = {
            "measurements": len(rows),
            "max_count": max(r["count"] for r in rows) if rows else 0,
            "fps_range": {
                "min": min(r["fps_avg"] for r in rows) if rows else 0,
                "max": max(r["fps_avg"] for r in rows) if rows else 0,
            },
            "breakpoints": {},
        }
        for threshold, row in bp.items():
            test_summary["breakpoints"][f"below_{threshold}fps"] = {
                "count": row["count"],
                "fps_avg": round(row["fps_avg"], 1),
                "cpu_frame_ms": round(row["cpu_frame_ms"], 2),
            }
        summary["tests"][sys_name] = test_summary

    with open(filename, "w") as f:
        json.dump(summary, f, indent=2)
    print(f"Summary written to: {filename}")


def print_breakpoint_summary(all_rows):
    """Print human-readable breakpoint summary."""
    by_system = {}
    for row in all_rows:
        sys_name = row.get("system", "unknown")
        by_system.setdefault(sys_name, []).append(row)

    print("\n" + "=" * 60)
    print("FPS BREAKPOINT SUMMARY")
    print("=" * 60)

    for sys_name, rows in sorted(by_system.items()):
        bp = find_breakpoints(rows)
        print(f"\n  {sys_name.upper()}:")
        if not bp:
            max_count = max(r["count"] for r in rows) if rows else 0
            min_fps = min(r["fps_avg"] for r in rows) if rows else 0
            print(f"    No drops below 60 FPS (tested up to {max_count} objects, min FPS={min_fps:.1f})")
        else:
            for threshold in [60, 30, 15]:
                if threshold in bp:
                    r = bp[threshold]
                    print(f"    Below {threshold} FPS at {r['count']} objects "
                          f"(avg={r['fps_avg']:.1f}, cpu={r['cpu_frame_ms']:.2f}ms)")
                else:
                    print(f"    Never dropped below {threshold} FPS")


# =========================================================================
# Main
# =========================================================================

def main():
    parser = argparse.ArgumentParser(description="Phyxel GPU/Bullet performance stress tester")
    parser.add_argument("--mode", choices=["all", "gpu", "bullet", "voxel", "mixed", "scale", "sustained"],
                        default="all", help="Test mode (default: all)")
    parser.add_argument("--quick", action="store_true",
                        help="Fewer test steps for faster results")
    parser.add_argument("--settle", type=float, default=SETTLE_TIME,
                        help=f"Settle time after spawning (default: {SETTLE_TIME}s)")
    parser.add_argument("--output", type=str, default=None,
                        help="Output CSV filename (default: auto-generated)")
    args = parser.parse_args()

    # Check engine
    if not check_engine():
        print("ERROR: Engine not running at %s" % BASE_URL)
        print("Start the engine first, then run this script.")
        sys.exit(1)

    print("Phyxel Performance Stress Tester")
    print(f"Engine: {BASE_URL}")
    print(f"Mode: {args.mode}, Quick: {args.quick}, Settle: {args.settle}s")

    # Verify new endpoints work
    try:
        get_timing()
        get_stats()
    except Exception as e:
        print(f"ERROR: New endpoints not available: {e}")
        print("Make sure you've rebuilt the engine with the latest changes.")
        sys.exit(1)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_file = args.output or f"perf_results_{timestamp}.csv"
    json_file = csv_file.replace(".csv", ".json")

    all_rows = []

    modes = {
        "gpu": lambda: test_gpu_ramp(args.quick, args.settle),
        "bullet": lambda: test_bullet_ramp(args.quick, args.settle),
        "voxel": lambda: test_voxel_ramp(args.quick, args.settle),
        "mixed": lambda: test_mixed_ramp(args.quick, args.settle),
        "scale": lambda: test_scale_comparison(args.quick, args.settle),
        "sustained": lambda: test_sustained(args.settle),
    }

    if args.mode == "all":
        for name in ["gpu", "bullet", "voxel", "mixed", "scale", "sustained"]:
            try:
                rows = modes[name]()
                all_rows.extend(rows)
            except Exception as e:
                print(f"\n  ERROR in {name} test: {e}")
    else:
        all_rows = modes[args.mode]()

    # Output results
    write_csv(all_rows, csv_file)
    write_summary(all_rows, json_file)
    print_breakpoint_summary(all_rows)

    print(f"\nDone! {len(all_rows)} measurements collected.")


if __name__ == "__main__":
    main()
