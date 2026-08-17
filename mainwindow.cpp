#include "mainwindow.h"

#include <DComboBox>
#include <DIconTheme>
#include <DInputDialog>
#include <DStatusBar>
#include <DSwitchButton>
#include <DTitlebar>

#include "discovery.h"
#include "finddialog.h"
#include "remotemodel.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIdentityProxyModel>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QMimeData>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QShortcut>
#include <QUrl>
#include <QVBoxLayout>

// 自定义列表视图：区分“本窗口内拖拽(移动)”与“外部拖入(复制)”，
// 并自行接管落点逻辑（QFileSystemModel 自带的 drop 不支持目录）。
class FileListView : public QListView
{
    Q_OBJECT
public:
    using QListView::QListView;

signals:
    void filesDropped(const QMimeData *mime, const QModelIndex &index, bool internal);
    // 从远程视图拖出文件时发出（供主窗口预下载到本机缓存，使系统文件管理器可落地）
    void remoteFileDragged(const RemoteFileRef &ref, const QString &cachePath);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        QListView::dragEnterEvent(event);
        applyAction(event);
    }
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        QListView::dragMoveEvent(event);
        applyAction(event);
    }
    void dropEvent(QDropEvent *event) override
    {
        const bool internal = (event->source() == this);
        const QModelIndex idx = indexAt(event->position().toPoint());
        const QMimeData *mime = event->mimeData();
        const bool ok = mime && (mime->hasUrls()
                                 || mime->hasFormat(QString::fromUtf8(kRemoteFileMime)));
        event->setDropAction(internal ? Qt::MoveAction : Qt::CopyAction);
        if (ok) {
            event->accept();
            emit filesDropped(mime, idx, internal);
        } else {
            event->ignore();
        }
    }

    // 远程文件被拖出时，先通知主窗口预下载到本机缓存（供拖到系统文件管理器/桌面）。
    void startDrag(Qt::DropActions supportedActions) override
    {
        if (auto *rm = qobject_cast<RemoteFileModel *>(model())) {
            const QModelIndexList idxs = selectedIndexes();
            if (idxs.size() == 1) {
                RemoteFileRef ref;
                if (rm->refAt(idxs.first(), ref)) {
                    const QString cachePath = rm->localCachePath(idxs.first());
                    emit remoteFileDragged(ref, cachePath);
                }
            }
        }
        QListView::startDrag(supportedActions);
    }

private:
    void applyAction(QDropEvent *event)
    {
        const bool internal = (event->source() == this);
        event->setDropAction(internal ? Qt::MoveAction : Qt::CopyAction);
        const QMimeData *mime = event->mimeData();
        if (mime && (mime->hasUrls() || mime->hasFormat(QString::fromUtf8(kRemoteFileMime))))
            event->acceptProposedAction();
    }
};

DWIDGET_USE_NAMESPACE

#include "updirproxy.h"

