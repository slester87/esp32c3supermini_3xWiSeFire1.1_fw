#!/usr/bin/env bash
set -euo pipefail

# Source ESP-IDF environment (IDF_PATH set by base image)
. "$IDF_PATH/export.sh" 2>/dev/null

case "${1:-build}" in
    build)
        python3 scripts/build.py
        ;;
    lint)
        scripts/lint.sh
        ;;
    verify)
        python3 scripts/verify_routes.py
        python3 scripts/verify_spiffs.py
        ;;
    shell)
        exec /bin/bash
        ;;
    *)
        exec "$@"
        ;;
esac
