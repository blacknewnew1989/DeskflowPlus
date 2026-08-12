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
set(OS_STRING "macos-${BUILD_ARCHITECTURE}-${RELAYDESK_MACOS_PACKAGE_VARIANT}")

if (OSX_BUNDLE)
  configure_file(
    "${MY_DIR}/generate_ds_store.applescript"
    "${CMAKE_CURRENT_BINARY_DIR}/generate_ds_store.applescript"
    @ONLY
  )
  install(CODE "execute_process(COMMAND
    ${DEPLOYQT}
    \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\"
    -timestamp -codesign=-
  )")
  set(CPACK_PACKAGE_ICON "${MY_DIR}/dmg-volume.icns")
  set(CPACK_DMG_BACKGROUND_IMAGE "${MY_DIR}/dmg-background.tiff")
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
endif()
