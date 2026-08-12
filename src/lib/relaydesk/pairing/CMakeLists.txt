# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_pairing STATIC
  PairingManager.cpp
  PairingManager.h
  PairingMessageCodec.cpp
  PairingMessageCodec.h
  PairingStateMachine.cpp
  PairingStateMachine.h
  PairingTrustCommitter.cpp
  PairingTrustCommitter.h
)

target_include_directories(
  relaydesk_pairing
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(relaydesk_pairing PUBLIC relaydesk_device relaydesk_trust Qt6::Core Qt6::Network)
