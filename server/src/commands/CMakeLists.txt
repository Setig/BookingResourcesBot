set(BOOKING_RESOURCES_COMMANDS_SOURCES
  MessengerCommandAudience.h
  MessengerCommandStaticRegistration.h
  MessengerAbstractCommand.cpp
  MessengerAbstractCommand.h
  MessengerStartCommand.cpp
  MessengerStartCommand.h
  MessengerHelpCommand.cpp
  MessengerHelpCommand.h
  MessengerServerSettingsCommand.cpp
  MessengerServerSettingsCommand.h
  MessengerOccupationDurationsCommand.cpp
  MessengerOccupationDurationsCommand.h
  MessengerUserSettingsCommand.cpp
  MessengerUserSettingsCommand.h
  MessengerMenuCommand.cpp
  MessengerMenuCommand.h
  MessengerStatisticsCommand.cpp
  MessengerStatisticsCommand.h
  MessengerSendNotificationCommand.cpp
  MessengerSendNotificationCommand.h
  MessengerAddUserCommand.cpp
  MessengerAddUserCommand.h
  MessengerUpdateUserCommand.cpp
  MessengerUpdateUserCommand.h
  MessengerDeleteUserCommand.cpp
  MessengerDeleteUserCommand.h
  MessengerBanUserCommand.cpp
  MessengerBanUserCommand.h
  MessengerUnbanUserCommand.cpp
  MessengerUnbanUserCommand.h
  MessengerBlacklistCommand.cpp
  MessengerBlacklistCommand.h
  MessengerAddResourceCommand.cpp
  MessengerAddResourceCommand.h
  MessengerUpdateResourceCommand.cpp
  MessengerUpdateResourceCommand.h
  MessengerDeleteResourceCommand.cpp
  MessengerDeleteResourceCommand.h
  MessengerListUsersCommand.cpp
  MessengerListUsersCommand.h
  MessengerListPendingAccessUsersCommand.cpp
  MessengerListPendingAccessUsersCommand.h
  MessengerListResourcesCommand.cpp
  MessengerListResourcesCommand.h
  MessengerQueueCommand.cpp
  MessengerQueueCommand.h
  MessengerOccupyCommand.cpp
  MessengerOccupyCommand.h
  MessengerScheduleCommand.cpp
  MessengerScheduleCommand.h
  MessengerUnscheduleCommand.cpp
  MessengerUnscheduleCommand.h
  MessengerReleaseCommand.cpp
  MessengerReleaseCommand.h
  MessengerReleaseAllCommand.cpp
  MessengerReleaseAllCommand.h
  MessengerFreeResourcesCommand.cpp
  MessengerFreeResourcesCommand.h
  MessengerSoonestCommand.cpp
  MessengerSoonestCommand.h
)

list(TRANSFORM BOOKING_RESOURCES_COMMANDS_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")

set(BOOKING_RESOURCES_COMMANDS_SOURCES ${BOOKING_RESOURCES_COMMANDS_SOURCES} PARENT_SCOPE)


