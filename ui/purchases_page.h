#pragma once

#include <QStringList>
#include <QWidget>

#include "core/purchase.h"
#include "data/database.h"

class QPushButton;
class QTableWidget;

namespace app::ui {

QStringList formatPurchaseRow(const core::Purchase& purchase, const QString& supplierName);

class PurchasesPage : public QWidget {
    Q_OBJECT

public:
    explicit PurchasesPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int rowCount() const;
    QTableWidget* table() const { return m_table; }
    QPushButton* newPurchaseButton() const { return m_newPurchase; }

private slots:
    void onNewPurchaseClicked();

private:
    app::data::Database& m_db;
    QTableWidget* m_table = nullptr;
    QPushButton* m_newPurchase = nullptr;
};

} // namespace app::ui