MainWindow::MainWindow(QWidget *parent)
    : DMainWindow(parent)
{
    m_shareRoot = QDir::home().absoluteFilePath(QStringLiteral("myshare"));
    QDir().mkpath(m_shareRoot);

    setWindowTitle(tr("文件共享"));
    resize(900, 640);
    if (titlebar())
        titlebar()->setTitle(tr("文件共享"));
    setWindowIcon(Dtk::Gui::DIconTheme::findQIcon(QStringLiteral("folder")));

    // ---- file model & view ----
    m_model = new QFileSystemModel(this);
    m_model->setRootPath(m_shareRoot);
    m_model->setReadOnly(false); // 允许拖拽落点（写入文件）
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

    m_proxy = new UpDirProxy(m_shareRoot, this);
    m_proxy->setSourceModel(m_model);

    m_view = new FileListView(this);
    m_view->setModel(m_proxy);
    m_view->setRootIndex(QModelIndex()); // 平面代理：顶层即“当前目录”
    m_view->setViewMode(QListView::IconMode);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setSpacing(12);
    m_view->setUniformItemSizes(true);
    m_view->setGridSize(QSize(120, 92));
    m_view->setWordWrap(true);
    m_view->viewport()->setContentsMargins(10, 10, 10, 10);
    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDropIndicatorShown(true);
    m_view->setDragDropMode(QAbstractItemView::DragDrop);
    m_view->setDefaultDropAction(Qt::CopyAction);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // ---- top tool bar ----
    auto *backBtn = new DPushButton(tr("返回上级"), this);
    auto *newBtn = new DPushButton(tr("新建文件夹"), this);
    auto *refreshBtn = new DPushButton(tr("刷新"), this);
    m_copyBtn = new DPushButton(tr("复制"), this);
    m_pasteBtn = new DPushButton(tr("粘贴"), this);
    m_copyBtn->setEnabled(false);
    m_pasteBtn->setEnabled(false);
    m_pathLabel = new QLabel(this);
    m_pathLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    auto *shareLabel = new QLabel(tr("共享"), this);
    m_shareSwitch = new DSwitchButton(this);
    m_shareSwitch->setChecked(true);

    auto *top = new QWidget(this);
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(10, 8, 10, 8);
    topLayout->addWidget(backBtn);
    topLayout->addWidget(newBtn);
    topLayout->addWidget(refreshBtn);
    topLayout->addWidget(m_copyBtn);
    topLayout->addWidget(m_pasteBtn);
    topLayout->addWidget(shareLabel);
    topLayout->addWidget(m_shareSwitch);

    // ---- 地址栏（机器选择 + 当前目录 + 添加按钮）----
    m_machineCombo = new DComboBox(this);
    m_machineCombo->setMinimumWidth(180);
    m_machineCombo->addItem(tr("本机"));
    m_addBtn = new DPushButton(tr("添加"), this);
    auto *addrBar = new QWidget(this);
    auto *addrLayout = new QHBoxLayout(addrBar);
    addrLayout->setContentsMargins(10, 6, 10, 6);
    addrLayout->setSpacing(8);
    addrLayout->addWidget(new QLabel(tr("机器："), this));
    addrLayout->addWidget(m_machineCombo);
    addrLayout->addWidget(m_pathLabel, 1);
    addrLayout->addWidget(m_addBtn);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(8);
    centralLayout->addWidget(top);
    centralLayout->addWidget(addrBar);
    centralLayout->addWidget(m_view, 1);
    setCentralWidget(central);

    // ---- status bar ----
    m_statusBar = new DStatusBar(this);
    m_msgLabel = new QLabel(this);
    m_msgLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_urlLabel = new QLabel(this);
    m_urlLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_statusBar->addWidget(m_msgLabel, 1); // 左侧：瞬时日志
    m_statusBar->addPermanentWidget(new QLabel(tr("共享目录：~/myshare"), this));
    m_statusBar->addPermanentWidget(m_urlLabel, 1); // 右侧：Web 访问地址
    setStatusBar(m_statusBar);

    // ---- file server ----
    m_fileServer = new FileServer(this);
    m_fileServer->setShareRoot(m_shareRoot);
    connect(m_fileServer, &FileServer::logMessage, this, [this](const QString &msg) {
        m_msgLabel->setText(msg);
    });

    // ---- 局域网发现 + 远程浏览 ----
    m_discovery = new Discovery(this);
    connect(m_discovery, &Discovery::logMessage, this, [this](const QString &msg) {
        m_msgLabel->setText(msg);
    });
    m_remoteModel = new RemoteFileModel(this);
    m_remoteModel->setLocalShareRoot(m_shareRoot);
    connect(m_remoteModel, &RemoteFileModel::logMessage, this, [this](const QString &msg) {
        m_msgLabel->setText(msg);
    });
    connect(m_remoteModel, &RemoteFileModel::pathChanged, this, &MainWindow::onRemotePathChanged);

    // 跨机传输（下载远程文件 / 上传本地文件）使用的网络管理器
    m_xfer = new QNetworkAccessManager(this);
    connect(m_xfer, &QNetworkAccessManager::finished, this, &MainWindow::onTransferFinished);

    // 机器列表：默认本机
    MachineInfo local;
    local.isLocal = true;
    local.name = m_discovery->localMachineName();
    local.ip = m_discovery->localIp();
    m_machines.append(local);

    // ---- signals ----
    connect(backBtn, &DPushButton::clicked, this, &MainWindow::goUp);
    connect(newBtn, &DPushButton::clicked, this, &MainWindow::newFolder);
    connect(refreshBtn, &DPushButton::clicked, this, &MainWindow::refresh);
    connect(m_copyBtn, &DPushButton::clicked, this, &MainWindow::copySelection);
    connect(m_pasteBtn, &DPushButton::clicked, this, &MainWindow::paste);
    connect(m_view, &QListView::doubleClicked, this, &MainWindow::onDoubleClicked);
    connect(m_view, &QListView::customContextMenuRequested, this, &MainWindow::onCustomContextMenu);
    connect(m_view, &FileListView::filesDropped, this, &MainWindow::onFilesDropped);
    connect(m_view, &FileListView::remoteFileDragged, this, &MainWindow::onRemoteFileDragged);
    connect(m_shareSwitch, &DSwitchButton::toggled, this, &MainWindow::onToggleShare);
    connect(m_machineCombo, QOverload<int>::of(&DComboBox::currentIndexChanged),
            this, &MainWindow::onMachineChanged);
    connect(m_addBtn, &DPushButton::clicked, this, &MainWindow::onAddMachine);

    auto *copyShortcut = new QShortcut(QKeySequence::Copy, m_view);
    connect(copyShortcut, &QShortcut::activated, this, &MainWindow::copySelection);
    auto *pasteShortcut = new QShortcut(QKeySequence::Paste, m_view);
    connect(pasteShortcut, &QShortcut::activated, this, &MainWindow::paste);

    auto *clipboard = QApplication::clipboard();
    connect(clipboard, &QClipboard::dataChanged, this, [this]() {
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        m_pasteBtn->setEnabled(mime && mime->hasUrls());
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        m_copyBtn->setEnabled(!m_view->selectionModel()->selectedIndexes().isEmpty());
    });
    m_pasteBtn->setEnabled(clipboard->mimeData() && clipboard->mimeData()->hasUrls());

    setCurrentDir(m_shareRoot);
    updateShareInfo();
    onToggleShare(true); // start sharing on launch
}

