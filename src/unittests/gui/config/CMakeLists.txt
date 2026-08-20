# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME ScreenTests
  DEPENDS gui
  SOURCE ScreenTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME RelayDeskInputLayoutTests
  DEPENDS gui
  SOURCE RelayDeskInputLayoutTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME RelayDeskInputTargetTests
  DEPENDS gui
  SOURCE RelayDeskInputTargetTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
