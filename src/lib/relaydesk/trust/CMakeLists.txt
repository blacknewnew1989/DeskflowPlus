# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_trust STATIC
  TrustedDeviceStore.cpp
  TrustedDeviceStore.h
)

target_include_directories(
  relaydesk_trust
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(relaydesk_trust PUBLIC relaydesk_device Qt6::Core Qt6::Network)