MainWindow::~MainWindow() = default;

QString MainWindow::currentDir() const
{
    return m_proxy->currentDir();
}

QString MainWindow::relativePath(const QString &absPath) const
{
    QString root = QDir(m_shareRoot).canonicalPath();
    QString abs = QDir(absPath).canonicalPath();
    if (abs == root)
        return QStringLiteral("myshare");
    if (abs.startsWith(root + QLatin1Char('/')))
        return QStringLiteral("myshare/") + abs.mid(root.length() + 1);
    return abs;
}

void MainWindow::setCurrentDir(const QString &path)
{
    m_proxy->setCurrentDir(path);
    m_pathLabel->setText(relativePath(path));
}

void MainWindow::refreshView()
{
    m_proxy->setCurrentDir(currentDir()); // 重新 set 触发整体 reset/刷新
}

void MainWindow::onDoubleClicked(const QModelIndex &index)
{
    if (m_remote) {
        const RemoteEntry e = m_remoteModel->entryAt(index);
        if (e.isUp) {
            m_remoteModel->cdUp();
            return;
        }
        if (e.isDir) {
            const QString base = m_remoteModel->currentRelPath();
            const QString next = base.isEmpty() ? (QStringLiteral("/") + e.name)
                                                : (base + QLatin1Char('/') + e.name);
            m_remoteModel->cd(next);
            return;
        }
        // 文件：下载到本机缓存目录 ~/myshare/.cache/<机器名>/<远程目录>/ 并执行默认操作
        const QString dest = m_remoteModel->localCachePath(index);
        const QString base = m_remoteModel->currentRelPath();
        const QString relPath = base.isEmpty() ? (QStringLiteral("/") + e.name)
                                               : (base + QLatin1Char('/') + e.name);
        startDownload(m_remoteModel->remoteIp(), m_remoteModel->remotePort(),
                      relPath, e.name, dest, true);
        return;
    }
    if (m_proxy->isUp(index)) {
        goUp();
        return;
    }
    const QModelIndex src = m_proxy->mapToSource(index);
    if (m_model->isDir(src))
        setCurrentDir(m_model->filePath(src));
}

void MainWindow::goUp()
{
    if (m_remote) {
        m_remoteModel->cdUp();
        return;
    }
    const QString cur = QDir(currentDir()).canonicalPath();
    const QString root = QDir(m_shareRoot).canonicalPath();
    if (cur.isEmpty() || cur == root)
        return; // already at the share root
    QDir dir(cur);
    if (dir.cdUp())
        setCurrentDir(dir.absolutePath());
}

