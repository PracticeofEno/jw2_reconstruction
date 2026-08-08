"""Wire helpers for the legacy Ranker asynchronous TCP protocol."""

from __future__ import annotations

from dataclasses import dataclass
import struct


HEADER_BYTES = 0x0D
MAX_PACKET_BYTES = 1024 * 1024
LEGACY_PACKET_TYPE = 3


class ProtocolError(ValueError):
    """Raised when a peer sends a malformed stream frame."""


def read_u32(data: bytes | bytearray | memoryview, offset: int, default: int = 0) -> int:
    if offset < 0 or offset + 4 > len(data):
        return default
    return struct.unpack_from("<I", data, offset)[0]


def read_i32(data: bytes | bytearray | memoryview, offset: int, default: int = 0) -> int:
    if offset < 0 or offset + 4 > len(data):
        return default
    return struct.unpack_from("<i", data, offset)[0]


def write_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value & 0xFFFFFFFF)


def write_i32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<i", data, offset, value)


def read_c_string(
    data: bytes | bytearray | memoryview, offset: int, maximum: int
) -> str:
    if offset < 0 or maximum <= 0 or offset >= len(data):
        return ""
    raw = bytes(data[offset : min(len(data), offset + maximum)])
    raw = raw.split(b"\0", 1)[0]
    return raw.decode("cp949", errors="replace")


def encode_legacy_text(text: str, maximum: int) -> bytes:
    if maximum <= 0:
        return b""
    encoded = text.encode("cp949", errors="replace")[: maximum - 1]
    return encoded + b"\0"


def write_fixed_text(data: bytearray, offset: int, maximum: int, text: str) -> None:
    if offset < 0 or maximum <= 0 or offset + maximum > len(data):
        raise ValueError("fixed text field is outside its packet")
    encoded = encode_legacy_text(text, maximum)
    data[offset : offset + maximum] = b"\0" * maximum
    data[offset : offset + len(encoded)] = encoded


def packet_checksum(data: bytes | bytearray | memoryview) -> int:
    checksum = 0
    for index in range(HEADER_BYTES, len(data)):
        checksum = (checksum + data[index] * ((index % 9) + 1)) & 0xFF
    return checksum


def apply_packet_checksum(data: bytearray) -> None:
    if len(data) < HEADER_BYTES:
        raise ValueError("packet is shorter than the legacy header")
    data[0x0C] = 0
    data[0x0C] = packet_checksum(data)


def verify_packet_checksum(data: bytes | bytearray | memoryview) -> bool:
    return len(data) >= HEADER_BYTES and data[0x0C] == packet_checksum(data)


def build_packet(
    opcode: int,
    payload: bytes | bytearray | memoryview = b"",
    *,
    packet_type: int = LEGACY_PACKET_TYPE,
) -> bytes:
    packet = bytearray(HEADER_BYTES + len(payload))
    struct.pack_into("<III", packet, 0, packet_type, opcode, len(packet))
    packet[HEADER_BYTES:] = payload
    apply_packet_checksum(packet)
    return bytes(packet)


def build_status_packet(opcode: int, status: int) -> bytes:
    return build_packet(opcode, struct.pack("<I", status & 0xFFFFFFFF))


def build_colored_text_packet(
    first_text: str,
    second_text: str,
    *,
    first_color: tuple[int, int, int] = (0, 250, 250),
    second_color: tuple[int, int, int] = (250, 250, 250),
) -> bytes:
    """Build the raw type-zero two-segment text packet consumed by lobby UIs."""
    first = encode_legacy_text(first_text, 0x100)
    second = encode_legacy_text(second_text, 0x100)
    if len(first) > 0xFF or len(second) > 0xFF:
        raise ValueError("colored text segment is too long")
    packet = bytearray(4)
    packet.extend(bytes((*first_color, len(first))))
    packet.extend(first)
    packet.extend(bytes((*second_color, len(second))))
    packet.extend(second)
    return bytes(packet)


@dataclass(frozen=True, slots=True)
class Packet:
    packet_type: int
    opcode: int
    raw: bytes
    checksum_valid: bool

    @property
    def payload(self) -> bytes:
        return self.raw[HEADER_BYTES:]


class StreamDecoder:
    """Incrementally split a TCP byte stream using the legacy size field."""

    def __init__(self, *, maximum_packet_bytes: int = MAX_PACKET_BYTES) -> None:
        self._buffer = bytearray()
        self.maximum_packet_bytes = maximum_packet_bytes

    @property
    def buffered_bytes(self) -> int:
        return len(self._buffer)

    def feed(self, data: bytes | bytearray | memoryview) -> list[Packet]:
        self._buffer.extend(data)
        packets: list[Packet] = []
        while len(self._buffer) >= 12:
            packet_bytes = read_u32(self._buffer, 8)
            if packet_bytes < HEADER_BYTES:
                raise ProtocolError(f"invalid packet size {packet_bytes}")
            if packet_bytes > self.maximum_packet_bytes:
                raise ProtocolError(
                    f"packet size {packet_bytes} exceeds {self.maximum_packet_bytes}"
                )
            if len(self._buffer) < packet_bytes:
                break
            raw = bytes(self._buffer[:packet_bytes])
            del self._buffer[:packet_bytes]
            packets.append(
                Packet(
                    packet_type=read_u32(raw, 0),
                    opcode=read_u32(raw, 4),
                    raw=raw,
                    checksum_valid=verify_packet_checksum(raw),
                )
            )
        return packets
