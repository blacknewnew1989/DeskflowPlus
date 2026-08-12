# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskAutoReconnectCoordinatorTests
  DEPENDS relaydesk_reconnect
  SOURCE AutoReconnectCoordinatorTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/reconnect"
)
