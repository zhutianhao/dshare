#pragma once

#include <QAbstractProxyModel>
#include <QFileSystemModel>
#include <QList>
#include <QDir>
#include <DIconTheme>

// 平面列表代理：只代理“当前目录”这一层，并在非共享根目录下于列表顶部
// 插入一个“返回上一级”项（与文件夹同款图标）。视图的 rootIndex 始终为无效
// （即代理的顶层 = 当前目录的平面列表）；目录切换通过 setCurrentDir() 触发
// 整体 reset 实现。这样完全避免树形索引重建，映射简单且绝对正确。
//
// 注意：当前目录的源索引不缓存，每次按需用路径重新取
// （QFileSystemModel 在懒加载后会让旧的 QModelIndex 失效）。
class UpDirProxy : public QAbstractProxyModel
{
    Q_OBJECT
public:
    explicit UpDirProxy(const QString &rootPath, QObject *parent = nullptr)
        : QAbstractProxyModel(parent), m_root(rootPath), m_current(rootPath) {}

    void setRootPath(const QString &rootPath) { m_root = rootPath; }
    QString currentDir() const { return m_current; }

    void setCurrentDir(const QString &path)
    {
        beginResetModel();
        m_current = path;
        endResetModel();
    }

    // 当前目录在源模型中对应的索引（按需重建，避免缓存失效）
    QModelIndex currentSourceIndex() const
    {
        auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
        return fs ? fs->index(m_current) : QModelIndex();
    }

    bool isCurrentSourceParent(const QModelIndex &srcParent) const
    {
        auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
        if (!fs || !srcParent.isValid())
            return false;
        return QDir(fs->filePath(srcParent)).canonicalPath()
               == QDir(m_current).canonicalPath();
    }

    // ---- 映射（仅对“当前目录”内的项有效）----
    QModelIndex mapFromSource(const QModelIndex &s) const override
    {
        if (!s.isValid() || s.parent() != currentSourceIndex())
            return QModelIndex();
        const int off = needUp() ? 1 : 0;
        return createIndex(s.row() + off, s.column(), s.internalPointer());
    }

    QModelIndex mapToSource(const QModelIndex &p) const override
    {
        if (!p.isValid() || isUpIndex(p) || !sourceModel())
            return QModelIndex();
        const int off = needUp() ? 1 : 0;
        return sourceModel()->index(p.row() - off, p.column(), currentSourceIndex());
    }

    // ---- 模型接口（平面：所有项都在顶层）----
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() || !sourceModel() || row < 0 || column < 0)
            return QModelIndex();
        if (row >= rowCount() || column >= columnCount())
            return QModelIndex();
        if (needUp() && row == 0)
            return createIndex(0, column, quintptr(UpMagic));
        return mapFromSource(sourceModel()->index(row - (needUp() ? 1 : 0), column,
                                                   currentSourceIndex()));
    }

    QModelIndex parent(const QModelIndex &) const override { return QModelIndex(); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() || !sourceModel())
            return 0;
        int n = sourceModel()->rowCount(currentSourceIndex());
        if (needUp())
            ++n;
        return n;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() || !sourceModel())
            return 0;
        return sourceModel()->columnCount(currentSourceIndex());
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (isUpIndex(index)) {
            if (role == Qt::DisplayRole)
                return tr("返回上一级");
            if (role == Qt::DecorationRole)
                return Dtk::Gui::DIconTheme::findQIcon(QStringLiteral("folder"));
            return QVariant();
        }
        return QAbstractProxyModel::data(index, role);
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (isUpIndex(index))
            return false;
        return QAbstractProxyModel::setData(index, value, role);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (isUpIndex(index))
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
        return QAbstractProxyModel::flags(index);
    }

    bool hasChildren(const QModelIndex &) const override { return false; }

    bool canFetchMore(const QModelIndex &parent) const override
    {
        return !parent.isValid() && sourceModel()
               && sourceModel()->canFetchMore(currentSourceIndex());
    }

    void fetchMore(const QModelIndex &parent) override
    {
        if (!parent.isValid() && sourceModel())
            sourceModel()->fetchMore(currentSourceIndex());
    }

    bool isUp(const QModelIndex &index) const { return isUpIndex(index); }

    void setSourceModel(QAbstractItemModel *source) override
    {
        beginResetModel();
        QAbstractProxyModel::setSourceModel(source);
        if (source) {
            // 断开基类对行/列增删、布局、dataChanged 信号的处理器，
            // 改用自己的、只对“当前目录”生效且带 +1 偏移的版本。
            disconnect(source, &QAbstractItemModel::rowsAboutToBeInserted, this, nullptr);
            disconnect(source, &QAbstractItemModel::rowsInserted, this, nullptr);
            disconnect(source, &QAbstractItemModel::rowsAboutToBeRemoved, this, nullptr);
            disconnect(source, &QAbstractItemModel::rowsRemoved, this, nullptr);
            disconnect(source, &QAbstractItemModel::rowsAboutToBeMoved, this, nullptr);
            disconnect(source, &QAbstractItemModel::rowsMoved, this, nullptr);
            disconnect(source, &QAbstractItemModel::layoutAboutToBeChanged, this, nullptr);
            disconnect(source, &QAbstractItemModel::layoutChanged, this, nullptr);
            disconnect(source, &QAbstractItemModel::dataChanged, this, nullptr);

            connect(source, &QAbstractItemModel::rowsAboutToBeInserted,
                    this, &UpDirProxy::onRowsAboutToBeInserted);
            connect(source, &QAbstractItemModel::rowsInserted,
                    this, &UpDirProxy::onRowsInserted);
            connect(source, &QAbstractItemModel::rowsAboutToBeRemoved,
                    this, &UpDirProxy::onRowsAboutToBeRemoved);
            connect(source, &QAbstractItemModel::rowsRemoved,
                    this, &UpDirProxy::onRowsRemoved);
            connect(source, &QAbstractItemModel::rowsAboutToBeMoved,
                    this, &UpDirProxy::onRowsAboutToBeMoved);
            connect(source, &QAbstractItemModel::rowsMoved,
                    this, &UpDirProxy::onRowsMoved);
            connect(source, &QAbstractItemModel::layoutAboutToBeChanged,
                    this, &UpDirProxy::onLayoutAboutToBeChanged);
            connect(source, &QAbstractItemModel::layoutChanged,
                    this, &UpDirProxy::onLayoutChanged);
            connect(source, &QAbstractItemModel::dataChanged,
                    this, &UpDirProxy::onDataChanged);
        }
        endResetModel();
    }

