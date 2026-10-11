#ifndef OCCUPATIONDURATIONMODEL_H
#define OCCUPATIONDURATIONMODEL_H

#include "AbstractServerDatabaseModel.h"

/**
 * @brief Simple list model that stores allowed occupation durations in minutes.
 */
class OccupationDurationModel : public AbstractServerDatabaseModel<int, int>
{
    Q_OBJECT

public:
    enum Roles {
        MinutesRole = Qt::UserRole + 1
    };

    explicit OccupationDurationModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replaceAll(const QList<int> &minutes);

    const QList<int> &minutes() const;
    QList<int> occupationDurations() const;

protected:
    int keyFromEntry(const int &minutes) const override;
    bool lessThan(const int &lhs, const int &rhs) const override;
};

#endif // OCCUPATIONDURATIONMODEL_H
