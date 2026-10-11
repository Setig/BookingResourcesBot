set(_BookingResources_known_components LocalCli SharedMemory Translator Application ApplicationLog Core)

if(NOT BookingResources_FIND_COMPONENTS)
  set(BookingResources_FIND_COMPONENTS LocalCli)
endif()

set(_BookingResources_missing_components)

foreach(_BookingResources_component IN LISTS BookingResources_FIND_COMPONENTS)
  list(FIND _BookingResources_known_components "${_BookingResources_component}"
       _BookingResources_component_index)
  if(_BookingResources_component_index EQUAL -1)
    set(BookingResources_${_BookingResources_component}_FOUND FALSE)
    list(APPEND _BookingResources_missing_components "${_BookingResources_component}")
    continue()
  endif()

  if(_BookingResources_component STREQUAL "LocalCli")
    include("${CMAKE_CURRENT_LIST_DIR}/BookingResourcesLocalCli.cmake")
    set(BookingResources_${_BookingResources_component}_FOUND TRUE)
  elseif(_BookingResources_component STREQUAL "SharedMemory")
    include("${CMAKE_CURRENT_LIST_DIR}/BookingResourcesSharedMemory.cmake")
    set(BookingResources_${_BookingResources_component}_FOUND TRUE)
  elseif(_BookingResources_component STREQUAL "Translator")
    include("${CMAKE_CURRENT_LIST_DIR}/BookingResourcesTranslator.cmake")
    set(BookingResources_${_BookingResources_component}_FOUND TRUE)
  elseif(_BookingResources_component STREQUAL "Application")
    include("${CMAKE_CURRENT_LIST_DIR}/BookingResourcesApplication.cmake")
    set(BookingResources_${_BookingResources_component}_FOUND TRUE)
  elseif(_BookingResources_component STREQUAL "ApplicationLog")
    include("${CMAKE_CURRENT_LIST_DIR}/BookingResourcesApplicationLog.cmake")
    set(BookingResources_${_BookingResources_component}_FOUND TRUE)
  elseif(_BookingResources_component STREQUAL "Core")
    include("${CMAKE_CURRENT_LIST_DIR}/BookingResourcesCore.cmake")
    set(BookingResources_${_BookingResources_component}_FOUND TRUE)
  endif()
endforeach()

if(_BookingResources_missing_components)
  set(BookingResources_NOT_FOUND_MESSAGE
      "Unknown BookingResources component(s): ${_BookingResources_missing_components}")
endif()

set(_BookingResources_required_vars)
foreach(_BookingResources_component IN LISTS BookingResources_FIND_COMPONENTS)
  list(APPEND _BookingResources_required_vars
       "BookingResources_${_BookingResources_component}_FOUND")
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BookingResources
  REQUIRED_VARS ${_BookingResources_required_vars}
  HANDLE_COMPONENTS
)
