# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_transfer STATIC
  CapabilityCodec.cpp
  CapabilityCodec.h
  ControlMessageCodec.cpp
  ControlMessageCodec.h
  ConflictResolver.cpp
  ConflictResolver.h
  FrameCodec.cpp
  FrameCodec.h
  IFileTransferService.h
  FileMessageCodec.cpp
  FileMessageCodec.h
  FileId.cpp
  FileId.h
  FileReceiver.cpp
  FileReceiver.h
  ManifestBuilder.cpp
  ManifestBuilder.h
  ManifestPageCodec.cpp
  ManifestPageCodec.h
  PathPolicy.cpp
  PathPolicy.h
  PartialCleanupPolicy.cpp
  PartialCleanupPolicy.h
  Protocol.h
  ProtocolMessageRegistry.cpp
  ProtocolMessageRegistry.def
  ProtocolMessageRegistry.h
  ResumeStore.cpp
  ResumeStore.h
  ResumeMessageCodec.cpp
  ResumeMessageCodec.h
  SessionMessageCodec.cpp
  SessionMessageCodec.h
  TransferOfferStateMachine.cpp
  TransferOfferStateMachine.h
  TransferProgressPublisher.cpp
  TransferProgressPublisher.h
  TransferRecoveryStore.cpp
  TransferRecoveryStore.h
  TransferControlStateMachine.cpp
  TransferControlStateMachine.h
  TransferCommandCodec.cpp
  TransferCommandCodec.h
  TransferCompletionCodec.cpp
  TransferCompletionCodec.h
  TransferError.h
  TransferHistoryStore.cpp
  TransferHistoryStore.h
  TransferId.cpp
  TransferId.h
  TransferSender.cpp
  TransferSender.h
  TransferSettings.cpp
  TransferSettings.h
  TransferTypes.h
)

target_include_directories(relaydesk_transfer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(relaydesk_transfer PUBLIC relaydesk_device Qt6::Core)
