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
