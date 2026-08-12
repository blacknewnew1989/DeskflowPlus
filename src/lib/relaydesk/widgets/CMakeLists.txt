# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_widgets STATIC
  DevicesDock.cpp
  DevicesDock.h
  TransferCenterDock.cpp
  TransferCenterDock.h
  TransferHistoryDetailsDialog.cpp
  TransferHistoryDetailsDialog.h
)

target_include_directories(
  relaydesk_widgets
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(
  relaydesk_widgets
  PUBLIC
    relaydesk_device_model
    relaydesk_transfer
    Qt6::Core
    Qt6::Widgets
)
