#!/bin/bash
set -euo pipefail

CONFIG="Release"
REPO_ROOT=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) REPO_ROOT="$2"; shift 2 ;;
    --config) CONFIG="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
REPO_ROOT="${REPO_ROOT:-$(git rev-parse --show-toplevel)}"
REPO_ROOT="$(cd "$REPO_ROOT" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/setup-macos.sh" --repo "$REPO_ROOT"
"$SCRIPT_DIR/build-macos.sh" --repo "$REPO_ROOT" --config "$CONFIG" --tests
CONFIG_LOWER="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="$REPO_ROOT/build/macos/$CONFIG_LOWER"
cmake --build "$BUILD_DIR" --config "$CONFIG" --target package package_source --parallel

FULL_SHA="$(git -C "$REPO_ROOT" rev-parse HEAD)"
OUT_DIR="$REPO_ROOT/dist/macos/$FULL_SHA"
python3 "$REPO_ROOT/product/scripts/collect-ci-artifacts.py" \
  --build-dir "$BUILD_DIR" \
  --out-dir "$OUT_DIR" \
  --platform macos-arm64 \
  --commit "$FULL_SHA"

echo "MACOS_ARTIFACTS=$OUT_DIR"