private slots:
    void onRowsAboutToBeInserted(const QModelIndex &parent, int first, int last)
    {
        if (!isCurrentSourceParent(parent))
            return;
        const int off = needUp() ? 1 : 0;
        beginInsertRows(QModelIndex(), first + off, last + off);
    }
    void onRowsInserted(const QModelIndex &parent, int, int)
    {
        if (isCurrentSourceParent(parent))
            endInsertRows();
    }

    void onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last)
    {
        if (!isCurrentSourceParent(parent))
            return;
        const int off = needUp() ? 1 : 0;
        beginRemoveRows(QModelIndex(), first + off, last + off);
    }
    void onRowsRemoved(const QModelIndex &parent, int, int)
    {
        if (isCurrentSourceParent(parent))
            endRemoveRows();
    }

    void onRowsAboutToBeMoved(const QModelIndex &srcParent, int srcStart, int srcEnd,
                              const QModelIndex &dstParent, int dstRow)
    {
        if (!isCurrentSourceParent(srcParent) || !isCurrentSourceParent(dstParent))
            return;
        const int off = needUp() ? 1 : 0;
        beginMoveRows(QModelIndex(), srcStart + off, srcEnd + off, QModelIndex(), dstRow + off);
    }
    void onRowsMoved(const QModelIndex &srcParent, int, int, const QModelIndex &dstParent, int)
    {
        if (isCurrentSourceParent(srcParent) && isCurrentSourceParent(dstParent))
            endMoveRows();
    }

    void onLayoutAboutToBeChanged(const QList<QPersistentModelIndex> &,
                                  QAbstractItemModel::LayoutChangeHint hint)
    {
        emit layoutAboutToBeChanged({}, hint);
    }

    void onLayoutChanged(const QList<QPersistentModelIndex> &,
                         QAbstractItemModel::LayoutChangeHint hint)
    {
        emit layoutChanged({}, hint);
    }

    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                       const QList<int> &roles = QList<int>())
    {
        if (!isCurrentSourceParent(topLeft.parent()))
            return;
        emit dataChanged(mapFromSource(topLeft), mapFromSource(bottomRight), roles);
    }

private:
    enum { UpMagic = 0x2A };

    QString m_root;
    QString m_current;

    bool isUpIndex(const QModelIndex &index) const
    {
        return index.isValid() && index.internalId() == UpMagic;
    }

    bool needUp() const
    {
        auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
        if (!fs)
            return false;
        const QString p = fs->filePath(currentSourceIndex());
        return !p.isEmpty() && QDir(p).canonicalPath() != QDir(m_root).canonicalPath();
    }
};
