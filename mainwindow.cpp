#include "mainwindow.h"

#include <DIconTheme>
#include <DInputDialog>
#include <DStatusBar>
#include <DSwitchButton>
#include <DTitlebar>

#include <QApplication>
#include <QClipboard>
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
#include <QNetworkInterface>
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
    void filesDropped(const QList<QUrl> &urls, const QModelIndex &index, bool internal);

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
        const QList<QUrl> urls = event->mimeData()->hasUrls()
                                     ? event->mimeData()->urls() : QList<QUrl>();
        event->setDropAction(internal ? Qt::MoveAction : Qt::CopyAction);
        if (!urls.isEmpty()) {
            event->accept();
            emit filesDropped(urls, idx, internal);
        } else {
            event->ignore();
        }
    }

private:
    void applyAction(QDropEvent *event)
    {
        const bool internal = (event->source() == this);
        event->setDropAction(internal ? Qt::MoveAction : Qt::CopyAction);
        if (event->mimeData()->hasUrls())
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
    topLayout->addWidget(m_pathLabel, 1);
    topLayout->addWidget(shareLabel);
    topLayout->addWidget(m_shareSwitch);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(8);
    centralLayout->addWidget(top);
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

    // ---- signals ----
    connect(backBtn, &DPushButton::clicked, this, &MainWindow::goUp);
    connect(newBtn, &DPushButton::clicked, this, &MainWindow::newFolder);
    connect(refreshBtn, &DPushButton::clicked, this, &MainWindow::refresh);
    connect(m_copyBtn, &DPushButton::clicked, this, &MainWindow::copySelection);
    connect(m_pasteBtn, &DPushButton::clicked, this, &MainWindow::paste);
    connect(m_view, &QListView::doubleClicked, this, &MainWindow::onDoubleClicked);
    connect(m_view, &QListView::customContextMenuRequested, this, &MainWindow::onCustomContextMenu);
    connect(m_view, &FileListView::filesDropped, this, &MainWindow::onFilesDropped);
    connect(m_shareSwitch, &DSwitchButton::toggled, this, &MainWindow::onToggleShare);

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

void MainWindow::onFilesDropped(const QList<QUrl> &urls, const QModelIndex &index, bool internal)
{
    if (urls.isEmpty())
        return;
    // 落点目录：
    //  - 拖到“返回上一级”项上 → 移动到当前目录的父目录
    //  - 拖到文件夹上 → 进入该文件夹
    //  - 其它（空白处/文件上） → 进入当前目录
    QString targetDir;
    if (m_proxy->isUp(index)) {
        const QString cur = currentDir();
        if (QDir(cur).canonicalPath() == QDir(m_shareRoot).canonicalPath())
            return;
        QDir dir(cur);
        if (!dir.cdUp())
            return;
        targetDir = dir.absolutePath();
    } else if (index.isValid() && m_model->isDir(m_proxy->mapToSource(index))) {
        targetDir = m_model->filePath(m_proxy->mapToSource(index));
    } else {
        targetDir = currentDir();
    }

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


void MainWindow::newFolder()
{
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
    refreshView();
}

void MainWindow::onCustomContextMenu(const QPoint &pos)
{
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
