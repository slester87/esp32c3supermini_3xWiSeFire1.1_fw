#!/usr/bin/env python3
"""Capture a logic-analyzer trace with sigrok-cli."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Capture a sigrok trace for Poofer GPIO4 HIL")
    parser.add_argument("output", type=Path, help="Output file path, typically .sr")
    parser.add_argument("--driver", default="fx2lafw", help="sigrok driver name")
    parser.add_argument("--conn", help="sigrok connection identifier, e.g. 1.13")
    parser.add_argument("--samplerate", default="12m", help="Samplerate, e.g. 12m or 24m")
    parser.add_argument("--samples", type=int, help="Number of samples to acquire")
    parser.add_argument("--time-ms", type=int, help="Capture duration in milliseconds")
    parser.add_argument(
        "--channels",
        default="D0",
        help="Analyzer channels to enable; default assumes D0 is clipped to DUT GPIO4",
    )
    parser.add_argument("--triggers", help="Optional trigger expression")
    parser.add_argument(
        "--wait-trigger",
        action="store_true",
        help="Wait for trigger before capturing",
    )
    args = parser.parse_args()

    if bool(args.samples) == bool(args.time_ms):
        raise SystemExit("exactly one of --samples or --time-ms is required")

    args.output.parent.mkdir(parents=True, exist_ok=True)

    driver = args.driver
    config_parts = [f"samplerate={args.samplerate}"]
    if args.conn:
        driver = f"{driver}:conn={args.conn}"

    cmd = [
        "sigrok-cli",
        "--driver",
        driver,
        "--config",
        ",".join(config_parts),
        "--channels",
        args.channels,
        "--output-file",
        str(args.output),
    ]

    if args.samples:
        cmd += ["--samples", str(args.samples)]
    if args.time_ms:
        cmd += ["--time", str(args.time_ms)]
    if args.triggers:
        cmd += ["--triggers", args.triggers]
    if args.wait_trigger:
        cmd += ["--wait-trigger"]

    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
