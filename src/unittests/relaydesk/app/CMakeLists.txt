# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskDeviceDiscoveryRuntimeTests
  DEPENDS relaydesk_app
  SOURCE DeviceDiscoveryRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskTransferUiRuntimeTests
  DEPENDS relaydesk_app
  SOURCE TransferUiRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)
