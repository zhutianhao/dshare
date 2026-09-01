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

#include "fileserver.h"

#include <QByteArray>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpHeaders>
#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkInterface>
#include <QProcess>
#include <QRandomGenerator>
#include <QSslConfiguration>
#include <QSslServer>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <utility>

FileServer::FileServer(QObject *parent)
    : QObject(parent)
{
}

FileServer::~FileServer()
{
    m_destroying = true;
    stop();
}

void FileServer::setShareRoot(const QString &path)
{
    m_shareRoot = path;
}

QString FileServer::shareRoot() const
{
    return m_shareRoot;
}

quint16 FileServer::port() const
{
    return m_port;
}

bool FileServer::isSecure() const
{
    return m_secure;
}

bool FileServer::isRunning() const
{
    return m_tcp && m_tcp->isListening();
}

bool FileServer::requireAuth() const
{
    return m_authRequired;
}

void FileServer::setRequireAuth(bool on)
{
    if (m_authRequired == on)
        return;
    m_authRequired = on;
    if (!on) {
        // 关闭授权：拒绝所有在途授权请求并清空状态。
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            PendingAuth &pa = it.value();
            if (pa.timer) {
                pa.timer->stop();
                pa.timer->deleteLater();
            }
            if (pa.responder) {
                QHttpHeaders hdr;
                hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                           QByteArray("application/json; charset=utf-8"));
                pa.responder->write(QJsonDocument(QJsonObject{{QStringLiteral("error"),
                                                               QStringLiteral("server-auth-disabled")}})
                                        .toJson(QJsonDocument::Compact),
                                    hdr, QHttpServerResponder::StatusCode::Forbidden);
                delete pa.responder;
            }
        }
        m_pending.clear();
        m_approved.clear();
    }
    emit logMessage(on ? tr("已开启访问授权：未授权设备需经您批准")
                       : tr("已关闭访问授权：任何人可访问"));
}

void FileServer::resolveAuth(const QString &token, bool approve)
{
    auto it = m_pending.find(token);
    if (it == m_pending.end())
        return; // 已超时或已被处理

    PendingAuth &pa = it.value();
    pa.approved = approve;
    if (approve) {
        pa.authcode = randomHex(16);
        m_approved[token] = pa.authcode;
    }

    // 通过之前保存的 responder 回写结果（非阻塞：处理器早已返回）。
    QHttpHeaders hdr;
    hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
               QByteArray("application/json; charset=utf-8"));
    if (pa.approved) {
        pa.responder->write(QJsonDocument(QJsonObject{{QStringLiteral("authcode"), pa.authcode}})
                                .toJson(QJsonDocument::Compact),
                            hdr, QHttpServerResponder::StatusCode::Ok);
    } else {
        pa.responder->write(QJsonDocument(QJsonObject{{QStringLiteral("error"),
                                                       QStringLiteral("denied")}})
                                .toJson(QJsonDocument::Compact),
                            hdr, QHttpServerResponder::StatusCode::Forbidden);
    }
    if (pa.timer) {
        pa.timer->stop();
        pa.timer->deleteLater();
    }
    delete pa.responder;
    m_pending.erase(it);
}

QString FileServer::randomHex(int bytes)
{
    QByteArray b;
    b.resize(bytes);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32 *>(b.data()),
                                           bytes / sizeof(quint32));
    // 处理不能被 4 整除的尾部字节
    for (int i = (bytes / sizeof(quint32)) * sizeof(quint32); i < bytes; ++i)
        b[i] = quint8(QRandomGenerator::global()->generate() & 0xFF);
    return QString::fromLatin1(b.toHex());
}

