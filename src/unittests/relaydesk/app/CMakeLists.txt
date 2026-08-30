# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

create_test(
  NAME RelayDeskDeviceDiscoveryRuntimeTests
  DEPENDS relaydesk_app
  SOURCE DeviceDiscoveryRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskBackgroundLifecycleControllerTests
  DEPENDS relaydesk_app
  SOURCE BackgroundLifecycleControllerTests.cpp
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
  NAME RelayDeskTransferRuntimeCompositionTests
  DEPENDS relaydesk_app
  SOURCE TransferRuntimeCompositionTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskFileTransferRuntimeTests
  DEPENDS relaydesk_app
  SOURCE FileTransferRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskAutoReconnectRuntimeTests
  DEPENDS relaydesk_app
  SOURCE AutoReconnectRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)

create_test(
  NAME RelayDeskIncomingFileReceiverWorkerTests
  DEPENDS relaydesk_app
  SOURCE IncomingFileReceiverWorkerTests.cpp
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

add_executable(RelayDeskTwoProcessPeer TwoProcessRelayDeskPeer.cpp)
target_link_libraries(RelayDeskTwoProcessPeer PRIVATE relaydesk_app net Qt6::Core)

if(WIN32)
  function(relaydesk_copy_qt_runtime target)
    foreach(qt_target Qt6::Core Qt6::Gui Qt6::Network Qt6::Test Qt6::Widgets)
      add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:${qt_target}>"
          "$<TARGET_FILE_DIR:${target}>"
      )
    endforeach()
  endfunction()
  relaydesk_copy_qt_runtime(RelayDeskTwoProcessPeer)
  add_custom_command(
    TARGET RelayDeskTwoProcessPeer
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RelayDeskTwoProcessPeer>/tls"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt6::QTlsBackendOpenSSLPlugin>"
      "$<TARGET_FILE_DIR:RelayDeskTwoProcessPeer>/tls"
  )
  file(GLOB relaydesk_two_process_ssl_runtime
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/libcrypto-*.dll"
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/libssl-*.dll"
  )
  foreach(runtime_file ${relaydesk_two_process_ssl_runtime})
    add_custom_command(
      TARGET RelayDeskTwoProcessPeer
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${runtime_file}"
        "$<TARGET_FILE_DIR:RelayDeskTwoProcessPeer>"
    )
  endforeach()
endif()

create_test(
  NAME RelayDeskTwoProcessRuntimeTests
  DEPENDS relaydesk_app
  LIBS Qt6::Network
  SOURCE TwoProcessRelayDeskRuntimeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/relaydesk/app"
)
add_dependencies(RelayDeskTwoProcessRuntimeTests RelayDeskTwoProcessPeer)
target_compile_definitions(
  RelayDeskTwoProcessRuntimeTests
  PRIVATE RELAYDESK_TWO_PROCESS_PEER_PATH="$<TARGET_FILE:RelayDeskTwoProcessPeer>"
)
if(WIN32)
  relaydesk_copy_qt_runtime(RelayDeskTwoProcessRuntimeTests)
endif()
