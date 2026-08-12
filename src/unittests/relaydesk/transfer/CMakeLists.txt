# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskControlMessageCodecTests
  DEPENDS relaydesk_transfer
  SOURCE ControlMessageCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskFrameCodecTests
  DEPENDS relaydesk_transfer
  SOURCE FrameCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskPathPolicyTests
  DEPENDS relaydesk_transfer
  SOURCE PathPolicyTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskManifestBuilderTests
  DEPENDS relaydesk_transfer
  SOURCE ManifestBuilderTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskManifestPageCodecTests
  DEPENDS relaydesk_transfer
  SOURCE ManifestPageCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskSessionMessageCodecTests
  DEPENDS relaydesk_transfer
  SOURCE SessionMessageCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskCapabilityCodecTests
  DEPENDS relaydesk_transfer
  SOURCE CapabilityCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)
