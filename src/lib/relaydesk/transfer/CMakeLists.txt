# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_transfer STATIC
  ControlMessageCodec.cpp
  ControlMessageCodec.h
  FrameCodec.cpp
  FrameCodec.h
  Protocol.h
)

target_include_directories(relaydesk_transfer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(relaydesk_transfer PUBLIC Qt6::Core)
