"""Command-line entry point for the reconstructed WizardNet server."""

from __future__ import annotations

import argparse
import asyncio
import logging
from pathlib import Path

from ranker_server import RankerServer, load_config


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the reconstructed Ranker WizardNet control server."
    )
    parser.add_argument(
        "--config",
        default=str(Path(__file__).with_name("config.json")),
        help="JSON configuration path (default: config.json next to server.py)",
    )
    parser.add_argument("--host", help="override listen.host")
    parser.add_argument("--port", type=int, help="override listen.port")
    parser.add_argument("--log-level", help="override logging.level")
    parser.add_argument(
        "--check-config", action="store_true", help="validate configuration and exit"
    )
    return parser.parse_args()


async def async_main() -> None:
    args = parse_arguments()
    config = load_config(args.config)
    if args.host is not None:
        config.host = args.host
    if args.port is not None:
        config.port = args.port
    if args.log_level is not None:
        config.log_level = args.log_level.upper()
    config.validate()
    logging.basicConfig(
        level=getattr(logging, config.log_level, logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    if args.check_config:
        logging.getLogger("ranker_server").info("configuration is valid")
        return

    server = RankerServer(config)
    try:
        await server.serve_forever()
    finally:
        await server.close()


if __name__ == "__main__":
    try:
        asyncio.run(async_main())
    except KeyboardInterrupt:
        pass
