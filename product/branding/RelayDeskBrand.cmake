# RelayDesk product identity. Keep temporary/internal identity values here until
# the final product name, domains, and signing identities are selected.

set(RELAYDESK_PRODUCT_NAME "RelayDesk")
set(RELAYDESK_PRODUCT_DESCRIPTION "Local network keyboard, mouse, clipboard, and file sharing")
set(RELAYDESK_VENDOR_NAME "RelayDesk Internal")
set(RELAYDESK_CONTACT "RelayDesk Internal Build")
set(RELAYDESK_ORGANIZATION_DOMAIN "relaydesk.local")
set(RELAYDESK_BUNDLE_IDENTIFIER "local.relaydesk.desktop")
set(RELAYDESK_WINDOWS_APP_USER_MODEL_ID "RelayDesk.Internal.Desktop")
set(RELAYDESK_WINDOWS_WIX_UPGRADE_GUID "50C1FCAB-2BF8-447C-806D-A53C21C6A237")
set(RELAYDESK_WINDOWS_RUN_VALUE_NAME "RelayDesk")
set(RELAYDESK_WINDOWS_SERVICE_NAME "RelayDesk")
set(RELAYDESK_PACKAGE_ID "relaydesk")

# Original RelayDesk artwork and its generated macOS resources. Platform assets
# must be regenerated from the canonical SVG instead of being hand-drawn.
set(RELAYDESK_BRAND_MARK_SOURCE "product/assets/branding/relaydesk-mark.svg")
set(RELAYDESK_MACOS_ICON_FILE "RelayDesk.icns")
set(RELAYDESK_MACOS_ICON_SOURCE "src/apps/res/RelayDesk.icns")
set(RELAYDESK_MACOS_MENU_BAR_ICON_NAME "${RELAYDESK_BUNDLE_IDENTIFIER}-symbolic")
set(RELAYDESK_MACOS_MENU_BAR_TEMPLATE_SOURCE
    "product/assets/branding/generated/relaydesk-menu-bar-template.svg")
set(RELAYDESK_MACOS_DMG_BACKGROUND_SOURCE "deploy/mac/dmg-background.tiff")
set(RELAYDESK_MACOS_LOCAL_NETWORK_USAGE_DESCRIPTION
    "RelayDesk uses the local network to discover trusted computers and transfer input, clipboard, and files.")

# Upstream binary and protocol names stay stable to minimize Deskflow sync risk.
set(RELAYDESK_INTERNAL_EXECUTABLE_ID "deskflow")
set(RELAYDESK_FILE_PROTOCOL "RDFT")
set(RELAYDESK_FILE_PROTOCOL_MAJOR 1)
set(RELAYDESK_FILE_FALLBACK_PORT 24801)
set(RELAYDESK_DEFAULT_RECEIVE_FOLDER "RelayDesk")
set(RELAYDESK_TRANSLATION_CATALOG "relaydesk")

# Internal builds must not query Deskflow's public update service as though they
# were official Deskflow releases.
set(RELAYDESK_UPDATE_CHECK_ENABLED OFF)
