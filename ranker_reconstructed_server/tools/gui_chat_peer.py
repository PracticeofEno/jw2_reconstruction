"""Manual peer used to verify chat against a running reconstructed client UI."""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ranker_server.protocol import (
    HEADER_BYTES,
    build_colored_text_packet,
    build_packet,
    read_u32,
)


def login_packet(account: str, password: str) -> bytes:
    payload = bytearray(0x11D - HEADER_BYTES)
    payload[0 : len(account)] = account.encode("cp949")
    payload[0x80 : 0x80 + len(password)] = password.encode("cp949")
    return build_packet(1, payload)


async def read_framed_packet(reader: asyncio.StreamReader) -> bytes:
    header = await reader.readexactly(12)
    packet_bytes = read_u32(header, 8)
    return header + await reader.readexactly(packet_bytes - 12)


async def read_next_message(reader: asyncio.StreamReader) -> tuple[str, bytes]:
    first = await reader.readexactly(4)
    if read_u32(first, 0) != 0:
        rest = await reader.readexactly(8)
        packet_bytes = read_u32(first + rest, 8)
        return "packet", first + rest + await reader.readexactly(packet_bytes - 12)

    first_meta = await reader.readexactly(4)
    first_length = first_meta[3]
    first_text = await reader.readexactly(first_length)
    second_meta = await reader.readexactly(4)
    second_length = second_meta[3]
    second_text = await reader.readexactly(second_length)
    return "chat", first + first_meta + first_text + second_meta + second_text


def decode_chat(raw: bytes) -> str:
    first_length = raw[7]
    first_start = 8
    second_meta = first_start + first_length
    second_length = raw[second_meta + 3]
    first = raw[first_start : first_start + first_length].rstrip(b"\0")
    second_start = second_meta + 4
    second = raw[second_start : second_start + second_length].rstrip(b"\0")
    return (first + second).decode("cp949", errors="replace")


async def run(args: argparse.Namespace) -> dict[str, object]:
    reader, writer = await asyncio.open_connection(args.host, args.port)
    try:
        writer.write(login_packet(args.account, args.password))
        await writer.drain()
        response = await read_framed_packet(reader)
        login_status = read_u32(response, HEADER_BYTES)
        if read_u32(response, 4) != 2 or login_status != 0:
            raise RuntimeError(f"login failed with status {login_status}")

        raw_chat = build_colored_text_packet(
            f"{args.account}> ", args.send_text
        )
        writer.write(build_packet(0x2A, raw_chat[4:]))
        await writer.drain()

        while True:
            kind, raw = await asyncio.wait_for(
                read_next_message(reader), timeout=args.timeout
            )
            if kind == "chat":
                return {
                    "login_status": login_status,
                    "sent": args.send_text,
                    "received": decode_chat(raw),
                }
    finally:
        writer.close()
        await writer.wait_closed()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19777)
    parser.add_argument("--account", default="ObserverGui")
    parser.add_argument("--password", default="Test1234")
    parser.add_argument("--send-text", default="peer-to-gui")
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    result = asyncio.run(run(args))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
