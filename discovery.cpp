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

#include "discovery.h"

#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTimer>

Discovery::Discovery(QObject *parent)
    : QObject(parent)
{
    // 计时器始终创建，保证 startQuery/stopQuery/isQuerying 在任何情况下都安全。
    m_queryTimer = new QTimer(this);
    m_queryTimer->setInterval(1000); // 每秒发送一次查询
    connect(m_queryTimer, &QTimer::timeout, this, &Discovery::sendQuery);

    m_socket = new QUdpSocket(this);
    // 注意：Qt6 中 QHostAddress::Any 为 IPv6 双栈，无法加入 IPv4 组播组，
    // 必须绑定到 AnyIPv4，否则 joinMulticastGroup 会失败。
    if (!m_socket->bind(QHostAddress::AnyIPv4, Port,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit logMessage(tr("发现服务绑定端口 %1 失败：%2")
                            .arg(Port).arg(m_socket->errorString()));
        return;
    }
    // 限制多目广播不出子网。
    m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
    if (!m_socket->joinMulticastGroup(QHostAddress(Group))) {
        emit logMessage(tr("加入多目广播组 %1 失败：%2")
                            .arg(Group).arg(m_socket->errorString()));
        return;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &Discovery::onReadyRead);
}

Discovery::~Discovery()
{
    stopQuery();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->leaveMulticastGroup(QHostAddress(Group));
        m_socket->close();
    }
}

QString Discovery::localMachineName() const
{
    return QHostInfo::localHostName();
}

QString Discovery::localIp() const
{
    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : addrs) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && a != QHostAddress::LocalHost)
            return a.toString();
    }
    return QString();
}

void Discovery::setSecure(bool secure)
{
    m_secure = secure;
}

void Discovery::startQuery(const QString &pattern)
{
    m_pattern = pattern;
    emit logMessage(tr("开始查找机器：%1").arg(pattern.isEmpty()
                                                  ? tr("（匹配所有）") : pattern));
    sendQuery();              // 立即发送一次
    m_queryTimer->start();    // 之后每秒一次
}

void Discovery::stopQuery()
{
    if (m_queryTimer)
        m_queryTimer->stop();
}

bool Discovery::isQuerying() const
{
    return m_queryTimer && m_queryTimer->isActive();
}

void Discovery::sendQuery()
{
    if (!m_socket)
        return;
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("discover");
    obj[QStringLiteral("pattern")] = m_pattern;
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    m_socket->writeDatagram(data, QHostAddress(Group), Port);
}

void Discovery::onReadyRead()
{
    if (!m_socket)
        return;
    while (m_socket->hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(buf.data(), buf.size(), &sender, &senderPort);
        if (sender.isNull())
            continue;

        const QJsonObject obj = QJsonDocument::fromJson(buf).object();
        const QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("discover")) {
            // 忽略自己发出的查询，避免自应答 / 把自己加入列表。
            if (isLocalAddress(sender))
                continue;
            const QString pattern = obj.value(QStringLiteral("pattern")).toString();
            if (patternMatches(pattern, localMachineName())) {
                // 延迟回复，避免网络拥堵。
                QTimer::singleShot(replyDelayMs(), this, [this, sender]() {
                    sendResponse(sender);
                });
            }
        } else if (type == QStringLiteral("response")) {
            const QString name = obj.value(QStringLiteral("name")).toString();
            const QString ip = obj.value(QStringLiteral("ip")).toString();
            if (name.isEmpty() || ip.isEmpty())
                continue;
            // 过滤掉“自己”（极端情况下本机也收到自己应答）。
            if (name == localMachineName() && ip == localIp())
                continue;
            const bool secure = obj.value(QStringLiteral("secure")).toBool(true);
            emit peerDiscovered(name, ip, secure);
        }
    }
}

void Discovery::sendResponse(const QHostAddress &target)
{
    if (!m_socket)
        return;
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("response");
    obj[QStringLiteral("name")] = localMachineName();
    obj[QStringLiteral("ip")] = localIp();
    obj[QStringLiteral("secure")] = m_secure;
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    // 单播回查询方（查询方发现套接字绑定在 Port 上，可收到）。
    m_socket->writeDatagram(data, target, Port);
}

bool Discovery::isLocalAddress(const QHostAddress &addr) const
{
    if (addr.isNull())
        return false;
    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : addrs) {
        if (a == addr)
            return true;
    }
    return false;
}

bool Discovery::patternMatches(const QString &pattern, const QString &name) const
{
    if (pattern.isEmpty())
        return true; // 空模式匹配所有机器
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid())
        return false;
    return re.match(name).hasMatch();
}

int Discovery::replyDelayMs() const
{
    const QString ip = localIp();
    if (ip.isEmpty())
        return 0;
    const QString last = ip.section(QLatin1Char('.'), 3, 3);
    bool ok = false;
    const int octet = last.toInt(&ok);
    if (!ok)
        return 0;
    return int(qRound(double(octet) * 1000.0 / 255.0));
}
