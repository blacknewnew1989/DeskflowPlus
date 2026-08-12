# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_i18n STATIC
  ProductStrings.cpp
  ProductStrings.h
)

target_include_directories(
  relaydesk_i18n
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(relaydesk_i18n PUBLIC Qt6::Core)
