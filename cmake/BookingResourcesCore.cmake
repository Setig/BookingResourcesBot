include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/BookingResourcesCoreMacros.cmake")

if(TARGET BookingResources::Core)
  return()
endif()

if(NOT TARGET BookingResourcesCore)
  message(FATAL_ERROR "BookingResourcesCore target was not created yet.")
endif()

add_library(BookingResources::Core ALIAS BookingResourcesCore)


