#pragma once

#include <QAbstractListModel>
#include <QHostAddress>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// 远程文件拖拽使用的自定义 MIME 类型（携带机器 ip/port 与文件相对路径）。
inline constexpr char kRemoteFileMime[] = "application/x-fileshare-remote";

// 远程客户端（另一台运行本程序的 deepin 机器）共享目录的扁平列表模型。
// 通过 HTTP GET 对方 5000 端口的 /api/list 接口获取 JSON 目录列表，
// 不支持写入（只读浏览）。在非根目录下于列表顶部插入一个“返回上一级”伪项。
struct RemoteEntry
{
    QString name;
    bool isDir = false;
    qint64 size = 0;
    bool isUp = false; // 仅用于“返回上一级”伪项
};

// 拖拽时携带的一个远程文件描述。
struct RemoteFileRef
{
    QString ip;
    quint16 port = 5000;
    QString relPath; // 相对共享根的路径，如 "/file.txt" 或 "/sub/file.txt"
    QString name;
};

class RemoteFileModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit RemoteFileModel(QObject *parent = nullptr);

    // 设置要浏览的远程目标，并拉取根目录。
    void setTarget(const QString &name, const QString &ip, quint16 port = 5000);
    // 进入指定相对路径（如 "" 表示根，"/sub" 表示子目录）。
    void cd(const QString &relPath);
    void cdUp();
    void refresh();

    bool isUp(const QModelIndex &index) const;
    QString currentRelPath() const;
    QString machineName() const;
    QString remoteIp() const;
    quint16 remotePort() const;
    // 返回某行的条目信息（用于视图判断是否为目录、是否“返回上一级”）。
    RemoteEntry entryAt(const QModelIndex &index) const;

    // 设置本机共享根目录，用于计算远程文件在本机的缓存落点。
    void setLocalShareRoot(const QString &root);
    // 取出某行（文件项）的远程文件描述；非文件项返回 false。
    bool refAt(const QModelIndex &index, RemoteFileRef &out) const;
    // 本机缓存落点：~/myshare/.cache/<远程机器名>/<远程目录>/<文件名>
    QString localCachePath(const QModelIndex &index) const;

    // 拖拽支持：文件项可拖出（携带机器信息与相对路径），供拖到系统文件管理器。
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // 从拖拽数据中解析远程文件描述；失败返回 false。
    static bool parseRemoteMime(const QMimeData *mime, RemoteFileRef &out);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

signals:
    void logMessage(const QString &msg);
    void pathChanged(const QString &relPath);
    void listingChanged();

private slots:
    void onFinished(QNetworkReply *reply);

private:
    void doFetch();
    bool needUp() const;

    QString m_name;
    QString m_ip;
    quint16 m_port = 5000;
    QString m_rel;            // 当前相对路径："" 或 "/sub/..."
    QString m_localShareRoot; // 本机共享根目录，用于计算缓存落点
    QList<RemoteEntry> m_entries;
    class QNetworkAccessManager *m_nam = nullptr;
};
