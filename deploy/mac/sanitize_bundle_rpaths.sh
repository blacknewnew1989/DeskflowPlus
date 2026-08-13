#!/bin/bash
# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

set -euo pipefail

app_bundle=${1:?app bundle is required}
package_variant=${2:?package variant is required}
signing_identity=${3-}

if [[ ! -d "$app_bundle/Contents" ]]; then
  echo "invalid RelayDesk app bundle: $app_bundle" >&2
  exit 2
fi

while IFS= read -r -d '' candidate; do
  if ! /usr/bin/file -b "$candidate" | /usr/bin/grep -q 'Mach-O'; then
    continue
  fi

  while IFS= read -r rpath; do
    case "$rpath" in
      @loader_path|@loader_path/*|@executable_path|@executable_path/*|@rpath|@rpath/*|/System/Library/*|/usr/lib/*)
        ;;
      *)
        /usr/bin/install_name_tool -delete_rpath "$rpath" "$candidate"
        ;;
    esac
  done < <(
    /usr/bin/otool -l "$candidate" | /usr/bin/awk '
      $1 == "cmd" && $2 == "LC_RPATH" { need_path = 1; next }
      need_path && $1 == "path" {
        sub(/^[[:space:]]*path[[:space:]]+/, "")
        sub(/[[:space:]]+\(offset[[:space:]]+[0-9]+\)$/, "")
        print
        need_path = 0
      }
    '
  )
done < <(/usr/bin/find "$app_bundle/Contents" -type f -print0)

case "$package_variant" in
  adhoc)
    if [[ -n "$signing_identity" ]]; then
      echo "ad-hoc package must not provide a signing identity" >&2
      exit 3
    fi
    /usr/bin/codesign --force --deep --sign - "$app_bundle"
    ;;
  signed)
    if [[ -z "$signing_identity" ]]; then
      echo "signed package requires a signing identity" >&2
      exit 4
    fi
    /usr/bin/codesign \
      --force \
      --deep \
      --timestamp \
      --options runtime \
      --sign "$signing_identity" \
      "$app_bundle"
    ;;
  *)
    echo "unsupported package variant: $package_variant" >&2
    exit 5
    ;;
esac