bool FileServer::start(quint16 port)
{
    m_port = port;
    if (isRunning())
        stop();

    if (m_shareRoot.isEmpty() || canonicalRoot().isEmpty()) {
        emit logMessage(tr("共享目录无效：%1").arg(m_shareRoot));
        return false;
    }

    m_server = new QHttpServer(this);

    // 需要授权时走 HTTPS，保护授权凭据；否则明文 HTTP，避免自签证书告警。
    const bool useSsl = m_authRequired;
    if (useSsl && !setupSsl()) {
        emit logMessage(tr("SSL 证书不可用，无法以加密方式启动共享服务"));
        delete m_server;
        m_server = nullptr;
        return false;
    }

    if (useSsl) {
        auto *ssl = new QSslServer(m_server);
        QSslConfiguration conf = ssl->sslConfiguration();
        conf.setLocalCertificateChain({ m_cert });
        conf.setPrivateKey(m_key);
        conf.setProtocol(QSsl::TlsV1_2OrLater);
        ssl->setSslConfiguration(conf);
        connect(ssl, &QSslServer::errorOccurred, this,
                [this](QSslSocket *, QAbstractSocket::SocketError err) {
                    emit logMessage(tr("SSL 错误：%1").arg(int(err)));
                });
        if (!ssl->listen(QHostAddress::Any, port)) {
            emit logMessage(tr("无法监听端口 %1：%2").arg(port).arg(ssl->errorString()));
            delete m_server;
            m_server = nullptr;
            return false;
        }
        if (!m_server->bind(ssl)) {
            emit logMessage(tr("HTTPS 服务绑定失败"));
            delete m_server;
            m_server = nullptr;
            return false;
        }
        m_tcp = ssl;
        m_secure = true;
        emit logMessage(tr("文件共享已启动（HTTPS），监听端口 %1").arg(port));
    } else {
        // 未开启授权：使用明文 HTTP，浏览器等访问无自签证书告警。
        auto *tcp = new QTcpServer(m_server);
        if (!tcp->listen(QHostAddress::Any, port)) {
            emit logMessage(tr("无法监听端口 %1：%2").arg(port).arg(tcp->errorString()));
            delete m_server;
            m_server = nullptr;
            return false;
        }
        if (!m_server->bind(tcp)) {
            emit logMessage(tr("HTTP 服务绑定失败"));
            delete m_server;
            m_server = nullptr;
            return false;
        }
        m_tcp = tcp;
        m_secure = false;
        emit logMessage(tr("文件共享已启动（明文 HTTP），监听端口 %1").arg(port));
    }

    m_server->setMissingHandler(this, [this](const QHttpServerRequest &request,
                                             QHttpServerResponder &responder) {
        handleRequest(request, responder);
    });

    return true;
}

void FileServer::stop()
{
    if (m_server) {
        delete m_server;
        m_server = nullptr;
    }
    m_tcp = nullptr;
    // 析构期间 UI 可能已失效，避免向其发射信号导致崩溃。
    if (!m_destroying)
        emit logMessage(tr("文件共享已停止"));
}

bool FileServer::setupSsl()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/dshare");
    QDir().mkpath(dir);
    const QString certPath = dir + QStringLiteral("/server.crt");
    const QString keyPath = dir + QStringLiteral("/server.key");

    if (!QFile::exists(certPath) || !QFile::exists(keyPath)) {
        if (!generateCert(certPath, keyPath))
            return false;
    }

    {
        QFile cf(certPath);
        if (!cf.open(QIODevice::ReadOnly)) return false;
        m_cert = QSslCertificate(cf.readAll());
    }
    {
        QFile kf(keyPath);
        if (!kf.open(QIODevice::ReadOnly)) return false;
        m_key = QSslKey(kf.readAll(), QSsl::Rsa);
    }

    // 证书损坏时尝试重新生成一次。
    if (m_cert.isNull() || m_key.isNull()) {
        QFile::remove(certPath);
        QFile::remove(keyPath);
        if (!generateCert(certPath, keyPath))
            return false;
        {
            QFile cf(certPath);
            cf.open(QIODevice::ReadOnly);
            m_cert = QSslCertificate(cf.readAll());
        }
        {
            QFile f(keyPath);
            f.open(QIODevice::ReadOnly);
            m_key = QSslKey(f.readAll(), QSsl::Rsa);
        }
    }
    return !m_cert.isNull() && !m_key.isNull();
}

