#ifndef WEBSTATICTRANSLATIONS_H
#define WEBSTATICTRANSLATIONS_H

#include <QCoreApplication>
#include <QVector>

class WebStaticTranslations final
{
    Q_DECLARE_TR_FUNCTIONS(WebStaticTranslations)

public:
    struct Entry
    {
        const char *key;
        const char *source;
    };

    [[nodiscard]] static QVector<Entry> entries();
    static void touchTranslations();
};

#endif // WEBSTATICTRANSLATIONS_H
