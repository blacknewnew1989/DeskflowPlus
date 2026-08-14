# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

add_subdirectory(config)
add_subdirectory(core)

create_test(
  NAME LoggerTests
  DEPENDS gui
  SOURCE LoggerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME MainWindowLayoutTests
  DEPENDS gui
  SOURCE MainWindowLayoutTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
set_tests_properties(
  MainWindowLayoutTests
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    RUN_SERIAL TRUE
)
