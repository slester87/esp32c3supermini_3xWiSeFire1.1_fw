#!/usr/bin/env python3
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "firmware" / "build"
SOURCE_DB = BUILD_DIR / "compile_commands.json"
OUT_DIR = BUILD_DIR / "clang_tidy"
OUT_DB = OUT_DIR / "compile_commands.json"

DROP_FLAGS = {
    "-fno-shrink-wrap",
    "-fno-tree-switch-conversion",
    "-fstrict-volatile-bitfields",
    "-nostartfiles",
    "-mlongcalls",
    'PROJECT_NAME="poofer"',
}


def _filter_args(entry):
    args = entry.get("arguments")
    if not args:
        command = entry.get("command", "")
        if command:
            args = command.split()
    if not args:
        return entry

    filtered = []
    for arg in args:
        if arg in DROP_FLAGS:
            continue
        if arg.startswith("--target=xtensa"):
            continue
        # Replace Xtensa cross-compiler with clang for clang-tidy
        if "xtensa" in arg and arg == args[0]:
            arg = "clang"
        if '\\"' in arg:
            arg = arg.replace('\\"', '"')
        filtered.append(arg)
    entry["arguments"] = filtered
    entry.pop("command", None)
    return entry


def main():
    if not SOURCE_DB.exists():
        raise SystemExit(f"Missing {SOURCE_DB}")

    data = json.loads(SOURCE_DB.read_text())
    filtered = [_filter_args(entry.copy()) for entry in data]

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    OUT_DB.write_text(json.dumps(filtered, indent=2))


if __name__ == "__main__":
    main()
