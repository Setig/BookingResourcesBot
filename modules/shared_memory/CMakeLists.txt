set(BOOKING_RESOURCES_SHARED_MEMORY_SOURCES
  src/SharedMemoryClient.cpp
  src/SharedMemoryClient.h
  src/SharedMemoryEndpoint.cpp
  src/SharedMemoryEndpoint.h
  src/SharedMemoryProtocol.cpp
  src/SharedMemoryProtocol.h
  src/SharedMemoryServer.cpp
  src/SharedMemoryServer.h
)

add_library(BookingResourcesSharedMemory STATIC
  ${BOOKING_RESOURCES_SHARED_MEMORY_SOURCES}
)

add_library(BookingResources::SharedMemory ALIAS BookingResourcesSharedMemory)

target_include_directories(BookingResourcesSharedMemory
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BookingResourcesSharedMemory
  PUBLIC
  Qt${QT_VERSION_MAJOR}::Core
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${BOOKING_RESOURCES_SHARED_MEMORY_SOURCES})
