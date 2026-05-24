"""
Engine lifecycle controller for the interaction pipeline.

Single source of truth for starting/stopping/probing the Phyxel engine from
Python. Used by both the CLI orchestrator and (mirrored via MCP tools) by the
chat skill. Handles:

- Idempotent launch — detects engine already running in correct mode and reuses
- Mode/asset mismatch — stops and relaunches with correct args
- Crash detection — process death, API timeout, fatal log entry
- Crash dump capture — logs + last screenshot + engine state to crashes/<ts>/
- Graceful shutdown — only stops if we started it; SIGINT-safe

Design rule: NO other module in the pipeline talks to the engine without
going through an EngineSession.
"""

from __future__ import annotations

import datetime as _dt
import enum
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Callable, Iterable, Optional

import httpx

try:  # Optional but recommended on Windows for process-death checks
    import psutil  # type: ignore
    _HAS_PSUTIL = True
except ImportError:  # pragma: no cover
    psutil = None  # type: ignore
    _HAS_PSUTIL = False


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LOG_PATH = PROJECT_ROOT / "phyxel.log"
DEFAULT_PORT = 8090
DEFAULT_LAUNCH_TIMEOUT_S = 45.0
DEFAULT_POLL_INTERVAL_S = 2.0
DEFAULT_API_TIMEOUT_S = 3.0
DEFAULT_HEARTBEAT_INTERVAL_S = 2.0
DEFAULT_STOP_GRACE_S = 10.0

# Regex patterns that indicate the engine has crashed or is about to.
# Any one match in the log tail trips the CRASHED state.
FATAL_LOG_PATTERNS = [
    re.compile(r"\bFATAL\b"),
    re.compile(r"\bSEGFAULT\b"),
    re.compile(r"Vulkan validation error", re.IGNORECASE),
    re.compile(r"terminate called", re.IGNORECASE),
    re.compile(r"Assertion failed", re.IGNORECASE),
    re.compile(r"unhandled exception", re.IGNORECASE),
    re.compile(r"access violation", re.IGNORECASE),
]


# ---------------------------------------------------------------------------
# Mode / state types
# ---------------------------------------------------------------------------

class Mode(str, enum.Enum):
    INTERACTION_EDITOR = "interaction_editor"
    ASSET_EDITOR = "asset_editor"
    ANIM_EDITOR = "anim_editor"
    PROJECT = "project"
    NO_PROJECT = "no_project"  # raw launch, no flags
    UNKNOWN = "unknown"

    def cli_flag(self) -> Optional[str]:
        return {
            Mode.INTERACTION_EDITOR: "--interaction-editor",
            Mode.ASSET_EDITOR: "--asset-editor",
            Mode.ANIM_EDITOR: "--anim-editor",
            Mode.PROJECT: "--project",
        }.get(self)


class LifecycleState(str, enum.Enum):
    UNKNOWN = "unknown"
    LAUNCHING = "launching"
    READY = "ready"
    REUSING = "reusing"
    CRASHED = "crashed"
    STOPPED = "stopped"


class CrashReason(str, enum.Enum):
    PROCESS_DEATH = "process_death"
    API_TIMEOUT = "api_timeout"
    FATAL_LOG = "fatal_log"
    EXTERNAL = "external"  # exception thrown by caller inside the `with` block


@dataclass
class HealthReport:
    timestamp: str
    pid_alive: bool
    api_responsive: bool
    api_latency_ms: Optional[float]
    fatal_log_match: Optional[str]
    state: LifecycleState
    mode: Optional[str]
    loaded_asset: Optional[str]
    uptime_s: Optional[float]


@dataclass
class CrashDump:
    timestamp: str
    reason: CrashReason
    log_path: str
    log_tail: list[str]
    health_at_crash: HealthReport
    dump_dir: str


# ---------------------------------------------------------------------------
# EngineSession
# ---------------------------------------------------------------------------

@dataclass
class _LaunchPlan:
    mode: Mode
    target: Optional[str]  # asset path or project dir
    extra_args: list[str] = field(default_factory=list)
    port: int = DEFAULT_PORT
    config: str = "Debug"

    def to_argv(self) -> list[str]:
        argv: list[str] = []
        flag = self.mode.cli_flag()
        if flag and self.target:
            argv.extend([flag, self.target])
        if self.port != DEFAULT_PORT:
            argv.extend(["--port", str(self.port)])
        argv.extend(self.extra_args)
        return argv


