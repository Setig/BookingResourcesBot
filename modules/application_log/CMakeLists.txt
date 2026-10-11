set(BOOKING_RESOURCES_APPLICATION_LOG_SOURCES
  src/ApplicationLog.cpp
  src/ApplicationLog.h
)

add_library(BookingResourcesApplicationLog STATIC
  ${BOOKING_RESOURCES_APPLICATION_LOG_SOURCES}
)

add_library(BookingResources::ApplicationLog ALIAS BookingResourcesApplicationLog)

target_include_directories(BookingResourcesApplicationLog
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BookingResourcesApplicationLog
  PUBLIC
  Qt${QT_VERSION_MAJOR}::Core
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${BOOKING_RESOURCES_APPLICATION_LOG_SOURCES})
