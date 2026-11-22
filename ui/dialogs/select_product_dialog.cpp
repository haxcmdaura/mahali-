#include "dialogs/select_product_dialog.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cstddef>

#include "format_utils.h"

namespace app::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("SelectProductDialog", text);
}

}

std::optional<core::Product> showSelectProductDialog(QWidget* parent,
                                                    const std::vector<core::Product>& products)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Choisir un produit"));
    dialog.setModal(true);
    dialog.resize(500, 500);

    auto* layout = new QVBoxLayout(&dialog);
    auto* search = new QLineEdit;
    search->setObjectName(QStringLiteral("searchField"));
    search->setPlaceholderText(tr("Nom ou code-barres"));
    layout->addWidget(search);

    auto* table = new QTableWidget(0, 3);
    table->setHorizontalHeaderLabels({tr("Nom"), tr("Prix"), tr("Stock")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    auto* cancelButton = new QPushButton(tr("Annuler"));
    buttonRow->addWidget(cancelButton);
    layout->addLayout(buttonRow);

    std::optional<core::Product> chosen;

    const auto refill = [&]() {
        const QString needle = search->text().trimmed();
        table->setRowCount(0);
        for (std::size_t index = 0; index < products.size(); ++index) {
            const core::Product& product = products.at(index);
            if (!needle.isEmpty() && !product.name.contains(needle, Qt::CaseInsensitive)
                && !product.barcode.contains(needle, Qt::CaseInsensitive)) {
                continue;
            }
            const int line = table->rowCount();
            table->insertRow(line);

            auto* nameItem = new QTableWidgetItem(product.name);
            nameItem->setData(Qt::UserRole, static_cast<int>(index));
            table->setItem(line, 0, nameItem);

            auto* priceItem = new QTableWidgetItem(formatMoney(product.salePriceCents));
            priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(line, 1, priceItem);

            auto* stockItem = new QTableWidgetItem(QString::number(product.quantity));
            stockItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(line, 2, stockItem);
        }
        if (table->rowCount() == 1) {
            table->selectRow(0);
        }
    };

    const auto take = [&]() {
        const int line = table->currentRow();
        if (line < 0) {
            return;
        }
        const QTableWidgetItem* nameItem = table->item(line, 0);
        if (!nameItem) {
            return;
        }
        const int index = nameItem->data(Qt::UserRole).toInt();
        if (index < 0 || index >= static_cast<int>(products.size())) {
            return;
        }
        chosen = products.at(static_cast<std::size_t>(index));
        dialog.accept();
    };

    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(table, &QTableWidget::cellDoubleClicked, &dialog,
                     [take](int, int) { take(); });
    QObject::connect(search, &QLineEdit::textChanged, &dialog,
                     [refill](const QString&) { refill(); });
    QObject::connect(search, &QLineEdit::returnPressed, &dialog, [take]() { take(); });

    refill();
    search->setFocus();
    dialog.exec();

    return chosen;
}

}
