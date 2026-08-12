# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskDeviceHomeModelTests
  DEPENDS relaydesk_device_model
  SOURCE DeviceHomeModelTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/model"
)

create_test(
  NAME RelayDeskPairingWizardModelTests
  DEPENDS relaydesk_device_model
  SOURCE PairingWizardModelTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/model"
)
