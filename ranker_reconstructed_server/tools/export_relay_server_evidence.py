"""Export one validated WizardNet relay server summary as evidence JSON."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

import relay_server_summary_check


def summary_args(args: argparse.Namespace) -> argparse.Namespace:
    return argparse.Namespace(
        log=args.log,
        room=args.room,
        game_id=args.game_id,
        min_link_frames=args.min_link_frames,
        min_mode1_frames=args.min_mode1_frames,
        min_deliveries=args.min_deliveries,
        min_members=args.min_members,
        min_distinct_peer_hosts=args.min_distinct_peer_hosts,
        min_distinct_peer_endpoints=args.min_distinct_peer_endpoints,
        min_bidirectional_mode1_members=args.min_bidirectional_mode1_members,
        max_no_target=args.max_no_target,
        max_invalid_stream=args.max_invalid_stream,
        max_invalid_payload=args.max_invalid_payload,
        tail_bytes=args.tail_bytes,
        show_lines=args.show_lines,
    )


def build_evidence(args: argparse.Namespace) -> dict[str, object]:
    result = relay_server_summary_check.check_summary(summary_args(args))
    return {
        "ok": result["ok"],
        "exported_utc": datetime.now(timezone.utc).isoformat(),
        "source_log": str(args.log.resolve()),
        "room": args.room,
        "game_id": args.game_id,
        "server_summary": result,
    }


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(
        description="Export validated relay summary evidence from a server log."
    )
    parser.add_argument("log", type=Path, help="Path to the server stderr log.")
    parser.add_argument("--room", required=True, help="Expected room name.")
    parser.add_argument("--game-id", type=int, help="Expected server game ID.")
    parser.add_argument("--output", type=Path, help="Optional JSON output path.")
    parser.add_argument("--min-link-frames", type=int, default=1)
    parser.add_argument("--min-mode1-frames", type=int, default=1)
    parser.add_argument("--min-deliveries", type=int, default=1)
    parser.add_argument("--min-members", type=int, default=2)
    parser.add_argument("--min-distinct-peer-hosts", type=int, default=0)
    parser.add_argument("--min-distinct-peer-endpoints", type=int, default=2)
    parser.add_argument("--min-bidirectional-mode1-members", type=int, default=2)
    parser.add_argument("--max-no-target", type=int, default=0)
    parser.add_argument("--max-invalid-stream", type=int, default=0)
    parser.add_argument("--max-invalid-payload", type=int, default=0)
    parser.add_argument("--tail-bytes", type=int, default=1024 * 1024)
    parser.add_argument("--show-lines", type=int, default=5)
    args = parser.parse_args()

    evidence = build_evidence(args)
    output = json.dumps(evidence, ensure_ascii=False, indent=2)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output + "\n", encoding="utf-8")
    print(output)
    return 0 if evidence["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
