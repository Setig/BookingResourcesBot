if(NOT WIN32)
  return()
endif()

if(NOT DEFINED BOOKING_RESOURCES_WINDEPLOYQT_EXECUTABLE
   OR BOOKING_RESOURCES_WINDEPLOYQT_EXECUTABLE STREQUAL "")
  message(WARNING "windeployqt was not configured; Windows runtime dependencies were not bundled.")
  return()
endif()

if(NOT EXISTS "${BOOKING_RESOURCES_WINDEPLOYQT_EXECUTABLE}")
  message(WARNING
          "Configured windeployqt executable was not found: ${BOOKING_RESOURCES_WINDEPLOYQT_EXECUTABLE}")
  return()
endif()

set(_br_windows_deploy_bin_dir "${CMAKE_INSTALL_PREFIX}/${BOOKING_RESOURCES_WINDOWS_DEPLOY_BINDIR}")

foreach(_br_windows_deploy_executable IN LISTS BOOKING_RESOURCES_WINDOWS_DEPLOY_EXECUTABLES)
  set(_br_windows_deploy_target
      "${_br_windows_deploy_bin_dir}/${_br_windows_deploy_executable}")

  if(NOT EXISTS "${_br_windows_deploy_target}")
    message(STATUS "Skipping windeployqt for missing target ${_br_windows_deploy_target}")
    continue()
  endif()

  execute_process(
    COMMAND
      "${BOOKING_RESOURCES_WINDEPLOYQT_EXECUTABLE}"
      --no-translations
      --force
      --sql
      --dir "${_br_windows_deploy_bin_dir}"
      "${_br_windows_deploy_target}"
    RESULT_VARIABLE _br_windows_deploy_result
    OUTPUT_VARIABLE _br_windows_deploy_output
    ERROR_VARIABLE _br_windows_deploy_error
  )

  if(NOT _br_windows_deploy_result EQUAL 0)
    message(FATAL_ERROR
            "windeployqt failed for ${_br_windows_deploy_target}: ${_br_windows_deploy_error}")
  endif()

  if(NOT _br_windows_deploy_output STREQUAL "")
    message(STATUS "${_br_windows_deploy_output}")
  endif()

  if(NOT _br_windows_deploy_error STREQUAL "")
    message(STATUS "${_br_windows_deploy_error}")
  endif()
endforeach()
