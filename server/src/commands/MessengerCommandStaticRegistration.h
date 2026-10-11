#ifndef MESSENGER_COMMAND_STATIC_REGISTRATION_H
#define MESSENGER_COMMAND_STATIC_REGISTRATION_H

#include <QVector>

class MessengerCommandManager;
class MessengerAbstractCommand;

/**
 * @brief Static command factory registry.
 *
 * Command implementations register a factory during static initialization (typically from their
 * .cpp translation unit). MessengerCommandManager then instantiates commands by iterating the
 * factories.
 *
 * Note: when linked from a static library, consumers must ensure the object files containing
 * registrations are not dropped by the linker (e.g. via whole-archive / whole-program linking).
 */
class MessengerCommandStaticRegistry
{
public:
    using Factory = MessengerAbstractCommand *(*)(MessengerCommandManager * manager);

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

/**
 * Registers a command with a factory that always constructs the command instance.
 *
 * @param CommandClass Concrete command class with constructor (MessengerCommandManager*).
 */
#define BOOKING_RESOURCES_REGISTER_COMMAND(CommandClass)                                     \
    namespace BookingResourcesCommandRegistration {                                          \
    inline MessengerAbstractCommand *create_##CommandClass(MessengerCommandManager *manager) \
    {                                                                                        \
        return new CommandClass(manager);                                                    \
    }                                                                                        \
    [[maybe_unused]] inline const bool registered_##CommandClass = []() {                    \
        MessengerCommandStaticRegistry::registerFactory(&create_##CommandClass);             \
        return true;                                                                         \
    }();                                                                                     \
    } // namespace BookingResourcesCommandRegistration

/**
 * Registers a command with a runtime condition (factory returns nullptr when disabled).
 *
 * @param ConditionExpr Expression that can use the local variable `manager`.
 */
#define BOOKING_RESOURCES_REGISTER_COMMAND_IF(CommandClass, ConditionExpr)                   \
    namespace BookingResourcesCommandRegistration {                                          \
    inline MessengerAbstractCommand *create_##CommandClass(MessengerCommandManager *manager) \
    {                                                                                        \
        if (!(ConditionExpr))                                                                \
            return nullptr;                                                                  \
        return new CommandClass(manager);                                                    \
    }                                                                                        \
    [[maybe_unused]] inline const bool registered_##CommandClass = []() {                    \
        MessengerCommandStaticRegistry::registerFactory(&create_##CommandClass);             \
        return true;                                                                         \
    }();                                                                                     \
    } // namespace BookingResourcesCommandRegistration

#endif // MESSENGER_COMMAND_STATIC_REGISTRATION_H
