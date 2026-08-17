#include "remotemodel.h"

#include <DIconTheme>
#include <QMimeData>

#include <QDir>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
// 规范化相对路径："" 或 "/a/b"（无尾斜杠，非空前导斜杠）
QString normalizeRel(const QString &raw)
{
    QString s = raw;
    while (s.startsWith(QLatin1String("//")))
        s.remove(0, 1);
    if (s.isEmpty())
        return QString();
    if (!s.startsWith(QLatin1Char('/')))
        s.prepend(QLatin1Char('/'));
    while (s.endsWith(QLatin1Char('/')) && s.size() > 1)
        s.chop(1);
    return s;
}
} // namespace

RemoteFileModel::RemoteFileModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_nam = new QNetworkAccessManager(this);
    connect(m_nam, &QNetworkAccessManager::finished, this, &RemoteFileModel::onFinished);
}

void RemoteFileModel::setTarget(const QString &name, const QString &ip, quint16 port)
{
    m_name = name;
    m_ip = ip;
    m_port = port;
    m_rel.clear();
    doFetch();
}

void RemoteFileModel::cd(const QString &relPath)
{
    m_rel = normalizeRel(relPath);
    doFetch();
}

void RemoteFileModel::cdUp()
{
    if (m_rel.isEmpty())
        return;
    const int idx = m_rel.lastIndexOf(QLatin1Char('/'));
    m_rel = (idx <= 0) ? QString() : m_rel.left(idx);
    doFetch();
}

void RemoteFileModel::refresh()
{
    doFetch();
}

bool RemoteFileModel::needUp() const
{
    return !m_rel.isEmpty();
}

bool RemoteFileModel::isUp(const QModelIndex &index) const
{
    return index.isValid() && needUp() && index.row() == 0;
}

QString RemoteFileModel::currentRelPath() const
{
    return m_rel;
}

QString RemoteFileModel::machineName() const
{
    return m_name;
}

QString RemoteFileModel::remoteIp() const
{
    return m_ip;
}

quint16 RemoteFileModel::remotePort() const
{
    return m_port;
}

void RemoteFileModel::setLocalShareRoot(const QString &root)
{
    m_localShareRoot = root;
}

RemoteEntry RemoteFileModel::entryAt(const QModelIndex &index) const
{
    RemoteEntry up;
    up.isUp = true;
    up.name = tr("返回上一级");
    if (isUp(index))
        return up;

    const int off = needUp() ? 1 : 0;
    const int r = index.row() - off;
    if (r >= 0 && r < m_entries.size())
        return m_entries.at(r);
    return RemoteEntry();
}

QStringList RemoteFileModel::mimeTypes() const
{
    return { QString::fromUtf8(kRemoteFileMime) };
}

Qt::ItemFlags RemoteFileModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractListModel::flags(index);
    if (!index.isValid())
        return f;
    const RemoteEntry e = entryAt(index);
    if (e.isUp || e.isDir)
        return f; // 伪项与目录暂不可拖拽（仅支持文件下载）
    return f | Qt::ItemIsDragEnabled;
}

QMimeData *RemoteFileModel::mimeData(const QModelIndexList &indexes) const
{
    for (const QModelIndex &idx : indexes) {
        const RemoteEntry e = entryAt(idx);
        if (e.isUp || e.isDir || e.name.isEmpty())
            continue;
        RemoteFileRef ref;
        ref.ip = m_ip;
        ref.port = m_port;
        ref.name = e.name;
        ref.relPath = m_rel.isEmpty() ? (QStringLiteral("/") + e.name)
                                       : (m_rel + QLatin1Char('/') + e.name);
        QJsonObject o;
        o[QStringLiteral("ip")] = ref.ip;
        o[QStringLiteral("port")] = ref.port;
        o[QStringLiteral("relPath")] = ref.relPath;
        o[QStringLiteral("name")] = ref.name;
        const QString cachePath = localCachePath(idx);
        auto *mime = new QMimeData;
        mime->setData(QString::fromUtf8(kRemoteFileMime),
                      QJsonDocument(o).toJson(QJsonDocument::Compact));
        mime->setText(e.name);
        // 让系统文件管理器/桌面也能识别落点（拖出时文件已预下载到该缓存路径）
        mime->setUrls({ QUrl::fromLocalFile(cachePath) });
        return mime;
    }
    return QAbstractListModel::mimeData(indexes);
}

