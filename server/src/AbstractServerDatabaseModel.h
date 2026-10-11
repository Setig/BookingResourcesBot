#ifndef ABSTRACTSERVERDATABASEMODEL_H
#define ABSTRACTSERVERDATABASEMODEL_H

#include <QAbstractListModel>

#include <QList>
#include <QHash>
#include <QDateTime>

/**
 * @brief Common base for server list models.
 *
 * Provides the shared errorOccurred signal used by models that are backed by persistent storage.
 */
class AbstractServerDatabaseModelBase : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit AbstractServerDatabaseModelBase(QObject *parent = nullptr);
    ~AbstractServerDatabaseModelBase() override;

signals:
    void errorOccurred(const QString &description);

protected:
    /**
     * @brief Returns true if @p lhs represents a more recent (valid) timestamp than @p rhs.
     */
    [[nodiscard]] static bool isMoreRecent(const QDateTime &lhs, const QDateTime &rhs)
    {
        if (!lhs.isValid())
            return false;
        if (!rhs.isValid())
            return true;
        return lhs > rhs;
    }
};

/**
 * @brief Generic list model with key-based lookup and optional persistence hooks.
 *
 * Stores entries in memory, keeps a key->row index, and provides helpers for replace/upsert/remove
 * operations. Derived models supply key extraction and can optionally normalize entries and define
 * a sorting order.
 *
 * @tparam EntryType Value type stored by the model.
 * @tparam KeyType Key type used to uniquely identify entries.
 */
template <typename EntryType, typename KeyType>
class AbstractServerDatabaseModel : public AbstractServerDatabaseModelBase
{
public:
    /**
     * @brief Storage callbacks used to persist entry changes.
     */
    struct StorageAdapter
    {
        std::function<bool(const EntryType &entry)> upsert;
        std::function<bool(const KeyType &key)> remove;
    };

    /**
     * @brief Specifies how new entries are inserted into the model.
     */
    enum class InsertPolicy {
        PreserveInsertionOrder,
        Sorted
    };

protected:
    explicit AbstractServerDatabaseModel(InsertPolicy policy, QObject *parent = nullptr)
        : AbstractServerDatabaseModelBase(parent)
        , m_policy(policy)
    {
        // do nothing
    }