bool FileServer::generateCert(const QString &certPath, const QString &keyPath)
{
    // 收集本机局域网 IP，写入证书的 subjectAltName，便于浏览器按 IP 访问时减少告警。
    QStringList san;
    san << QStringLiteral("DNS:localhost") << QStringLiteral("IP:127.0.0.1");
    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : addrs) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && a != QHostAddress::LocalHost) {
            san << QStringLiteral("IP:%1").arg(a.toString());
            break;
        }
    }

    QProcess p;
    p.setProgram(QStringLiteral("openssl"));
    p.setArguments({
        QStringLiteral("req"), QStringLiteral("-x509"),
        QStringLiteral("-newkey"), QStringLiteral("rsa:2048"),
        QStringLiteral("-nodes"),
        QStringLiteral("-keyout"), keyPath,
        QStringLiteral("-out"), certPath,
        QStringLiteral("-days"), QStringLiteral("3650"),
        QStringLiteral("-subj"), QStringLiteral("/CN=dshare"),
        QStringLiteral("-addext"), QStringLiteral("subjectAltName=") + san.join(QLatin1Char(',')),
    });
    p.start();
    if (!p.waitForFinished(30000))
        return false;
    return p.exitCode() == 0 && QFile::exists(certPath) && QFile::exists(keyPath);
}

QString FileServer::canonicalRoot() const
{
    QDir dir(m_shareRoot);
    return dir.canonicalPath();
}

QString FileServer::safePath(const QString &rel)
{
    const QString root = canonicalRoot();
    if (root.isEmpty())
        return QString();

    // rel 可能以 "/" 开头（如远程模型传来的 "/sub"），filePath 会把
    // 绝对路径原样返回而忽略 root，需先去掉前导斜杠再拼接。
    QString r = rel;
    while (r.startsWith(QLatin1Char('/')))
        r.remove(0, 1);

    const QString abs = QDir(root).filePath(r);
    const QFileInfo info(abs);
    if (info.exists()) {
        const QString c = info.canonicalFilePath();
        if (c == root || c.startsWith(root + QLatin1Char('/')))
            return c;
        return QString();
    }

    // Not existing yet: validate the parent directory and the final component.
    const QFileInfo parentInfo(info.path());
    if (parentInfo.exists()) {
        const QString pc = parentInfo.canonicalFilePath();
        if ((pc == root || pc.startsWith(root + QLatin1Char('/')))
            && !info.fileName().contains(QLatin1Char('/'))
            && !info.fileName().contains(QLatin1Char('\\'))) {
            return abs;
        }
    }
    return QString();
}

QString FileServer::htmlEscape(const QString &s) const
{
    QString out = s;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    out.replace(QLatin1Char('\''), QStringLiteral("&#39;"));
    return out;
}

QString FileServer::urlEncode(const QString &s) const
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

