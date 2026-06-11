#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT_DIR"

source scripts/load_env.sh

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

declare -a c_files=()
while IFS= read -r -d '' file; do
  c_files+=("$file")
done < <(git ls-files -z "firmware/**/*.c" "firmware/**/*.h")
if [ "${#c_files[@]}" -gt 0 ]; then
  clang-format --dry-run --Werror "${c_files[@]}"
fi
