#!/bin/bash
set -euo pipefail

REPO_ROOT=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) REPO_ROOT="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
REPO_ROOT="${REPO_ROOT:-$(git rev-parse --show-toplevel)}"
REPO_ROOT="$(cd "$REPO_ROOT" && pwd)"
WORKING="$REPO_ROOT/product/working/toolchains"
mkdir -p "$WORKING"
ACTIONS_FALLBACK=false

if ! command -v xcodebuild >/dev/null 2>&1; then
  ACTIONS_FALLBACK=true
fi

if command -v brew >/dev/null 2>&1; then
  brew update >/dev/null || true
  brew install cmake ninja qt openssl@3 googletest python || ACTIONS_FALLBACK=true
else
  # Homebrew/Xcode may require an interactive system authorization. Do not ask
  # the user during development; the macOS GitHub Actions runner is the fallback.
  ACTIONS_FALLBACK=true
fi

QT_PREFIX=""
OPENSSL_PREFIX=""
if command -v brew >/dev/null 2>&1; then
  QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
  OPENSSL_PREFIX="$(brew --prefix openssl@3 2>/dev/null || true)"
fi

for cmd in cmake ninja python3; do
  command -v "$cmd" >/dev/null 2>&1 || ACTIONS_FALLBACK=true
done
[[ -n "$QT_PREFIX" && -d "$QT_PREFIX/lib/cmake/Qt6" ]] || ACTIONS_FALLBACK=true

cat > "$REPO_ROOT/.relaydesk-toolchain-macos.env" <<ENV
export RELAYDESK_QT_PREFIX="$QT_PREFIX"
export RELAYDESK_OPENSSL_PREFIX="$OPENSSL_PREFIX"
export PATH="$QT_PREFIX/bin:\$PATH"
ENV

python3 - "$WORKING/macos.json" "$REPO_ROOT" "$QT_PREFIX" "$OPENSSL_PREFIX" "$ACTIONS_FALLBACK" <<'PY'
import json, platform, subprocess, sys
out, repo, qt, openssl, fallback = sys.argv[1:]
def cmd(*args):
    try:
        return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT).strip()
    except Exception:
        return ""
cmake = cmd("cmake", "--version")
json.dump({
    "repo": repo,
    "system": platform.platform(),
    "architecture": platform.machine(),
    "xcode": cmd("xcodebuild", "-version"),
    "cmake": cmake.splitlines()[0] if cmake else "",
    "qtPrefix": qt,
    "opensslPrefix": openssl,
    "actionsFallback": fallback.lower() == "true",
}, open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
PY

if [[ "$ACTIONS_FALLBACK" == "true" ]]; then
  echo "ACTIONS_FALLBACK=true"
  echo "Local macOS toolchain incomplete; A0 must use relaydesk-build.yml and must not ask the user to install tools." >&2
else
  echo "ACTIONS_FALLBACK=false"
  echo "macOS toolchain prepared."
fi