QByteArray FileServer::renderDirectory(const QDir &dir, const QString &relPath) const
{
    const QString root = canonicalRoot();
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    QString body;
    body += QStringLiteral("<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
                           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                           "<title>文件共享 - %1</title></head><body>")
                .arg(htmlEscape(relPath.isEmpty() ? QStringLiteral("/") : relPath));

    body += QStringLiteral("<h2>文件共享：%1</h2>").arg(htmlEscape(relPath.isEmpty() ? QStringLiteral("/") : relPath));

    // Breadcrumb
    body += QStringLiteral("<p>当前路径：");
    QStringList parts = relPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString accum;
    body += QStringLiteral("<a href=\"/browse\">根目录</a>");
    for (const QString &p : parts) {
        accum += QLatin1Char('/') + p;
        body += QStringLiteral(" / <a href=\"/browse/%1\">%2</a>")
                    .arg(urlEncode(accum.mid(1)), htmlEscape(p));
    }
    body += QStringLiteral("</p>");

    // Upload form：使用 XHR 分片上传，实时显示进度并在上传期间禁用按钮，
    // 避免重复提交；分片同时让服务端无需把整个文件缓冲进内存（支持大文件）。
    body += QStringLiteral(
        "<form id=\"up\"><input type=\"file\" id=\"file\" required>"
        "<button type=\"submit\" id=\"upbtn\">上传到当前目录</button>"
        "<span id=\"msg\"></span></form>"
        "<div id=\"progwrap\" style=\"display:none;margin:6px 0\">"
        "<progress id=\"bar\" value=\"0\" max=\"100\" style=\"width:260px;vertical-align:middle\">"
        "</progress><span id=\"pct\">0%</span></div>"
        "<script>"
        "(function(){"
        "var CH=4*1024*1024;"
        "var form=document.getElementById('up'),fi=document.getElementById('file'),"
        "btn=document.getElementById('upbtn'),msg=document.getElementById('msg'),"
        "wrap=document.getElementById('progwrap'),bar=document.getElementById('bar'),"
        "pct=document.getElementById('pct'),rel=\"%1\";"
        "function fmt(n){"
        " if(n<1024)return n+' B';"
        " if(n<1048576)return (n/1024).toFixed(1)+' KB';"
        " if(n<1073741824)return (n/1048576).toFixed(1)+' MB';"
        " return (n/1073741824).toFixed(2)+' GB';"
        "}"
        "function setProg(done,total){"
        " var p=total>0?done*100/total:0;"
        " bar.value=p;"
        " pct.textContent=p.toFixed(1)+'\\u0025 ('+fmt(done)+' / '+fmt(total)+')';"
        "}"
        "function done(ok,text){"
        " fi.disabled=false;btn.disabled=false;msg.textContent=text;"
        " if(ok)setTimeout(function(){location.reload();},600);"
        "}"
        "form.onsubmit=function(e){"
        " e.preventDefault();"
        " var f=fi.files[0];"
        " if(!f||btn.disabled)return;"
        " btn.disabled=true;fi.disabled=true;wrap.style.display='block';msg.textContent='';"
        " setProg(0,f.size);"
        " var t0=Date.now();"
        " function put(off){"
        "  var end=Math.min(off+CH,f.size);"
        "  var xhr=new XMLHttpRequest();"
        "  xhr.open('POST','/upload/'+rel+'?name='+encodeURIComponent(f.name)"
        "           +'&offset='+off+'&total='+f.size);"
        "  xhr.upload.onprogress=function(ev){setProg(off+ev.loaded,f.size);};"
        "  xhr.onload=function(){"
        "   if(xhr.status>=200&&xhr.status<300){"
        "    setProg(end,f.size);"
        "    if(end<f.size){put(end);return;}"
        "    var sec=(Date.now()-t0)/1000;"
        "    var sp=sec>0?'，平均 '+fmt(f.size/sec)+'/s':'';"
        "    done(true,'上传完成'+sp);"
        "   }else{done(false,'上传失败（HTTP '+xhr.status+'）');}"
        "  };"
        "  xhr.onerror=function(){done(false,'上传失败：网络错误');};"
        "  xhr.send(f.slice(off,end));"
        " }"
        " put(0);"
        "};"
        "})();"
        "</script>")
                .arg(urlEncode(relPath));

    // New folder form
    body += QStringLiteral(
        "<form id=\"nf\"><input type=\"text\" id=\"dirname\" placeholder=\"新文件夹名称\" required>"
        "<button type=\"submit\">新建文件夹</button></form>"
        "<script>"
        "document.getElementById('nf').onsubmit=async function(e){"
        "e.preventDefault();"
        "var n=document.getElementById('dirname').value;"
        "if(!n)return;"
        "var rel=\"%1\";"
        "var r=await fetch('/mkdir/'+rel+'?name='+encodeURIComponent(n),{method:'POST'});"
        "if(r.ok)location.reload();"
        "};"
        "</script>")
                .arg(urlEncode(relPath));

    body += QStringLiteral("<hr><table border=\"0\" cellpadding=\"4\">");
    for (const QFileInfo &fi : entries) {
    const QString rel = relPath.isEmpty() ? fi.fileName()
                                           : (relPath + QLatin1Char('/') + fi.fileName());
    const QString link = QStringLiteral("/browse/%1").arg(urlEncode(rel));
        QString type = fi.isDir() ? QStringLiteral("目录") : QStringLiteral("%1 字节").arg(fi.size());
        body += QStringLiteral("<tr><td><a href=\"%1\">%2</a></td><td>%3</td></tr>")
                    .arg(link, htmlEscape(fi.fileName()), htmlEscape(type));
    }
    if (entries.isEmpty())
        body += QStringLiteral("<tr><td colspan=\"2\">（空目录）</td></tr>");
    body += QStringLiteral("</table></body></html>");

    return body.toUtf8();
}

