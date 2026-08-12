# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_discovery STATIC
  AddressCandidateProvider.cpp
  AddressCandidateProvider.h
  DiscoveryCodec.cpp
  DiscoveryCodec.h
  DiscoveryRegistry.cpp
  DiscoveryRegistry.h
  DiscoverySettings.cpp
  DiscoverySettings.h
  DiscoveryService.cpp
  DiscoveryService.h
)

target_include_directories(
  relaydesk_discovery
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(relaydesk_discovery PUBLIC relaydesk_device Qt6::Core Qt6::Network)
