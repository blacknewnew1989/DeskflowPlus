# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_device STATIC
  DeviceId.cpp
  DeviceId.h
  DeviceIdentity.cpp
  DeviceIdentity.h
  DeviceInfo.cpp
  DeviceInfo.h
)

target_include_directories(
  relaydesk_device
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(relaydesk_device PUBLIC Qt6::Core)