class EngineSession:
    """Repeatable engine lifecycle controller.

    Usage:
        with EngineSession(Mode.INTERACTION_EDITOR, asset_path) as eng:
            # engine is guaranteed ready
            ...
        # engine is stopped on exit IFF we started it
    """

    def __init__(
        self,
        mode: Mode,
        target: Optional[str] = None,
        *,
        port: int = DEFAULT_PORT,
        config: str = "Debug",
        extra_args: Optional[Iterable[str]] = None,
        launch_timeout_s: float = DEFAULT_LAUNCH_TIMEOUT_S,
        poll_interval_s: float = DEFAULT_POLL_INTERVAL_S,
        heartbeat_interval_s: float = DEFAULT_HEARTBEAT_INTERVAL_S,
        log_path: Optional[Path] = None,
        report_dir: Optional[Path] = None,
        on_crash: str = "abort",  # "abort" | "restart-once" | "restart-and-skip"
        verbose: bool = True,
    ) -> None:
        self.plan = _LaunchPlan(
            mode=mode,
            target=target,
            extra_args=list(extra_args or []),
            port=port,
            config=config,
        )
        self.launch_timeout_s = launch_timeout_s
        self.poll_interval_s = poll_interval_s
        self.heartbeat_interval_s = heartbeat_interval_s
        self.log_path = log_path or DEFAULT_LOG_PATH
        self.report_dir = report_dir or (PROJECT_ROOT / "tools" / "interaction_pipeline" / "reports")
        self.on_crash = on_crash
        self.verbose = verbose

        self._process: Optional[subprocess.Popen] = None
        self._pid: Optional[int] = None
        self._we_started_it: bool = False
        self._log_anchor_size: int = 0  # byte offset in phyxel.log at session start
        self._launch_time: Optional[float] = None
        self._state: LifecycleState = LifecycleState.UNKNOWN
        self._sigint_installed = False
        self._prev_sigint_handler = None
        self._crashes: list[CrashDump] = []

    # -------- properties --------

    @property
    def state(self) -> LifecycleState:
        return self._state

    @property
    def pid(self) -> Optional[int]:
        return self._pid

    @property
    def base_url(self) -> str:
        return f"http://localhost:{self.plan.port}"

    @property
    def we_started_it(self) -> bool:
        return self._we_started_it

    @property
    def crashes(self) -> list[CrashDump]:
        return list(self._crashes)

    # -------- context manager --------

    def __enter__(self) -> "EngineSession":
        self._install_sigint_handler()
        try:
            self.ensure_ready()
        except Exception:
            self._restore_sigint_handler()
            raise
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        try:
            if exc is not None:
                self._capture_crash(CrashReason.EXTERNAL, note=f"Caller exception: {exc!r}")
            if self._we_started_it:
                self.stop()
        finally:
            self._restore_sigint_handler()

    # -------- public lifecycle API --------

    def ensure_ready(self) -> None:
        """Idempotent: get to a READY state in the correct mode.

        State routing on entry:
          - running + correct mode + correct target  → REUSE
          - running + wrong mode/target              → STOP + LAUNCH fresh
          - running + API unresponsive               → HARD-KILL + LAUNCH fresh
          - not running                              → LAUNCH fresh
        """
        self._log(f"[lifecycle] target mode={self.plan.mode.value} target={self.plan.target!r}")
        snapshot = self._probe()

        if snapshot.api_responsive:
            if self._matches_target(snapshot):
                self._we_started_it = False
                self._pid = self._discover_external_pid()
                self._state = LifecycleState.REUSING
                self._log(f"[lifecycle] reusing engine pid={self._pid}")
                self._log_anchor_size = self._log_size()
                self._launch_time = time.monotonic()
                return
            else:
                self._log(
                    f"[lifecycle] engine is running but mode/target mismatch "
                    f"(mode={snapshot.mode}, asset={snapshot.loaded_asset}); stopping..."
                )
                self.stop(timeout_s=DEFAULT_STOP_GRACE_S, force_stop_external=True)

        elif snapshot.pid_alive and not snapshot.api_responsive:
            self._log("[lifecycle] engine process exists but API unresponsive; hard-killing...")
            self.hard_kill()

        self.launch()

    def launch(self, *, force_restart: bool = False) -> None:
        """Build (if needed)? No — caller does that. Launch the engine fresh."""
        if force_restart:
            self.stop(force_stop_external=True)

        exe = self._find_executable()
        if exe is None:
            raise RuntimeError(
                f"phyxel.exe not found under build/. Run `build_project` (config={self.plan.config}) first."
            )

        argv = [str(exe)] + self.plan.to_argv()
        self._log(f"[lifecycle] launching: {' '.join(argv)}")

        # Truncate log so the session has a clean anchor
        self._clear_log()
        self._launch_time = time.monotonic()

        self._process = subprocess.Popen(
            argv,
            cwd=str(PROJECT_ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self._pid = self._process.pid
        self._we_started_it = True
        self._state = LifecycleState.LAUNCHING
        self._log_anchor_size = 0

        # Poll until API is up
        deadline = time.monotonic() + self.launch_timeout_s
        last_err: Optional[str] = None
        while time.monotonic() < deadline:
            if self._process.poll() is not None:
                raise RuntimeError(
                    f"Engine process exited immediately with code {self._process.returncode}. "
                    f"Check {self.log_path}"
                )
            health = self._snapshot()
            if health.api_responsive:
                self._state = LifecycleState.READY
                self._log(f"[lifecycle] engine ready pid={self._pid} in {time.monotonic() - self._launch_time:.1f}s")
                # Brief settle period for scene init
                time.sleep(2.0)
                return
            last_err = "API not yet responsive"
            time.sleep(self.poll_interval_s)

        # Timed out
        self.capture_crash_dump(CrashReason.API_TIMEOUT, note=f"Launch timeout: {last_err}")
        self.hard_kill()
        raise RuntimeError(
            f"Engine launch timed out after {self.launch_timeout_s}s. See crash dump in {self.report_dir}"
        )

    def stop(
        self,
        *,
        timeout_s: float = DEFAULT_STOP_GRACE_S,
        force_stop_external: bool = False,
    ) -> None:
        """Stop the engine if we started it (or if force_stop_external).

        Sends terminate, waits grace_s, then hard-kills if needed.
        """
        if not self._we_started_it and not force_stop_external:
            return
        if self._state == LifecycleState.STOPPED:
            return

        proc = self._process
        pid = self._pid

        if proc is None and pid is not None and force_stop_external:
            # Engine was external; attempt orderly shutdown via API then SIGTERM
            self._log(f"[lifecycle] stopping external engine pid={pid}")
            self._try_api_shutdown()
            self._terminate_pid(pid, timeout_s=timeout_s)
        elif proc is not None:
            self._log(f"[lifecycle] stopping engine pid={proc.pid}")
            try:
                proc.terminate()
            except Exception:
                pass
            try:
                proc.wait(timeout=timeout_s)
            except subprocess.TimeoutExpired:
                self._log(f"[lifecycle] terminate timed out after {timeout_s}s; killing")
                try:
                    proc.kill()
                    proc.wait(timeout=3.0)
                except Exception:
                    pass

        self._process = None
        self._pid = None
        self._we_started_it = False
        self._state = LifecycleState.STOPPED

    def restart(self) -> None:
        self._log("[lifecycle] restarting engine")
        self.stop(force_stop_external=True)
        # Brief pause so the port is released
        time.sleep(1.0)
        self.launch()

    def hard_kill(self) -> None:
        """Last-resort termination of any process bound to our port."""
        proc = self._process
        pid = self._pid
        if proc is not None:
            try:
                proc.kill()
                proc.wait(timeout=3.0)
            except Exception:
                pass
        elif pid is not None and _HAS_PSUTIL:
            try:
                p = psutil.Process(pid)  # type: ignore
                p.kill()
                p.wait(timeout=3.0)
            except Exception:
                pass

        self._process = None
        self._pid = None
        self._we_started_it = False
        self._state = LifecycleState.STOPPED

    # -------- health / crash detection --------

    def heartbeat(self) -> HealthReport:
        """Read current health and check all crash signals.

        Does NOT throw on crash — caller decides what to do with the report.
        """
        report = self._snapshot()
        if not report.pid_alive:
            self._on_crash_detected(CrashReason.PROCESS_DEATH, report)
        elif not report.api_responsive and self._state == LifecycleState.READY:
            self._on_crash_detected(CrashReason.API_TIMEOUT, report)
        elif report.fatal_log_match:
            self._on_crash_detected(CrashReason.FATAL_LOG, report)
        return report

    def is_running(self) -> bool:
        return self._snapshot().pid_alive

    def is_api_responsive(self) -> bool:
        return self._snapshot().api_responsive

    def current_mode(self) -> Optional[Mode]:
        snap = self._snapshot()
        if snap.mode is None:
            return None
        try:
            return Mode(snap.mode)
        except ValueError:
            return Mode.UNKNOWN

    # -------- logs / dumps --------

    def capture_logs_since_anchor(self) -> list[str]:
        """Return log lines written since the session started."""
        if not self.log_path.exists():
            return []
        try:
            with self.log_path.open("rb") as f:
                f.seek(self._log_anchor_size)
                data = f.read()
            return data.decode("utf-8", errors="replace").splitlines()
        except Exception:
            return []

    def capture_crash_dump(self, reason: CrashReason, *, note: str = "") -> CrashDump:
        """Capture log tail + state to a timestamped dir under report_dir/crashes/."""
        ts = _dt.datetime.now().strftime("%Y%m%dT%H%M%S")
        dump_dir = self.report_dir / "crashes" / ts
        dump_dir.mkdir(parents=True, exist_ok=True)

        log_tail = self._tail_log(lines=500)
        try:
            (dump_dir / "phyxel.log").write_text("\n".join(log_tail), encoding="utf-8")
        except Exception:
            pass

        health = self._snapshot()
        dump = CrashDump(
            timestamp=ts,
            reason=reason,
            log_path=str(self.log_path),
            log_tail=log_tail[-100:],
            health_at_crash=health,
            dump_dir=str(dump_dir),
        )
        try:
            (dump_dir / "crash.json").write_text(
                json.dumps(
                    {
                        "timestamp": ts,
                        "reason": reason.value,
                        "note": note,
                        "health": asdict(health),
                        "log_tail": log_tail[-100:],
                    },
                    indent=2,
                ),
                encoding="utf-8",
            )
        except Exception:
            pass
        self._crashes.append(dump)
        self._log(f"[lifecycle] crash dump written to {dump_dir} (reason={reason.value})")
        return dump

    # -------- internal helpers --------

    def _on_crash_detected(self, reason: CrashReason, report: HealthReport) -> None:
        if self._state == LifecycleState.CRASHED:
            return  # already crashed; don't redump
        self._state = LifecycleState.CRASHED
        self.capture_crash_dump(reason, note=f"State at detection: {report.state.value}")

    def _snapshot(self) -> HealthReport:
        api_responsive = False
        latency_ms: Optional[float] = None
        mode: Optional[str] = None
        loaded_asset: Optional[str] = None
        uptime_s: Optional[float] = (
            (time.monotonic() - self._launch_time) if self._launch_time else None
        )
        try:
            t0 = time.monotonic()
            with httpx.Client(timeout=DEFAULT_API_TIMEOUT_S) as client:
                r = client.get(f"{self.base_url}/api/engine/status")
                if r.status_code == 404:
                    # Fall back to legacy /api/status until Phase A adds the new endpoint
                    r = client.get(f"{self.base_url}/api/status")
                if r.status_code == 200:
                    api_responsive = True
                    latency_ms = (time.monotonic() - t0) * 1000.0
                    try:
                        body = r.json()
                        mode = body.get("mode")
                        loaded_asset = body.get("loaded_asset") or body.get("loaded_project")
                    except Exception:
                        pass
        except Exception:
            pass

        pid_alive = self._pid_alive()

        fatal: Optional[str] = None
        for line in self._tail_log(lines=50):
            for pat in FATAL_LOG_PATTERNS:
                if pat.search(line):
                    fatal = line.strip()
                    break
            if fatal:
                break

        return HealthReport(
            timestamp=_dt.datetime.now().isoformat(timespec="seconds"),
            pid_alive=pid_alive,
            api_responsive=api_responsive,
            api_latency_ms=latency_ms,
            fatal_log_match=fatal,
            state=self._state,
            mode=mode,
            loaded_asset=loaded_asset,
            uptime_s=uptime_s,
        )

    def _probe(self) -> HealthReport:
        """Initial probe before we own a PID — talk only to the API."""
        snap = self._snapshot()
        # If we don't own a PID, override pid_alive with API responsiveness as a proxy
        if self._pid is None:
            snap = HealthReport(**{**asdict(snap), "pid_alive": snap.api_responsive})
        return snap

    def _matches_target(self, snap: HealthReport) -> bool:
        """Does the running engine match the requested mode + target?"""
        if snap.mode is None:
            # /api/engine/status not yet extended — can't tell, treat as mismatch
            # for safety in IE / asset / anim modes. Treat as match only for
            # NO_PROJECT (no target to compare).
            return self.plan.mode == Mode.NO_PROJECT
        if snap.mode != self.plan.mode.value:
            return False
        if self.plan.target is None:
            return True
        if snap.loaded_asset is None:
            return False
        return _paths_equal(snap.loaded_asset, self.plan.target)

    def _pid_alive(self) -> bool:
        if self._process is not None:
            return self._process.poll() is None
        if self._pid is None:
            return False
        if _HAS_PSUTIL:
            try:
                return psutil.pid_exists(self._pid) and psutil.Process(self._pid).is_running()  # type: ignore
            except Exception:
                return False
        # Fallback: signal 0
        try:
            os.kill(self._pid, 0)
            return True
        except Exception:
            return False

    def _discover_external_pid(self) -> Optional[int]:
        """If we're reusing an engine we didn't launch, find its PID."""
        if not _HAS_PSUTIL:
            return None
        try:
            for proc in psutil.process_iter(["name", "pid", "cwd"]):  # type: ignore
                if proc.info.get("name", "").lower() == "phyxel.exe":
                    return int(proc.info["pid"])
        except Exception:
            pass
        return None

    def _find_executable(self) -> Optional[Path]:
        candidates = [
            PROJECT_ROOT / "build" / "editor" / self.plan.config / "phyxel.exe",
            PROJECT_ROOT / "build" / "game" / self.plan.config / "phyxel.exe",
            PROJECT_ROOT / "phyxel.exe",
        ]
        for c in candidates:
            if c.exists():
                return c
        return None

    def _try_api_shutdown(self) -> None:
        try:
            with httpx.Client(timeout=2.0) as client:
                client.post(f"{self.base_url}/api/engine/shutdown")
        except Exception:
            pass

    def _terminate_pid(self, pid: int, *, timeout_s: float) -> None:
        if _HAS_PSUTIL:
            try:
                p = psutil.Process(pid)  # type: ignore
                p.terminate()
                p.wait(timeout=timeout_s)
                return
            except Exception:
                pass
            try:
                psutil.Process(pid).kill()  # type: ignore
            except Exception:
                pass
        else:
            try:
                os.kill(pid, signal.SIGTERM)
                time.sleep(timeout_s)
                os.kill(pid, signal.SIGKILL if hasattr(signal, "SIGKILL") else signal.SIGTERM)
            except Exception:
                pass

    def _log_size(self) -> int:
        try:
            return self.log_path.stat().st_size if self.log_path.exists() else 0
        except Exception:
            return 0

    def _clear_log(self) -> None:
        try:
            if self.log_path.exists():
                self.log_path.write_text("", encoding="utf-8")
        except Exception:
            pass

    def _tail_log(self, *, lines: int) -> list[str]:
        if not self.log_path.exists():
            return []
        try:
            with self.log_path.open("rb") as f:
                f.seek(self._log_anchor_size)
                data = f.read()
            text_lines = data.decode("utf-8", errors="replace").splitlines()
            return text_lines[-lines:]
        except Exception:
            return []

    def _install_sigint_handler(self) -> None:
        if self._sigint_installed:
            return
        try:
            self._prev_sigint_handler = signal.getsignal(signal.SIGINT)
            signal.signal(signal.SIGINT, self._handle_sigint)
            self._sigint_installed = True
        except Exception:
            # signal.signal only works on the main thread; ignore otherwise
            pass

    def _restore_sigint_handler(self) -> None:
        if not self._sigint_installed:
            return
        try:
            signal.signal(signal.SIGINT, self._prev_sigint_handler or signal.SIG_DFL)
        except Exception:
            pass
        self._sigint_installed = False

    def _handle_sigint(self, signum, frame) -> None:  # noqa: ARG002
        self._log("[lifecycle] SIGINT received; stopping engine before exit")
        try:
            self.stop()
        finally:
            # Re-raise as KeyboardInterrupt so caller's `with` block unwinds cleanly
            raise KeyboardInterrupt()

    def _log(self, msg: str) -> None:
        if self.verbose:
            print(msg, flush=True)


# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

def _paths_equal(a: str, b: str) -> bool:
    try:
        return Path(a).resolve() == Path(b).resolve()
    except Exception:
        return os.path.normcase(os.path.normpath(a)) == os.path.normcase(os.path.normpath(b))


def watch_for_crash(
    session: EngineSession,
    during: Callable[[], Any],
    *,
    poll_interval_s: float = DEFAULT_HEARTBEAT_INTERVAL_S,
) -> tuple[Any, list[HealthReport]]:
    """Run `during()` while polling session heartbeats in the background.

    Note: this is the *cooperative* version — the heartbeat is checked
    between sub-operations. For true concurrency, callers should run their
    own thread and poll `session.heartbeat()` themselves.
    """
    health_log: list[HealthReport] = []
    h = session.heartbeat()
    health_log.append(h)
    result = during()
    h2 = session.heartbeat()
    health_log.append(h2)
    return result, health_log


__all__ = [
    "EngineSession",
    "Mode",
    "LifecycleState",
    "CrashReason",
    "HealthReport",
    "CrashDump",
    "DEFAULT_PORT",
    "DEFAULT_LAUNCH_TIMEOUT_S",
    "PROJECT_ROOT",
    "watch_for_crash",
]
