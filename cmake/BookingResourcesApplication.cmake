include_guard(GLOBAL)

if(TARGET BookingResources::Application)
  return()
endif()

get_filename_component(_booking_resources_application_root_dir "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(_booking_resources_application_root "${_booking_resources_application_root_dir}/modules/application")

add_subdirectory(
  "${_booking_resources_application_root}"
  "${CMAKE_CURRENT_BINARY_DIR}/booking_resources_application"
)

if(NOT TARGET BookingResources::Application)
  message(FATAL_ERROR "BookingResources::Application alias was not created by modules/application/CMakeLists.txt")
endif()