void MainWindow::copySelection()
{
    if (m_remote) {
        m_msgLabel->setText(tr("远程目录为只读，无法复制"));
        return;
    }
    const QModelIndexList idxs = m_view->selectionModel()->selectedIndexes();
    if (idxs.isEmpty())
        return;
    QList<QUrl> urls;
    for (const QModelIndex &idx : idxs) {
        if (idx.column() != 0)
            continue;
        if (m_proxy->isUp(idx))
            continue;
        urls.append(QUrl::fromLocalFile(m_model->filePath(m_proxy->mapToSource(idx))));
    }
    if (urls.isEmpty())
        return;
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    QApplication::clipboard()->setMimeData(mime);
    m_msgLabel->setText(tr("已复制 %n 项", "", urls.size()));
}

void MainWindow::paste()
{
    if (m_remote) {
        m_msgLabel->setText(tr("远程目录为只读，无法粘贴"));
        return;
    }
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls())
        return;
    const QString destDir = currentDir();
    int copied = 0;
    for (const QUrl &u : mime->urls()) {
        const QString src = u.toLocalFile();
        if (src.isEmpty())
            continue;
        const QFileInfo fi(src);
        if (!fi.exists())
            continue;
        const QString dest = makeUniqueDest(destDir, fi.fileName());
        if (copyPath(src, dest))
            ++copied;
    }
    refreshView();
    if (copied > 0)
        m_msgLabel->setText(tr("已粘贴 %n 项", "", copied));
}

QString MainWindow::makeUniqueDest(const QString &dir, const QString &name)
{
    QString candidate = QDir(dir).filePath(name);
    if (!QFile::exists(candidate) && !QDir(candidate).exists())
        return candidate;
    const QFileInfo fi(name);
    const QString base = fi.baseName();
    const QString ext = fi.completeSuffix();
    const QString suffix = ext.isEmpty() ? QString() : QStringLiteral(".") + ext;
    for (int n = 1; n < 1000; ++n) {
        const QString newName = base + tr(" - 副本%1").arg(n) + suffix;
        candidate = QDir(dir).filePath(newName);
        if (!QFile::exists(candidate) && !QDir(candidate).exists())
            return candidate;
    }
    return candidate;
}

bool MainWindow::copyPath(const QString &src, const QString &dst)
{
    const QFileInfo fi(src);
    if (fi.isDir()) {
        QDir().mkpath(dst);
        const QDir srcDir(src);
        const QFileInfoList list = srcDir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot);
        bool ok = true;
        for (const QFileInfo &child : list) {
            if (!copyPath(child.absoluteFilePath(), QDir(dst).filePath(child.fileName())))
                ok = false;
        }
        return ok;
    }
    if (QFile::exists(dst) || QDir(dst).exists())
        return false;
    return QFile::copy(src, dst);
}

bool MainWindow::movePath(const QString &src, const QString &dst)
{
    if (QFile::exists(dst) || QDir(dst).exists())
        return false;
    // 同文件系统直接用 rename 移动（对文件与目录均有效）
    if (QFile::rename(src, dst))
        return true;
    // 跨文件系统回退：先复制再删除源
    if (copyPath(src, dst)) {
        const QFileInfo fi(src);
        if (fi.isDir())
            QDir(src).removeRecursively();
        else
            QFile::remove(src);
        return true;
    }
    return false;
}

