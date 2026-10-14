#!/usr/bin/env python3
"""Drive UniMate (external checkout) to draft Phyxel clips from text prompts.

UniMate lives OUTSIDE this repo (``UNIMATE_ROOT``, default ``../UniMate`` next to the
Phyxel checkout) with its own Python 3.10 venv; it is never vendored or imported here.
This wrapper calls UniMate's Python entry points directly — its bash wrappers hardcode
``blender -b``, while the entry points run under the pip ``bpy`` module
(``data_process/mesh_animation/common.py::parse_blender_argv``). Design + gates:
docs/UniMateIntegrationPlan.md.

Stages (each is one subcommand; ``all`` chains them):

  preprocess  rigged+ANIMATED source asset (GLB/FBX) -> <features>/cond.npy +
              <features>/motions/<name>-<clip>.npz + <name>_canonical.glb
              (UniMate ``preprocess_char.py``). Inference only realizes object types
              that have >=1 reference clip on disk, hence "animated".
  sample      text prompts -> <exp_dir>/samples/*.npy (T, J, 12) + captions.json
              (``unimate.inference.sample``). ``--exp-dir`` holds a config.json whose
              dataset path points at <features> (see UniMate/phyxel/make_exp.py) and
              a dataset_stats.npy; the 1.19 GB checkpoint is shared via ``--model-path``.
  animate     one .npy -> animated GLB + FBX on the ORIGINAL source asset
              (``animate_motion.py``), so bone names/bind pose are the rig's own and
              ``unimate_import.py`` can remap by name.

Example (M0, humanoid):
    python tools/anim_pipeline/unimate_generate.py preprocess \
        --char phyxel/assets/phyxel_humanoid.fbx --features phyxel/features \
        --face-r mixamorig:RightUpLeg --face-l mixamorig:LeftUpLeg
    python tools/anim_pipeline/unimate_generate.py sample \
        --exp-dir outputs/phyxel_objaverse --cases phyxel/test_cases_humanoid.json --reps 3
    python tools/anim_pipeline/unimate_generate.py animate \
        --npy outputs/phyxel_objaverse/samples/phyxel_humanoid-walk-rep_0-0.npy \
        --char phyxel/assets/phyxel_humanoid.fbx --cond phyxel/features/cond.npy \
        --out outputs/phyxel_objaverse/animated
Relative paths resolve against UNIMATE_ROOT.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
UNIMATE_ROOT = Path(os.environ.get("UNIMATE_ROOT", REPO.parent / "UniMate")).resolve()
VENV_PY = UNIMATE_ROOT / ".venv" / "Scripts" / "python.exe"
if not VENV_PY.exists():
    VENV_PY = UNIMATE_ROOT / ".venv" / "bin" / "python"
DEFAULT_CKPT = ("outputs/uniml3d_60frames_graph_adaln_v2/checkpoints/"
                "checkpoint_step_100000.pt")
# Stages 1 & 5 need bpy. On Windows there is no cp310 bpy on PyPI (4.2+ only, py3.11+)
# and download.blender.org answers 403 here, so those stages run inside a portable
# Blender 4.0.2 (bundled Python 3.10 == the venv's) with the venv's site-packages on
# PYTHONPATH (Blender honours it only with --python-use-system-env). The stages import
# numpy/loguru/tqdm/Motion + bpy — no torch — verified 2026-09-29.
BLENDER = Path(os.environ.get("UNIMATE_BLENDER",
                              UNIMATE_ROOT / "tools" / "blender-4.0.2-windows-x64" / "blender.exe"))
VENV_SITE = UNIMATE_ROOT / ".venv" / "Lib" / "site-packages"


def _p(path: str | Path) -> str:
    p = Path(path)
    return str(p if p.is_absolute() else UNIMATE_ROOT / p)


def _venv_has_bpy() -> bool:
    return (VENV_SITE / "bpy").is_dir()


def run(argv: list[str], log: Path | None = None, needs_bpy: bool = False) -> int:
    """Run a UniMate entry point with cwd = UNIMATE_ROOT, streaming output.

    needs_bpy: run under the portable Blender (``blender -b --python-use-system-env -P
    script -- args``) unless the venv itself has the bpy module.
    """
    # UniMate's rich/tqdm output is UTF-8; the Windows console may be cp1252. Never let
    # an unencodable progress-bar glyph kill the driver mid-run (it did, 2026-09-29).
    try:
        sys.stdout.reconfigure(errors="replace")
    except (AttributeError, ValueError):
        pass
    if not VENV_PY.exists():
        raise SystemExit(f"UniMate venv python not found at {VENV_PY}; set UNIMATE_ROOT")
    env = dict(os.environ, PYTHONUNBUFFERED="1")
    if needs_bpy and not _venv_has_bpy():
        if not BLENDER.exists():
            raise SystemExit(f"bpy is not in the venv and no Blender at {BLENDER}; set UNIMATE_BLENDER")
        env["PYTHONPATH"] = os.pathsep.join([str(UNIMATE_ROOT), str(VENV_SITE)])
        script, rest = argv[0], argv[1:]
        cmd = [str(BLENDER), "-b", "--python-use-system-env", "-P", _p(script), "--"] + rest
    else:
        env["PYTHONPATH"] = str(UNIMATE_ROOT)
        cmd = [str(VENV_PY)] + argv
    print("$", " ".join(cmd), flush=True)
    if log is None:
        return subprocess.run(cmd, cwd=UNIMATE_ROOT, env=env).returncode
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as fh:
        proc = subprocess.Popen(cmd, cwd=UNIMATE_ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                errors="replace")
        for line in proc.stdout:
            sys.stdout.write(line)
            fh.write(line)
        return proc.wait()


def cmd_preprocess(a) -> int:
    argv = ["data_process/mesh_animation/preprocess_char.py",
            f"--char_path={_p(a.char)}", f"--output_dir={_p(a.features)}",
            f"--formats={a.formats}"]
    if a.face_r:
        argv.append(f"--face_r={a.face_r}")
    if a.face_l:
        argv.append(f"--face_l={a.face_l}")
    if a.body_axis:
        argv.append("--body_axis")
    if a.keep_intermediate:
        argv.append("--keep_intermediate")
    return run(argv, a.log and Path(_p(a.log)), needs_bpy=True)


def cmd_sample(a) -> int:
    argv = ["-m", "unimate.inference.sample", f"--exp_dir={_p(a.exp_dir)}",
            f"--model_path={_p(a.model_path)}", f"--test_cases_json={_p(a.cases)}",
            f"--num_repetitions={a.reps}", f"--batch_size={a.batch_size}"]
    if a.only_motion:
        argv.append("--only_save_motion")
    if a.seed is not None:
        argv.append(f"--seed={a.seed}")
    if a.cfg is not None:
        argv.append(f"--cfg_scale={a.cfg}")
    if a.out:
        argv.append(f"--output_dir={_p(a.out)}")
    if a.expand:
        argv += ["--motion_expand", f"--expand_overlap={a.expand_overlap}"]
    return run(argv, a.log and Path(_p(a.log)))


def cmd_animate(a) -> int:
    rc = 0
    npys = [a.npy] if not Path(_p(a.npy)).is_dir() else \
        sorted(str(p) for p in Path(_p(a.npy)).glob("*.npy"))
    for npy in npys:
        argv = ["data_process/mesh_animation/animate_motion.py",
                f"--dataset_type={a.layout}", f"--anim_path={_p(npy)}",
                f"--char_path={_p(a.char)}", f"--cond_path={_p(a.cond)}",
                f"--output_dir={_p(a.out)}", f"--anim_mode={a.mode}",
                f"--extra_bones_strategy={a.extra_bones}"]
        rc |= run(argv, a.log and Path(_p(a.log)), needs_bpy=True)
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("preprocess", help="source asset -> cond.npy + motions/ + canonical asset")
    p.add_argument("--char", required=True, help="rigged, ANIMATED GLB/FBX")
    p.add_argument("--features", required=True, help="output dir (becomes the dataset 'path')")
    p.add_argument("--face-r", help="raw right-hip joint name (facing canonicalization)")
    p.add_argument("--face-l", help="raw left-hip joint name")
    p.add_argument("--body-axis", action="store_true", help="face pair is a head/tail axis")
    p.add_argument("--formats", default="glb")
    p.add_argument("--keep-intermediate", action="store_true")
    p.add_argument("--log")
    p.set_defaults(fn=cmd_preprocess)

    p = sub.add_parser("sample", help="text prompts -> .npy motion features")
    p.add_argument("--exp-dir", required=True, help="dir with config.json + dataset_stats.npy")
    p.add_argument("--model-path", default=DEFAULT_CKPT)
    p.add_argument("--cases", required=True, help='JSON {"<object_type>-<tag>": "prompt"}')
    p.add_argument("--reps", type=int, default=3)
    p.add_argument("--batch-size", type=int, default=16)
    p.add_argument("--seed", type=int)
    p.add_argument("--cfg", type=float, help="classifier-free guidance scale (config default 3.0)")
    p.add_argument("--out", help="output dir (default <exp-dir>/samples)")
    p.add_argument("--only-motion", action="store_true", default=True,
                   help="skip mp4/png renders (default on)")
    p.add_argument("--renders", dest="only_motion", action="store_false",
                   help="also write mp4/png renders")
    p.add_argument("--expand", action="store_true", help="motion expansion (prompt lists)")
    p.add_argument("--expand-overlap", type=int, default=10)
    p.add_argument("--log")
    p.set_defaults(fn=cmd_sample)

    p = sub.add_parser("animate", help=".npy -> animated GLB+FBX on the original asset")
    p.add_argument("--npy", required=True, help="one .npy or a directory of them")
    p.add_argument("--char", required=True, help="ORIGINAL source asset (keeps bone names)")
    p.add_argument("--cond", required=True, help="cond.npy from preprocess")
    p.add_argument("--out", required=True)
    p.add_argument("--layout", default="objaverse", choices=["objaverse", "mixamo", "truebones"],
                   help="dataset layout the cond/npy were produced under")
    p.add_argument("--mode", default="fk", choices=["fk", "ik"])
    p.add_argument("--extra-bones", default="merge", choices=["merge", "remove", "keep"])
    p.add_argument("--log")
    p.set_defaults(fn=cmd_animate)

    a = ap.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    raise SystemExit(main())
