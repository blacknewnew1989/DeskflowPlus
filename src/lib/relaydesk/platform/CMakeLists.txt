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
