include_guard(GLOBAL)

if(TARGET BookingResources::ApplicationLog)
  return()
endif()

get_filename_component(_booking_resources_application_log_root_dir "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(_booking_resources_application_log_root
    "${_booking_resources_application_log_root_dir}/modules/application_log")

add_subdirectory(
  "${_booking_resources_application_log_root}"
  "${CMAKE_CURRENT_BINARY_DIR}/booking_resources_application_log"
)

if(NOT TARGET BookingResources::ApplicationLog)
  message(FATAL_ERROR "BookingResources::ApplicationLog alias was not created by modules/application_log/CMakeLists.txt")
endif()
