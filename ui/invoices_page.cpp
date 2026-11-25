#include "invoices_page.h"

#include <QAbstractButton>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <QCoreApplication>

#include "core/format_utils.h"
#include "data/customer_repository.h"
#include "data/date_utils.h"
#include "data/product_repository.h"
#include "data/purchase_item_repository.h"
#include "data/purchase_repository.h"
#include "data/sale_repository.h"
#include "data/service_invoice_repository.h"
#include "data/supplier_repository.h"
#include "dialogs/service_invoice_dialog.h"
#include "sales_page.h"
#include "scan_safe_dialog.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

constexpr int kColId = 0;

QString tr(const char* text)
{
    return QCoreApplication::translate("InvoicesPage", text);
}

QString displayDate(const QString& value)
{
    if (value.isEmpty()) {
        return QStringLiteral("—");
    }
    const QDate date = QDate::fromString(value.left(10), Qt::ISODate);
    if (date.isValid()) {
        return date.toString(QStringLiteral("dd/MM/yyyy"));
    }
    const auto stamp = data::fromIso(value);
    if (stamp.has_value()) {
        return stamp->date().toString(QStringLiteral("dd/MM/yyyy"));
    }
    return value;
}

QString serviceTypeLabel(const QString& type)
{
    if (type == QStringLiteral("electricity")) {
        return tr("كهرباء");
    }
    if (type == QStringLiteral("gas")) {
        return tr("غاز");
    }
    if (type == QStringLiteral("water")) {
        return tr("ماء");
    }
    return type;
}

QString serviceStatusLabel(const QString& status)
{
    if (status == QStringLiteral("paid")) {
        return tr("مدفوعة");
    }
    if (status == QStringLiteral("unpaid")) {
        return tr("غير مدفوعة");
    }
    return status;
}

QTableWidget* makeTable(const QStringList& headers, const QString& objectName)
{
    auto* table = new QTableWidget;
    table->setObjectName(objectName);
    table->setAlternatingRowColors(true);
    table->setFrameShape(QFrame::NoFrame);
    table->setShowGrid(true);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setDefaultSectionSize(42);
    table->verticalHeader()->hide();
    return table;
}

void setRow(QTableWidget* table, const QStringList& cells, int id)
{
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < cells.size(); ++column) {
        auto* item = new QTableWidgetItem(cells.at(column));
        if (column == kColId) {
            item->setData(Qt::UserRole, id);
        }
        table->setItem(row, column, item);
    }
}

void filterTable(QTableWidget* table, const QString& text)
{
    if (!table) {
        return;
    }
    const QString needle = text.trimmed();
    for (int row = 0; row < table->rowCount(); ++row) {
        bool matches = needle.isEmpty();
        for (int column = 0; column < table->columnCount() && !matches; ++column) {
            const QTableWidgetItem* item = table->item(row, column);
            if (item && item->text().contains(needle, Qt::CaseInsensitive)) {
                matches = true;
            }
        }
        table->setRowHidden(row, !matches);
    }
}

