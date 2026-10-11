set(BOOKING_RESOURCES_TRANSLATOR_SOURCES
  src/BookingResourcesTranslator.cpp
  src/BookingResourcesTranslator.h
)

add_library(BookingResourcesTranslator STATIC
  ${BOOKING_RESOURCES_TRANSLATOR_SOURCES}
)

add_library(BookingResources::Translator ALIAS BookingResourcesTranslator)

target_include_directories(BookingResourcesTranslator
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BookingResourcesTranslator
  PUBLIC
  Qt${QT_VERSION_MAJOR}::Core
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${BOOKING_RESOURCES_TRANSLATOR_SOURCES})
