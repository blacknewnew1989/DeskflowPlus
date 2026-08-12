# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskDevicesDockTests
  DEPENDS relaydesk_widgets
  SOURCE DevicesDockTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/widgets"
)

create_test(
  NAME RelayDeskTransferCenterDockTests
  DEPENDS relaydesk_widgets
  SOURCE TransferCenterDockTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/widgets"
)
