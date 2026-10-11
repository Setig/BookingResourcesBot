find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

if(NOT COMMAND booking_resources_target_link_core)
  include(${PROJECT_SOURCE_DIR}/cmake/BookingResourcesCoreMacros.cmake)
endif()

if(NOT COMMAND booking_resources_setup_translations)
  include(${PROJECT_SOURCE_DIR}/cmake/BookingResourcesTranslatorMacros.cmake)
endif()

set(BOOKING_RESOURCES_WEB_TEST_SOURCES
  ${PROJECT_SOURCE_DIR}/web/src/WebAuthManager.cpp
  ${PROJECT_SOURCE_DIR}/web/src/WebAuthManager.h
  ${PROJECT_SOURCE_DIR}/web/src/WebClient.cpp
  ${PROJECT_SOURCE_DIR}/web/src/WebClient.h
  ${PROJECT_SOURCE_DIR}/web/src/WebCommandLineParser.cpp
  ${PROJECT_SOURCE_DIR}/web/src/WebCommandLineParser.h
  ${PROJECT_SOURCE_DIR}/web/src/WebConfig.cpp
  ${PROJECT_SOURCE_DIR}/web/src/WebConfig.h
  ${PROJECT_SOURCE_DIR}/web/src/WebLocalCliCommands.cpp
  ${PROJECT_SOURCE_DIR}/web/src/WebLocalCliCommands.h
  ${PROJECT_SOURCE_DIR}/web/src/WebSecurityPolicy.cpp
  ${PROJECT_SOURCE_DIR}/web/src/WebSecurityPolicy.h
  ${PROJECT_SOURCE_DIR}/web/src/WebServer.cpp
  ${PROJECT_SOURCE_DIR}/web/src/WebServer.h
  ${PROJECT_SOURCE_DIR}/web/src/WebSessionStore.cpp
  ${PROJECT_SOURCE_DIR}/web/src/WebSessionStore.h
  ${PROJECT_SOURCE_DIR}/web/src/WebStaticTranslations.cpp
  ${PROJECT_SOURCE_DIR}/web/src/WebStaticTranslations.h
  ${PROJECT_SOURCE_DIR}/web/src/html/web.qrc
)

add_library(BookingResourcesWebTestObjects OBJECT
  ${BOOKING_RESOURCES_WEB_TEST_SOURCES}
)

target_include_directories(BookingResourcesWebTestObjects
  PRIVATE
  ${PROJECT_SOURCE_DIR}/web/src
)

target_compile_definitions(BookingResourcesWebTestObjects
  PRIVATE
  APP_NAME="${PROJECT_NAME}-Web"
  APP_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(BookingResourcesWebTestObjects
  PRIVATE
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Network
  Qt${QT_VERSION_MAJOR}::Sql
  BookingResources::Application
  BookingResources::LocalCli
  BookingResources::SharedMemory
  BookingResources::Translator
)

set(BOOKING_RESOURCES_TEST_SOURCES
  Tests.cpp
  TestRegistry.h
  application/BookingResourcesApplicationTests.cpp
  application/ConsoleInterruptPolicyTests.cpp
  cli/CliCommandLineParserTests.cpp
  cli/CliConsoleInterruptPolicyTests.cpp
  cli/CliHistoryTests.cpp
  local_cli/LocalCliEndpointTests.cpp
  local_cli/LocalCliProtocolTests.cpp
  local_cli/LocalCliRegistryTests.cpp
  server/MessengerBotTests.cpp
  server/MessengerCommandParserTests.cpp
  server/ServerAntiSpamTests.cpp
  server/ServerConfigTests.cpp
  server/ServerDatabaseTests.cpp
  shared_memory/SharedMemoryEndpointTests.cpp
  shared_memory/SharedMemoryProtocolTests.cpp
  translator/BookingResourcesTranslatorTests.cpp
  web/WebAuthManagerTests.cpp
  web/WebCommandLineParserTests.cpp
  web/WebLocalCliCommandTests.cpp
  web/WebServerSecurityTests.cpp
  web/WebSessionStoreTests.cpp
)

add_executable(tests
  ${BOOKING_RESOURCES_TEST_SOURCES}
  ${PROJECT_SOURCE_DIR}/cli/src/CliCommandLineParser.cpp
  ${PROJECT_SOURCE_DIR}/cli/src/CliCommandLineParser.h
  ${PROJECT_SOURCE_DIR}/cli/src/CliConsoleInterruptPolicy.cpp
  ${PROJECT_SOURCE_DIR}/cli/src/CliConsoleInterruptPolicy.h
  ${PROJECT_SOURCE_DIR}/cli/src/CliHistory.cpp
  ${PROJECT_SOURCE_DIR}/cli/src/CliHistory.h
)

target_include_directories(tests
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/cli/src
  ${PROJECT_SOURCE_DIR}/web/src
)

booking_resources_target_link_core(tests)

target_link_libraries(tests
  PRIVATE
  BookingResourcesWebTestObjects
  BookingResources::Core
  BookingResources::Application
  BookingResources::LocalCli
  BookingResources::SharedMemory
  BookingResources::Translator
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Network
  Qt${QT_VERSION_MAJOR}::Sql
  Qt${QT_VERSION_MAJOR}::Test
)

target_compile_definitions(tests
  PRIVATE
  APP_VERSION="${PROJECT_VERSION}"
  WEB_APP_NAME="${PROJECT_NAME}-Web"
  WEB_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}/web"
)

set(BOOKING_RESOURCES_TRANSLATOR_TEST_TS
  ${CMAKE_CURRENT_SOURCE_DIR}/translator/translations/booking-resources-translator-tests_ru.ts
)

booking_resources_setup_translations(
  TARGET tests
  BASE_NAME "booking-resources-translator-tests"
  TS_FILES ${BOOKING_RESOURCES_TRANSLATOR_TEST_TS}
  TRANSLATIONS_TARGET booking_resources_translator_tests_translations
  UPDATE_TARGET_NAME update_translator_tests_translations
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/translator
)

set(BOOKING_RESOURCES_TEST_SUITES
  BookingResourcesApplicationTests
  BookingResourcesTranslatorTests
  CliCommandLineParserTests
  CliConsoleInterruptPolicyTests
  CliHistoryTests
  ConsoleInterruptPolicyTests
  LocalCliEndpointTests
  LocalCliProtocolTests
  LocalCliRegistryTests
  MessengerBotTests
  MessengerCommandParserTests
  ServerAntiSpamTests
  ServerConfigTests
  ServerDatabaseTests
  SharedMemoryEndpointTests
  SharedMemoryProtocolTests
  WebAuthManagerTests
  WebCommandLineParserTests
  WebLocalCliCommandTests
  WebServerSecurityTests
  WebSessionStoreTests
)

add_test(NAME tests COMMAND tests)
foreach(_br_suite IN LISTS BOOKING_RESOURCES_TEST_SUITES)
  add_test(NAME ${_br_suite} COMMAND tests ${_br_suite})
endforeach()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
  ${BOOKING_RESOURCES_TEST_SOURCES}
)
