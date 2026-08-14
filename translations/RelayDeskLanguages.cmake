# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

# This is the single source of truth for every RelayDesk runtime, translation,
# test, and package language list. Consumers must derive filenames and runtime
# tables from these values instead of copying the language codes.
set(RELAYDESK_SUPPORTED_LANGUAGES
  en
  es
  it
  ja
  ko
  ru
  zh_CN
)
set(RELAYDESK_FALLBACK_LANGUAGE en)

if(NOT RELAYDESK_FALLBACK_LANGUAGE IN_LIST RELAYDESK_SUPPORTED_LANGUAGES)
  message(FATAL_ERROR "RelayDesk fallback language must be supported")
endif()

list(LENGTH RELAYDESK_SUPPORTED_LANGUAGES RELAYDESK_SUPPORTED_LANGUAGE_COUNT)
set(RELAYDESK_SUPPORTED_LANGUAGES_CPP "")
foreach(RELAYDESK_LANGUAGE IN LISTS RELAYDESK_SUPPORTED_LANGUAGES)
  string(APPEND RELAYDESK_SUPPORTED_LANGUAGES_CPP
    "    std::string_view{\"${RELAYDESK_LANGUAGE}\"},\n")
endforeach()
unset(RELAYDESK_LANGUAGE)
