# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskI18NTests
  DEPENDS relaydesk_i18n
  SOURCE ProductStringsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/i18n"
)

string(JOIN "," RELAYDESK_SUPPORTED_LANGUAGES_FOR_TESTS ${RELAYDESK_SUPPORTED_LANGUAGES})
add_dependencies(RelayDeskI18NTests app_translations)
target_compile_definitions(
  RelayDeskI18NTests
  PRIVATE
    RELAYDESK_TRANSLATIONS_SOURCE_DIR="${CMAKE_SOURCE_DIR}/translations"
    RELAYDESK_TRANSLATIONS_BINARY_DIR="${CMAKE_BINARY_DIR}/translations"
    RELAYDESK_SUPPORTED_LANGUAGES_FOR_TESTS="${RELAYDESK_SUPPORTED_LANGUAGES_FOR_TESTS}"
)
