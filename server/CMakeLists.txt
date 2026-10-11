find_package(BookingResources REQUIRED COMPONENTS LocalCli SharedMemory Translator Application ApplicationLog)

add_subdirectory(src/bots)
add_subdirectory(src/commands)
add_subdirectory(src/menus)

set(BOOKING_RESOURCES_CORE_SOURCES
  src/ServerLog.cpp
  src/ServerLog.h
  src/ServerConfig.cpp
  src/ServerConfig.h
  src/ServerSettingEntry.cpp
  src/ServerSettingEntry.h
  src/ServerSettingsModel.cpp
  src/ServerSettingsModel.h
  src/ServerRuntimeSettings.cpp
  src/ServerRuntimeSettings.h
  src/ServerDateTimeFormat.cpp
  src/ServerDateTimeFormat.h
  src/UserTimeZone.cpp
  src/UserTimeZone.h
  src/AbstractServerDatabaseModel.cpp
  src/AbstractServerDatabaseModel.h
  src/ServerDatabaseError.cpp
  src/ServerDatabaseError.h
  src/ServerDatabase.cpp
  src/ServerDatabase.h
  src/ServerDatabase_p.cpp
  src/ServerDatabase_p.h
  src/ServerStats.cpp
  src/ServerStats.h
  src/ServerStats_p.h
  src/ServerStatsReportWorker.cpp
  src/ServerStatsReportWorker.h
  src/Group.cpp
  src/Group.h
  src/Resource.cpp
  src/Resource.h
  src/User.cpp
  src/User.h
  src/BlacklistEntry.cpp
  src/BlacklistEntry.h
  src/BlacklistModel.cpp
  src/BlacklistModel.h
  src/UserModel.cpp
  src/UserModel.h
  src/ResourceModel.cpp
  src/ResourceModel.h
  src/OccupationDurationModel.cpp
  src/OccupationDurationModel.h
  src/ResourceQueueModel.cpp
  src/ResourceQueueModel.h
  src/ResourceQueueUserMessages.cpp
  src/ResourceQueueUserMessages.h
  src/MessengerUserFacingErrors.cpp
  src/MessengerUserFacingErrors.h
  src/PendingAccessUserEntry.cpp
  src/PendingAccessUserEntry.h
  src/PendingAccessUserModel.cpp
  src/PendingAccessUserModel.h
  src/MessengerUserKey.cpp
  src/MessengerUserKey.h
  src/ResourceQueueTypes.cpp
  src/ResourceQueueTypes.h
  src/MessengerType.h
  src/ServerCommandLineParser.cpp
  src/ServerCommandLineParser.h
  src/AbstractBot.cpp
  src/AbstractBot.h
  src/MessengerCommandParser.cpp
  src/MessengerCommandParser.h
  src/MessengerCommandCompletions.cpp
  src/MessengerCommandCompletions.h
  src/MessengerCommandManager.cpp
  src/MessengerCommandManager.h
  src/MessengerTextRender.cpp
  src/MessengerTextRender.h
  src/MessengerLimiter.cpp
  src/MessengerLimiter.h
  src/MessengerBot.cpp
  src/MessengerBot.h
  src/MessengerBotPoll.cpp
  src/MessengerBotPoll.h
  src/MessengerMenuManager.cpp
  src/MessengerMenuManager.h
)

list(APPEND BOOKING_RESOURCES_CORE_SOURCES ${BOOKING_RESOURCES_BOTS_SOURCES})
list(APPEND BOOKING_RESOURCES_CORE_SOURCES ${BOOKING_RESOURCES_COMMANDS_SOURCES})
list(APPEND BOOKING_RESOURCES_CORE_SOURCES ${BOOKING_RESOURCES_MENUS_SOURCES})

add_library(BookingResourcesCore STATIC
  ${BOOKING_RESOURCES_CORE_SOURCES}
)

find_package(BookingResources REQUIRED COMPONENTS Core)

target_include_directories(BookingResourcesCore
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bots
  ${CMAKE_CURRENT_SOURCE_DIR}/src/menus
  ${CMAKE_CURRENT_SOURCE_DIR}/src/commands
)

target_compile_definitions(BookingResourcesCore
  PUBLIC
  APP_NAME="${PROJECT_NAME}-Server"
  APP_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(BookingResourcesCore
  PUBLIC
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Network
  Qt${QT_VERSION_MAJOR}::Sql
  BookingResources::Application
  BookingResources::ApplicationLog
  BookingResources::Translator
  BookingResources::LocalCli
  BookingResources::SharedMemory
)

add_executable(booking-resources-server
  src/main.cpp
  src/ServerApplication.cpp
  src/ServerApplication.h
)

booking_resources_target_link_core(booking-resources-server)

target_link_libraries(booking-resources-server
  PRIVATE
  BookingResources::Application
)

set(BOOKING_RESOURCES_TRANSLATION_TS
  ${CMAKE_CURRENT_SOURCE_DIR}/translations/booking-resources-server_en.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/translations/booking-resources-server_ru.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/translations/booking-resources-server_ru_stand.ts
)

booking_resources_setup_translations(
  TARGET booking-resources-server
  BASE_NAME "booking-resources-server"
  TS_FILES ${BOOKING_RESOURCES_TRANSLATION_TS}
  TRANSLATIONS_TARGET booking_resources_server_translations
  UPDATE_TARGET_NAME update_server_translations
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
)

include(GNUInstallDirs)
install(TARGETS booking-resources-server
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT server
)
