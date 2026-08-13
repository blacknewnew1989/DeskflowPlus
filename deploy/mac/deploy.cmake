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
option(
  RELAYDESK_MACOS_CUSTOM_DMG_LAYOUT
  "Use Finder automation to apply the custom RelayDesk DMG layout"
  OFF
)
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
  if(NOT DEFINED Qt6_DIR OR NOT IS_DIRECTORY "${Qt6_DIR}")
    message(FATAL_ERROR "Qt6_DIR is required to locate all macOS deployment frameworks")
  endif()
  get_filename_component(RELAYDESK_QT_CMAKE_PATH "${Qt6_DIR}" DIRECTORY)
  get_filename_component(RELAYDESK_QT_LIBRARY_PATH "${RELAYDESK_QT_CMAKE_PATH}" DIRECTORY)
  if(NOT IS_DIRECTORY "${RELAYDESK_QT_LIBRARY_PATH}")
    message(FATAL_ERROR "RelayDesk Qt library path does not exist: ${RELAYDESK_QT_LIBRARY_PATH}")
  endif()
  if(RELAYDESK_MACOS_CUSTOM_DMG_LAYOUT)
    configure_file(
      "${MY_DIR}/generate_ds_store.applescript"
      "${CMAKE_CURRENT_BINARY_DIR}/generate_ds_store.applescript"
      @ONLY
    )
    set(CPACK_DMG_BACKGROUND_IMAGE "${MY_DIR}/dmg-background.tiff")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generate_ds_store.applescript")
  endif()
  if(RELAYDESK_MACOS_PACKAGE_VARIANT STREQUAL "signed")
    set(RELAYDESK_MACDEPLOYQT_CODESIGN "${RELAYDESK_MACOS_SIGNING_IDENTITY}")
    set(RELAYDESK_MACDEPLOYQT_HARDENED "\"-hardened-runtime\"")
  else()
    set(RELAYDESK_MACDEPLOYQT_CODESIGN "-")
    set(RELAYDESK_MACDEPLOYQT_HARDENED "")
  endif()
  set(CPACK_PACKAGE_ICON "${MY_DIR}/dmg-volume.icns")
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

  # Keep this as the final app-bundle install rule: macdeployqt deploys the
  # completed bundle, then the sanitizer removes external runpaths and applies
  # the final signature. Later resource writes would invalidate CodeResources.
  install(CODE "
    set(relaydesk_app \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_PROPER_NAME}.app\")
    set(relaydesk_core \"\${relaydesk_app}/Contents/MacOS/deskflow-core\")
    if(NOT EXISTS \"\${relaydesk_core}\")
      message(FATAL_ERROR \"RelayDesk core executable is missing before macdeployqt\")
    endif()
    execute_process(
      COMMAND \"${DEPLOYQT}\"
              \"\${relaydesk_app}\"
              \"-executable=\${relaydesk_core}\"
              \"-libpath=${RELAYDESK_QT_LIBRARY_PATH}\"
              \"-timestamp\"
              \"-codesign=${RELAYDESK_MACDEPLOYQT_CODESIGN}\"
              ${RELAYDESK_MACDEPLOYQT_HARDENED}
      RESULT_VARIABLE relaydesk_macdeployqt_result
    )
    if(NOT relaydesk_macdeployqt_result EQUAL 0)
      message(FATAL_ERROR \"macdeployqt failed while deploying the RelayDesk app bundle\")
    endif()
    execute_process(
      COMMAND \"${MY_DIR}/sanitize_bundle_rpaths.sh\"
              \"\${relaydesk_app}\"
              \"${RELAYDESK_MACOS_PACKAGE_VARIANT}\"
              \"${RELAYDESK_MACOS_SIGNING_IDENTITY}\"
      RESULT_VARIABLE relaydesk_rpath_sanitize_result
    )
    if(NOT relaydesk_rpath_sanitize_result EQUAL 0)
      message(FATAL_ERROR \"failed to sanitize and re-sign the RelayDesk app bundle\")
    endif()
  ")
endif()
