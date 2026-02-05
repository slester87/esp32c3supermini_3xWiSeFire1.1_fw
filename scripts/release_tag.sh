#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $0 <version>" >&2
  echo "Example: $0 1.0.0" >&2
  exit 1
fi

VERSION="$1"
TAG="fw-${VERSION}"

git tag -a "$TAG" -m "Firmware ${VERSION}"
git push origin "$TAG"
