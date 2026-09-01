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

#include "fileuploader.h"

#include <QCloseEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "authmanager.h"

namespace {

QString formatSize(double bytes)
{
    if (bytes < 0)
        bytes = 0;
    return QLocale().formattedDataSize(qint64(bytes), 1, QLocale::DataSizeSIFormat);
}

} // namespace

// ---------------------------------------------------------------- FileUploader

FileUploader::FileUploader(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

void FileUploader::setAuthManager(AuthManager *mgr)
{
    m_auth = mgr;
}

void FileUploader::setTarget(const Target &target)
{
    m_target = target;
}

void FileUploader::addFiles(const QStringList &paths)
{
    m_files.append(paths);
}

void FileUploader::start()
{
    if (m_running)
        return;
    if (m_files.isEmpty()) {
        emit finished(0, 0);
        return;
    }
    m_running = true;
    m_canceled = false;
    m_finishedEmitted = false;
    // 已缓存的授权头直接带上，省去一次 401 往返。
    if (m_target.authHeader.isEmpty() && m_auth)
        m_target.authHeader = m_auth->headerFor(m_target.ip, m_target.port);
    m_ok = 0;
    m_failed = 0;
    m_index = -1;
    m_bytesBefore = 0;
    m_totalBytes = 0;
    for (const QString &p : m_files)
        m_totalBytes += QFileInfo(p).size();
    m_clock.start();
    m_lastMs = 0;
    m_lastSent = 0;
    m_speed = 0;
    startNextFile();
}

void FileUploader::cancel()
{
    if (!m_running)
        return;
    m_canceled = true;
    m_file.close();
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->abort(); // 触发 finished，由 onChunkFinished 统一收尾
        return;
    }
    finishAll();
}

void FileUploader::startNextFile()
{
    if (m_canceled) {
        finishAll();
        return;
    }
    ++m_index;
    if (m_index >= m_files.count()) {
        finishAll();
        return;
    }

    const QString path = m_files.at(m_index);
    m_curName = QFileInfo(path).fileName();
    m_fileSize = QFileInfo(path).size();
    m_offset = 0;
    m_chunksSent = 0;
    m_authTried = false;
    m_file.close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        failCurrent(tr("无法读取文件"));
        return;
    }
    emit progress(m_bytesBefore, m_totalBytes, m_speed, m_curName, m_index + 1,
                  m_files.count());
    sendNextChunk();
}

void FileUploader::sendNextChunk()
{
    if (m_canceled)
        return;
    // 已发过至少一块且写到末尾即完成；空文件也需先发一个空块以在接收端建文件。
    if (m_offset >= m_fileSize && m_chunksSent > 0) {
        finishCurrentFile();
        return;
    }

    const qint64 want = qMin<qint64>(kChunkSize, m_fileSize - m_offset);
    if (want > 0) {
        m_chunk = m_file.read(want);
        if (m_chunk.size() != want) {
            failCurrent(tr("读取文件失败"));
            return;
        }
    } else {
        m_chunk.clear();
    }
    postChunk();
}

void FileUploader::postChunk()
{
    if (m_canceled)
        return;
    QNetworkRequest req = buildRequest(m_curName, m_offset, m_fileSize);
    if (!m_target.authHeader.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Authorization"), m_target.authHeader.toUtf8());

    QNetworkReply *reply = m_nam->post(req, m_chunk);
    m_reply = reply;
    QPointer<FileUploader> guard(this);
    // 自签证书（无授权模式下的 HTTPS 共享）不阻塞传输。
    connect(reply, &QNetworkReply::sslErrors, reply,
            [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
    connect(reply, &QNetworkReply::uploadProgress, this, [this, guard](qint64 sent, qint64) {
        if (!guard)
            return;
        const qint64 done = m_bytesBefore + m_offset + sent;
        const qint64 now = m_clock.elapsed();
        const qint64 dt = now - m_lastMs;
        if (dt >= 400) {
            m_speed = double(done - m_lastSent) * 1000.0 / double(dt);
            m_lastSent = done;
            m_lastMs = now;
        }
        emit progress(done, m_totalBytes, m_speed, m_curName, m_index + 1, m_files.count());
    });
    connect(reply, &QNetworkReply::finished, this, [this, guard, reply]() {
        if (!guard)
            return;
        onChunkFinished(reply);
    });
}

void FileUploader::onChunkFinished(QNetworkReply *reply)
{
    if (m_reply == reply)
        m_reply = nullptr;
    reply->deleteLater();

    if (m_canceled) {
        finishAll();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // 需要授权：握手一次拿到授权头后重发当前块，不必重传整个文件。
    if (status == 401 && !m_authTried && m_auth) {
        m_authTried = true;
        QPointer<FileUploader> guard(this);
        m_auth->ensureHeader(m_target.ip, m_target.port, m_target.secure,
                             [this, guard](const QString &header) {
                                 if (!guard)
                                     return;
                                 if (m_canceled) {
                                     finishAll();
                                     return;
                                 }
                                 if (header.isEmpty()) {
                                     failCurrent(tr("授权被拒绝"));
                                     return;
                                 }
                                 m_target.authHeader = header;
                                 postChunk(); // 重发当前块，不重新读文件
                             });
        return;
    }

    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        failCurrent(status == 401 ? tr("授权失败") : reply->errorString());
        return;
    }

    m_offset += m_chunk.size();
    ++m_chunksSent;
    sendNextChunk();
}

void FileUploader::failCurrent(const QString &error)
{
    m_file.close();
    m_bytesBefore += m_fileSize; // 失败文件也计入进度，避免总进度回退
    ++m_failed;
    emit fileFailed(m_curName, error);
    // 排队进入下一个文件，避免大量连续失败时递归过深。
    QMetaObject::invokeMethod(this, &FileUploader::startNextFile, Qt::QueuedConnection);
}

void FileUploader::finishCurrentFile()
{
    m_file.close();
    m_bytesBefore += m_fileSize;
    ++m_ok;
    emit progress(m_bytesBefore, m_totalBytes, m_speed, m_curName, m_index + 1,
                  m_files.count());
    startNextFile();
}

void FileUploader::finishAll()
{
    m_file.close();
    m_running = false;
    if (m_finishedEmitted)
        return;
    m_finishedEmitted = true;
    emit finished(m_ok, m_failed);
}

QNetworkRequest FileUploader::buildRequest(const QString &name, qint64 offset,
                                           qint64 total) const
{
    QUrl url;
    url.setScheme(m_target.secure ? QStringLiteral("https") : QStringLiteral("http"));
    url.setHost(m_target.ip);
    url.setPort(m_target.port);
    url.setPath(QStringLiteral("/upload")
                + (m_target.destRel.isEmpty() ? QStringLiteral("/") : m_target.destRel));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), name);
    q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    q.addQueryItem(QStringLiteral("total"), QString::number(total));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QByteArrayLiteral("application/octet-stream"));
    return req;
}

