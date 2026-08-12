# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskDeviceTests
  DEPENDS relaydesk_device
  SOURCE DeviceIdentityTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/device"
)

create_test(
  NAME RelayDeskDeviceSnapshotTests
  DEPENDS relaydesk_device
  SOURCE DeviceSnapshotTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/device"
)
