#!/usr/bin/env bash
set -u

failed=0

check() {
  local name="$1"
  shift
  if command -v "$name" >/dev/null 2>&1; then
    printf "%-14s " "$name"
    "$@" 2>&1 | head -n 2
  else
    echo "MISSING: $name" >&2
    failed=1
  fi
}

echo "RelayDesk macOS/Unix development environment"
check git git --version
check cmake cmake --version
check clang clang --version
check xcodebuild xcodebuild -version
check python3 python3 --version

if command -v qtpaths6 >/dev/null 2>&1; then
  echo "qtpaths6: $(qtpaths6 --qt-version 2>/dev/null || true)"
elif command -v qmake6 >/dev/null 2>&1; then
  echo "qmake6: $(qmake6 -query QT_VERSION 2>/dev/null || true)"
elif command -v brew >/dev/null 2>&1; then
  echo "Qt formula: $(brew list --versions qt 2>/dev/null || echo 'not found')"
  failed=1
else
  echo "MISSING: Qt 6 detection (qtpaths6/qmake6/Homebrew)" >&2
  failed=1
fi

if command -v openssl >/dev/null 2>&1; then
  openssl version
else
  echo "MISSING: openssl" >&2
  failed=1
fi

echo
if [[ "$failed" -ne 0 ]]; then
  echo "Environment is incomplete. Use official Deskflow build documentation." >&2
  exit 2
fi
echo "Basic tools detected. A real v1.26.0 configure/build is still required."
