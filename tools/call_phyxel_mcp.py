#!/usr/bin/env python3
"""Call one local Phyxel MCP tool from scripts or CI."""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def call_tool(name: str, arguments: dict) -> int:
    root = Path(__file__).resolve().parent.parent
    server = StdioServerParameters(
        command=sys.executable,
        args=[str(root / "scripts" / "mcp" / "phyxel_mcp_server.py")],
        cwd=str(root),
    )
    async with stdio_client(server) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            result = await session.call_tool(name, arguments)
            payload = result.structuredContent
            if payload is None:
                payload = [block.model_dump(mode="json") for block in result.content]
            print(json.dumps(payload, indent=2))
            return 1 if result.isError else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tool", help="MCP tool name")
    parser.add_argument("arguments", nargs="?", default="{}", help="JSON object")
    args = parser.parse_args()
    try:
        arguments = json.loads(args.arguments)
    except json.JSONDecodeError as exc:
        parser.error(f"invalid arguments JSON: {exc}")
    if not isinstance(arguments, dict):
        parser.error("arguments must decode to a JSON object")
    return asyncio.run(call_tool(args.tool, arguments))


if __name__ == "__main__":
    raise SystemExit(main())
