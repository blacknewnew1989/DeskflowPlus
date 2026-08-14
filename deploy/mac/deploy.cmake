# SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-License-Identifier: MIT

# HACK This is set when the files is included so its the real path
# calling CMAKE_CURRENT_LIST_DIR after include would return the wrong scope var
set(MY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(OSX_BUNDLE ${BUILD_OSX_BUNDLE})

set(RELAYDESK_MACOS_PACKAGE_VARIANT "adhoc" CACHE STRING "RelayDesk macOS package signature variant")
set_property(CACHE RELAYDESK_MACOS_PACKAGE_VARIANT PROPERTY STRINGS adhoc signed)
if(NOT RELAYDESK_MACOS_PACKAGE_VARIANT MATCHES "^(adhoc|signed)$")
  message(FATAL_ERROR "RELAYDESK_MACOS_PACKAGE_VARIANT must be adhoc or signed")
endif()
set(RELAYDESK_MACOS_SIGNING_IDENTITY "" CACHE STRING "Developer ID Application identity for macOS packages")
if(RELAYDESK_MACOS_PACKAGE_VARIANT STREQUAL "signed" AND
   RELAYDESK_MACOS_SIGNING_IDENTITY STREQUAL "")
  message(FATAL_ERROR "signed macOS packages require RELAYDESK_MACOS_SIGNING_IDENTITY")
endif()
if(RELAYDESK_MACOS_PACKAGE_VARIANT STREQUAL "adhoc" AND
   NOT RELAYDESK_MACOS_SIGNING_IDENTITY STREQUAL "")
  message(FATAL_ERROR "a signing identity requires RELAYDESK_MACOS_PACKAGE_VARIANT=signed")
endif()
set(OS_STRING "macos-${BUILD_ARCHITECTURE}-${RELAYDESK_MACOS_PACKAGE_VARIANT}")

if (OSX_BUNDLE)
  configure_file(
    "${MY_DIR}/generate_ds_store.applescript"
    "${CMAKE_CURRENT_BINARY_DIR}/generate_ds_store.applescript"
    @ONLY
  )
  if(RELAYDESK_MACOS_PACKAGE_VARIANT STREQUAL "signed")
    set(RELAYDESK_MACDEPLOYQT_CODESIGN "${RELAYDESK_MACOS_SIGNING_IDENTITY}")
    set(RELAYDESK_MACDEPLOYQT_HARDENED "\"-hardened-runtime\"")
  else()
    set(RELAYDESK_MACDEPLOYQT_CODESIGN "-")
    set(RELAYDESK_MACDEPLOYQT_HARDENED "")
  endif()
  # Finder/Dock and the mounted DMG volume deliberately consume the same
  # generated RelayDesk ICNS. The background is generated from the same SVG.
  set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/${RELAYDESK_MACOS_ICON_SOURCE}")
  set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_SOURCE_DIR}/${RELAYDESK_MACOS_DMG_BACKGROUND_SOURCE}")
  set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generate_ds_store.applescript")
  set(CPACK_DMG_VOLUME_NAME "${CMAKE_PROJECT_PROPER_NAME}")
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE ON)
  set(CPACK_GENERATOR "DragNDrop")

  configure_file(
    "${MY_DIR}/README-macOS.txt.in"
    "${CMAKE_CURRENT_BINARY_DIR}/README-macOS.txt"
    @ONLY
  )
  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/README-macOS.txt"
    DESTINATION "${CMAKE_INSTALL_LICENSE_DIR}"
  )

  # Keep this as the final app-bundle install rule: macdeployqt signs the
  # completed bundle, so later resource writes would invalidate CodeResources.
  install(CODE "
    execute_process(
      COMMAND \"${DEPLOYQT}\"
              \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
              \"-timestamp\"
              \"-codesign=${RELAYDESK_MACDEPLOYQT_CODESIGN}\"
              ${RELAYDESK_MACDEPLOYQT_HARDENED}
      RESULT_VARIABLE relaydesk_macdeployqt_result
    )
    if(NOT relaydesk_macdeployqt_result EQUAL 0)
      message(FATAL_ERROR \"macdeployqt failed while deploying the RelayDesk app bundle\")
    endif()
  ")
endif()
