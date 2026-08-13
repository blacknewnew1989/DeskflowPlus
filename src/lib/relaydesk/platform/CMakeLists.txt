# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_platform_contract INTERFACE)

target_sources(
  relaydesk_platform_contract
  INTERFACE
    IPlatformFileSafety.h
    IPlatformPermissions.h
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
  MacLocalNetworkStatus.cpp
  MacLocalNetworkStatus.h
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

add_library(relaydesk_windows_permissions STATIC
  WindowsFirewallProbe.cpp
  WindowsFirewallProbe.h
)
find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED COMPONENTS Concurrent)
target_include_directories(
  relaydesk_windows_permissions
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(
  relaydesk_windows_permissions
  PUBLIC relaydesk_platform_contract Qt6::Core Qt6::Concurrent
)
if(WIN32)
  target_link_libraries(relaydesk_windows_permissions PRIVATE iphlpapi ole32 oleaut32)
endif()

add_library(relaydesk_windows_start_at_login STATIC
  WindowsStartAtLogin.cpp
  WindowsStartAtLogin.h
)
target_include_directories(
  relaydesk_windows_start_at_login
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(
  relaydesk_windows_start_at_login
  PUBLIC Qt6::Core
)
if(WIN32)
  target_link_libraries(relaydesk_windows_start_at_login PRIVATE advapi32)
endif()