void showPurchaseDetails(QWidget* parent, app::data::Database& db, int purchaseId)
{
    data::PurchaseRepository purchases(db);
    const auto purchase = purchases.findById(purchaseId);
    if (!purchase.has_value()) {
        return;
    }

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(tr("تفاصيل فاتورة الشراء #%1").arg(purchase->id));
    dialog.setModal(true);

    const auto supplier = data::SupplierRepository(db).findById(purchase->supplierId);
    const QString supplierName = supplier.has_value() ? supplier->name : QStringLiteral("—");
    const long long remaining = purchase->totalCents - purchase->paidCents;

    auto* summary = new QLabel(
        tr("المورد: %1\nرقم الفاتورة: %2\nالتاريخ: %3\nالإجمالي: %4    المدفوع: %5    المتبقي: %6")
            .arg(supplierName)
            .arg(purchase->invoiceNumber.isEmpty() ? QStringLiteral("—")
                                                   : purchase->invoiceNumber)
            .arg(displayDate(purchase->purchasedAt))
            .arg(formatMoney(purchase->totalCents))
            .arg(formatMoney(purchase->paidCents))
            .arg(formatMoney(remaining)));

    auto* items = new QTableWidget;
    items->setColumnCount(5);
    items->setHorizontalHeaderLabels(
        {tr("المنتج"), tr("الكمية"), tr("الوحدة"), tr("سعر الوحدة"), tr("الإجمالي")});
    items->setEditTriggers(QAbstractItemView::NoEditTriggers);
    items->setSelectionBehavior(QAbstractItemView::SelectRows);
    items->horizontalHeader()->setStretchLastSection(true);

    data::PurchaseItemRepository purchaseItems(db);
    data::ProductRepository products(db);
    for (const core::PurchaseItem& item : purchaseItems.findByPurchase(purchaseId)) {
        QString name = item.description;
        if (item.productId.has_value()) {
            const auto product = products.findById(*item.productId);
            if (product.has_value()) {
                name = product->name;
            }
        }
        if (name.isEmpty()) {
            name = QStringLiteral("—");
        }
        const int row = items->rowCount();
        items->insertRow(row);
        items->setItem(row, 0, new QTableWidgetItem(name));
        items->setItem(row, 1, new QTableWidgetItem(QString::number(item.quantity)));
        items->setItem(row, 2, new QTableWidgetItem(item.unit));
        items->setItem(row, 3, new QTableWidgetItem(formatMoney(item.unitPriceCents)));
        items->setItem(row, 4, new QTableWidgetItem(formatMoney(item.totalCents)));
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    for (QAbstractButton* button : buttons->buttons()) {
        if (auto* push = qobject_cast<QPushButton*>(button)) {
            push->setAutoDefault(false);
            push->setDefault(false);
        }
    }
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(summary);
    layout->addWidget(items);
    layout->addWidget(buttons);
    constrainDialogToAvailableGeometry(&dialog);
    dialog.exec();
}

} // namespace

InvoicesPage::InvoicesPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* header = new PageHeader(tr("Factures"),
                                  tr("Factures fournisseurs, ventes et services"));

    m_search = makeSearchField(tr("بحث بالتاريخ أو الاسم أو الرقم"));
    connect(m_search, &QLineEdit::textChanged, this, &InvoicesPage::applyFilter);

    m_purchasesTable = makeTable({tr("Date"), tr("Fournisseur"), tr("Total"), tr("Payé"),
                                  tr("Reste")},
                                 QStringLiteral("invoicesPurchasesTable"));
    m_salesTable = makeTable({tr("N° reçu"), tr("Date"), tr("Client"), tr("Total")},
                             QStringLiteral("invoicesSalesTable"));
    m_servicesTable = makeTable(
        {tr("Type"), tr("Date facture"), tr("Montant"), tr("Échéance"), tr("Statut")},
        QStringLiteral("invoicesServicesTable"));

    m_tabs = new QTabWidget;
    m_tabs->setObjectName(QStringLiteral("invoicesTabs"));
    m_tabs->addTab(m_purchasesTable, QStringLiteral("Factures Fournisseurs"));
    m_tabs->addTab(m_salesTable, QStringLiteral("Factures Ventes"));

    auto* servicesTab = new QWidget;
    auto* servicesLayout = new QVBoxLayout(servicesTab);
    servicesLayout->setContentsMargins(0, 0, 0, 0);
    auto* serviceBar = new QHBoxLayout;
    serviceBar->addStretch(1);
    m_newService = new QPushButton(tr("Nouvelle Facture Service"));
    m_newService->setObjectName(QStringLiteral("newServiceInvoiceButton"));
    serviceBar->addWidget(m_newService);
    servicesLayout->addLayout(serviceBar);
    servicesLayout->addWidget(m_servicesTable, 1);
    m_tabs->addTab(servicesTab, QStringLiteral("Factures Services"));

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(header);
    root->addWidget(m_search);
    root->addWidget(m_tabs, 1);

    connect(m_tabs, &QTabWidget::currentChanged, this, &InvoicesPage::applyFilter);
    connect(m_newService, &QPushButton::clicked, this, &InvoicesPage::onNewServiceInvoice);
    connect(m_purchasesTable, &QTableWidget::cellDoubleClicked,
            this, &InvoicesPage::onPurchaseActivated);
    connect(m_salesTable, &QTableWidget::cellDoubleClicked, this,
            &InvoicesPage::onSaleActivated);
    connect(m_servicesTable, &QTableWidget::cellDoubleClicked, this,
            &InvoicesPage::onServiceActivated);

    refresh();
}

