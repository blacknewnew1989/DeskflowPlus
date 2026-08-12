#!/bin/bash
set -euo pipefail

CONFIG="Debug"
RUN_TESTS=false
REPO_ROOT=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) REPO_ROOT="$2"; shift 2 ;;
    --config) CONFIG="$2"; shift 2 ;;
    --tests) RUN_TESTS=true; shift ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
REPO_ROOT="${REPO_ROOT:-$(git rev-parse --show-toplevel)}"
REPO_ROOT="$(cd "$REPO_ROOT" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$REPO_ROOT/.relaydesk-toolchain-macos.env"
if [[ ! -f "$ENV_FILE" ]]; then
  "$SCRIPT_DIR/setup-macos.sh" --repo "$REPO_ROOT"
fi
[[ -f "$ENV_FILE" ]] && source "$ENV_FILE"

for cmd in cmake ninja xcodebuild; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "$cmd unavailable; A0 must use the GitHub Actions macOS runner." >&2
    exit 10
  }
done
[[ -n "${RELAYDESK_QT_PREFIX:-}" && -d "${RELAYDESK_QT_PREFIX}/lib/cmake/Qt6" ]] || {
  echo "Qt unavailable; A0 must use the GitHub Actions macOS runner." >&2
  exit 10
}

CONFIG_LOWER="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="$REPO_ROOT/build/macos/$CONFIG_LOWER"
mkdir -p "$BUILD_DIR"
PREFIX_PATH="${RELAYDESK_QT_PREFIX:-}"
[[ -n "${RELAYDESK_OPENSSL_PREFIX:-}" ]] && PREFIX_PATH="${PREFIX_PATH};${RELAYDESK_OPENSSL_PREFIX}"

ARGS=(
  -S "$REPO_ROOT"
  -B "$BUILD_DIR"
  -G Ninja
  "-DCMAKE_BUILD_TYPE=$CONFIG"
  -DCMAKE_OSX_ARCHITECTURES=arm64
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14
  -DSKIP_BUILD_TESTS=ON
  -DBUILD_TESTS=ON
  -DBUILD_OSX_BUNDLE=ON
  -DBUILD_INSTALLER=ON
)
[[ -n "$PREFIX_PATH" ]] && ARGS+=("-DCMAKE_PREFIX_PATH=$PREFIX_PATH")
[[ -n "${RELAYDESK_OPENSSL_PREFIX:-}" ]] && ARGS+=("-DOPENSSL_ROOT_DIR=${RELAYDESK_OPENSSL_PREFIX}")

cmake "${ARGS[@]}"
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel
if [[ "$RUN_TESTS" == "true" ]]; then
  ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure
fi

echo "BUILD_DIR=$BUILD_DIR"
