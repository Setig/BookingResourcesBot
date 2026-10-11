cmake_minimum_required(VERSION 3.16)

project(BookingResourcesBot VERSION 1.0.1 LANGUAGES CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(GNUInstallDirs)
include(CTest)

option(BOOKING_RESOURCES_ENABLE_PACKAGING "Enable CPack packaging support." ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network Sql LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Sql LinguistTools)

add_subdirectory(modules)

add_subdirectory(server)
add_subdirectory(cli)
add_subdirectory(web)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(BOOKING_RESOURCES_ENABLE_PACKAGING)
  include(cmake/BookingResourcesPackaging.cmake)
  include(CPack)
endif()
