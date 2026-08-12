# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskPairingStateMachineTests
  DEPENDS relaydesk_pairing
  SOURCE PairingStateMachineTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/pairing"
)

create_test(
  NAME RelayDeskPairingMessageCodecTests
  DEPENDS relaydesk_pairing
  SOURCE PairingMessageCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/pairing"
)

create_test(
  NAME RelayDeskPairingTrustCommitterTests
  DEPENDS relaydesk_pairing
  SOURCE PairingTrustCommitterTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/pairing"
)

create_test(
  NAME RelayDeskPairingManagerTests
  DEPENDS relaydesk_pairing
  SOURCE PairingManagerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/pairing"
)

create_test(
  NAME RelayDeskPairingServiceTests
  DEPENDS relaydesk_pairing
  SOURCE PairingServiceTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/pairing"
)
