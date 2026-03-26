#!/usr/bin/env python3
"""
Goose Bridge Server — HTTP-to-WebSocket bridge for GooseBridge C++ client.

Starts `goose web` as a subprocess and exposes a simple HTTP REST API that
translates requests into WebSocket messages to the Goose web server.

Endpoints:
    GET  /status              — Health check
    POST /session/create      — Create a new Goose session, return session_id
    POST /chat                — Send message to session, return AI response
    POST /session/destroy     — Destroy a session

Usage:
    python scripts/goose_bridge.py [--goose-port 3000] [--bridge-port 3001] [--goose-path goose.exe]
"""

import argparse
import asyncio
import json
import os
import signal
import subprocess
import sys
import threading
import time
import traceback
import urllib.request
import urllib.error
from http.server import HTTPServer, BaseHTTPRequestHandler

import websockets
import websockets.sync.client


# ---------------------------------------------------------------------------
# Goose Process Manager
# ---------------------------------------------------------------------------

class GooseProcess:
    """Manages the goose web subprocess."""

    def __init__(self, goose_path: str, port: int):
        self.goose_path = goose_path
        self.port = port
        self.process = None

    def start(self) -> bool:
        """Start the goose web server subprocess."""
        env = os.environ.copy()
        # Propagate PHYXEL_AI_API_KEY → ANTHROPIC_API_KEY if needed
        if "ANTHROPIC_API_KEY" not in env and "PHYXEL_AI_API_KEY" in env:
            env["ANTHROPIC_API_KEY"] = env["PHYXEL_AI_API_KEY"]
        cmd = [self.goose_path, "web", "--port", str(self.port), "--no-auth"]
        try:
            self.process = subprocess.Popen(
                cmd,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
            )
            return True
        except Exception as e:
            print(f"[GooseBridge] Failed to start goose: {e}", file=sys.stderr)
            return False

    def wait_ready(self, timeout: float = 30.0) -> bool:
        """Poll goose health endpoint until it responds."""
        url = f"http://127.0.0.1:{self.port}/api/health"
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                req = urllib.request.Request(url,  method="GET")
                with urllib.request.urlopen(req, timeout=2) as resp:
                    if resp.status == 200:
                        return True
            except Exception:
                pass
            time.sleep(0.5)
        return False

    def stop(self):
        """Terminate the goose subprocess."""
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
            self.process = None

    def is_alive(self) -> bool:
        return self.process is not None and self.process.poll() is None


# ---------------------------------------------------------------------------
# Session Manager — tracks WebSocket tokens and session IDs
# ---------------------------------------------------------------------------

class SessionManager:
    """Creates and tracks Goose sessions."""

    def __init__(self, goose_port: int):
        self.goose_port = goose_port
        self.sessions = {}  # session_id -> {"ws_token": str}
        self.lock = threading.Lock()

    def create_session(self) -> str:
        """Create a new session by hitting the goose web root (which redirects to /session/{id})."""
        url = f"http://127.0.0.1:{self.goose_port}/"
        try:
            req = urllib.request.Request(url, method="GET")
            # Don't follow redirects — we want the Location header
            opener = urllib.request.build_opener(NoRedirectHandler())
            resp = opener.open(req, timeout=5)
            # Should be a 3xx redirect
            location = resp.headers.get("Location", "")
            # Location will be like /session/<session_id> or /session/<session_id>?...
            if "/session/" in location:
                session_id = location.split("/session/")[1].split("?")[0]
            else:
                return ""
        except RedirectResponse as e:
            location = e.location
            if "/session/" in location:
                session_id = location.split("/session/")[1].split("?")[0]
            else:
                return ""
        except Exception as e:
            print(f"[GooseBridge] Session creation failed: {e}", file=sys.stderr)
            return ""

        if not session_id:
            return ""

        # Now fetch the session page to get the ws_token
        ws_token = self._extract_ws_token(session_id)

        with self.lock:
            self.sessions[session_id] = {"ws_token": ws_token}

        return session_id

    def _extract_ws_token(self, session_id: str) -> str:
        """Fetch the session HTML page and extract the GOOSE_WS_TOKEN."""
        url = f"http://127.0.0.1:{self.goose_port}/session/{session_id}"
        try:
            req = urllib.request.Request(url, method="GET")
            with urllib.request.urlopen(req, timeout=5) as resp:
                html = resp.read().decode("utf-8", errors="replace")
                # Look for: window.GOOSE_WS_TOKEN = '<token>';
                marker = "window.GOOSE_WS_TOKEN = '"
                idx = html.find(marker)
                if idx >= 0:
                    start = idx + len(marker)
                    end = html.find("'", start)
                    if end > start:
                        return html[start:end]
        except Exception as e:
            print(f"[GooseBridge] Failed to extract ws_token: {e}", file=sys.stderr)
        return ""

    def get_session(self, session_id: str):
        with self.lock:
            return self.sessions.get(session_id)

    def destroy_session(self, session_id: str):
        with self.lock:
            self.sessions.pop(session_id, None)


class RedirectResponse(Exception):
    def __init__(self, location):
        self.location = location

class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise RedirectResponse(newurl)


# ---------------------------------------------------------------------------
# Chat via WebSocket
# ---------------------------------------------------------------------------

