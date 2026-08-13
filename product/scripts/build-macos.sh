#!/bin/bash
set -euo pipefail

CONFIG="Debug"
RUN_TESTS=false
REPO_ROOT=""
PACKAGE_VARIANT="adhoc"
SIGNING_IDENTITY=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) REPO_ROOT="$2"; shift 2 ;;
    --config) CONFIG="$2"; shift 2 ;;
    --tests) RUN_TESTS=true; shift ;;
    --package-variant) PACKAGE_VARIANT="$2"; shift 2 ;;
    --signing-identity) SIGNING_IDENTITY="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ "$PACKAGE_VARIANT" == "adhoc" || "$PACKAGE_VARIANT" == "signed" ]] || {
  echo "--package-variant must be adhoc or signed" >&2
  exit 2
}
if [[ "$PACKAGE_VARIANT" == "signed" && -z "$SIGNING_IDENTITY" ]]; then
  echo "signed package variant requires --signing-identity" >&2
  exit 2
fi
if [[ "$PACKAGE_VARIANT" == "adhoc" && -n "$SIGNING_IDENTITY" ]]; then
  echo "--signing-identity requires --package-variant signed" >&2
  exit 2
fi
REPO_ROOT="${REPO_ROOT:-$(git rev-parse --show-toplevel)}"
REPO_ROOT="$(cd "$REPO_ROOT" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$REPO_ROOT/.relaydesk-toolchain-macos.env"
if [[ ! -f "$ENV_FILE" ]]; then
  "$SCRIPT_DIR/setup-macos.sh" --repo "$REPO_ROOT"
fi
[[ -f "$ENV_FILE" ]] && source "$ENV_FILE"

for cmd in cmake ninja xcodebuild xcrun; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "$cmd unavailable; A0 must use the GitHub Actions macOS runner." >&2
    exit 10
  }
done
[[ -n "${RELAYDESK_QT_PREFIX:-}" && -d "${RELAYDESK_QT_PREFIX}/lib/cmake/Qt6" ]] || {
  echo "Qt unavailable; A0 must use the GitHub Actions macOS runner." >&2
  exit 10
}

MACOS_SDK="${RELAYDESK_MACOS_SDK:-$(xcrun --sdk macosx --show-sdk-path)}"
[[ -n "$MACOS_SDK" && -d "$MACOS_SDK" ]] || {
  echo "macOS SDK unavailable; A0 must use the GitHub Actions macOS runner." >&2
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
  "-DCMAKE_OSX_SYSROOT=$MACOS_SDK"
  -DSKIP_BUILD_TESTS=ON
  -DBUILD_TESTS=ON
  -DBUILD_OSX_BUNDLE=ON
  -DBUILD_INSTALLER=ON
  -DRELAYDESK_UPDATE_TRANSLATION_SOURCES=OFF
  "-DRELAYDESK_MACOS_PACKAGE_VARIANT=$PACKAGE_VARIANT"
)
[[ -n "$SIGNING_IDENTITY" ]] && ARGS+=("-DRELAYDESK_MACOS_SIGNING_IDENTITY=$SIGNING_IDENTITY")
[[ -n "$PREFIX_PATH" ]] && ARGS+=("-DCMAKE_PREFIX_PATH=$PREFIX_PATH")
[[ -n "${RELAYDESK_OPENSSL_PREFIX:-}" ]] && ARGS+=("-DOPENSSL_ROOT_DIR=${RELAYDESK_OPENSSL_PREFIX}")

cmake "${ARGS[@]}"
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel
if [[ "$RUN_TESTS" == "true" ]]; then
  ctest --test-dir "$BUILD_DIR/src/unittests" -C "$CONFIG" --output-on-failure
fi

echo "BUILD_DIR=$BUILD_DIR"
