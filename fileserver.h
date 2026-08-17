#pragma once

#include <QObject>
#include <QHttpServer>
#include <QTcpServer>

class FileServer : public QObject
{
    Q_OBJECT
public:
    explicit FileServer(QObject *parent = nullptr);
    ~FileServer() override;

    bool start(quint16 port = 5000);
    void stop();
    bool isRunning() const;
    quint16 port() const;
    void setShareRoot(const QString &path);
    QString shareRoot() const;

signals:
    void logMessage(const QString &msg);

private:
    void handleRequest(const QHttpServerRequest &request, QHttpServerResponder &responder);
    QString canonicalRoot() const;
    // Returns an absolute path contained within the share root.
    // For existing entries the canonical path is returned; for not-yet-existing
    // entries (e.g. an upload target) the parent directory is validated instead.
    QString safePath(const QString &rel);
    QString htmlEscape(const QString &s) const;
    QString urlEncode(const QString &s) const;
    QByteArray renderDirectory(const QDir &dir, const QString &relPath) const;

    QHttpServer *m_server = nullptr;
    QTcpServer *m_tcp = nullptr;
    QString m_shareRoot;
    quint16 m_port = 5000;
};
