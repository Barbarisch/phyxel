"""Lightweight Claude API call for dialogue enhancement.

Usage: python scripts/ai_enhance.py <api_key> <model>
Reads JSON from stdin: {"system": "...", "user": "..."}
Outputs: Just the response text on stdout. Exits 0 on success, 1 on error.
"""
import sys
import json
import httpx

def main():
    if len(sys.argv) < 3:
        print("Usage: ai_enhance.py <api_key> <model>", file=sys.stderr)
        sys.exit(1)

    api_key = sys.argv[1]
    model = sys.argv[2]

    try:
        input_data = json.loads(sys.stdin.read())
        system_prompt = input_data["system"]
        user_prompt = input_data["user"]
    except Exception as e:
        print(f"Error reading stdin JSON: {e}", file=sys.stderr)
        sys.exit(1)

    try:
        response = httpx.post(
            "https://api.anthropic.com/v1/messages",
            headers={
                "x-api-key": api_key,
                "anthropic-version": "2023-06-01",
                "content-type": "application/json",
            },
            json={
                "model": model,
                "max_tokens": 300,
                "system": system_prompt,
                "messages": [{"role": "user", "content": user_prompt}],
            },
            timeout=15.0,
        )
        response.raise_for_status()
        data = response.json()
        text = data["content"][0]["text"]
        print(text, end="")
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
