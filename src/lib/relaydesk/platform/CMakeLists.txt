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
