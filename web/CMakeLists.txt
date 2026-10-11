find_package(BookingResources REQUIRED COMPONENTS LocalCli SharedMemory Translator Application ApplicationLog)

set(BOOKING_RESOURCES_WEB_SOURCES
  src/main.cpp
  src/WebApplication.cpp
  src/WebApplication.h
  src/WebAuthManager.cpp
  src/WebAuthManager.h
  src/WebLocalCliCommands.cpp
  src/WebLocalCliCommands.h
  src/WebCommandLineParser.cpp
  src/WebCommandLineParser.h
  src/WebConfig.cpp
  src/WebConfig.h
  src/WebLog.cpp
  src/WebLog.h
  src/WebClient.cpp
  src/WebClient.h
  src/WebServer.cpp
  src/WebServer.h
  src/WebSessionStore.cpp
  src/WebSessionStore.h
  src/WebSecurityPolicy.cpp
  src/WebSecurityPolicy.h
  src/WebStaticTranslations.cpp
  src/WebStaticTranslations.h
  src/html/web.qrc
)

add_executable(booking-resources-web
  ${BOOKING_RESOURCES_WEB_SOURCES}
)

target_include_directories(booking-resources-web
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(booking-resources-web
  PRIVATE
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Network
  Qt${QT_VERSION_MAJOR}::Sql
  BookingResources::Application
  BookingResources::ApplicationLog
  BookingResources::LocalCli
  BookingResources::SharedMemory
  BookingResources::Translator
)

target_compile_definitions(booking-resources-web
  PRIVATE
  APP_NAME="${PROJECT_NAME}-Web"
  APP_VERSION="${PROJECT_VERSION}"
)

set(BOOKING_RESOURCES_WEB_TS
  ${CMAKE_CURRENT_SOURCE_DIR}/translations/booking-resources-web_en.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/translations/booking-resources-web_ru.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/translations/booking-resources-web_ru_stand.ts
)

booking_resources_setup_translations(
  TARGET booking-resources-web
  BASE_NAME "booking-resources-web"
  TS_FILES ${BOOKING_RESOURCES_WEB_TS}
  TRANSLATIONS_TARGET booking_resources_web_translations
  UPDATE_TARGET_NAME update_web_translations
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
)

include(GNUInstallDirs)
install(TARGETS booking-resources-web
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT web
)
