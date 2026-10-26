"""WaterCore A2j: the measuring tools' clock (tools/water_bench.py particle_tracks) on synthetic GPU particle logs.

The log's frame header carries the logging frame's dt and ticks, but the snapshot it prints is older and frames whose copy was
skipped are not logged at all, so summing dt undercounts time (a free fall read ~10 g, WaterCore 37). The reader uses the header's
last column, snap_ticks (the solver's total ticks at the snapshot, added in A2j). Run: python -m pytest tests/test_particle_tracks.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from water_bench import SOLVER_TICK, particle_tracks  # noqa: E402

G = 9.81


def header(frame, snap):
    # F,frame,dt,ticks,alpha,activeCount, 7 character columns, snap_ticks
    return f"F,{frame},0.0013,0,0.5,1,0,0,0,0,0,0,0,{snap}\n"


def prow(frame, slot, y, py, age, sleeping=False):
    flags = 1 | (2 if sleeping else 0) | (min(age, 255) << 16)
    return f"P,{frame},{slot},1,{y},1,1,{py},1,{flags},3\n"


def free_fall_log(tmp_path, every=4, n_ticks=60, y0=20.0, old_slot_y=None):
    """A stone falling from rest, logged on every `every`-th tick only (the copy is skipped while one is pending)."""
    lines = ["# Phyxel GPU Particle Position Log\n"]
    frame = 0
    if old_slot_y is not None:   # the cleared last body still in slot 0 on the first logged frame (age high)
        lines += [header(frame, 1000), prow(frame, 0, old_slot_y, old_slot_y, 200)]; frame += 1
    for k in range(0, n_ticks + 1, every):
        y = y0 - 0.5 * G * (k * SOLVER_TICK) ** 2
        py = y0 - 0.5 * G * (max(k - 1, 0) * SOLVER_TICK) ** 2
        lines += [header(frame, 1001 + k), prow(frame, 0, y, py, k)]; frame += 1
    p = tmp_path / "log.csv"
    p.write_text("".join(lines))
    return p


def test_free_fall_reads_g(tmp_path):
    tracks = particle_tracks(free_fall_log(tmp_path))
    (tr,) = tracks.values()
    t0, y0 = tr[0][0], tr[0][1]
    t, y = tr[-1][0] - t0, tr[-1][1]
    assert t == pytest.approx(1.0, abs=1e-9)
    assert 2 * (y0 - y) / t ** 2 == pytest.approx(G, rel=0.01)       # the fall's own acceleration
    assert -tr[-1][2] == pytest.approx(G * t, rel=0.02)              # vy = one tick's step / (1/60)


def test_the_old_summed_dt_clock_is_wrong(tmp_path):
    """RED, kept as the record: the reader the tools used before A2j (frame dt summed) on the same log reads far more than g."""
    t, rows = 0.0, []
    for line in free_fall_log(tmp_path).read_text().splitlines():
        if line.startswith("F,"): t += float(line.split(",")[2])
        elif line.startswith("P,"): rows.append((t, float(line.split(",")[4])))
    a = 2 * (rows[0][1] - rows[-1][1]) / (rows[-1][0] - rows[0][0]) ** 2
    assert a > 5 * G


def test_a_reused_slot_starts_a_new_track(tmp_path):
    tracks = particle_tracks(free_fall_log(tmp_path, old_slot_y=10.1667))
    assert sorted(tracks, key=str) == [0, "0#1"]
    assert len(tracks[0]) == 1 and tracks[0][0][1] == pytest.approx(10.1667)
    assert tracks["0#1"][0][1] == pytest.approx(20.0)


def test_a_log_without_snap_ticks_is_refused(tmp_path):
    p = tmp_path / "old.csv"
    p.write_text("F,0,0.0013,0,0.5,1,0,0,0,0,0,0,0\n" + prow(0, 0, 20.0, 20.0, 0))
    with pytest.raises(SystemExit, match="snap_ticks"):
        particle_tracks(p)