bool RemoteFileModel::refAt(const QModelIndex &index, RemoteFileRef &out) const
{
    const RemoteEntry e = entryAt(index);
    if (e.isUp || e.isDir || e.name.isEmpty())
        return false;
    out.ip = m_ip;
    out.port = m_port;
    out.name = e.name;
    out.relPath = m_rel.isEmpty() ? (QStringLiteral("/") + e.name)
                                   : (m_rel + QLatin1Char('/') + e.name);
    return true;
}

QString RemoteFileModel::localCachePath(const QModelIndex &index) const
{
    const RemoteEntry e = entryAt(index);
    QString dir = QDir(m_localShareRoot).filePath(QStringLiteral(".cache"));
    dir = QDir(dir).filePath(m_name);          // 远程机器名
    const QString rel = m_rel.mid(1);          // 远程相对目录（去掉前导 /）
    if (!rel.isEmpty())
        dir = QDir(dir).filePath(rel);
    return QDir(dir).filePath(e.name);
}

bool RemoteFileModel::parseRemoteMime(const QMimeData *mime, RemoteFileRef &out)
{
    if (!mime || !mime->hasFormat(QString::fromUtf8(kRemoteFileMime)))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(
        mime->data(QString::fromUtf8(kRemoteFileMime))).object();
    if (o.isEmpty())
        return false;
    out.ip = o.value(QStringLiteral("ip")).toString();
    out.port = quint16(o.value(QStringLiteral("port")).toInt(5000));
    out.relPath = o.value(QStringLiteral("relPath")).toString();
    out.name = o.value(QStringLiteral("name")).toString();
    return !out.ip.isEmpty() && !out.name.isEmpty();
}

int RemoteFileModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size() + (needUp() ? 1 : 0);
}

QVariant RemoteFileModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0)
        return QVariant();

    const bool up = isUp(index);
    if (up) {
        if (role == Qt::DisplayRole)
            return tr("返回上一级");
        if (role == Qt::DecorationRole)
            return Dtk::Gui::DIconTheme::findQIcon(QStringLiteral("folder"));
        return QVariant();
    }

    const int off = needUp() ? 1 : 0;
    const int r = index.row() - off;
    if (r < 0 || r >= m_entries.size())
        return QVariant();

    const RemoteEntry &e = m_entries.at(r);
    if (role == Qt::DisplayRole) {
        return e.isDir ? e.name : QStringLiteral("%1  (%2 字节)").arg(e.name).arg(e.size);
    }
    if (role == Qt::DecorationRole) {
        return Dtk::Gui::DIconTheme::findQIcon(e.isDir ? QStringLiteral("folder")
                                             : QStringLiteral("text-x-generic"));
    }
    return QVariant();
}

void RemoteFileModel::doFetch()
{
    if (m_ip.isEmpty()) {
        beginResetModel();
        m_entries.clear();
        endResetModel();
        emit listingChanged();
        return;
    }
    emit logMessage(tr("正在从 %1 (%2) 获取目录：%3")
                        .arg(m_name, m_ip, m_rel.isEmpty() ? QStringLiteral("/") : m_rel));

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_ip);
    url.setPort(m_port);
    url.setPath(QStringLiteral("/api/list"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("path"), m_rel);
    url.setQuery(q);

    m_nam->get(QNetworkRequest(url));
}

void RemoteFileModel::onFinished(QNetworkReply *reply)
{
    if (!reply)
        return;
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage(tr("获取远程目录失败：%1").arg(reply->errorString()));
        beginResetModel();
        m_entries.clear();
        endResetModel();
        emit listingChanged();
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonArray arr = root.value(QStringLiteral("entries")).toArray();

    QList<RemoteEntry> entries;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        RemoteEntry e;
        e.name = o.value(QStringLiteral("name")).toString();
        e.isDir = o.value(QStringLiteral("isDir")).toBool(false);
        e.size = qint64(o.value(QStringLiteral("size")).toDouble(0));
        entries.append(e);
    }

    beginResetModel();
    m_entries = entries;
    endResetModel();
    emit pathChanged(m_rel);
    emit listingChanged();
    emit logMessage(tr("已加载 %n 项", "", entries.size()));
}