QString FileServer::authCredential(const QHttpServerRequest &request) const
{
    const QHttpHeaders hdr = request.headers();
    const QByteArray auth(hdr.value(QByteArrayLiteral("Authorization")));
    if (!auth.isEmpty())
        return QString::fromLatin1(auth);

    const QByteArray cookie(hdr.value(QByteArrayLiteral("Cookie")));
    if (!cookie.isEmpty()) {
        for (const QByteArray &part : cookie.split(';')) {
            QByteArray p = part.trimmed();
            if (p.startsWith("fs_auth="))
                return QString::fromLatin1(p.mid(QLatin1String("fs_auth=").size()));
        }
    }
    return QString();
}

QByteArray FileServer::renderAuthPage() const
{
    // 浏览器未携带有效凭据时返回此页面：脚本自动发起授权请求（长轮询），
    // 共享端主人批准后写入 fs_auth Cookie 并刷新，后续请求即带凭据。
    const QString html = QStringLiteral(
        "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>文件共享 - 需要授权</title></head><body>"
        "<h2>访问需要授权</h2>"
        "<p id=\"msg\">正在请求授权，请在共享端点击“允许”…</p>"
        "<script>"
        "(async function(){"
        " try{"
        "  var token=crypto.randomUUID().replace(/-/g,'');"
        "  var r=await fetch('/api/auth',{method:'POST',"
        "     headers:{'Content-Type':'application/json'},"
        "     body:JSON.stringify({token:token,machineName:navigator.userAgent})});"
        "  if(!r.ok){document.getElementById('msg').textContent='访问被拒绝';return;}"
        "  var j=await r.json();"
        "  if(!j.authcode){document.getElementById('msg').textContent='未获得授权';return;}"
        "  document.cookie='fs_auth='+token+'-'+j.authcode+';path=/;max-age=86400';"
        "  location.reload();"
        " }catch(e){document.getElementById('msg').textContent='请求出错：'+e;}"
        "})();"
        "</script></body></html>");
    return html.toUtf8();
}

