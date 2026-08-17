#include "fileserver.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpHeaders>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkInterface>
#include <QUrl>
#include <QUrlQuery>

FileServer::FileServer(QObject *parent)
    : QObject(parent)
{
}

FileServer::~FileServer()
{
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

bool FileServer::isRunning() const
{
    return m_tcp && m_tcp->isListening();
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
    m_tcp = new QTcpServer(m_server);
    if (!m_tcp->listen(QHostAddress::Any, port)) {
        emit logMessage(tr("无法监听端口 %1：%2").arg(port).arg(m_tcp->errorString()));
        delete m_server;
        m_server = nullptr;
        m_tcp = nullptr;
        return false;
    }
    if (!m_server->bind(m_tcp)) {
        emit logMessage(tr("HTTP 服务绑定失败"));
        delete m_server;
        m_server = nullptr;
        m_tcp = nullptr;
        return false;
    }

    m_server->setMissingHandler(this, [this](const QHttpServerRequest &request,
                                             QHttpServerResponder &responder) {
        handleRequest(request, responder);
    });

    emit logMessage(tr("文件共享已启动，监听端口 %1").arg(port));
    return true;
}

void FileServer::stop()
{
    if (m_server) {
        delete m_server;
        m_server = nullptr;
    }
    m_tcp = nullptr;
    emit logMessage(tr("文件共享已停止"));
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

    // Upload form
    body += QStringLiteral(
        "<form id=\"up\"><input type=\"file\" id=\"file\" required>"
        "<button type=\"submit\">上传到当前目录</button>"
        "<span id=\"msg\"></span></form>"
        "<script>"
        "document.getElementById('up').onsubmit=async function(e){"
        "e.preventDefault();"
        "var f=document.getElementById('file').files[0];"
        "if(!f)return;"
        "var rel=\"%1\";"
        "var r=await fetch('/upload/'+rel+'?name='+encodeURIComponent(f.name),{method:'POST',body:f});"
        "document.getElementById('msg').textContent=r.ok?'上传成功':'上传失败';"
        "setTimeout(function(){location.reload();},600);"
        "};"
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

void FileServer::handleRequest(const QHttpServerRequest &request, QHttpServerResponder &responder)
{
    const QUrl url = request.url();
    // 使用 FullyDecoded：路径中的 %2F 等会被正确还原为 '/',
    // 所以 "/browse/test%2Ffileshare" 能正确解析为子目录下的文件。
    const QString path = url.path(QUrl::FullyDecoded);
    const QUrlQuery query(url);

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
            QFile f(abs);
            if (!f.open(QIODevice::ReadOnly)) {
                responder.write(QHttpServerResponder::StatusCode::NotFound);
                return;
            }
            const QByteArray data = f.readAll();
            QMimeDatabase db;
            const QString mime = db.mimeTypeForFile(abs).name();
            const QString disp = QStringLiteral("attachment; filename*=UTF-8''%1")
                                    .arg(QString::fromLatin1(QUrl::toPercentEncoding(info.fileName())));
            QHttpHeaders hdr;
            hdr.append(QHttpHeaders::WellKnownHeader::ContentType, mime.toLatin1());
            hdr.append(QHttpHeaders::WellKnownHeader::ContentDisposition, disp.toLatin1());
            responder.write(data, hdr, QHttpServerResponder::StatusCode::Ok);
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
            QFile f(target);
            if (!f.open(QIODevice::WriteOnly)) {
                responder.write(QHttpServerResponder::StatusCode::InternalServerError);
                return;
            }
            f.write(request.body());
            f.close();
            emit logMessage(tr("Web 上传：%1").arg(target));
            QHttpHeaders hdr;
            hdr.append(QHttpHeaders::WellKnownHeader::ContentType,
                       QByteArray("text/plain; charset=utf-8"));
            responder.write(QByteArrayLiteral("OK"), hdr, QHttpServerResponder::StatusCode::Ok);
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
