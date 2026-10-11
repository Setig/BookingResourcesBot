include_guard(GLOBAL)

macro(_booking_resources_translator_sanitize_target_name _input _output_var)
  string(REPLACE "::" "_" ${_output_var} "${_input}")
  string(REPLACE "-" "_" ${_output_var} "${${_output_var}}")
  string(REPLACE " " "_" ${_output_var} "${${_output_var}}")
endmacro()

macro(booking_resources_setup_translations)
  set(options)
  set(oneValueArgs TARGET BASE_NAME RESOURCE_PREFIX TRANSLATIONS_TARGET UPDATE_TARGET_NAME SOURCE_DIR)
  set(multiValueArgs TS_FILES)
  cmake_parse_arguments(BR_TR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT BR_TR_TARGET)
    message(FATAL_ERROR "booking_resources_setup_translations: TARGET is required.")
  endif()

  if(NOT TARGET ${BR_TR_TARGET})
    message(FATAL_ERROR "booking_resources_setup_translations: TARGET '${BR_TR_TARGET}' is unknown.")
  endif()

  if(NOT BR_TR_BASE_NAME)
    message(FATAL_ERROR "booking_resources_setup_translations: BASE_NAME is required.")
  endif()

  if(NOT BR_TR_TS_FILES)
    message(FATAL_ERROR "booking_resources_setup_translations: TS_FILES must list at least one file.")
  endif()

  if(NOT BR_TR_RESOURCE_PREFIX)
    set(BR_TR_RESOURCE_PREFIX "/translations")
  endif()

  if(NOT BR_TR_SOURCE_DIR)
    set(BR_TR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  _booking_resources_translator_sanitize_target_name("${BR_TR_TARGET}" _br_tr_sanitized_target)

  if(NOT BR_TR_TRANSLATIONS_TARGET)
    set(BR_TR_TRANSLATIONS_TARGET "${_br_tr_sanitized_target}_translations")
  endif()

  if(NOT BR_TR_UPDATE_TARGET_NAME)
    set(BR_TR_UPDATE_TARGET_NAME "update_${_br_tr_sanitized_target}_translations")
  endif()

  set(_br_tr_qm_var "BOOKING_RESOURCES_${_br_tr_sanitized_target}_QM_FILES")
  if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_translation(${_br_tr_qm_var}
      ${BR_TR_TS_FILES}
    )
  else()
    qt5_add_translation(${_br_tr_qm_var}
      ${BR_TR_TS_FILES}
    )
  endif()

  add_custom_target(${BR_TR_TRANSLATIONS_TARGET}
    DEPENDS ${${_br_tr_qm_var}}
  )

  add_dependencies(${BR_TR_TARGET}
    ${BR_TR_TRANSLATIONS_TARGET}
  )

  set(_br_tr_qrc "${CMAKE_CURRENT_BINARY_DIR}/${_br_tr_sanitized_target}_translations.qrc")
  set(_br_tr_qrc_content "<RCC>\n    <qresource prefix=\"${BR_TR_RESOURCE_PREFIX}\">\n")
  foreach(_br_tr_qm ${${_br_tr_qm_var}})
    get_filename_component(_br_tr_qm_name "${_br_tr_qm}" NAME)
    file(TO_CMAKE_PATH "${_br_tr_qm}" _br_tr_qm_normalized)
    string(APPEND _br_tr_qrc_content
      "        <file alias=\"${_br_tr_qm_name}\">${_br_tr_qm_normalized}</file>\n")
  endforeach()
  string(APPEND _br_tr_qrc_content "    </qresource>\n</RCC>\n")
  file(GENERATE OUTPUT "${_br_tr_qrc}"
    CONTENT "${_br_tr_qrc_content}"
  )
  set_source_files_properties("${_br_tr_qrc}"
    PROPERTIES GENERATED TRUE
  )

  # Build the resource with an explicit custom command so that rcc depends on the generated
  # translation files (*.qm). CMake's AUTORCC does not automatically add them as dependencies
  # for generated .qrc files, which can break clean rebuilds.
  set(_br_tr_rcc_name "${_br_tr_sanitized_target}_translations")
  set(_br_tr_rcc_cpp "${CMAKE_CURRENT_BINARY_DIR}/qrc_${_br_tr_rcc_name}.cpp")
  if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    set(_br_tr_rcc_extra_options --no-zstd)
  else()
    set(_br_tr_rcc_extra_options)
  endif()
  add_custom_command(
    OUTPUT "${_br_tr_rcc_cpp}"
    COMMAND Qt${QT_VERSION_MAJOR}::rcc
            ${_br_tr_rcc_extra_options}
            -name ${_br_tr_rcc_name}
            -o "${_br_tr_rcc_cpp}"
            "${_br_tr_qrc}"
    DEPENDS
            "${_br_tr_qrc}"
            ${${_br_tr_qm_var}}
            Qt${QT_VERSION_MAJOR}::rcc
    COMMENT "Compiling ${BR_TR_TARGET} translations resources"
    VERBATIM
  )
  set_source_files_properties("${_br_tr_rcc_cpp}"
    PROPERTIES GENERATED TRUE
  )
  target_sources(${BR_TR_TARGET}
    PRIVATE
      "${_br_tr_rcc_cpp}"
  )

  add_custom_target(${BR_TR_UPDATE_TARGET_NAME}
    COMMAND Qt${QT_VERSION_MAJOR}::lupdate
            -locations none
            "${BR_TR_SOURCE_DIR}"
            -ts ${BR_TR_TS_FILES}
    WORKING_DIRECTORY "${BR_TR_SOURCE_DIR}"
    COMMENT "Updating ${BR_TR_TARGET} translations"
  )

  unset(_br_tr_qm_var)
  unset(_br_tr_qrc)
  unset(_br_tr_qrc_content)
  unset(_br_tr_rcc_name)
  unset(_br_tr_rcc_cpp)
  unset(_br_tr_rcc_extra_options)
  unset(_br_tr_qm_name)
  unset(_br_tr_qm_normalized)
  unset(_br_tr_sanitized_target)
endmacro()
