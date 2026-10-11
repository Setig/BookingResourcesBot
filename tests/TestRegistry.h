#ifndef BOOKING_RESOURCES_TEST_REGISTRY_H
#define BOOKING_RESOURCES_TEST_REGISTRY_H

#include <QObject>
#include <QString>
#include <QVector>

/**
 * @brief Static Qt Test suite registry for the unified tests executable.
 *
 * Each suite translation unit registers a factory during static initialization.
 * Tests.cpp then instantiates suites by name or runs every registered factory.
 */
class TestRegistry
{
public:
    using Factory = QObject *(*)();

    struct Entry
    {
        QString name;
        Factory factory;
    };

    static void add(const QString &name, Factory factory)
    {
        if (name.isEmpty() || !factory)
            return;

        QVector<Entry> &entries = registry();
        for (const Entry &entry : entries) {
            if (entry.name == name)
                return;
        }

        Entry entry;
        entry.name = name;
        entry.factory = factory;
        entries.push_back(entry);
    }

    static const QVector<Entry> &all() { return registry(); }

private:
    static QVector<Entry> &registry()
    {
        static QVector<Entry> entries;
        return entries;
    }
};

/**
 * Registers a QObject Qt Test suite constructed with the default constructor.
 *
 * @param ClassName Concrete test class derived from QObject.
 */
#define BOOKING_RESOURCES_REGISTER_TEST(ClassName)                          \
    namespace BookingResourcesTestRegistration {                            \
    inline QObject *create_##ClassName()                                    \
    {                                                                       \
        return new ClassName;                                               \
    }                                                                       \
    [[maybe_unused]] inline const bool registered_##ClassName = []() {      \
        TestRegistry::add(QStringLiteral(#ClassName), &create_##ClassName); \
        return true;                                                        \
    }();                                                                    \
    } // namespace BookingResourcesTestRegistration

#endif // BOOKING_RESOURCES_TEST_REGISTRY_H
