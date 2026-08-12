# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskTrustedDeviceStoreTests
  DEPENDS relaydesk_trust
  SOURCE TrustedDeviceStoreTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/trust"
)

create_test(
  NAME RelayDeskTlsIdentityAdapterTests
  DEPENDS relaydesk_trust
  SOURCE TlsIdentityAdapterTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/trust"
)
