#include "authmanager.h"

#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QRandomGenerator>
#include <QUrl>

AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

QString AuthManager::headerFor(const QString &ip, quint16 port) const
{
    return m_cache.value(ip + QLatin1Char(':') + QString::number(port));
}

void AuthManager::invalidate(const QString &ip, quint16 port)
{
    const QString host = ip + QLatin1Char(':') + QString::number(port);
    m_cache.remove(host);
    m_inProgress.remove(host);
    m_waiters.remove(host);
}

void AuthManager::ensureHeader(const QString &ip, quint16 port,
                               const std::function<void(const QString &)> &cb)
{
    const QString host = ip + QLatin1Char(':') + QString::number(port);
    if (m_cache.contains(host)) {
        cb(m_cache.value(host));
        return;
    }
    if (m_inProgress.value(host)) {
        // 已有握手进行中，加入等待队列，结果到达时一并回调。
        m_waiters[host].append(cb);
        return;
    }
    m_inProgress[host] = true;
    m_waiters[host].append(cb);
    doHandshake(ip, port, [this, host](const QString &header) {
        m_inProgress[host] = false;
        if (!header.isEmpty())
            m_cache[host] = header;
        const auto list = m_waiters.take(host);
        for (const auto &fn : list)
            fn(header);
    });
}

void AuthManager::doHandshake(const QString &ip, quint16 port,
                              const std::function<void(const QString &)> &cb)
{
    const QString token = randomHex(16);
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(ip);
    url.setPort(port);
    url.setPath(QStringLiteral("/api/auth"));

    QNetworkRequest req(url);
    QJsonObject body;
    body[QStringLiteral("token")] = token;
    body[QStringLiteral("machineName")] = localMachineName();
    body[QStringLiteral("ip")] = localIp();

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::sslErrors, reply,
            [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, token, cb]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (status == 200) {
            const QJsonObject o = QJsonDocument::fromJson(data).object();
            const QString authcode = o.value(QStringLiteral("authcode")).toString();
            if (!authcode.isEmpty()) {
                cb(token + QLatin1Char('-') + authcode);
                return;
            }
        }
        emit logMessage(tr("授权请求失败（HTTP %1）").arg(status));
        cb(QString());
    });
}

QString AuthManager::localMachineName()
{
    return QHostInfo::localHostName();
}

QString AuthManager::localIp()
{
    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : addrs) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && a != QHostAddress::LocalHost)
            return a.toString();
    }
    return QString();
}

QString AuthManager::randomHex(int bytes)
{
    QByteArray b;
    b.resize(bytes);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32 *>(b.data()),
                                          bytes / sizeof(quint32));
    for (int i = (bytes / sizeof(quint32)) * sizeof(quint32); i < bytes; ++i)
        b[i] = quint8(QRandomGenerator::global()->generate() & 0xFF);
    return QString::fromLatin1(b.toHex());
}
