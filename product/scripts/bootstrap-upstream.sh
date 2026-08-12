#!/usr/bin/env bash
set -euo pipefail

PACKAGE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="${1:-$(git rev-parse --show-toplevel)}"
exec python3 "$PACKAGE_ROOT/scripts/autonomous-init-repo.py" \
  --package-root "$PACKAGE_ROOT" \
  --repo "$REPO"