def chat_via_websocket(goose_port: int, session_id: str, ws_token: str,
                       message: str, timeout: float = 120.0) -> dict:
    """
    Connect to the Goose WebSocket, send a message, and collect the full response.
    Returns: {"response": str, "tool_calls": list, "error": str|None}
    """
    ws_url = f"ws://127.0.0.1:{goose_port}/ws?token={ws_token}"

    response_parts = []
    tool_calls = []
    error = None

    try:
        with websockets.sync.client.connect(ws_url, close_timeout=5) as ws:
            # Send the user message
            msg_payload = json.dumps({
                "type": "message",
                "content": message,
                "session_id": session_id,
                "timestamp": int(time.time() * 1000),
            })
            ws.send(msg_payload)

            # Collect responses until we get a "complete" message
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                try:
                    raw = ws.recv(timeout=max(1, deadline - time.monotonic()))
                except TimeoutError:
                    error = "Timeout waiting for response"
                    break

                try:
                    data = json.loads(raw)
                except json.JSONDecodeError:
                    continue

                msg_type = data.get("type", "")

                if msg_type == "response":
                    content = data.get("content", "")
                    if content:
                        response_parts.append(content)

                elif msg_type == "tool_request":
                    tool_calls.append({
                        "id": data.get("id", ""),
                        "tool_name": data.get("tool_name", ""),
                        "arguments": data.get("arguments", {}),
                    })

                elif msg_type == "error":
                    error = data.get("message", "Unknown error")
                    break

                elif msg_type == "complete":
                    break

                elif msg_type == "cancelled":
                    error = "Cancelled"
                    break

                elif msg_type == "context_exceeded":
                    error = "Context exceeded"
                    break

    except Exception as e:
        error = str(e)

    return {
        "response": "\n".join(response_parts) if response_parts else "",
        "tool_calls": tool_calls,
        "error": error,
    }


# ---------------------------------------------------------------------------
# HTTP Request Handler
# ---------------------------------------------------------------------------

class BridgeHandler(BaseHTTPRequestHandler):
    """HTTP handler for the GooseBridge REST API."""

    # Shared state set by main
    goose_process: GooseProcess = None
    session_mgr: SessionManager = None
    goose_port: int = 3000

    def log_message(self, format, *args):
        # Suppress default request logging
        pass

    def _send_json(self, status: int, data: dict):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self) -> dict:
        length = int(self.headers.get("Content-Length", 0))
        if length == 0:
            return {}
        raw = self.rfile.read(length)
        return json.loads(raw)

    def do_GET(self):
        if self.path == "/status":
            alive = self.goose_process.is_alive() if self.goose_process else False
            self._send_json(200, {"status": "ok" if alive else "goose_down", "goose_alive": alive})
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self):
        try:
            if self.path == "/session/create":
                self._handle_create_session()
            elif self.path == "/chat":
                self._handle_chat()
            elif self.path == "/session/destroy":
                self._handle_destroy_session()
            else:
                self._send_json(404, {"error": "not found"})
        except Exception as e:
            traceback.print_exc()
            self._send_json(500, {"error": str(e)})

    def _handle_create_session(self):
        session_id = self.session_mgr.create_session()
        if session_id:
            self._send_json(200, {"session_id": session_id})
        else:
            self._send_json(500, {"error": "Failed to create session"})

    def _handle_chat(self):
        body = self._read_body()
        session_id = body.get("session_id", "")
        message = body.get("message", "")

        if not session_id or not message:
            self._send_json(400, {"error": "session_id and message required"})
            return

        session = self.session_mgr.get_session(session_id)
        if not session:
            self._send_json(404, {"error": f"Session {session_id} not found"})
            return

        ws_token = session.get("ws_token", "")
        result = chat_via_websocket(self.goose_port, session_id, ws_token, message)
        self._send_json(200, result)

    def _handle_destroy_session(self):
        body = self._read_body()
        session_id = body.get("session_id", "")
        if session_id:
            self.session_mgr.destroy_session(session_id)
        self._send_json(200, {"ok": True})


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Goose Bridge HTTP Server")
    parser.add_argument("--goose-port", type=int, default=3000,
                        help="Port for the goose web server (default: 3000)")
    parser.add_argument("--bridge-port", type=int, default=3001,
                        help="Port for this bridge HTTP server (default: 3001)")
    parser.add_argument("--goose-path", type=str, default="goose",
                        help="Path to the goose binary")
    parser.add_argument("--no-start-goose", action="store_true",
                        help="Don't start goose — assume it's already running")
    args = parser.parse_args()

    goose_proc = GooseProcess(args.goose_path, args.goose_port)
    session_mgr = SessionManager(args.goose_port)

    # Start goose web server
    if not args.no_start_goose:
        print(f"[GooseBridge] Starting goose web on port {args.goose_port}...")
        if not goose_proc.start():
            print("[GooseBridge] FATAL: Could not start goose", file=sys.stderr)
            sys.exit(1)

        print("[GooseBridge] Waiting for goose to become ready...")
        if not goose_proc.wait_ready(timeout=30):
            print("[GooseBridge] FATAL: Goose did not become ready", file=sys.stderr)
            goose_proc.stop()
            sys.exit(1)
        print("[GooseBridge] Goose is ready!")
    else:
        print(f"[GooseBridge] Assuming goose already running on port {args.goose_port}")

    # Configure handler class state
    BridgeHandler.goose_process = goose_proc
    BridgeHandler.session_mgr = session_mgr
    BridgeHandler.goose_port = args.goose_port

    # Start HTTP bridge server
    server = HTTPServer(("127.0.0.1", args.bridge_port), BridgeHandler)
    print(f"[GooseBridge] Bridge HTTP server listening on http://127.0.0.1:{args.bridge_port}")
    print(f"[GooseBridge] Ready for GooseBridge C++ client connections")

    def shutdown_handler(sig, frame):
        print("\n[GooseBridge] Shutting down...")
        server.shutdown()
        goose_proc.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown_handler)
    signal.signal(signal.SIGTERM, shutdown_handler)

    try:
        server.serve_forever()
    finally:
        goose_proc.stop()


if __name__ == "__main__":
    main()