void MainWindow::onFilesDropped(const QMimeData *mime, const QModelIndex &index, bool internal)
{
    if (!mime)
        return;

    if (m_remote) {
        // 远程视图内拖动（拖出又落回自身）视为无效操作
        RemoteFileRef ref;
        if (internal && RemoteFileModel::parseRemoteMime(mime, ref)) {
            m_msgLabel->setText(tr("不支持在远程视图内拖动"));
            return;
        }
        // 拖入本地文件 → 上传到当前远程机器
        if (!mime->hasUrls()) {
            m_msgLabel->setText(tr("不支持的操作"));
            return;
        }
        const QString destRel = remoteDropTargetRel(index);
        int done = 0;
        for (const QUrl &u : mime->urls()) {
            const QString src = u.toLocalFile();
            if (src.isEmpty())
                continue;
            if (uploadLocalFile(src, destRel))
                ++done;
        }
        if (done > 0)
            m_msgLabel->setText(tr("已上传 %n 项，正在刷新远程目录…", "", done));
        return;
    }

    // 本地模式：区分“远程文件拖入（下载）”与“本地文件（复制/移动）”
    RemoteFileRef ref;
    if (RemoteFileModel::parseRemoteMime(mime, ref)) {
        const QString destDir = localDropTargetDir(index);
        downloadRemoteFile(ref.ip, ref.port, ref.relPath, ref.name, destDir);
        return;
    }
    if (mime->hasUrls()) {
        const QList<QUrl> urls = mime->urls();
        if (urls.isEmpty())
            return;
        const QString targetDir = localDropTargetDir(index);
        int done = 0;
        for (const QUrl &u : urls) {
            const QString src = u.toLocalFile();
            if (src.isEmpty())
                continue;
            const QFileInfo fi(src);
            if (!fi.exists())
                continue;
            // 拖到自身所在目录且同名：无操作
            if (QDir(targetDir).canonicalPath() == QDir(fi.absolutePath()).canonicalPath()
                && fi.fileName() == QFileInfo(makeUniqueDest(targetDir, fi.fileName())).fileName())
                continue;
            const QString dest = makeUniqueDest(targetDir, fi.fileName());
            if (internal ? movePath(src, dest) : copyPath(src, dest))
                ++done;
        }
        refreshView();
        if (done > 0)
            m_msgLabel->setText(internal ? tr("已移动 %n 项", "", done)
                                         : tr("已复制 %n 项", "", done));
    }
}

QString MainWindow::localDropTargetDir(const QModelIndex &index) const
{
    if (m_proxy->isUp(index)) {
        const QString cur = currentDir();
        if (QDir(cur).canonicalPath() == QDir(m_shareRoot).canonicalPath())
            return cur;
        QDir dir(cur);
        if (dir.cdUp())
            return dir.absolutePath();
        return cur;
    } else if (index.isValid() && m_model->isDir(m_proxy->mapToSource(index))) {
        return m_model->filePath(m_proxy->mapToSource(index));
    }
    return currentDir();
}

QString MainWindow::remoteDropTargetRel(const QModelIndex &index) const
{
    if (m_remoteModel->isUp(index)) {
        const QString cur = m_remoteModel->currentRelPath();
        if (cur.isEmpty())
            return QString();
        const int i = cur.lastIndexOf(QLatin1Char('/'));
        return (i <= 0) ? QString() : cur.left(i);
    }
    const RemoteEntry e = m_remoteModel->entryAt(index);
    if (e.isDir) {
        const QString cur = m_remoteModel->currentRelPath();
        return cur.isEmpty() ? (QStringLiteral("/") + e.name)
                             : (cur + QLatin1Char('/') + e.name);
    }
    return m_remoteModel->currentRelPath();
}

void MainWindow::startDownload(const QString &ip, quint16 port, const QString &relPath,
                                const QString &name, const QString &destPath, bool openAfter)
{
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(ip);
    url.setPort(port);
    url.setPath(QStringLiteral("/browse") + relPath); // relPath 形如 "/file.txt"
    QNetworkRequest req(url);
    QNetworkReply *reply = m_xfer->get(req);
    reply->setProperty("op", QStringLiteral("download"));
    reply->setProperty("destPath", destPath);
    reply->setProperty("name", name);
    reply->setProperty("open", openAfter);
    m_msgLabel->setText(tr("正在下载：%1").arg(name));
}

void MainWindow::downloadRemoteFile(const QString &ip, quint16 port, const QString &relPath,
                                    const QString &name, const QString &destDir)
{
    const QString dest = makeUniqueDest(destDir, name);
    startDownload(ip, port, relPath, name, dest, false);
}

void MainWindow::onRemoteFileDragged(const RemoteFileRef &ref, const QString &cachePath)
{
    // 拖出到系统文件管理器/桌面：预下载到本机缓存，使落点文件可被系统复制
    startDownload(ref.ip, ref.port, ref.relPath, ref.name, cachePath, false);
}

