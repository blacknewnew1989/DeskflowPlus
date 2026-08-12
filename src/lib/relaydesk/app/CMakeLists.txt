# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_app STATIC
  DeviceDiscoveryRuntime.cpp
  DeviceDiscoveryRuntime.h
)

target_include_directories(
  relaydesk_app
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(
  relaydesk_app
  PUBLIC
    relaydesk_device_model
    relaydesk_discovery
    Qt6::Core
    Qt6::Network
)
