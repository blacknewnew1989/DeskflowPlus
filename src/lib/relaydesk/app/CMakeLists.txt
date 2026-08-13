# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED COMPONENTS Concurrent)

add_library(relaydesk_app STATIC
  AutoReconnectRuntime.cpp
  AutoReconnectRuntime.h
  DeviceDiscoveryRuntime.cpp
  DeviceDiscoveryRuntime.h
  FileTransferRuntime.cpp
  FileTransferRuntime.h
  IncomingFileReceiverWorker.cpp
  IncomingFileReceiverWorker.h
  IncomingTransferRuntime.cpp
  IncomingTransferRuntime.h
  PairingTrustRuntime.cpp
  PairingTrustRuntime.h
  TransferUiRuntime.cpp
  TransferUiRuntime.h
  TransferHistoryRuntime.cpp
  TransferHistoryRuntime.h
  TransferRuntimeComposition.cpp
  TransferRuntimeComposition.h
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
    relaydesk_pairing
    relaydesk_reconnect
    relaydesk_trust
    relaydesk_widgets
    Qt6::Core
    Qt6::Concurrent
    Qt6::Network
    Qt6::Widgets
)

if(WIN32)
  target_link_libraries(relaydesk_app PRIVATE relaydesk_windows_file_safety)
elseif(APPLE)
  target_link_libraries(relaydesk_app PRIVATE relaydesk_macos_file_safety)
endif()