bool MainWindow::uploadLocalFile(const QString &localPath, const QString &destRel)
{
    QFile f(localPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QFileInfo fi(localPath);
    const QString name = fi.fileName();
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_remoteModel->remoteIp());
    url.setPort(m_remoteModel->remotePort());
    url.setPath(QStringLiteral("/upload") + (destRel.isEmpty() ? QStringLiteral("/") : destRel));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), name);
    url.setQuery(q);
    QNetworkRequest req(url);
    const QByteArray data = f.readAll();
    f.close();
    QNetworkReply *reply = m_xfer->post(req, data);
    reply->setProperty("op", QStringLiteral("upload"));
    reply->setProperty("name", name);
    m_msgLabel->setText(tr("正在上传：%1").arg(name));
    return true;
}

void MainWindow::onTransferFinished(QNetworkReply *reply)
{
    if (!reply)
        return;
    const QString op = reply->property("op").toString();
    const QString name = reply->property("name").toString();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        m_msgLabel->setText(tr("%1失败：%2").arg(op == QStringLiteral("download")
                                                    ? tr("下载") : tr("上传"),
                                                reply->errorString()));
        reply->deleteLater();
        return;
    }
    if (op == QStringLiteral("download")) {
        const QString dest = reply->property("destPath").toString();
        QFile out(dest);
        if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size()) {
            out.close();
            m_msgLabel->setText(tr("已下载：%1").arg(name));
            if (reply->property("open").toBool()) {
                // 双击下载后执行默认操作（用系统默认程序打开）
                QDesktopServices::openUrl(QUrl::fromLocalFile(dest));
            }
            refreshView();
        } else {
            m_msgLabel->setText(tr("保存失败：%1").arg(dest));
        }
    } else if (op == QStringLiteral("upload")) {
        m_msgLabel->setText(tr("已上传：%1").arg(name));
        if (m_remote)
            m_remoteModel->refresh();
    }
    reply->deleteLater();
}


void MainWindow::newFolder()
{
    if (m_remote) {
        m_msgLabel->setText(tr("远程目录为只读，无法新建文件夹"));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("新建文件夹"),
                                               tr("请输入文件夹名称："), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || name.isEmpty())
        return;
    const QString target = QDir(currentDir()).filePath(name);
    if (QDir().mkpath(target)) {
        refreshView();
    } else {
        QMessageBox::warning(this, tr("新建失败"), tr("无法创建文件夹：%1").arg(name));
    }
}

void MainWindow::refresh()
{
    if (m_remote) {
        m_remoteModel->refresh();
        return;
    }
    refreshView();
}

void MainWindow::onCustomContextMenu(const QPoint &pos)
{
    if (m_remote) {
        QMenu menu(this);
        QAction *actRefresh = menu.addAction(tr("刷新"));
        if (menu.exec(m_view->viewport()->mapToGlobal(pos)) == actRefresh)
            refresh();
        return;
    }

    QMenu menu(this);
    QAction *actNew = menu.addAction(tr("新建文件夹"));
    QAction *actRefresh = menu.addAction(tr("刷新"));
    menu.addSeparator();
    QAction *actCopy = menu.addAction(tr("复制"));
    QAction *actPaste = menu.addAction(tr("粘贴"));
    menu.addSeparator();
    QAction *actDelete = menu.addAction(tr("删除"));
    QAction *actOpen = menu.addAction(tr("打开"));

    const QModelIndex idx = m_view->indexAt(pos);
    const bool hasSelection = idx.isValid();
    const bool canCopy = !m_view->selectionModel()->selectedIndexes().isEmpty();
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    const bool canPaste = mime && mime->hasUrls();
    actCopy->setEnabled(canCopy);
    actPaste->setEnabled(canPaste);
    actDelete->setEnabled(hasSelection);
    actOpen->setEnabled(hasSelection);

    QAction *chosen = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (chosen == actNew) {
        newFolder();
    } else if (chosen == actRefresh) {
        refresh();
    } else if (chosen == actCopy && canCopy) {
        copySelection();
    } else if (chosen == actPaste && canPaste) {
        paste();
    } else if (chosen == actDelete && hasSelection) {
        if (m_proxy->isUp(idx))
            return; // “返回上一级”不可删除
        const QString path = m_model->filePath(m_proxy->mapToSource(idx));
        const QFileInfo info(path);
        bool removed = info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (removed)
            refreshView();
        else
            QMessageBox::warning(this, tr("删除失败"), tr("无法删除：%1").arg(info.fileName()));
    } else if (chosen == actOpen && hasSelection) {
        onDoubleClicked(idx);
    }
}

