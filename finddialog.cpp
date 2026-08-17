#include "finddialog.h"

#include <DLineEdit>
#include <DPushButton>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

DWIDGET_USE_NAMESPACE

FindDialog::FindDialog(Discovery *discovery, QWidget *parent)
    : QDialog(parent)
    , m_discovery(discovery)
{
    setWindowTitle(tr("查找其他机器"));
    resize(420, 360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // 输入行：机器名 + 查找/停止
    auto *row = new QHBoxLayout;
    m_input = new DLineEdit(this);
    m_input->setPlaceholderText(tr("要查找的机器名（支持正则，留空匹配所有）"));
    m_findBtn = new DPushButton(tr("查找"), this);
    row->addWidget(m_input, 1);
    row->addWidget(m_findBtn);
    layout->addLayout(row);

    layout->addWidget(new QLabel(
        tr("点击“查找”后，将每秒向局域网广播一次查询请求，匹配的机器会自动回复。"), this));

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list, 1);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(btnBox);

    connect(m_findBtn, &DPushButton::clicked, this, &FindDialog::onFindClicked);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &FindDialog::onItemDoubleClicked);
    connect(m_discovery, &Discovery::peerDiscovered, this, &FindDialog::onPeerDiscovered);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(btnBox->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &QDialog::reject);
    // 关闭时停止查询
    connect(this, &QDialog::finished, m_discovery, &Discovery::stopQuery);
}

void FindDialog::onFindClicked()
{
    if (m_discovery->isQuerying()) {
        m_discovery->stopQuery();
        m_findBtn->setText(tr("查找"));
    } else {
        m_seen.clear();
        m_list->clear();
        m_discovery->startQuery(m_input->text());
        m_findBtn->setText(tr("停止"));
    }
}

void FindDialog::onPeerDiscovered(const QString &name, const QString &ip)
{
    addResult(name, ip);
}

void FindDialog::addResult(const QString &name, const QString &ip)
{
    const QString key = name + QLatin1Char('@') + ip;
    if (m_seen.contains(key))
        return;
    m_seen.insert(key);

    auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(name, ip));
    item->setData(Qt::UserRole, name);
    item->setData(Qt::UserRole + 1, ip);
    m_list->addItem(item);
}

void FindDialog::onItemDoubleClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString name = item->data(Qt::UserRole).toString();
    const QString ip = item->data(Qt::UserRole + 1).toString();
    if (name.isEmpty() || ip.isEmpty())
        return;
    m_discovery->stopQuery();
    m_findBtn->setText(tr("查找"));
    emit peerSelected(name, ip);
    accept();
}
