from __future__ import annotations

import argparse
import asyncio
import socket
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from ranker_server.protocol import HEADER_BYTES, build_packet, read_u32

STALE_ASYNC_OPCODES = {
    0x06,
    0x07,
    0x09,
    0x11,
    0x13,
    0x15,
    0x1A,
    0x1C,
    0x1E,
    0x23,
    0x26,
    0x27,
    0x38,
    0x3E,
    0x46,
    0x64,
    0x76,
    0x78,
    0x7A,
    0x7E,
    0x80,
    0x84,
}


def login_packet(account: str, password: str) -> bytes:
    payload = bytearray(0x11D - HEADER_BYTES)
    account_bytes = account.encode("ascii", errors="replace")[:0x7F]
    password_bytes = password.encode("ascii", errors="replace")[:0x7F]
    payload[0 : len(account_bytes)] = account_bytes
    payload[0x80 : 0x80 + len(password_bytes)] = password_bytes
    return build_packet(1, payload)


def host_game_packet(name: str, *, port: int = 23010, map_name: str = "RelaySmokeMap") -> bytes:
    request = bytearray(0x409 - HEADER_BYTES)
    name_bytes = name.encode("ascii", errors="replace")[:0x7F]
    request[0 : len(name_bytes)] = name_bytes
    sockaddr = (
        struct.pack("<H", socket.AF_INET)
        + struct.pack(">H", port)
        + socket.inet_aton("127.0.0.1")
        + b"\0" * 8
    )
    request[0x10D - HEADER_BYTES : 0x11D - HEADER_BYTES] = sockaddr
    map_bytes = map_name.encode("ascii", errors="replace")[:0x20]
    map_offset = 0x12D - HEADER_BYTES + 8
    request[map_offset : map_offset + len(map_bytes)] = map_bytes
    return build_packet(0x19, request)


async def read_packet(reader: asyncio.StreamReader, timeout: float) -> bytes:
    header = await asyncio.wait_for(reader.readexactly(12), timeout=timeout)
    packet_bytes = read_u32(header, 8)
    return header + await asyncio.wait_for(
        reader.readexactly(packet_bytes - 12), timeout=timeout
    )


async def read_until_opcode(
    reader: asyncio.StreamReader,
    opcode: int,
    *,
    timeout: float,
    limit: int = 30,
) -> bytes:
    seen: list[str] = []
    for _ in range(limit):
        packet = await read_packet(reader, timeout)
        seen.append(f"0x{read_u32(packet, 4):02x}")
        if read_u32(packet, 4) == opcode:
            return packet
    raise RuntimeError(f"opcode 0x{opcode:02x} was not received; saw {seen}")


async def read_until_game_opcode(
    reader: asyncio.StreamReader,
    opcode: int,
    game_id: int,
    id_offset: int,
    *,
    timeout: float,
    limit: int = 60,
) -> bytes:
    seen: list[str] = []
    for _ in range(limit):
        packet = await read_packet(reader, timeout)
        packet_opcode = read_u32(packet, 4)
        packet_game_id = read_u32(packet, id_offset) if len(packet) >= id_offset + 4 else 0
        seen.append(f"0x{packet_opcode:02x}/game={packet_game_id}")
        if packet_opcode == opcode and packet_game_id == game_id:
            return packet
    raise RuntimeError(
        f"opcode 0x{opcode:02x} for game {game_id} was not received; saw {seen}"
    )


async def read_relay_packet(
    reader: asyncio.StreamReader,
    timeout: float,
    *,
    limit: int = 30,
) -> bytes:
    seen: list[str] = []
    for _ in range(limit):
        packet = await read_packet(reader, timeout)
        opcode = read_u32(packet, 4)
        seen.append(f"0x{opcode:02x}")
        if opcode in (0x93, 0x94, 0x95):
            return packet
        if opcode in STALE_ASYNC_OPCODES:
            continue
        raise RuntimeError(
            "received an unexpected non-relay packet after entering link mode: "
            f"opcode=0x{opcode:02x}; saw {seen}"
        )
    raise RuntimeError(f"relay packet was not received; saw {seen}")


