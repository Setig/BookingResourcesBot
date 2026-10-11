set(BOOKING_RESOURCES_LOCAL_CLI_SOURCES
  src/LocalCliEndpoint.cpp
  src/LocalCliEndpoint.h
  src/LocalCliHelper.cpp
  src/LocalCliHelper.h
  src/LocalCliProtocol.cpp
  src/LocalCliProtocol.h
  src/LocalCliRegistry.cpp
  src/LocalCliRegistry.h
)

add_library(BookingResourcesLocalCli STATIC
  ${BOOKING_RESOURCES_LOCAL_CLI_SOURCES}
)

add_library(BookingResources::LocalCli ALIAS BookingResourcesLocalCli)

target_include_directories(BookingResourcesLocalCli
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BookingResourcesLocalCli
  PUBLIC
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Network
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${BOOKING_RESOURCES_LOCAL_CLI_SOURCES})
