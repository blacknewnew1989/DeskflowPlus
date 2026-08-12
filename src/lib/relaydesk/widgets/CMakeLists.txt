# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_widgets STATIC
  DevicesDock.cpp
  DevicesDock.h
)

target_include_directories(
  relaydesk_widgets
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(
  relaydesk_widgets
  PUBLIC
    relaydesk_device_model
    Qt6::Core
    Qt6::Widgets
)
