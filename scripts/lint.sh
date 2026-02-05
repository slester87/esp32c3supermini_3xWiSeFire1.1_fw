#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT_DIR"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required." >&2
  exit 1
fi

python3 -m ruff check scripts
python3 -m ruff format --check scripts

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format is required." >&2
  exit 1
fi

mapfile -d '' c_files < <(git ls-files -z "firmware/**/*.c" "firmware/**/*.h")
if [ "${#c_files[@]}" -gt 0 ]; then
  clang-format --dry-run --Werror "${c_files[@]}"
fi

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy is required." >&2
  exit 1
fi

if [ ! -f firmware/build/compile_commands.json ]; then
  if command -v idf.py >/dev/null 2>&1; then
    (cd firmware && idf.py build)
  else
    echo "clang-tidy skipped: idf.py not found and compile_commands.json missing."
    exit 0
  fi
fi

for file in "${c_files[@]}"; do
  clang-tidy -p firmware/build "$file"
done
