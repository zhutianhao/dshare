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
#include <QNetworkAccessManager>
#include <functional>
#include <QMap>
#include <QStringList>

// 客户端侧共享的授权握手管理器。
// 当访问开启了“需授权”的共享端而缺少凭据（401）时，调用 ensureHeader：
// 生成随机 token，POST 到对方 /api/auth 授权接口（携带 token + 机器名 + IP），
// 共享端主人批准后返回 authcode，本端缓存 "token-authcode" 作为后续请求的
// Authorization 头。同一 host 的并发握手会被合并，结果广播给所有等待者。
class AuthManager : public QObject
{
    Q_OBJECT
public:
    explicit AuthManager(QObject *parent = nullptr);

    // 已缓存的授权头（"token-authcode"），未缓存则返回空。
    QString headerFor(const QString &ip, quint16 port) const;

    // 确保持有有效的授权头：命中缓存则立即回调；否则执行一次握手，
    // 成功后回调 "token-authcode"，失败回调空字符串。secure 表示对方是否 HTTPS。
    void ensureHeader(const QString &ip, quint16 port, bool secure,
                      const std::function<void(const QString &)> &cb);

    // 使某 host 的缓存失效（如授权被拒或凭据失效后），便于下次重新握手。
    void invalidate(const QString &ip, quint16 port);

signals:
    void logMessage(const QString &msg);

private:
    void doHandshake(const QString &ip, quint16 port, bool secure,
                     const std::function<void(const QString &)> &cb);
    static QString localMachineName();
    static QString localIp();
    static QString randomHex(int bytes);

    QNetworkAccessManager *m_nam = nullptr;
    QMap<QString, QString> m_cache;                 // "ip:port" -> "token-authcode"
    QMap<QString, bool> m_inProgress;               // "ip:port" -> 正在握手
    QMap<QString, QList<std::function<void(const QString &)>>> m_waiters;
};
