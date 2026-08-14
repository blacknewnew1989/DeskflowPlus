#!/bin/bash
set -euo pipefail

CONFIG="Release"
REPO_ROOT=""
SIGNING_IDENTITY="${RELAYDESK_MACOS_SIGNING_IDENTITY:-}"
NOTARY_PROFILE="${RELAYDESK_MACOS_NOTARY_PROFILE:-}"
PLAN_ONLY=false
while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) REPO_ROOT="$2"; shift 2 ;;
    --config) CONFIG="$2"; shift 2 ;;
    --signing-identity) SIGNING_IDENTITY="$2"; shift 2 ;;
    --notary-profile) NOTARY_PROFILE="$2"; shift 2 ;;
    --plan-only) PLAN_ONLY=true; shift ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
REPO_ROOT="${REPO_ROOT:-$(git rev-parse --show-toplevel)}"
REPO_ROOT="$(cd "$REPO_ROOT" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PACKAGE_VARIANT="adhoc"
if [[ -n "$SIGNING_IDENTITY" ]]; then
  PACKAGE_VARIANT="signed"
elif [[ -n "$NOTARY_PROFILE" ]]; then
  echo "A notary profile requires a Developer ID signing identity." >&2
  exit 2
fi

redact_output() {
  local protected="$1"
  local sensitive
  for sensitive in "$NOTARY_PROFILE"; do
    if [[ -n "$sensitive" ]]; then
      protected="${protected//"$sensitive"/***}"
    fi
  done
  printf '%s\n' "$protected"
}

if [[ "$PLAN_ONLY" == "true" ]]; then
  echo "MACOS_SIGNING_STATUS=$PACKAGE_VARIANT"
  if [[ -n "$NOTARY_PROFILE" ]]; then
    echo "MACOS_NOTARIZATION_STATUS=requested"
  else
    echo "MACOS_NOTARIZATION_STATUS=not-requested"
  fi
  exit 0
fi

python3 "$SCRIPT_DIR/generate-macos-brand-assets.py" --check
"$SCRIPT_DIR/setup-macos.sh" --repo "$REPO_ROOT"
BUILD_ARGS=(
  --repo "$REPO_ROOT"
  --config "$CONFIG"
  --tests
  --package-variant "$PACKAGE_VARIANT"
)
[[ -n "$SIGNING_IDENTITY" ]] && BUILD_ARGS+=(--signing-identity "$SIGNING_IDENTITY")
"$SCRIPT_DIR/build-macos.sh" "${BUILD_ARGS[@]}"
CONFIG_LOWER="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="$REPO_ROOT/build/macos/$CONFIG_LOWER"
for previous_dmg in "$BUILD_DIR"/relaydesk-*-macos-*.dmg; do
  [[ -f "$previous_dmg" ]] && cmake -E rm -f "$previous_dmg"
done
cmake --build "$BUILD_DIR" --config "$CONFIG" --target package package_source --parallel

FULL_SHA="$(git -C "$REPO_ROOT" rev-parse HEAD)"
STAGE_DIR="$BUILD_DIR/relaydesk-package-stage/$FULL_SHA"
cmake -E rm -rf "$STAGE_DIR"
cmake --install "$BUILD_DIR" --config "$CONFIG" --prefix "$STAGE_DIR"
APP_BUNDLES=()
for app_bundle in "$STAGE_DIR"/*.app; do
  [[ -d "$app_bundle" ]] && APP_BUNDLES+=("$app_bundle")
done
if [[ "${#APP_BUNDLES[@]}" -ne 1 ]]; then
  echo "MACOS_APP_INVALID: expected one staged app bundle, found ${#APP_BUNDLES[@]}" >&2
  exit 1
fi
APP_BUNDLE="${APP_BUNDLES[0]}"
OUT_DIR="$REPO_ROOT/dist/macos/$FULL_SHA"
TRANSLATION_REPORT="$OUT_DIR/macos-translation-bundle.json"
python3 "$SCRIPT_DIR/verify-macos-translation-bundle.py" \
  --repo-root "$REPO_ROOT" \
  --app-bundle "$APP_BUNDLE" \
  --report "$TRANSLATION_REPORT"
echo "MACOS_TRANSLATION_BUNDLE_REPORT=$TRANSLATION_REPORT"

codesign --verify --deep --strict "$APP_BUNDLE"
if [[ "$PACKAGE_VARIANT" == "adhoc" ]]; then
  echo "MACOS_SIGNING_STATUS=adhoc"
  echo "MACOS_SIGNING_DIAGNOSTIC=no Developer ID configured; producing an ad-hoc internal package"
else
  codesign --verify --deep --strict --verbose=2 "$APP_BUNDLE"
  echo "MACOS_SIGNING_STATUS=signed"
fi

DMG_FILES=()
for dmg_file in "$BUILD_DIR"/relaydesk-*-macos-*-$PACKAGE_VARIANT.dmg; do
  [[ -f "$dmg_file" ]] && DMG_FILES+=("$dmg_file")
done
if [[ "${#DMG_FILES[@]}" -ne 1 ]]; then
  echo "MACOS_DMG_INVALID: expected one $PACKAGE_VARIANT DMG, found ${#DMG_FILES[@]}" >&2
  exit 1
fi
DMG_PATH="${DMG_FILES[0]}"
NOTARIZED=false
if [[ "$PACKAGE_VARIANT" == "signed" ]]; then
  codesign --force --timestamp --sign "$SIGNING_IDENTITY" "$DMG_PATH"
  codesign --verify --strict --verbose=2 "$DMG_PATH"
  if [[ -n "$NOTARY_PROFILE" ]]; then
    set +e
    NOTARY_OUTPUT="$(xcrun notarytool submit "$DMG_PATH" --keychain-profile "$NOTARY_PROFILE" --wait 2>&1)"
    NOTARY_RESULT=$?
    set -e
    if [[ $NOTARY_RESULT -ne 0 ]]; then
      redact_output "$NOTARY_OUTPUT" >&2
      echo "MACOS_NOTARIZATION_FAILED" >&2
      exit $NOTARY_RESULT
    fi
    redact_output "$NOTARY_OUTPUT"
    xcrun stapler staple "$DMG_PATH"
    xcrun stapler validate "$DMG_PATH"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$DMG_PATH"
    NOTARIZED=true
    echo "MACOS_NOTARIZATION_STATUS=notarized"
  else
    echo "MACOS_NOTARIZATION_STATUS=not-requested"
    echo "MACOS_NOTARIZATION_DIAGNOSTIC=no keychain profile configured; signed package remains unnotarized"
  fi
else
  echo "MACOS_NOTARIZATION_STATUS=not-requested"
fi

COLLECT_ARGS=(
  "$REPO_ROOT/product/scripts/collect-ci-artifacts.py"
  --build-dir "$BUILD_DIR"
  --out-dir "$OUT_DIR"
  --platform macos-arm64
  --commit "$FULL_SHA"
  --package-variant "$PACKAGE_VARIANT"
  --app-bundle "$APP_BUNDLE"
)
[[ "$PACKAGE_VARIANT" == "signed" ]] && COLLECT_ARGS+=(--signed)
[[ "$NOTARIZED" == "true" ]] && COLLECT_ARGS+=(--notarized)
python3 "${COLLECT_ARGS[@]}"

echo "MACOS_ARTIFACTS=$OUT_DIR"
