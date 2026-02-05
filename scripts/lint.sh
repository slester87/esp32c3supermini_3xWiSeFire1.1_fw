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

CLANG_TIDY_BIN=$(command -v clang-tidy || true)
if [ -z "$CLANG_TIDY_BIN" ] && [ -n "${POOFER_LLVM_BIN:-}" ] && [ -x "${POOFER_LLVM_BIN}/clang-tidy" ]; then
  CLANG_TIDY_BIN="${POOFER_LLVM_BIN}/clang-tidy"
fi
if [ -z "$CLANG_TIDY_BIN" ]; then
  echo "clang-tidy is required." >&2
  exit 1
fi

if [ ! -f firmware/build/compile_commands.json ]; then
  if command -v idf.py >/dev/null 2>&1; then
    (cd firmware && idf.py build)
  elif [ -n "${POOFER_IDF_PATH:-}" ] && [ -f "${POOFER_IDF_PATH}/export.sh" ]; then
    # shellcheck disable=SC1090
    source "${POOFER_IDF_PATH}/export.sh"
    (cd firmware && idf.py build)
  else
    echo "clang-tidy skipped: idf.py not found and compile_commands.json missing."
    exit 0
  fi
fi

python3 scripts/prepare_clang_tidy_db.py

for file in "${c_files[@]}"; do
  "$CLANG_TIDY_BIN" -p firmware/build/clang_tidy "$file"
done