void FileServer::handleRequest(const QHttpServerRequest &request, QHttpServerResponder &responder)
{
    const QUrl url = request.url();
    // 使用 FullyDecoded：路径中的 %2F 等会被正确还原为 '/',
    // 所以 "/browse/test%2Ffileshare" 能正确解析为子目录下的文件。
    const QString path = url.path(QUrl::FullyDecoded);
    const QUrlQuery query(url);

    // 授权接口始终开放（无需授权头），供客户端/浏览器发起授权请求。
    if (request.method() == QHttpServerRequest::Method::Post
        && path == QStringLiteral("/api/auth")) {
        handleAuthRequest(request, responder);
        return;
    }

    // 需要授权时校验凭据；未通过则按请求类型返回 401（页面或 JSON）。
    if (m_authRequired) {
        const QString cred = authCredential(request);
        bool ok = false;
        if (!cred.isEmpty()) {
            const int idx = cred.indexOf(QLatin1Char('-'));
            if (idx > 0) {
                const QString token = cred.left(idx);
                const QString code = cred.mid(idx + 1);
                if (m_approved.value(token) == code)
                    ok = true;
            }
        }
        if (!ok) {
            const bool isPage = (request.method() == QHttpServerRequest::Method::Get)
                                && (path == QStringLiteral("/") || path.startsWith(QStringLiteral("/browse")));
            QHttpHeaders hdr;
            if (isPage) {
                hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                           QByteArray("text/html; charset=utf-8"));
                responder.write(renderAuthPage(), hdr,
                                QHttpServerResponder::StatusCode::Unauthorized);
            } else {
                hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                           QByteArray("application/json; charset=utf-8"));
                responder.write(QJsonDocument(QJsonObject{{QStringLiteral("error"),
                                                           QStringLiteral("unauthorized")}}).toJson(QJsonDocument::Compact),
                                hdr, QHttpServerResponder::StatusCode::Unauthorized);
            }
            return;
        }
    }

    if (request.method() == QHttpServerRequest::Method::Get) {
        // 结构化目录列表接口：供其他 deepin 客户端（本程序）拉取并展示。
        if (path == QStringLiteral("/api/list")) {
            const QString rel = query.queryItemValue(QStringLiteral("path"),
                                                     QUrl::FullyDecoded);
            const QString abs = safePath(rel);
            if (abs.isEmpty() || !QFileInfo(abs).isDir()) {
                responder.write(QHttpServerResponder::StatusCode::Forbidden);
                return;
            }
            const QFileInfoList entries = QDir(abs).entryInfoList(
                QDir::AllEntries | QDir::NoDotAndDotDot,
                QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
            QJsonArray arr;
            for (const QFileInfo &fi : entries) {
                QJsonObject o;
                o[QStringLiteral("name")] = fi.fileName();
                o[QStringLiteral("isDir")] = fi.isDir();
                o[QStringLiteral("size")] = fi.isDir() ? 0 : fi.size();
                arr.append(o);
            }
            QJsonObject root;
            root[QStringLiteral("path")] = rel;
            root[QStringLiteral("entries")] = arr;
            QHttpHeaders hdr;
            hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                       QByteArray("application/json; charset=utf-8"));
            responder.write(QJsonDocument(root).toJson(QJsonDocument::Compact), hdr,
                            QHttpServerResponder::StatusCode::Ok);
            return;
        }

        QString rel;
        if (path == QStringLiteral("/") || path == QStringLiteral("/browse")
            || path == QStringLiteral("/browse/")) {
            rel.clear();
        } else if (path.startsWith(QStringLiteral("/browse/"))) {
            rel = path.mid(8); // strip "/browse/"
        } else {
            responder.write(QHttpServerResponder::StatusCode::NotFound);
            return;
        }

        const QString abs = safePath(rel);
        if (abs.isEmpty()) {
            responder.write(QHttpServerResponder::StatusCode::Forbidden);
            return;
        }

        QFileInfo info(abs);
        if (info.isDir()) {
            QHttpHeaders hdr;
            hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                       QByteArray("text/html; charset=utf-8"));
            responder.write(renderDirectory(QDir(abs), rel), hdr,
                           QHttpServerResponder::StatusCode::Ok);
        } else if (info.isFile()) {
            // 流式发送：把 QIODevice 交给 responder 边读边发，避免 readAll()
            // 把整个文件读进内存（>2G 会因 QByteArray 上限而崩溃）。responder
            // 会在发送结束后接管并释放该 QFile；同时挂到 m_server 上，确保异常
            // 路径（如连接中断）也不会泄漏。
            auto *f = new QFile(abs, m_server);
            if (!f->open(QIODevice::ReadOnly)) {
                delete f;
                responder.write(QHttpServerResponder::StatusCode::NotFound);
                return;
            }
            QMimeDatabase db;
            const QString mime = db.mimeTypeForFile(abs).name();
            const QString disp = QStringLiteral("attachment; filename*=UTF-8''%1")
                                    .arg(QString::fromLatin1(QUrl::toPercentEncoding(info.fileName())));
            QHttpHeaders hdr;
            hdr.append(QHttpHeaders::WellKnownHeader::ContentType, mime.toLatin1());
            hdr.append(QHttpHeaders::WellKnownHeader::ContentDisposition, disp.toLatin1());
            responder.write(f, hdr, QHttpServerResponder::StatusCode::Ok);
        } else {
            responder.write(QHttpServerResponder::StatusCode::NotFound);
        }
        return;
    }

    if (request.method() == QHttpServerRequest::Method::Post) {
        const bool isUpload = (path == QStringLiteral("/upload"))
                              || path.startsWith(QStringLiteral("/upload/"));
        const bool isMkdir = (path == QStringLiteral("/mkdir"))
                             || path.startsWith(QStringLiteral("/mkdir/"));
        if (!isUpload && !isMkdir) {
            responder.write(QHttpServerResponder::StatusCode::NotFound);
            return;
        }

        QString rel;
        if (isUpload)
            rel = (path == QStringLiteral("/upload")) ? QString() : path.mid(8);
        else
            rel = (path == QStringLiteral("/mkdir")) ? QString() : path.mid(7);

        const QString dirAbs = safePath(rel);
        if (dirAbs.isEmpty() || !QFileInfo(dirAbs).isDir()) {
            responder.write(QHttpServerResponder::StatusCode::Forbidden);
            return;
        }

        const QString name = QFileInfo(query.queryItemValue(QStringLiteral("name"),
                                                            QUrl::FullyDecoded)).fileName();
        if (isUpload) {
            if (name.isEmpty()) {
                responder.write(QHttpServerResponder::StatusCode::BadRequest);
                return;
            }
            const QString target = QDir(dirAbs).filePath(name);

            // 分片上传：带 offset（与可选 total）时只接收一块并写入指定偏移，
            // 服务端不再把整个文件读进 QByteArray（QByteArray 上限 2G，更大文件会崩溃）。
            const QString offStr = query.queryItemValue(QStringLiteral("offset"));
            const QString totalStr = query.queryItemValue(QStringLiteral("total"));
            const QByteArray body = request.body();
            const bool chunked = !offStr.isEmpty();
            qint64 offset = 0;
            qint64 total = body.size();
            if (chunked) {
                bool ok = false;
                offset = offStr.toLongLong(&ok);
                if (!ok || offset < 0) {
                    responder.write(QHttpServerResponder::StatusCode::BadRequest);
                    return;
                }
                bool okTotal = false;
                const qint64 t = totalStr.toLongLong(&okTotal);
                if (okTotal && t > 0)
                    total = t;
            }

            QFile f(target);
            // 分片模式用 ReadWrite 以便 seek 到 offset；首块（offset==0）清空旧内容，
            // 这样重试同一块是幂等的。非分片模式 WriteOnly 即截断写入。
            if (!f.open(chunked ? QIODevice::ReadWrite : QIODevice::WriteOnly)) {
                responder.write(QHttpServerResponder::StatusCode::InternalServerError);
                return;
            }
            if (chunked && offset == 0)
                f.resize(0);
            if (chunked && !f.seek(offset)) {
                responder.write(QHttpServerResponder::StatusCode::InternalServerError);
                return;
            }
            const qint64 written = f.write(body);
            f.close();
            if (written != body.size()) {
                responder.write(QHttpServerResponder::StatusCode::InternalServerError);
                return;
            }

            const bool last = !chunked || (offset + written >= total);
            if (offset == 0 && !last) {
                emit logMessage(tr("Web 上传开始：%1").arg(target));
            } else if (last) {
                emit logMessage(tr("Web 上传完成：%1").arg(target));
            }

            QHttpHeaders hdr;
            if (chunked) {
                // 回写本次写入位置，便于客户端校验/续传。
                hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                           QByteArray("application/json; charset=utf-8"));
                responder.write(QJsonDocument(QJsonObject{
                                                  {QStringLiteral("offset"), offset},
                                                  {QStringLiteral("received"), written}})
                                    .toJson(QJsonDocument::Compact),
                                hdr, QHttpServerResponder::StatusCode::Ok);
            } else {
                hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                           QByteArray("text/plain; charset=utf-8"));
                responder.write(QByteArrayLiteral("OK"), hdr,
                                QHttpServerResponder::StatusCode::Ok);
            }
        } else {
            if (name.isEmpty() || !QDir(dirAbs).mkdir(name)) {
                responder.write(QHttpServerResponder::StatusCode::BadRequest);
                return;
            }
            emit logMessage(tr("Web 新建目录：%1").arg(QDir(dirAbs).filePath(name)));
            QHttpHeaders hdr;
            hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                       QByteArray("text/plain; charset=utf-8"));
            responder.write(QByteArrayLiteral("OK"), hdr, QHttpServerResponder::StatusCode::Ok);
        }
        return;
    }

    responder.write(QHttpServerResponder::StatusCode::NotFound);
}

