# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskDeviceDiscoveryRuntimeTests
  DEPENDS relaydesk_app
  SOURCE DeviceDiscoveryRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskPairingTrustRuntimeTests
  DEPENDS relaydesk_app
  SOURCE PairingTrustRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskPairingInterfaceFreezeTests
  DEPENDS relaydesk_app
  SOURCE PairingInterfaceFreezeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskTransferUiRuntimeTests
  DEPENDS relaydesk_app
  SOURCE TransferUiRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskFileTransferRuntimeTests
  DEPENDS relaydesk_app
  SOURCE FileTransferRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskIncomingTransferRuntimeTests
  DEPENDS relaydesk_app
  SOURCE IncomingTransferRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)
target_compile_definitions(
  RelayDeskFileTransferRuntimeTests
  PRIVATE RELAYDESK_FILE_TRANSFER_RUNTIME_SOURCE_PATH="${CMAKE_SOURCE_DIR}/src/lib/relaydesk/app/FileTransferRuntime.cpp"
)

create_test(
  NAME RelayDeskSharedInterfaceFreezeTests
  DEPENDS relaydesk_app
  SOURCE SharedInterfaceFreezeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)