void InvoicesPage::refresh()
{
    data::PurchaseRepository purchases(m_db);
    data::SupplierRepository suppliers(m_db);
    data::SaleRepository sales(m_db);
    data::CustomerRepository customers(m_db);
    data::ServiceInvoiceRepository services(m_db);

    {
        const QSignalBlocker blocker(m_purchasesTable);
        m_purchasesTable->setRowCount(0);
        for (const core::Purchase& purchase : purchases.findAll()) {
            const auto supplier = suppliers.findById(purchase.supplierId);
            const QString supplierName = supplier.has_value() ? supplier->name
                                                              : QStringLiteral("—");
            setRow(m_purchasesTable,
                   {displayDate(purchase.purchasedAt), supplierName,
                    formatMoney(purchase.totalCents), formatMoney(purchase.paidCents),
                    formatMoney(purchase.totalCents - purchase.paidCents)},
                   purchase.id);
        }
    }

    {
        const QSignalBlocker blocker(m_salesTable);
        m_salesTable->setRowCount(0);
        for (const core::Sale& sale : sales.findAll()) {
            QString customerName = tr("زبون نقدي");
            if (sale.customerId.has_value()) {
                const auto customer = customers.findById(*sale.customerId);
                if (customer.has_value()) {
                    customerName = customer->name;
                }
            }
            const QString receipt = sale.receiptNumber.isEmpty()
                                        ? QStringLiteral("—")
                                        : sale.receiptNumber;
            setRow(m_salesTable,
                   {receipt, displayDate(sale.createdAt.toString(Qt::ISODateWithMs)),
                    customerName, formatMoney(sale.totalCents)},
                   sale.id);
        }
    }

    {
        const QSignalBlocker blocker(m_servicesTable);
        m_servicesTable->setRowCount(0);
        for (const core::ServiceInvoice& invoice : services.findAll()) {
            setRow(m_servicesTable,
                   {serviceTypeLabel(invoice.type), displayDate(invoice.invoiceDate),
                    formatMoney(invoice.amountCents), displayDate(invoice.dueDate),
                    serviceStatusLabel(invoice.status)},
                   invoice.id);
        }
    }

    applyFilter();
}

void InvoicesPage::onNewServiceInvoice()
{
    if (showServiceInvoiceDialog(this, m_db) > 0) {
        refresh();
    }
}

void InvoicesPage::onPurchaseActivated(int row, int column)
{
    Q_UNUSED(column);
    const QTableWidgetItem* item = m_purchasesTable->item(row, kColId);
    if (!item) {
        return;
    }
    showPurchaseDetails(this, m_db, item->data(Qt::UserRole).toInt());
}

void InvoicesPage::onSaleActivated(int row, int column)
{
    Q_UNUSED(column);
    const QTableWidgetItem* item = m_salesTable->item(row, kColId);
    if (!item) {
        return;
    }
    showSaleDetailsDialog(this, m_db, item->data(Qt::UserRole).toInt());
}

void InvoicesPage::onServiceActivated(int row, int column)
{
    Q_UNUSED(column);
    const QTableWidgetItem* item = m_servicesTable->item(row, kColId);
    if (!item) {
        return;
    }
    data::ServiceInvoiceRepository services(m_db);
    const auto invoice = services.findById(item->data(Qt::UserRole).toInt());
    if (!invoice.has_value()) {
        return;
    }
    if (showServiceInvoiceDialog(this, m_db, *invoice) > 0) {
        refresh();
    }
}

void InvoicesPage::applyFilter()
{
    if (!m_search || !m_tabs) {
        return;
    }
    const QString text = m_search->text();
    QTableWidget* table = nullptr;
    switch (m_tabs->currentIndex()) {
    case 0:
        table = m_purchasesTable;
        break;
    case 1:
        table = m_salesTable;
        break;
    case 2:
        table = m_servicesTable;
        break;
    default:
        break;
    }
    filterTable(table, text);
}

} // namespace app::ui