    void setStorageAdapter(StorageAdapter adapter) { m_storage = std::move(adapter); }

public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return m_items.size();
    }

    void replaceAllEntries(const QList<EntryType> &entries)
    {
        beginResetModel();
        m_items = entries;

        if (m_policy == InsertPolicy::Sorted) {
            std::sort(m_items.begin(),
                      m_items.end(),
                      [this](const EntryType &lhs, const EntryType &rhs) {
                          return lessThan(lhs, rhs);
                      });
        }

        rebuildIndexFrom(0);
        endResetModel();
        onEntriesReset(m_items);
    }

    bool upsertEntry(const EntryType &entry)
    {
        const KeyType key = keyFromEntry(entry);
        const int existingIndex = indexOfKey(key);

        if (existingIndex >= 0) {
            const KeyType previousKey = keyFromEntry(m_items.at(existingIndex));
            m_items[existingIndex] = entry;
            if (!(previousKey == key)) {
                m_indexByKey.remove(previousKey);
                m_indexByKey.insert(key, existingIndex);
            }
            const QModelIndex modelIndex = index(existingIndex, 0);
            emit dataChanged(modelIndex, modelIndex);
            onEntryUpdated(existingIndex, entry);
            return true;
        }

        const int insertIndex =
                m_policy == InsertPolicy::Sorted ? findInsertIndex(entry) : m_items.size();
        beginInsertRows(QModelIndex(), insertIndex, insertIndex);
        m_items.insert(insertIndex, entry);
        endInsertRows();
        rebuildIndexFrom(insertIndex);
        onEntryInserted(insertIndex, entry);
        return true;
    }

    bool removeEntryByKey(const KeyType &key)
    {
        const int row = indexOfKey(key);
        if (row < 0)
            return false;

        const EntryType removed = m_items.at(row);
        beginRemoveRows(QModelIndex(), row, row);
        m_items.removeAt(row);
        endRemoveRows();
        rebuildIndexFrom(row);
        onEntryRemoved(row, removed);
        return true;
    }

    bool containsKey(const KeyType &key) const { return m_indexByKey.contains(key); }

    EntryType entryByKey(const KeyType &key) const
    {
        const int row = indexOfKey(key);
        if (row < 0)
            return EntryType();
        return m_items.at(row);
    }

    const QList<EntryType> &entriesRef() const { return m_items; }

    QList<EntryType> entriesCopy() const { return m_items; }

    int indexOfKey(const KeyType &key) const
    {
        const typename QHash<KeyType, int>::const_iterator it = m_indexByKey.constFind(key);
        if (it == m_indexByKey.cend())
            return -1;
        return it.value();
    }

    bool persistUpsert(const EntryType &entry) const
    {
        return !m_storage.upsert || m_storage.upsert(entry);
    }

    bool persistRemove(const KeyType &key) const
    {
        return !m_storage.remove || m_storage.remove(key);
    }

    EntryType &mutableEntryAt(int row)
    {
        Q_ASSERT(row >= 0 && row < m_items.size());
        return m_items[row];
    }

    const EntryType &entryAt(int row) const
    {
        Q_ASSERT(row >= 0 && row < m_items.size());
        return m_items.at(row);
    }

    int entryCount() const { return m_items.size(); }

    int appendEntry(const EntryType &entry) { return insertEntry(m_items.size(), entry); }

    int insertEntry(int row, const EntryType &entry)
    {
        Q_ASSERT(row >= 0 && row <= m_items.size());

        beginInsertRows(QModelIndex(), row, row);
        m_items.insert(row, entry);
        endInsertRows();

        rebuildIndexFrom(row);
        onEntryInserted(row, entry);
        return row;
    }

    EntryType takeEntryAt(int row)
    {
        EntryType removed;
        removeEntryAt(row, &removed);
        return removed;
    }

    bool removeEntryAt(int row, EntryType *removedOut = nullptr)
    {
        if (row < 0 || row >= m_items.size())
            return false;

        const EntryType removed = m_items.at(row);
        beginRemoveRows(QModelIndex(), row, row);
        m_items.removeAt(row);
        endRemoveRows();
        rebuildIndexFrom(row);
        if (removedOut)
            *removedOut = removed;
        onEntryRemoved(row, removed);
        return true;
    }

    void moveEntry(int from, int to)
    {
        if (from == to || from < 0 || from >= m_items.size() || to < 0 || to >= m_items.size())
            return;

        const int destination = from < to ? to + 1 : to;
        beginMoveRows(QModelIndex(), from, from, QModelIndex(), destination);
        m_items.move(from, to);
        endMoveRows();
        rebuildIndexFrom(std::min(from, to));
        onEntryMoved(from, to);
    }

    void rebuildIndexFrom(int startRow)
    {
        if (startRow <= 0) {
            m_indexByKey.clear();
            startRow = 0;
        } else {
            typename QHash<KeyType, int>::iterator it = m_indexByKey.begin();
            while (it != m_indexByKey.end()) {
                if (it.value() >= startRow) {
                    it = m_indexByKey.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (int i = startRow; i < m_items.size(); ++i)
            m_indexByKey.insert(keyFromEntry(m_items.at(i)), i);
    }

    virtual bool lessThan(const EntryType &lhs, const EntryType &rhs) const
    {
        Q_UNUSED(lhs);
        Q_UNUSED(rhs);
        return false;
    }

    virtual void onEntriesReset(const QList<EntryType> &entries) { Q_UNUSED(entries); }

    virtual void onEntryInserted(int row, const EntryType &entry)
    {
        Q_UNUSED(row);
        Q_UNUSED(entry);
    }

    virtual void onEntryRemoved(int row, const EntryType &entry)
    {
        Q_UNUSED(row);
        Q_UNUSED(entry);
    }

    virtual void onEntryMoved(int from, int to)
    {
        Q_UNUSED(from);
        Q_UNUSED(to);
    }

    virtual void onEntryUpdated(int row, const EntryType &entry)
    {
        Q_UNUSED(row);
        Q_UNUSED(entry);
    }

    virtual KeyType keyFromEntry(const EntryType &entry) const = 0;

private:
    int findInsertIndex(const EntryType &entry) const
    {
        for (int i = 0; i < m_items.size(); ++i) {
            if (lessThan(entry, m_items.at(i)))
                return i;
        }
        return m_items.size();
    }

    QList<EntryType> m_items;
    QHash<KeyType, int> m_indexByKey;
    StorageAdapter m_storage;
    InsertPolicy m_policy;
};

#endif // ABSTRACTSERVERDATABASEMODEL_H
