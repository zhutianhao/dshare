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

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUdpSocket>

// 基于多目广播（组播）的局域网客户端互相发现。
//
// 协议（UDP，端口 Discovery::Port，组播组 Discovery::Group）：
//   查询：{"type":"discover","pattern":"<机器名正则表达式>"}
//   响应：{"type":"response","name":"<本机机器名>","ip":"<本机局域网IP>"}
//
// 所有客户端启动时即加入组播组并持续监听，因此随时可以应答他人的查询。
// 应答方收到 discover 后用正则比对自身机器名，命中则按
//   delay = 本机IP最后一段 / 255  (秒)
// 延迟后向查询方单播回复，以避免多个客户端同时回复造成网络拥堵。
class Discovery : public QObject
{
    Q_OBJECT
public:
    static constexpr quint16 Port = 5001;
    static constexpr char Group[] = "239.255.42.99";

    explicit Discovery(QObject *parent = nullptr);
    ~Discovery() override;

    // 本机机器名（主机名），用于应答时上报，也用于正则匹配。
    QString localMachineName() const;
    // 本机局域网 IPv4 地址（非回环），应答 / 延迟计算使用。
    QString localIp() const;

    void startQuery(const QString &pattern);
    void stopQuery();
    bool isQuerying() const;

signals:
    // 收到他人应答时发出（可能重复，由调用方去重）。
    void peerDiscovered(const QString &name, const QString &ip);
    void logMessage(const QString &msg);

private slots:
    void onReadyRead();
    void sendQuery();

private:
    bool isLocalAddress(const QHostAddress &addr) const;
    bool patternMatches(const QString &pattern, const QString &name) const;
    int replyDelayMs() const;
    void sendResponse(const QHostAddress &target);

    QUdpSocket *m_socket = nullptr;
    QTimer *m_queryTimer = nullptr;
    QString m_pattern;
};
