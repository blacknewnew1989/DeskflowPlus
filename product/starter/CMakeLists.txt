# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

cmake_minimum_required(VERSION 3.24)

project(relaydesk-transfer-starter
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Test)
qt_standard_project_setup()

add_library(relaydesk_transfer_protocol STATIC
    include/relaydesk/filetransfer/FrameCodec.h
    include/relaydesk/filetransfer/PathPolicy.h
    include/relaydesk/filetransfer/Protocol.h
    src/FrameCodec.cpp
    src/PathPolicy.cpp
)

target_include_directories(relaydesk_transfer_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(relaydesk_transfer_protocol
    PUBLIC
        Qt6::Core
)

target_compile_definitions(relaydesk_transfer_protocol
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

enable_testing()

qt_add_executable(relaydesk_frame_codec_tests
    tests/FrameCodecTests.cpp
)
target_link_libraries(relaydesk_frame_codec_tests
    PRIVATE relaydesk_transfer_protocol Qt6::Test
)
add_test(NAME relaydesk_frame_codec_tests COMMAND relaydesk_frame_codec_tests)

qt_add_executable(relaydesk_path_policy_tests
    tests/PathPolicyTests.cpp
)
target_link_libraries(relaydesk_path_policy_tests
    PRIVATE relaydesk_transfer_protocol Qt6::Test
)
add_test(NAME relaydesk_path_policy_tests COMMAND relaydesk_path_policy_tests)