async def assert_no_relay_packet(
    reader: asyncio.StreamReader,
    timeout: float,
    *,
    context: str,
    limit: int = 30,
) -> None:
    deadline = asyncio.get_running_loop().time() + timeout
    seen: list[str] = []
    for _ in range(limit):
        remaining = deadline - asyncio.get_running_loop().time()
        if remaining <= 0:
            return
        try:
            packet = await read_packet(reader, remaining)
        except asyncio.TimeoutError:
            return
        opcode = read_u32(packet, 4)
        seen.append(f"0x{opcode:02x}")
        if opcode in STALE_ASYNC_OPCODES:
            continue
        raise RuntimeError(
            f"{context}: unexpected packet opcode=0x{opcode:02x}; saw {seen}"
        )
    raise RuntimeError(f"{context}: too many stale packets while checking for leaks")


def assert_relay_frame(
    packet: bytes,
    *,
    game_id: int,
    from_member: int,
    stream_id: int,
    payload: bytes,
    context: str,
) -> None:
    if (
        read_u32(packet, 4) != 0x94
        or read_u32(packet, 0x0D) != game_id
        or read_u32(packet, 0x11) != from_member
        or read_u32(packet, 0x15) != stream_id
        or packet[0x19:] != payload
    ):
        raise RuntimeError(f"{context}: relay frame mismatch")


async def connect_and_login(
    host: str,
    port: int,
    account: str,
    password: str,
    timeout: float,
) -> tuple[asyncio.StreamReader, asyncio.StreamWriter]:
    reader, writer = await asyncio.wait_for(
        asyncio.open_connection(host, port), timeout=timeout
    )
    writer.write(login_packet(account, password))
    await writer.drain()
    response = await read_until_opcode(reader, 2, timeout=timeout)
    status = read_u32(response, 0x0D)
    if status != 0:
        raise RuntimeError(f"login failed for {account}: status={status}")
    return reader, writer


