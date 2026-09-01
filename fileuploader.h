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

#include <DDialog>
#include <DProgressBar>

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QStringList>

class AuthManager;
class QCloseEvent;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

// 客户端侧的分片上传器。
// 大文件一次性 POST 会同时把发送端和接收端（QHttpServer 的 request.body()）
// 的内存打爆——QByteArray 上限 2G，超过即异常退出。因此这里把文件切成固定大小的
// 块，逐块 POST 到服务端的 /upload 接口（带 offset/total 查询串），服务端按偏移
// 追加写入。同一文件的授权（401）握手只做一次，成功后重发当前块继续。
class FileUploader : public QObject
{
    Q_OBJECT
public:
    // 一次上传的目标共享端信息。
    struct Target
    {
        QString ip;
        quint16 port = 5000;
        bool secure = true;    // 对方是否以 HTTPS 提供共享
        QString destRel;       // 目标相对目录（空串表示共享根）
        QString authHeader;    // 授权头，可为空（401 时自动握手获取）
    };

    explicit FileUploader(QObject *parent = nullptr);

    void setAuthManager(AuthManager *mgr);
    void setTarget(const Target &target);
    void addFiles(const QStringList &paths);
    void start();
    void cancel();

    bool isRunning() const { return m_running; }
    int fileCount() const { return m_files.count(); }
    qint64 totalBytes() const { return m_totalBytes; }

signals:
    // bytesSent/bytesTotal 为整个队列的字节进度；speed 为字节/秒；
    // curName/fileIndex/fileCount 描述当前正在上传的文件。
    void progress(qint64 bytesSent, qint64 bytesTotal, double speedBytesPerSec,
                  const QString &curName, int fileIndex, int fileCount);
    void fileFailed(const QString &name, const QString &error);
    void finished(int succeeded, int failed);

private:
    void startNextFile();
    void sendNextChunk();
    void postChunk(); // 发送 m_chunk（401 重试时复用，不重新读文件）
    void onChunkFinished(QNetworkReply *reply);
    void failCurrent(const QString &error);
    void finishCurrentFile();
    void finishAll();
    QNetworkRequest buildRequest(const QString &name, qint64 offset, qint64 total) const;

    static constexpr qint64 kChunkSize = 4 * 1024 * 1024;

    QNetworkAccessManager *m_nam = nullptr;
    AuthManager *m_auth = nullptr;
    Target m_target;
    QStringList m_files;
    int m_index = -1;          // 当前文件序号
    QString m_curName;
    QFile m_file;
    qint64 m_fileSize = 0;
    qint64 m_offset = 0;       // 当前文件已确认写入的字节数
    int m_chunksSent = 0;      // 当前文件已发送的块数（用于空文件）
    QByteArray m_chunk;        // 当前块内容（便于 401 后重发）
    QNetworkReply *m_reply = nullptr;
    bool m_authTried = false;  // 当前文件是否已尝试过授权握手
    bool m_running = false;
    bool m_canceled = false;
    bool m_finishedEmitted = false;
    int m_ok = 0;
    int m_failed = 0;

    qint64 m_totalBytes = 0;   // 队列总字节数
    qint64 m_bytesBefore = 0;  // 已完成文件累计字节数
    QElapsedTimer m_clock;     // 用于计算上传速度
    qint64 m_lastMs = 0;
    qint64 m_lastSent = 0;
    double m_speed = 0;
};

// 上传进度窗口：显示当前文件、总进度条、实时速度与整体百分比，
// 用户可随时取消（取消会中止后续分片，已写入的部分留在接收端）。
class UploadProgressDialog : public Dtk::Widget::DDialog
{
    Q_OBJECT
public:
    explicit UploadProgressDialog(QWidget *parent = nullptr);
    ~UploadProgressDialog() override;

    void upload(const QString &machineName, const QString &ip, quint16 port, bool secure,
                const QString &destRel, const QStringList &files, AuthManager *auth);

signals:
    void finished(int okCount, int failCount);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void updateUi(qint64 sent, qint64 total, double speed, const QString &name,
                  int fileIndex, int fileCount);
    void onUploadFinished(int ok, int fail);

    FileUploader *m_uploader = nullptr;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_statLabel = nullptr;
    Dtk::Widget::DProgressBar *m_bar = nullptr;
    bool m_closing = false;
    bool m_done = false;
};