void MainWindow::updateCopyBtn()
{
    if (m_remote)
        return;
    m_copyBtn->setEnabled(!m_view->selectionModel()->selectedIndexes().isEmpty());
}

void MainWindow::updateAddressBar()
{
    if (m_remote) {
        const QString rel = m_remoteModel->currentRelPath();
        m_pathLabel->setText(tr("目录：%1").arg(rel.isEmpty() ? QStringLiteral("/") : rel));
    } else {
        m_pathLabel->setText(relativePath(currentDir()));
    }
}

void MainWindow::showLocal()
{
    m_remote = false;
    m_view->setModel(m_proxy);
    m_view->setRootIndex(QModelIndex());
    m_view->setAcceptDrops(true);
    m_view->setDragEnabled(true);
    m_copyBtn->setEnabled(false);
    m_pasteBtn->setEnabled(QApplication::clipboard()->mimeData()
                           && QApplication::clipboard()->mimeData()->hasUrls());
    setCurrentDir(m_shareRoot); // 重置代理到共享根目录并刷新
    updateAddressBar();

    if (m_selConn)
        disconnect(m_selConn);
    m_selConn = connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
                        this, &MainWindow::updateCopyBtn);
}

void MainWindow::showRemote(const MachineInfo &m)
{
    m_remote = true;
    m_view->setModel(m_remoteModel);
    m_view->setRootIndex(QModelIndex());
    m_view->setAcceptDrops(true);  // 允许把本地文件拖入以“上传”
    m_view->setDragEnabled(true); // 允许把远程文件拖出以“下载”
    m_copyBtn->setEnabled(false);
    m_pasteBtn->setEnabled(false);
    m_remoteModel->setTarget(m.name, m.ip, 5000);
    updateAddressBar();
}

void MainWindow::addMachine(const QString &name, const QString &ip)
{
    for (const MachineInfo &m : m_machines) {
        if (!m.isLocal && m.name == name && m.ip == ip)
            return; // 已存在，避免重复
    }
    MachineInfo m;
    m.isLocal = false;
    m.name = name;
    m.ip = ip;
    m_machines.append(m);
    m_machineCombo->addItem(QStringLiteral("%1 (%2)").arg(name, ip));
    m_machineCombo->setCurrentIndex(m_machines.size() - 1); // 自动选中新加入的机器
}

void MainWindow::onMachineChanged(int index)
{
    if (index < 0 || index >= m_machines.size() || index == m_currentMachine)
        return;
    m_currentMachine = index;
    const MachineInfo &m = m_machines.at(index);
    if (m.isLocal)
        showLocal();
    else
        showRemote(m);
}

void MainWindow::onAddMachine()
{
    auto *dlg = new FindDialog(m_discovery, this);
    connect(dlg, &FindDialog::peerSelected, this, &MainWindow::onPeerSelected);
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    dlg->exec();
}

void MainWindow::onPeerSelected(const QString &name, const QString &ip)
{
    addMachine(name, ip);
}

void MainWindow::onRemotePathChanged(const QString &)
{
    updateAddressBar();
}

void MainWindow::onToggleShare(bool on)
{
    if (on) {
        if (!m_fileServer->start(5000))
            m_shareSwitch->setChecked(false);
    } else {
        m_fileServer->stop();
    }
    updateShareInfo();
}

void MainWindow::updateShareInfo()
{
    QString localUrl = QStringLiteral("http://localhost:5000");
    QString lanUrl;
    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : addrs) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && a != QHostAddress::LocalHost) {
            lanUrl = QStringLiteral("http://%1:5000").arg(a.toString());
            break;
        }
    }
    QString text = m_fileServer->isRunning()
                       ? tr("Web 访问（本机）：%1    局域网：%2")
                             .arg(localUrl, lanUrl.isEmpty() ? tr("无网络连接") : lanUrl)
                       : tr("共享已关闭");
    m_urlLabel->setText(text);
}

#include "mainwindow.moc"
