# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_app STATIC
  DeviceDiscoveryRuntime.cpp
  DeviceDiscoveryRuntime.h
  FileTransferRuntime.cpp
  FileTransferRuntime.h
  TransferUiRuntime.cpp
  TransferUiRuntime.h
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
    relaydesk_file_transport
    relaydesk_trust
    relaydesk_widgets
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
)
