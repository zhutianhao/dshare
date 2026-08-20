/*
 * Copyright (C) 2026  zhutianhao <zhutianhao75@hotmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QObject>
#include <QHttpServer>
#include <QHttpServerResponder>
#include <QTcpServer>
#include <QSslCertificate>
#include <QSslKey>
#include <QMap>
#include <QTimer>

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

    // 访问是否需要授权：开启后未授权设备必须经主人批准；关闭则任何人可访问。
    void setRequireAuth(bool on);
    bool requireAuth() const;
    // 由 UI 在授权弹窗中调用，决定某次授权请求是否通过。
    void resolveAuth(const QString &token, bool approve);

signals:
    void logMessage(const QString &msg);
    // 有设备请求访问且需要授权时发出，UI 据此弹出授权界面（显示机器名与 IP）。
    void authRequested(const QString &token, const QString &machineName, const QString &ip);

private:
    void handleRequest(const QHttpServerRequest &request, QHttpServerResponder &responder);
    void handleAuthRequest(const QHttpServerRequest &request, QHttpServerResponder &responder);
    QString canonicalRoot() const;
    // Returns an absolute path contained within the share root.
    // For existing entries the canonical path is returned; for not-yet-existing
    // entries (e.g. an upload target) the parent directory is validated instead.
    QString safePath(const QString &rel);
    QString htmlEscape(const QString &s) const;
    QString urlEncode(const QString &s) const;
    QByteArray renderDirectory(const QDir &dir, const QString &relPath) const;

    // HTTPS / SSL
    bool setupSsl();
    bool generateCert(const QString &certPath, const QString &keyPath);

    // 授权
    QString authCredential(const QHttpServerRequest &request) const; // Authorization 头或 fs_auth Cookie
    QByteArray renderAuthPage() const; // 浏览器未授权时返回的自动握手页面
    static QString randomHex(int bytes);

    QHttpServer *m_server = nullptr;
    QTcpServer *m_tcp = nullptr;
    QString m_shareRoot;
    quint16 m_port = 5000;
    bool m_destroying = false; // 析构进行中，停止向 UI 发射信号

    // SSL
    QSslCertificate m_cert;
    QSslKey m_key;

    // 授权
    bool m_authRequired = false;
    struct PendingAuth
    {
        QString machineName;
        QString ip;
        QString authcode;
        bool approved = false;
        QHttpServerResponder *responder = nullptr; // 延后响应（所有权已转移至此）
        QTimer *timer = nullptr;                   // 超时自动拒绝
    };
    QMap<QString, PendingAuth> m_pending; // token -> 等待主人决定的请求
    QMap<QString, QString> m_approved;     // token -> authcode（批准后长期有效）
};
