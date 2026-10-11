#include "OccupationDurationModel.h"

using Base = AbstractServerDatabaseModel<int, int>;

OccupationDurationModel::OccupationDurationModel(QObject *parent)
    : Base(InsertPolicy::Sorted, parent)
{
    // do nothing
}

QVariant OccupationDurationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entryCount())
        return QVariant();

    if (role == MinutesRole)
        return entryAt(index.row());

    return QVariant();
}

QHash<int, QByteArray> OccupationDurationModel::roleNames() const
{
    return {
        { MinutesRole, QByteArrayLiteral("minutes") },
    };
}

void OccupationDurationModel::replaceAll(const QList<int> &minutes)
{
    QList<int> normalized = minutes;
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());

    replaceAllEntries(normalized);
}

const QList<int> &OccupationDurationModel::minutes() const
{
    return entriesRef();
}

QList<int> OccupationDurationModel::occupationDurations() const
{
    return entriesCopy();
}

int OccupationDurationModel::keyFromEntry(const int &minutes) const
{
    return minutes;
}

bool OccupationDurationModel::lessThan(const int &lhs, const int &rhs) const
{
    return lhs < rhs;
}
