# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskPermissionSnapshotTests
  DEPENDS relaydesk_platform_contract
  SOURCE PermissionSnapshotTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/platform"
)

create_test(
  NAME RelayDeskMacPermissionProbeTests
  DEPENDS relaydesk_macos_permissions
  SOURCE MacPermissionProbeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/platform"
)

create_test(
  NAME RelayDeskWindowsFirewallProbeTests
  DEPENDS relaydesk_windows_permissions
  LIBS Qt6::Network
  SOURCE WindowsFirewallProbeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/platform"
)
