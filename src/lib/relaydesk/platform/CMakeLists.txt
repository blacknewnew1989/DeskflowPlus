# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_platform_contract INTERFACE)

target_sources(
  relaydesk_platform_contract
  INTERFACE
    PermissionSnapshot.h
)
target_include_directories(
  relaydesk_platform_contract
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(
  relaydesk_platform_contract
  INTERFACE Qt6::Core
)

add_library(relaydesk_macos_permissions STATIC
  MacPermissionBackend.h
  MacPermissionProbe.cpp
  MacPermissionProbe.h
)

if(APPLE)
  target_sources(relaydesk_macos_permissions PRIVATE MacPermissionBackend.mm)
  target_link_libraries(
    relaydesk_macos_permissions
    PRIVATE
      "-framework AppKit"
      "-framework ApplicationServices"
      "-framework Network"
  )
  set_source_files_properties(MacPermissionBackend.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
else()
  target_sources(relaydesk_macos_permissions PRIVATE MacPermissionBackendStub.cpp)
endif()

target_include_directories(
  relaydesk_macos_permissions
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(
  relaydesk_macos_permissions
  PUBLIC relaydesk_platform_contract Qt6::Core
)