void FileServer::handleAuthRequest(const QHttpServerRequest &request, QHttpServerResponder &responder)
{
    // 未开启授权：直接返回空 authcode（客户端一般不会走到这里，但保持健壮）。
    if (!m_authRequired) {
        QHttpHeaders hdr;
        hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                   QByteArray("application/json; charset=utf-8"));
        responder.write(QJsonDocument(QJsonObject{{QStringLiteral("authcode"),
                                                   QStringLiteral("")}}).toJson(QJsonDocument::Compact),
                        hdr, QHttpServerResponder::StatusCode::Ok);
        return;
    }

    const QJsonObject o = QJsonDocument::fromJson(request.body()).object();
    const QString token = o.value(QStringLiteral("token")).toString();
    if (token.isEmpty()) {
        responder.write(QHttpServerResponder::StatusCode::BadRequest);
        return;
    }

    // 幂等：同一 token 已批准，直接返回原 authcode。
    auto approved = m_approved.find(token);
    if (approved != m_approved.end()) {
        QHttpHeaders hdr;
        hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                   QByteArray("application/json; charset=utf-8"));
        responder.write(QJsonDocument(QJsonObject{{QStringLiteral("authcode"),
                                                   *approved}}).toJson(QJsonDocument::Compact),
                        hdr, QHttpServerResponder::StatusCode::Ok);
        return;
    }

    const QString name = o.value(QStringLiteral("machineName")).toString();
    const QString clientIp = request.remoteAddress().toString();
    const QString displayName = name.isEmpty() ? tr("未知设备") : name;

    // 将 responder 的所有权转移到堆上，待主人决定后再回写响应（非阻塞，
    // 避免请求处理器被授权弹窗的对话框事件循环卡住）。
    PendingAuth pa;
    pa.machineName = displayName;
    pa.ip = clientIp;
    pa.approved = false;
    pa.responder = new QHttpServerResponder(std::move(responder));
    pa.timer = new QTimer(this);
    pa.timer->setSingleShot(true);
    pa.timer->setInterval(5 * 60 * 1000);
    connect(pa.timer, &QTimer::timeout, this, [this, token]() {
        resolveAuth(token, false);
    });
    pa.timer->start();
    m_pending[token] = pa;

    emit authRequested(token, displayName, clientIp);
    // 处理器立即返回，响应由 resolveAuth 在主人决定后发出。
}
