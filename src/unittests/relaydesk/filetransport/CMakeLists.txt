# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

get_target_property(relaydesk_file_transport_links relaydesk_file_transport LINK_LIBRARIES)
if(relaydesk_file_transport_links MATCHES "deskflow|server|client")
  message(FATAL_ERROR "RelayDesk file transport must not depend on Deskflow input services")
endif()

create_test(
  NAME RelayDeskFileTlsTransportTests
  DEPENDS relaydesk_file_transport
  SOURCE FileTlsTransportTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/filetransport"
)
