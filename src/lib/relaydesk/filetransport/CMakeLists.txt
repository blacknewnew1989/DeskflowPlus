# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_file_transport STATIC
  FileTlsTransport.cpp
  FileTlsTransport.h
)

target_include_directories(relaydesk_file_transport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(
  relaydesk_file_transport
  PUBLIC relaydesk_device relaydesk_transfer relaydesk_trust Qt6::Core Qt6::Network
)
