find_package(BookingResources REQUIRED COMPONENTS LocalCli Translator)

set(BOOKING_RESOURCES_CLI_SOURCES
  src/CliCommandLineParser.cpp
  src/CliCommandLineParser.h
  src/CliCompletion.cpp
  src/CliCompletion.h
  src/CliConsoleInterruptPolicy.cpp
  src/CliConsoleInterruptPolicy.h
  src/CliHistory.cpp
  src/CliHistory.h
  src/CliLineEditor.cpp
  src/CliLineEditor.h
  src/main.cpp
  src/LocalCliClient.cpp
  src/LocalCliClient.h
)

add_executable(booking-resources-cli
  ${BOOKING_RESOURCES_CLI_SOURCES}
)

target_include_directories(booking-resources-cli
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(booking-resources-cli
  PRIVATE
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Network
  BookingResources::Translator
  BookingResources::LocalCli
)

target_compile_definitions(booking-resources-cli
  PRIVATE
  APP_NAME="${PROJECT_NAME}"
  APP_VERSION="${PROJECT_VERSION}"
)

set(BOOKING_RESOURCES_CLI_TS
  ${CMAKE_CURRENT_SOURCE_DIR}/translations/booking-resources-cli_en.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/translations/booking-resources-cli_ru.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/translations/booking-resources-cli_ru_stand.ts
)

booking_resources_setup_translations(
  TARGET booking-resources-cli
  BASE_NAME "booking-resources-cli"
  TS_FILES ${BOOKING_RESOURCES_CLI_TS}
  TRANSLATIONS_TARGET booking_resources_cli_translations
  UPDATE_TARGET_NAME update_cli_translations
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
)

include(GNUInstallDirs)
install(TARGETS booking-resources-cli
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT cli
)
