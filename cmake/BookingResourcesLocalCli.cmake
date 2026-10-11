include_guard(GLOBAL)

if (TARGET BookingResources::LocalCli)
  return()
endif()

get_filename_component(_booking_resources_local_cli_root_dir "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(_booking_resources_local_cli_root "${_booking_resources_local_cli_root_dir}/modules/local_cli")

add_subdirectory(
  "${_booking_resources_local_cli_root}"
  "${CMAKE_CURRENT_BINARY_DIR}/booking_resources_local_cli"
)

if (NOT TARGET BookingResources::LocalCli)
  message(FATAL_ERROR "BookingResources::LocalCli alias was not created by modules/local_cli/CMakeLists.txt")
endif()
