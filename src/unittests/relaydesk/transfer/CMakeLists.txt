# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskTransferSettingsTests
  DEPENDS relaydesk_transfer
  SOURCE TransferSettingsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskControlMessageCodecTests
  DEPENDS relaydesk_transfer
  SOURCE ControlMessageCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskConflictResolverTests
  DEPENDS relaydesk_transfer
  SOURCE ConflictResolverTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskFrameCodecTests
  DEPENDS relaydesk_transfer
  SOURCE FrameCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskProtocolMessageRegistryTests
  DEPENDS relaydesk_transfer
  SOURCE ProtocolMessageRegistryTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)
target_compile_definitions(
  RelayDeskProtocolMessageRegistryTests
  PRIVATE
    RELAYDESK_PROTOCOL_CDDL_PATH="${CMAKE_SOURCE_DIR}/product/spec/protocol/messages.cddl"
    RELAYDESK_PROTOCOL_TEST_VECTORS_PATH="${CMAKE_SOURCE_DIR}/product/spec/protocol/test-vectors.json"
)

create_test(
  NAME RelayDeskProtocolVectorTests
  DEPENDS relaydesk_transfer
  SOURCE ProtocolVectorTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskStrongIdTests
  DEPENDS relaydesk_transfer
  SOURCE StrongIdTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)
target_compile_definitions(
  RelayDeskProtocolVectorTests
  PRIVATE RELAYDESK_PROTOCOL_TEST_VECTORS_PATH="${CMAKE_SOURCE_DIR}/product/spec/protocol/test-vectors.json"
)

create_test(
  NAME RelayDeskPathPolicyTests
  DEPENDS relaydesk_transfer
  SOURCE PathPolicyTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskPartialCleanupPolicyTests
  DEPENDS relaydesk_transfer
  SOURCE PartialCleanupPolicyTests.cpp
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

create_test(
  NAME RelayDeskTransferOfferStateMachineTests
  DEPENDS relaydesk_transfer
  SOURCE TransferOfferStateMachineTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskTransferControlStateMachineTests
  DEPENDS relaydesk_transfer
  SOURCE TransferControlStateMachineTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskTransferCommandCodecTests
  DEPENDS relaydesk_transfer
  SOURCE TransferCommandCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskTransferCompletionCodecTests
  DEPENDS relaydesk_transfer
  SOURCE TransferCompletionCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskTransferHistoryStoreTests
  DEPENDS relaydesk_transfer
  SOURCE TransferHistoryStoreTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskFileMessageCodecTests
  DEPENDS relaydesk_transfer
  SOURCE FileMessageCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskFileReceiverTests
  DEPENDS relaydesk_transfer
  SOURCE FileReceiverTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskResumeStoreTests
  DEPENDS relaydesk_transfer
  SOURCE ResumeStoreTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskResumeMessageCodecTests
  DEPENDS relaydesk_transfer
  SOURCE ResumeMessageCodecTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskTransferSenderTests
  DEPENDS relaydesk_transfer
  SOURCE TransferSenderTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskTransferProgressPublisherTests
  DEPENDS relaydesk_transfer
  SOURCE TransferProgressPublisherTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskLargeFileBoundedBenchmark
  DEPENDS relaydesk_transfer
  SOURCE LargeFileBoundedBenchmark.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskInputPriorityUnderTransferBenchmark
  DEPENDS relaydesk_transfer
  SOURCE InputPriorityUnderTransferBenchmark.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)

create_test(
  NAME RelayDeskTransferRecoveryMatrixTests
  DEPENDS relaydesk_transfer
  SOURCE TransferRecoveryMatrixTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/transfer"
)
