#!/usr/bin/env python3
"""Quick test of the goose bridge — tests session creation and chat."""

import json
import sys
import time
import urllib.request
import urllib.error

GOOSE_PORT = 3000
BRIDGE_PORT = 3001


def test_goose_direct():
    """Test goose web server directly (must already be running)."""
    print("=== Testing Goose Web Server Directly ===")

    # 1. Health check
    print("[1] Health check...")
    resp = urllib.request.urlopen(f"http://127.0.0.1:{GOOSE_PORT}/api/health", timeout=5)
    data = json.loads(resp.read())
    print(f"    Health: {data}")
    assert data["status"] == "ok", f"Expected ok, got {data['status']}"

    # 2. Create session via redirect
    print("[2] Creating session...")

    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            raise Exception(newurl)

    opener = urllib.request.build_opener(NoRedirect())
    session_id = None
    try:
        opener.open(f"http://127.0.0.1:{GOOSE_PORT}/")
    except Exception as e:
        location = str(e)
        if "/session/" in location:
            session_id = location.split("/session/")[1].split("?")[0]
    print(f"    Session ID: {session_id}")
    assert session_id, "Failed to create session"

    # 3. Extract ws_token from session page
    print("[3] Extracting ws_token...")
    resp = urllib.request.urlopen(f"http://127.0.0.1:{GOOSE_PORT}/session/{session_id}", timeout=5)
    html = resp.read().decode("utf-8", errors="replace")
    ws_token = ""
    marker = "window.GOOSE_WS_TOKEN = '"
    idx = html.find(marker)
    if idx >= 0:
        start = idx + len(marker)
        end = html.find("'", start)
        ws_token = html[start:end]
    print(f"    ws_token: {ws_token[:20]}..." if ws_token else "    ws_token: NOT FOUND")
    assert ws_token, "Failed to extract ws_token"

    # 4. Chat via WebSocket
    print("[4] Chatting via WebSocket...")
    import websockets.sync.client
    ws_url = f"ws://127.0.0.1:{GOOSE_PORT}/ws?token={ws_token}"
    with websockets.sync.client.connect(ws_url, close_timeout=5) as ws:
        msg = json.dumps({
            "type": "message",
            "content": "Say exactly: GOOSE_TEST_OK",
            "session_id": session_id,
            "timestamp": int(time.time() * 1000),
        })
        ws.send(msg)
        print("    Message sent, waiting for response...")

        response_parts = []
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            try:
                raw = ws.recv(timeout=30)
                data = json.loads(raw)
                msg_type = data.get("type", "")
                if msg_type == "response":
                    content = data.get("content", "")
                    if content:
                        response_parts.append(content)
                        print(f"    [response chunk]: {content[:100]}")
                elif msg_type == "complete":
                    print("    [complete]")
                    break
                elif msg_type == "error":
                    print(f"    [error]: {data.get('message', '')}")
                    break
                elif msg_type == "thinking":
                    print("    [thinking...]")
                else:
                    print(f"    [{msg_type}]")
            except Exception as e:
                print(f"    WebSocket recv error: {e}")
                break

        full_response = "".join(response_parts)
        print(f"    Full response: {full_response[:200]}")
        assert full_response, "Got empty response"

    print("\n=== Direct Goose Test PASSED ===\n")
    return session_id


def test_bridge():
    """Test the Python bridge (must already be running on BRIDGE_PORT)."""
    print("=== Testing Bridge Server ===")

    # 1. Status check
    print("[1] Bridge status...")
    try:
        resp = urllib.request.urlopen(f"http://127.0.0.1:{BRIDGE_PORT}/status", timeout=5)
        data = json.loads(resp.read())
        print(f"    Status: {data}")
    except Exception as e:
        print(f"    Bridge not running ({e}), skipping bridge tests")
        return False

    # 2. Create session
    print("[2] Creating session via bridge...")
    req = urllib.request.Request(
        f"http://127.0.0.1:{BRIDGE_PORT}/session/create",
        data=b"{}",
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    resp = urllib.request.urlopen(req, timeout=10)
    data = json.loads(resp.read())
    session_id = data.get("session_id", "")
    print(f"    Session: {session_id}")
    assert session_id, "Bridge failed to create session"

    # 3. Chat
    print("[3] Chatting via bridge...")
    chat_body = json.dumps({
        "session_id": session_id,
        "message": "Say exactly: BRIDGE_TEST_OK"
    }).encode("utf-8")
    req = urllib.request.Request(
        f"http://127.0.0.1:{BRIDGE_PORT}/chat",
        data=chat_body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    resp = urllib.request.urlopen(req, timeout=120)
    data = json.loads(resp.read())
    print(f"    Response: {data.get('response', '')[:200]}")
    print(f"    Tool calls: {data.get('tool_calls', [])}")
    print(f"    Error: {data.get('error')}")
    assert data.get("response"), "Bridge returned empty response"

    # 4. Destroy session
    print("[4] Destroying session...")
    destroy_body = json.dumps({"session_id": session_id}).encode("utf-8")
    req = urllib.request.Request(
        f"http://127.0.0.1:{BRIDGE_PORT}/session/destroy",
        data=destroy_body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    resp = urllib.request.urlopen(req, timeout=5)
    print(f"    Destroy: {json.loads(resp.read())}")

    print("\n=== Bridge Test PASSED ===\n")
    return True


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "direct"

    if mode == "direct":
        test_goose_direct()
    elif mode == "bridge":
        test_bridge()
    elif mode == "all":
        test_goose_direct()
        test_bridge()
    else:
        print(f"Usage: {sys.argv[0]} [direct|bridge|all]")
