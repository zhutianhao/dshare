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

#include <DMainWindow>
#include <DStatusBar>
#include <DSwitchButton>
#include <QFileSystemModel>
#include <QNetworkReply>

#include "fileserver.h"
#include "remotemodel.h"
#include "authmanager.h"

QT_BEGIN_NAMESPACE
class QListView;
class QLabel;
class QModelIndex;
QT_END_NAMESPACE

#include <DComboBox>

class FileListView;
class UpDirProxy;
class Discovery;
class FindDialog;

// 地址栏机器下拉框中的一项。
struct MachineInfo
{
    bool isLocal = true;
    QString name; // 机器名（本机为本地主机名）
    QString ip;   // 局域网 IP（本机可空）
};

class MainWindow : public Dtk::Widget::DMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onDoubleClicked(const QModelIndex &index);
    void goUp();
    void newFolder();
    void refresh();
    void copySelection();
    void paste();
    void onFilesDropped(const QMimeData *mime, const QModelIndex &index, bool internal);
    void onCustomContextMenu(const QPoint &pos);
    void onToggleRequireAuth(bool on);
    void onAuthRequested(const QString &token, const QString &name, const QString &ip);
    void processAuthQueue();
    void updateShareInfo();

    void onMachineChanged(int index);
    void onAddMachine();
    void onPeerSelected(const QString &name, const QString &ip);
    void onRemotePathChanged(const QString &relPath);
    void updateCopyBtn();
    void onTransferFinished(QNetworkReply *reply);
    void onRemoteFileDragged(const RemoteFileRef &ref, const QString &cachePath);

private:
    QString localDropTargetDir(const QModelIndex &index) const;
    QString remoteDropTargetRel(const QModelIndex &index) const;
    void startDownload(const QString &ip, quint16 port, const QString &relPath,
                       const QString &name, const QString &destPath, bool openAfter,
                       const QString &authHeader = QString());
    void downloadRemoteFile(const QString &ip, quint16 port, const QString &relPath,
                            const QString &name, const QString &destDir);
    bool uploadLocalFile(const QString &localPath, const QString &destRel,
                         const QString &authHeader = QString());

private:
    QString currentDir() const;
    void setCurrentDir(const QString &path);
    QString relativePath(const QString &absPath) const;
    QString makeUniqueDest(const QString &dir, const QString &name);
    bool copyPath(const QString &src, const QString &dst);
    bool movePath(const QString &src, const QString &dst);
    void refreshView();
    void showLocal();
    void showRemote(const MachineInfo &m);
    void updateAddressBar();
    void addMachine(const QString &name, const QString &ip);

    QFileSystemModel *m_model = nullptr;
    UpDirProxy *m_proxy = nullptr;
    FileListView *m_view = nullptr;
    QLabel *m_pathLabel = nullptr;
    Dtk::Widget::DPushButton *m_copyBtn = nullptr;
    Dtk::Widget::DPushButton *m_pasteBtn = nullptr;
    Dtk::Widget::DStatusBar *m_statusBar = nullptr;
    Dtk::Widget::DSwitchButton *m_shareSwitch = nullptr;
    QLabel *m_urlLabel = nullptr;
    QLabel *m_msgLabel = nullptr;

    // 地址栏：机器选择下拉框 + 添加按钮
    Dtk::Widget::DComboBox *m_machineCombo = nullptr;
    Dtk::Widget::DPushButton *m_addBtn = nullptr;
    QMetaObject::Connection m_selConn;

    FileServer *m_fileServer = nullptr;
    QString m_shareRoot;

    // 授权请求弹窗队列（共享端收到他人访问请求时按序弹出）
    struct AuthReq
    {
        QString token;
        QString name;
        QString ip;
    };
    QList<AuthReq> m_authQueue;
    bool m_authDlgOpen = false;
    AuthManager *m_auth = nullptr;

    // 局域网发现与远程浏览
    Discovery *m_discovery = nullptr;
    RemoteFileModel *m_remoteModel = nullptr;
    QList<MachineInfo> m_machines;
    bool m_remote = false;
    int m_currentMachine = 0;
    QNetworkAccessManager *m_xfer = nullptr;
};

