from __future__ import annotations

import unittest

from ranker_server.protocol import (
    StreamDecoder,
    apply_packet_checksum,
    build_packet,
    verify_packet_checksum,
)


class ProtocolTests(unittest.TestCase):
    def test_checksum_matches_client_algorithm(self) -> None:
        packet = bytearray(build_packet(0x12, b"\x01\x02\x03\x04"))
        self.assertTrue(verify_packet_checksum(packet))
        packet[-1] ^= 0x01
        self.assertFalse(verify_packet_checksum(packet))
        apply_packet_checksum(packet)
        self.assertTrue(verify_packet_checksum(packet))

    def test_stream_decoder_retains_fragmented_packet(self) -> None:
        first = build_packet(1, b"A" * 300)
        second = build_packet(0x12, b"\0\0\0\0")
        decoder = StreamDecoder()
        self.assertEqual(decoder.feed(first[:11]), [])
        self.assertEqual(decoder.feed(first[11:57]), [])
        packets = decoder.feed(first[57:] + second)
        self.assertEqual([packet.opcode for packet in packets], [1, 0x12])
        self.assertEqual(packets[0].payload, b"A" * 300)
        self.assertEqual(decoder.buffered_bytes, 0)


if __name__ == "__main__":
    unittest.main()
