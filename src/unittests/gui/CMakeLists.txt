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
add_dependencies(MainWindowLayoutTests app_translations)
set(main_window_layout_translations_dir "$<TARGET_FILE_DIR:MainWindowLayoutTests>/translations")
add_custom_target(
  MainWindowLayoutTestTranslations
  COMMAND ${CMAKE_COMMAND} -E make_directory "${main_window_layout_translations_dir}"
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_BINARY_DIR}/translations/${CMAKE_PROJECT_NAME}_zh_CN.qm"
    "${CMAKE_BINARY_DIR}/translations/${RELAYDESK_TRANSLATION_CATALOG}_zh_CN.qm"
    "${main_window_layout_translations_dir}"
  VERBATIM
)
add_dependencies(MainWindowLayoutTestTranslations app_translations)
add_dependencies(MainWindowLayoutTests MainWindowLayoutTestTranslations)
set_tests_properties(
  MainWindowLayoutTests
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    RUN_SERIAL TRUE
)

add_executable(MainWindowQuitRegression MainWindowQuitRegression.cpp)
target_link_libraries(MainWindowQuitRegression gui Qt::Test)
if(CMAKE_CXX_BYTE_ORDER STREQUAL "BIG_ENDIAN")
  target_compile_definitions(MainWindowQuitRegression PUBLIC WORDS_BIGENDIAN=1)
endif()
foreach(entrypoint IN ITEMS menu tray)
  add_test(
    NAME MainWindowQuitRegression-${entrypoint}
    COMMAND $<TARGET_FILE:MainWindowQuitRegression> --entrypoint=${entrypoint}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
  )
  set_tests_properties(
    MainWindowQuitRegression-${entrypoint}
    PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
      RUN_SERIAL TRUE
      TIMEOUT 15
  )
endforeach()
set_property(GLOBAL APPEND PROPERTY ${CMAKE_PROJECT_NAME}_tests MainWindowQuitRegression)
