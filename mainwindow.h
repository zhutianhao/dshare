#pragma once

#include <DMainWindow>
#include <DStatusBar>
#include <DSwitchButton>
#include <QFileSystemModel>

#include "fileserver.h"

QT_BEGIN_NAMESPACE
class QListView;
class QLabel;
QT_END_NAMESPACE

class FileListView;
class UpDirProxy;

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
    void onFilesDropped(const QList<QUrl> &urls, const QModelIndex &index, bool internal);
    void onCustomContextMenu(const QPoint &pos);
    void onToggleShare(bool on);
    void updateShareInfo();

private:
    QString currentDir() const;
    void setCurrentDir(const QString &path);
    QString relativePath(const QString &absPath) const;
    QString makeUniqueDest(const QString &dir, const QString &name);
    bool copyPath(const QString &src, const QString &dst);
    bool movePath(const QString &src, const QString &dst);
    void refreshView();

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

    FileServer *m_fileServer = nullptr;
    QString m_shareRoot;
};
