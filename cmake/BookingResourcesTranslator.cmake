include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/BookingResourcesTranslatorMacros.cmake")

if(TARGET BookingResources::Translator)
  return()
endif()

get_filename_component(_booking_resources_translator_root_dir "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(_booking_resources_translator_root "${_booking_resources_translator_root_dir}/modules/translator")

add_subdirectory(
  "${_booking_resources_translator_root}"
  "${CMAKE_CURRENT_BINARY_DIR}/booking_resources_translator"
)

if(NOT TARGET BookingResources::Translator)
  message(FATAL_ERROR "BookingResources::Translator alias was not created by modules/translator/CMakeLists.txt")
endif()

