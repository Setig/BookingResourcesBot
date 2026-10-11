#ifndef MESSENGER_MENU_STATIC_REGISTRATION_H
#define MESSENGER_MENU_STATIC_REGISTRATION_H

#include <QVector>

class MessengerMenuManager;
class MessengerAbstractMenu;

class MessengerUserResourcesState;
class MessengerAdminUsersState;
class MessengerAdminResourcesState;

/**
 * @brief Dependencies shared across multiple menus.
 *
 * MessengerMenuManager creates these objects once and passes pointers to menu factories.
 */
struct MessengerMenuStaticContext
{
    MessengerUserResourcesState *userResourcesState = nullptr;
    MessengerAdminUsersState *adminUsersState = nullptr;
    MessengerAdminResourcesState *adminResourcesState = nullptr;
};

/**
 * @brief Static menu factory registry.
 *
 * Menu implementations register a factory during static initialization (typically from their .cpp
 * translation unit). MessengerMenuManager then instantiates menus by iterating the factories.
 *
 * Note: when linked from a static library, consumers must ensure the object files containing
 * registrations are not dropped by the linker (e.g. via whole-archive / whole-program linking).
 */
class MessengerMenuStaticRegistry
{
public:
    using Factory = MessengerAbstractMenu *(*)(MessengerMenuManager * manager,
                                               const MessengerMenuStaticContext &context);

    static void registerFactory(Factory factory)
    {
        if (!factory)
            return;

        QVector<Factory> &factories = registry();
        if (factories.contains(factory))
            return;

        factories.push_back(factory);
    }

    static const QVector<Factory> &factories() { return registry(); }

private:
    static QVector<Factory> &registry()
    {
        static QVector<Factory> factories;
        return factories;
    }
};

#define BOOKING_RESOURCES_REGISTER_MENU(MenuClass)                                       \
    namespace BookingResourcesMenuRegistration {                                         \
    inline MessengerAbstractMenu *create_##MenuClass(MessengerMenuManager *manager,      \
                                                     const MessengerMenuStaticContext &) \
    {                                                                                    \
        return new MenuClass(manager);                                                   \
    }                                                                                    \
    [[maybe_unused]] inline const bool registered_##MenuClass = []() {                   \
        MessengerMenuStaticRegistry::registerFactory(&create_##MenuClass);               \
        return true;                                                                     \
    }();                                                                                 \
    } // namespace BookingResourcesMenuRegistration

#define BOOKING_RESOURCES_REGISTER_MENU_USER_RESOURCES_STATE(MenuClass)                         \
    namespace BookingResourcesMenuRegistration {                                                \
    inline MessengerAbstractMenu *create_##MenuClass(MessengerMenuManager *manager,             \
                                                     const MessengerMenuStaticContext &context) \
    {                                                                                           \
        if (!context.userResourcesState)                                                        \
            return nullptr;                                                                     \
        return new MenuClass(manager, context.userResourcesState);                              \
    }                                                                                           \
    [[maybe_unused]] inline const bool registered_##MenuClass = []() {                          \
        MessengerMenuStaticRegistry::registerFactory(&create_##MenuClass);                      \
        return true;                                                                            \
    }();                                                                                        \
    } // namespace BookingResourcesMenuRegistration

#define BOOKING_RESOURCES_REGISTER_MENU_ADMIN_USERS_STATE(MenuClass)                            \
    namespace BookingResourcesMenuRegistration {                                                \
    inline MessengerAbstractMenu *create_##MenuClass(MessengerMenuManager *manager,             \
                                                     const MessengerMenuStaticContext &context) \
    {                                                                                           \
        if (!context.adminUsersState)                                                           \
            return nullptr;                                                                     \
        return new MenuClass(manager, context.adminUsersState);                                 \
    }                                                                                           \
    [[maybe_unused]] inline const bool registered_##MenuClass = []() {                          \
        MessengerMenuStaticRegistry::registerFactory(&create_##MenuClass);                      \
        return true;                                                                            \
    }();                                                                                        \
    } // namespace BookingResourcesMenuRegistration

#define BOOKING_RESOURCES_REGISTER_MENU_ADMIN_RESOURCES_STATE(MenuClass)                        \
    namespace BookingResourcesMenuRegistration {                                                \
    inline MessengerAbstractMenu *create_##MenuClass(MessengerMenuManager *manager,             \
                                                     const MessengerMenuStaticContext &context) \
    {                                                                                           \
        if (!context.adminResourcesState)                                                       \
            return nullptr;                                                                     \
        return new MenuClass(manager, context.adminResourcesState);                             \
    }                                                                                           \
    [[maybe_unused]] inline const bool registered_##MenuClass = []() {                          \
        MessengerMenuStaticRegistry::registerFactory(&create_##MenuClass);                      \
        return true;                                                                            \
    }();                                                                                        \
    } // namespace BookingResourcesMenuRegistration

#endif // MESSENGER_MENU_STATIC_REGISTRATION_H