async def run_smoke(args: argparse.Namespace) -> None:
    suffix = f"{time.time_ns() % 10_000_000_000:010d}"
    host_name = f"{args.account_prefix}H{suffix}"[:16]
    join_name = f"{args.account_prefix}J{suffix}"[:16]
    third_name = f"{args.account_prefix}T{suffix}"[:16]
    late_name = f"{args.account_prefix}L{suffix}"[:16]
    noise_name = f"{args.account_prefix}N{suffix}"[:16]
    chat_name = f"{args.account_prefix}C{suffix}"[:16]
    browser_name = f"{args.account_prefix}B{suffix}"[:16]
    room_name = f"{args.room_prefix}{suffix}"[:20]
    host_reader, host_writer = await connect_and_login(
        args.host, args.port, host_name, args.password, args.timeout
    )
    join_reader, join_writer = await connect_and_login(
        args.host, args.port, join_name, args.password, args.timeout
    )
    browser_reader, browser_writer = await connect_and_login(
        args.host, args.port, browser_name, args.password, args.timeout
    )
    noise_reader: asyncio.StreamReader | None = None
    noise_writer: asyncio.StreamWriter | None = None
    chat_writer: asyncio.StreamWriter | None = None
    third_reader: asyncio.StreamReader | None = None
    third_writer: asyncio.StreamWriter | None = None
    late_writer: asyncio.StreamWriter | None = None
    try:
        await read_until_opcode(host_reader, 7, timeout=args.timeout)

        join_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await join_writer.drain()
        await read_until_opcode(join_reader, 0x1E, timeout=args.timeout)

        browser_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await browser_writer.drain()
        await read_until_opcode(browser_reader, 0x1E, timeout=args.timeout)

        host_writer.write(host_game_packet(room_name))
        await host_writer.drain()
        hosted = await read_until_opcode(host_reader, 0x1A, timeout=args.timeout)
        if read_u32(hosted, 0x0D) != 1:
            raise RuntimeError(f"host failed: status={read_u32(hosted, 0x0D)}")
        game_id = read_u32(hosted, 0x11)
        host_member = read_u32(hosted, 0x15)
        if game_id == 0 or host_member != 1:
            raise RuntimeError(
                f"bad host relay ids: game={game_id} member={host_member}"
            )
        await read_until_game_opcode(
            join_reader, 0x27, game_id, 0xB1, timeout=args.timeout
        )
        await read_until_game_opcode(
            browser_reader, 0x27, game_id, 0xB1, timeout=args.timeout
        )

        join_payload = b"remote-smoke-join"
        join_writer.write(build_packet(0x90, struct.pack("<I", game_id) + join_payload))
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93, timeout=args.timeout)
        if read_u32(joined, 0x0D) != 0:
            raise RuntimeError(f"relay join failed: status={read_u32(joined, 0x0D)}")
        join_member = read_u32(joined, 0x15)
        if join_member < 2:
            raise RuntimeError(f"bad join member id: {join_member}")

        forwarded_join = await read_until_opcode(host_reader, 0x94, timeout=args.timeout)
        if (
            read_u32(forwarded_join, 0x0D) != game_id
            or read_u32(forwarded_join, 0x11) != join_member
            or forwarded_join[0x19:] != join_payload
        ):
            raise RuntimeError("host did not receive forwarded join payload")

        link_setup_payload = b"remote-smoke-link-setup"
        host_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, join_member, 0) + link_setup_payload,
            )
        )
        await host_writer.drain()
        assert_relay_frame(
            await read_relay_packet(join_reader, args.timeout),
            game_id=game_id,
            from_member=1,
            stream_id=0,
            payload=link_setup_payload,
            context="joiner did not receive stream-0 link setup",
        )

        link_ack_payload = b"remote-smoke-link-ack"
        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 1, 0) + link_ack_payload,
            )
        )
        await join_writer.drain()
        assert_relay_frame(
            await read_relay_packet(host_reader, args.timeout),
            game_id=game_id,
            from_member=join_member,
            stream_id=0,
            payload=link_ack_payload,
            context="host did not receive stream-0 link ack",
        )

        noise_reader, noise_writer = await connect_and_login(
            args.host, args.port, noise_name, args.password, args.timeout
        )
        noise_writer.write(host_game_packet(f"{args.room_prefix}Noise{suffix}"[:20]))
        await noise_writer.drain()
        noise_hosted = await read_until_opcode(
            noise_reader, 0x1A, timeout=args.timeout
        )
        if read_u32(noise_hosted, 0x0D) != 1:
            raise RuntimeError("noise game advertisement failed")

        _, chat_writer = await connect_and_login(
            args.host, args.port, chat_name, args.password, args.timeout
        )
        chat_writer.write(build_packet(0x2A, b"\0\0\0\0Online>\0hello lobby\0"))
        await chat_writer.drain()

        frame_payload = b"remote-smoke-frame"
        host_writer.write(
            build_packet(
                0x92, struct.pack("<III", game_id, join_member, 1) + frame_payload
            )
        )
        await host_writer.drain()
        forwarded_frame = await read_relay_packet(join_reader, args.timeout)
        if read_u32(forwarded_frame, 4) != 0x94:
            raise RuntimeError(
                "joiner received a non-relay packet after entering link mode: "
                f"opcode=0x{read_u32(forwarded_frame, 4):02x}"
            )
        if (
            read_u32(forwarded_frame, 0x0D) != game_id
            or read_u32(forwarded_frame, 0x11) != 1
            or read_u32(forwarded_frame, 0x15) != 1
            or forwarded_frame[0x19:] != frame_payload
        ):
            raise RuntimeError("joiner did not receive forwarded frame payload")

        large_payload = b"L" * 0x5000
        host_writer.write(
            build_packet(
                0x92, struct.pack("<III", game_id, join_member, 1) + large_payload
            )
        )
        await host_writer.drain()
        forwarded_large = await read_relay_packet(join_reader, args.timeout)
        if (
            read_u32(forwarded_large, 4) != 0x94
            or read_u32(forwarded_large, 0x0D) != game_id
            or read_u32(forwarded_large, 0x11) != 1
            or read_u32(forwarded_large, 0x15) != 1
            or forwarded_large[0x19:] != large_payload
        ):
            raise RuntimeError("joiner did not receive large forwarded frame payload")

        burst_count = 32
        for index in range(burst_count):
            burst_payload = f"remote-smoke-burst-{index:02d}".encode("ascii")
            host_writer.write(
                build_packet(
                    0x92,
                    struct.pack("<III", game_id, join_member, 1) + burst_payload,
                )
            )
        await host_writer.drain()
        for index in range(burst_count):
            expected_payload = f"remote-smoke-burst-{index:02d}".encode("ascii")
            assert_relay_frame(
                await read_relay_packet(join_reader, args.timeout),
                game_id=game_id,
                from_member=1,
                stream_id=1,
                payload=expected_payload,
                context=f"joiner did not receive ordered burst frame {index}",
            )

        third_reader, third_writer = await connect_and_login(
            args.host, args.port, third_name, args.password, args.timeout
        )
        third_writer.write(build_packet(0x90, struct.pack("<I", game_id) + b"third"))
        await third_writer.drain()
        third_joined = await read_until_opcode(third_reader, 0x93, timeout=args.timeout)
        if read_u32(third_joined, 0x0D) != 0:
            raise RuntimeError(
                f"third relay join failed: status={read_u32(third_joined, 0x0D)}"
            )
        third_member = read_u32(third_joined, 0x15)
        if third_member < 3:
            raise RuntimeError(f"bad third member id: {third_member}")
        forwarded_third_join = await read_until_opcode(
            host_reader, 0x94, timeout=args.timeout
        )
        if (
            read_u32(forwarded_third_join, 0x0D) != game_id
            or read_u32(forwarded_third_join, 0x11) != third_member
            or forwarded_third_join[0x19:] != b"third"
        ):
            raise RuntimeError("host did not receive forwarded third join payload")

        link_broadcast_payload = b"remote-smoke-link-broadcast"
        host_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 0, 0) + link_broadcast_payload,
            )
        )
        await host_writer.drain()
        assert_relay_frame(
            await read_relay_packet(join_reader, args.timeout),
            game_id=game_id,
            from_member=1,
            stream_id=0,
            payload=link_broadcast_payload,
            context="joiner did not receive stream-0 broadcast",
        )
        assert_relay_frame(
            await read_relay_packet(third_reader, args.timeout),
            game_id=game_id,
            from_member=1,
            stream_id=0,
            payload=link_broadcast_payload,
            context="third member did not receive stream-0 broadcast",
        )

        host_only_payload = b"joiner-to-host-only"
        join_writer.write(
            build_packet(
                0x92, struct.pack("<III", game_id, 1, 1) + host_only_payload
            )
        )
        await join_writer.drain()
        host_only = await read_until_opcode(host_reader, 0x94, timeout=args.timeout)
        if (
            read_u32(host_only, 0x0D) != game_id
            or read_u32(host_only, 0x11) != join_member
            or read_u32(host_only, 0x15) != 1
            or host_only[0x19:] != host_only_payload
        ):
            raise RuntimeError("host did not receive targeted joiner frame")
        await assert_no_relay_packet(
            third_reader,
            0.1,
            context="targeted joiner frame leaked to third member",
        )

        invalid_stream_payload = b"invalid-stream"
        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 1, 99) + invalid_stream_payload,
            )
        )
        await join_writer.drain()
        await assert_no_relay_packet(
            host_reader,
            0.1,
            context="invalid stream relay frame was forwarded",
        )

        broadcast_payload = b"host-broadcast"
        host_writer.write(
            build_packet(
                0x92, struct.pack("<III", game_id, 0, 1) + broadcast_payload
            )
        )
        await host_writer.drain()
        join_broadcast = await read_until_opcode(
            join_reader, 0x94, timeout=args.timeout
        )
        third_broadcast = await read_until_opcode(
            third_reader, 0x94, timeout=args.timeout
        )
        if (
            read_u32(join_broadcast, 0x0D) != game_id
            or read_u32(join_broadcast, 0x11) != 1
            or read_u32(join_broadcast, 0x15) != 1
            or join_broadcast[0x19:] != broadcast_payload
            or read_u32(third_broadcast, 0x0D) != game_id
            or read_u32(third_broadcast, 0x11) != 1
            or read_u32(third_broadcast, 0x15) != 1
            or third_broadcast[0x19:] != broadcast_payload
        ):
            raise RuntimeError("host broadcast was not delivered to all relay members")

        third_writer.write(build_packet(0x91, struct.pack("<I", game_id)))
        await third_writer.drain()
        third_left = await read_until_opcode(host_reader, 0x95, timeout=args.timeout)
        if read_u32(third_left, 0x0D) != game_id or read_u32(third_left, 0x11) != third_member:
            raise RuntimeError("host did not receive third member-left notification")

        join_writer.write(build_packet(0x91, struct.pack("<I", game_id)))
        await join_writer.drain()
        left = await read_until_opcode(host_reader, 0x95, timeout=args.timeout)
        if read_u32(left, 0x0D) != game_id or read_u32(left, 0x11) != join_member:
            raise RuntimeError("host did not receive member-left notification")

        rejoin_payload = b"remote-smoke-rejoin"
        join_writer.write(
            build_packet(0x90, struct.pack("<I", game_id) + rejoin_payload)
        )
        await join_writer.drain()
        rejoined = await read_until_opcode(join_reader, 0x93, timeout=args.timeout)
        if read_u32(rejoined, 0x0D) != 0 or read_u32(rejoined, 0x15) != join_member:
            raise RuntimeError(
                "relay did not reuse the freed member id: "
                f"status={read_u32(rejoined, 0x0D)} member={read_u32(rejoined, 0x15)}"
            )
        forwarded_rejoin = await read_until_opcode(
            host_reader, 0x94, timeout=args.timeout
        )
        if (
            read_u32(forwarded_rejoin, 0x0D) != game_id
            or read_u32(forwarded_rejoin, 0x11) != join_member
            or forwarded_rejoin[0x19:] != rejoin_payload
        ):
            raise RuntimeError("host did not receive forwarded rejoin payload")

        stale_leave_game_id = game_id + 9999
        join_writer.write(build_packet(0x91, struct.pack("<I", stale_leave_game_id)))
        await join_writer.drain()
        stale_probe = b"remote-smoke-stale-leave"
        join_writer.write(
            build_packet(
                0x92, struct.pack("<III", game_id, 1, 1) + stale_probe
            )
        )
        await join_writer.drain()
        forwarded_probe = await read_until_opcode(
            host_reader, 0x94, timeout=args.timeout
        )
        if (
            read_u32(forwarded_probe, 0x0D) != game_id
            or read_u32(forwarded_probe, 0x11) != join_member
            or read_u32(forwarded_probe, 0x15) != 1
            or forwarded_probe[0x19:] != stale_probe
        ):
            raise RuntimeError("stale relay leave removed the active membership")

        host_writer.write(build_packet(0x28, b"\0" * 0x20))
        await host_writer.drain()
        await read_until_game_opcode(
            browser_reader, 0x26, game_id, 0x0D, timeout=args.timeout
        )
        late_reader, late_writer = await connect_and_login(
            args.host, args.port, late_name, args.password, args.timeout
        )
        late_writer.write(build_packet(0x90, struct.pack("<I", game_id) + b"late"))
        await late_writer.drain()
        late_rejected = await read_until_opcode(
            late_reader, 0x93, timeout=args.timeout
        )
        if read_u32(late_rejected, 0x0D) != 1 or read_u32(late_rejected, 0x11) != game_id:
            raise RuntimeError("hidden relay advertisement accepted a late join")

        hidden_probe = b"remote-smoke-hidden-relay"
        join_writer.write(
            build_packet(
                0x92, struct.pack("<III", game_id, 1, 1) + hidden_probe
            )
        )
        await join_writer.drain()
        forwarded_hidden_probe = await read_until_opcode(
            host_reader, 0x94, timeout=args.timeout
        )
        if (
            read_u32(forwarded_hidden_probe, 0x0D) != game_id
            or read_u32(forwarded_hidden_probe, 0x11) != join_member
            or read_u32(forwarded_hidden_probe, 0x15) != 1
            or forwarded_hidden_probe[0x19:] != hidden_probe
        ):
            raise RuntimeError("hidden relay advertisement stopped active relay traffic")

        join_writer.write(build_packet(0x91, struct.pack("<I", game_id)))
        await join_writer.drain()
        left_again = await read_until_opcode(host_reader, 0x95, timeout=args.timeout)
        if (
            read_u32(left_again, 0x0D) != game_id
            or read_u32(left_again, 0x11) != join_member
        ):
            raise RuntimeError("host did not receive repeated member-left notification")

        host_writer.write(build_packet(0x0E, b"\0" * 8))
        await host_writer.drain()
        print(
            "relay smoke passed "
            f"host={host_name} join={join_name} game_id={game_id} member={join_member}"
        )
    finally:
        host_writer.close()
        join_writer.close()
        browser_writer.close()
        await host_writer.wait_closed()
        await join_writer.wait_closed()
        await browser_writer.wait_closed()
        if noise_writer is not None:
            noise_writer.close()
            await noise_writer.wait_closed()
        if chat_writer is not None:
            chat_writer.close()
            await chat_writer.wait_closed()
        if third_writer is not None:
            third_writer.close()
            await third_writer.wait_closed()
        if late_writer is not None:
            late_writer.close()
            await late_writer.wait_closed()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Exercise the reconstructed WizardNet room relay over TCP."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19777)
    parser.add_argument("--password", default="pw")
    parser.add_argument("--account-prefix", default="Rly")
    parser.add_argument("--room-prefix", default="RlyRoom")
    parser.add_argument("--timeout", type=float, default=5.0)
    return parser.parse_args()


def main() -> None:
    asyncio.run(run_smoke(parse_arguments()))


if __name__ == "__main__":
    main()
