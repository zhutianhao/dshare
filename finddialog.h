#pragma once

#include <QDialog>
#include <QListWidget>
#include <QSet>
#include <QString>

#include <DLineEdit>
#include <DPushButton>

#include "discovery.h"

// 查找其他 deepin 客户端机器的对话框。
// 输入机器名（作为正则）后点击“查找”，通过 Discovery 每秒发送一次组播查询，
// 将收到的应答去重显示在下方列表；双击列表项即选中该机器。
class FindDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FindDialog(Discovery *discovery, QWidget *parent = nullptr);

signals:
    // 选中某台机器后发出，由主窗口加入地址栏下拉框。
    void peerSelected(const QString &name, const QString &ip);

private slots:
    void onFindClicked();
    void onPeerDiscovered(const QString &name, const QString &ip);
    void onItemDoubleClicked(QListWidgetItem *item);

private:
    void addResult(const QString &name, const QString &ip);

    Discovery *m_discovery = nullptr;
    Dtk::Widget::DLineEdit *m_input = nullptr;
    Dtk::Widget::DPushButton *m_findBtn = nullptr;
    QListWidget *m_list = nullptr;
    QSet<QString> m_seen;
};
