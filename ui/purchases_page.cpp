#include "purchases_page.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "core/format_utils.h"
#include "data/date_utils.h"
#include "data/purchase_repository.h"
#include "data/supplier_repository.h"
#include "dialogs/purchase_dialog.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

constexpr int kColDate = 0;
constexpr int kColSupplier = 1;
constexpr int kColInvoice = 2;
constexpr int kColTotal = 3;

void addRow(QTableWidget* table, const QStringList& cells)
{
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < cells.size() && column < table->columnCount(); ++column) {
        table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
}

} // namespace

QStringList formatPurchaseRow(const core::Purchase& purchase, const QString& supplierName)
{
    const auto stamp = data::fromIso(purchase.purchasedAt);
    const QString date = stamp.has_value() ? stamp->date().toString(QStringLiteral("dd/MM/yyyy"))
                                           : purchase.purchasedAt.left(10);
    return {date, supplierName, purchase.invoiceNumber, formatMoney(purchase.totalCents)};
}

PurchasesPage::PurchasesPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* header = new PageHeader(tr("Achats"), tr("Factures d'achat enregistrées"));
    m_newPurchase = new QPushButton(tr("Nouvel Achat"));
    m_newPurchase->setObjectName(QStringLiteral("primary"));
    m_newPurchase->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    header->addAction(m_newPurchase);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("purchasesTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(true);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("Date"), tr("Fournisseur"), tr("N° facture"), tr("Total")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColDate, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kColSupplier, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColInvoice, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kColTotal, QHeaderView::Fixed);
    m_table->setColumnWidth(kColDate, 120);
    m_table->setColumnWidth(kColInvoice, 150);
    m_table->setColumnWidth(kColTotal, 140);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->verticalHeader()->hide();

    auto* gridCard = makeCard();
    auto* gridLayout = new QVBoxLayout(gridCard);
    padCardLayout(gridLayout);
    gridLayout->addWidget(makeCardTitle(tr("Achats enregistrés")));
    gridLayout->addWidget(m_table, 1);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(header);
    root->addWidget(gridCard, 1);

    connect(m_newPurchase, &QPushButton::clicked, this, &PurchasesPage::onNewPurchaseClicked);
    refresh();
}

void PurchasesPage::refresh()
{
    data::PurchaseRepository purchases(m_db);
    data::SupplierRepository suppliers(m_db);

    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(0);
    for (const core::Purchase& purchase : purchases.findAll()) {
        const auto supplier = suppliers.findById(purchase.supplierId);
        const QString supplierName = supplier.has_value() ? supplier->name : QStringLiteral("—");
        addRow(m_table, formatPurchaseRow(purchase, supplierName));
    }
}

int PurchasesPage::rowCount() const
{
    return m_table->rowCount();
}

void PurchasesPage::onNewPurchaseClicked()
{
    const PurchaseDialogResult result = showPurchaseDialog(this, m_db, 0);
    if (result.saved) {
        refresh();
    }
}

} // namespace app::ui
