set(BOOKING_RESOURCES_APPLICATION_SOURCES
  src/BookingResourcesApplication.cpp
  src/BookingResourcesApplication.h
  src/ConsoleInterruptPolicy.cpp
  src/ConsoleInterruptPolicy.h
  src/LayeredIniSettings.cpp
  src/LayeredIniSettings.h
)

add_library(BookingResourcesApplication STATIC
  ${BOOKING_RESOURCES_APPLICATION_SOURCES}
)

add_library(BookingResources::Application ALIAS BookingResourcesApplication)

target_include_directories(BookingResourcesApplication
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BookingResourcesApplication
  PUBLIC
  Qt${QT_VERSION_MAJOR}::Core
)

if(WIN32)
  target_link_libraries(BookingResourcesApplication
    PUBLIC
    user32
  )
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${BOOKING_RESOURCES_APPLICATION_SOURCES})
