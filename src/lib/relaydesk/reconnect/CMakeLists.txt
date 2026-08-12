# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_reconnect STATIC
  AutoReconnectCoordinator.cpp
  AutoReconnectCoordinator.h
)

target_include_directories(relaydesk_reconnect PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(
  relaydesk_reconnect
  PUBLIC relaydesk_device relaydesk_discovery relaydesk_trust Qt6::Core Qt6::Network
)
