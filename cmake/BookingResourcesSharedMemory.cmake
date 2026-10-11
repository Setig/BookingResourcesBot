include_guard(GLOBAL)

if (TARGET BookingResources::SharedMemory)
  return()
endif()

get_filename_component(_booking_resources_shared_memory_root_dir "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(_booking_resources_shared_memory_root "${_booking_resources_shared_memory_root_dir}/modules/shared_memory")

add_subdirectory(
  "${_booking_resources_shared_memory_root}"
  "${CMAKE_CURRENT_BINARY_DIR}/booking_resources_shared_memory"
)

if (NOT TARGET BookingResources::SharedMemory)
  message(FATAL_ERROR "BookingResources::SharedMemory alias was not created by modules/shared_memory/CMakeLists.txt")
endif()