// -------------------------------------------------------- UploadProgressDialog

UploadProgressDialog::UploadProgressDialog(QWidget *parent)
    : Dtk::Widget::DDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setTitle(tr("正在上传"));
    setOnButtonClickedClose(false);

    m_uploader = new FileUploader(this);

    auto *content = new QWidget;
    auto *vbox = new QVBoxLayout(content);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(8);
    m_fileLabel = new QLabel(tr("准备上传…"));
    m_fileLabel->setWordWrap(true);
    m_bar = new Dtk::Widget::DProgressBar;
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    m_bar->setTextVisible(false);
    m_bar->setFixedWidth(420);
    m_statLabel = new QLabel(tr("0%"));
    vbox->addWidget(m_fileLabel);
    vbox->addWidget(m_bar);
    vbox->addWidget(m_statLabel);
    addContent(content);
    addButton(tr("取消"));

    connect(m_uploader, &FileUploader::progress, this, &UploadProgressDialog::updateUi);
    connect(m_uploader, &FileUploader::finished, this, &UploadProgressDialog::onUploadFinished);
    connect(this, &DDialog::buttonClicked, this, [this](int, const QString &) {
        // 上传中：取消；已完成（按钮已换成“关闭”）：由 setOnButtonClickedClose 关闭。
        if (!m_done)
            m_uploader->cancel();
    });
}

UploadProgressDialog::~UploadProgressDialog()
{
    m_closing = true;
    // 析构期间不再回调界面，仅中止在途请求。
    disconnect(m_uploader, nullptr, this, nullptr);
    if (m_uploader->isRunning())
        m_uploader->cancel();
}

void UploadProgressDialog::upload(const QString &machineName, const QString &ip, quint16 port,
                                  bool secure, const QString &destRel,
                                  const QStringList &files, AuthManager *auth)
{
    setTitle(tr("正在上传到 %1").arg(machineName));
    FileUploader::Target t;
    t.ip = ip;
    t.port = port;
    t.secure = secure;
    t.destRel = destRel;
    m_uploader->setAuthManager(auth);
    m_uploader->setTarget(t);
    m_uploader->addFiles(files);
    m_uploader->start();
    show();
    raise();
    activateWindow();
}

void UploadProgressDialog::updateUi(qint64 sent, qint64 total, double speed,
                                    const QString &name, int fileIndex, int fileCount)
{
    if (m_closing)
        return;
    const int percent = total > 0 ? int(qMin<qint64>(100, sent * 100 / total)) : 0;
    m_bar->setValue(percent);
    m_fileLabel->setText(tr("正在上传：%1（第 %2/%3 个）")
                             .arg(name, QString::number(fileIndex), QString::number(fileCount)));
    m_statLabel->setText(tr("%1%　%2 / %3%4")
                             .arg(QString::number(percent), formatSize(sent), formatSize(total),
                                  speed > 0 ? tr("　%1/s").arg(formatSize(speed)) : QString()));
}

void UploadProgressDialog::onUploadFinished(int ok, int fail)
{
    m_done = true;
    clearButtons();
    addButton(tr("关闭"));
    setOnButtonClickedClose(true);

    if (m_closing) {
        close();
        return;
    }

    if (fail == 0) {
        setTitle(tr("上传完成"));
        m_fileLabel->setText(tr("已上传 %n 个文件", "", ok));
        m_bar->setValue(100);
        m_statLabel->setText(formatSize(m_uploader->totalBytes()));
        // 全部成功时短暂停留后自动关闭，减少一次点击。
        QTimer::singleShot(900, this, [this]() {
            if (!m_closing)
                close();
        });
    } else {
        setTitle(tr("上传结束"));
        m_fileLabel->setText(tr("成功 %1 个，失败 %2 个")
                                 .arg(QString::number(ok), QString::number(fail)));
    }

    emit finished(ok, fail);
}

void UploadProgressDialog::closeEvent(QCloseEvent *event)
{
    if (!m_done && m_uploader->isRunning()) {
        // 上传中关闭：先中止传输，等收尾完成后再真正关闭。
        m_closing = true;
        m_uploader->cancel();
        event->ignore();
        return;
    }
    DDialog::closeEvent(event);
}
