# SPDX-FileCopyrightText: Chris Rizzitello <sithlord48@gmail.com>
# SPDX-License-Identifier: MIT
set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)
option(CLEAN_TRS "Clean obsolete translations from tr files" OFF)
option(
  RELAYDESK_UPDATE_TRANSLATION_SOURCES
  "Update tracked .ts catalogs from source while building translations"
  ON
)
find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED NO_MODULE COMPONENTS LinguistTools)

# To add a new language Add 639 shortname
set (${CMAKE_PROJECT_NAME}_TRS
  ${CMAKE_PROJECT_NAME}_es.ts
  ${CMAKE_PROJECT_NAME}_it.ts
  ${CMAKE_PROJECT_NAME}_ja.ts
  ${CMAKE_PROJECT_NAME}_zh_CN.ts
  ${CMAKE_PROJECT_NAME}_ru.ts
  ${CMAKE_PROJECT_NAME}_ko.ts
)

set(TR_OPTIONS -no-ui-lines -locations none -silent)
if(CLEAN_TRS)
  list(APPEND TR_OPTIONS -no-obsolete)
endif()

# RelayDesk UI uses semantic keys, so keep its source-language catalog complete
# instead of applying Deskflow's plurals-only English catalog rule.
set(RELAYDESK_TS
  ${RELAYDESK_TRANSLATION_CATALOG}_en.ts
  ${RELAYDESK_TRANSLATION_CATALOG}_zh_CN.ts
)

if(RELAYDESK_UPDATE_TRANSLATION_SOURCES)
  # English will have only plurals when explicitly refreshing the source catalogs.
  qt_create_translation(TRS ${CMAKE_SOURCE_DIR}/src ${CMAKE_PROJECT_NAME}_en.ts OPTIONS -pluralonly ${TR_OPTIONS})

  # Other languages contain the full set of strings.
  qt_create_translation(TRS ${CMAKE_SOURCE_DIR}/src ${${CMAKE_PROJECT_NAME}_TRS} OPTIONS ${TR_OPTIONS})
  qt_create_translation(
    RELAYDESK_TRS
    ${CMAKE_SOURCE_DIR}/src/lib/relaydesk/i18n
    ${RELAYDESK_TS}
    OPTIONS ${TR_OPTIONS}
  )
else()
  # Release and package builds compile the reviewed catalogs without rewriting
  # tracked source files. Catalog extraction remains an explicit maintainer task.
  qt_add_translation(TRS ${CMAKE_PROJECT_NAME}_en.ts ${${CMAKE_PROJECT_NAME}_TRS})
  qt_add_translation(RELAYDESK_TRS ${RELAYDESK_TS})
endif()

#ensure that the targets are built always
add_custom_target(app_translations ALL DEPENDS ${TRS} ${RELAYDESK_TRS})

if (NOT BUILD_OSX_BUNDLE)
  # install our translations
  install(FILES ${TRS} ${RELAYDESK_TRS} DESTINATION ${CMAKE_INSTALL_I18N_DIR})
else()
  #when making the bundle copy our translation into the bundle
  set(BUNDLE_TR_DIR $<TARGET_BUNDLE_CONTENT_DIR:${CMAKE_PROJECT_PROPER_NAME}>/Resources/translations)

  # find the qt translation files needed to copy into the bundle
  get_target_property(lupdate_executable Qt6::lupdate IMPORTED_LOCATION)
  get_filename_component(_qt_bin_dir "${lupdate_executable}" DIRECTORY)
  file(REAL_PATH "${_qt_bin_dir}/../" QT_ROOT_DIR)
  find_file(_QT_QM_FILE NAMES qtbase_en.qm PATHS ${QT_ROOT_DIR} PATH_SUFFIXES "translations" "share/qt/translations" REQUIRED)
  get_filename_component(MAC_QT_LANG_PATH "${_QT_QM_FILE}" DIRECTORY)

  add_custom_command(
    TARGET app_translations POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE_TR_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/*.qm" ${BUNDLE_TR_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MAC_QT_LANG_PATH}/qtbase_en.qm" "${BUNDLE_TR_DIR}/qt_en.qm"
  )

  # install the other Qt lang files renamed to qt_lang.qm
  foreach(LANG ${${CMAKE_PROJECT_NAME}_TRS})
    string(REPLACE "${CMAKE_PROJECT_NAME}_" "" LANG ${LANG})
    string(REPLACE ".ts" "" LANG ${LANG})
    add_custom_command(
      TARGET app_translations POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MAC_QT_LANG_PATH}/qtbase_${LANG}.qm" "${BUNDLE_TR_DIR}/qt_${LANG}.qm"
    )
  endforeach()
endif()
