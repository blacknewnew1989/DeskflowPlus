# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskDiscoveryCodecTests
  DEPENDS relaydesk_discovery
  SOURCE DiscoveryCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/discovery"
)

create_test(
  NAME RelayDeskDiscoveryServiceTests
  DEPENDS relaydesk_discovery
  SOURCE DiscoveryServiceTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/discovery"
)

create_test(
  NAME RelayDeskDiscoveryRegistryTests
  DEPENDS relaydesk_discovery
  SOURCE DiscoveryRegistryTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/discovery"
)
