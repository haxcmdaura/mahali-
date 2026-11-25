#pragma once

#include <QWidget>

#include "data/database.h"

class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace app::ui {

class InvoicesPage : public QWidget {
    Q_OBJECT

public:
    explicit InvoicesPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();

    QTabWidget* tabs() const { return m_tabs; }
    QTableWidget* purchasesTable() const { return m_purchasesTable; }
    QTableWidget* salesTable() const { return m_salesTable; }
    QTableWidget* servicesTable() const { return m_servicesTable; }
    QPushButton* newServiceButton() const { return m_newService; }
    QLineEdit* searchField() const { return m_search; }

private slots:
    void onNewServiceInvoice();
    void onPurchaseActivated(int row, int column);
    void onSaleActivated(int row, int column);
    void onServiceActivated(int row, int column);
    void applyFilter();

private:
    app::data::Database& m_db;
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_purchasesTable = nullptr;
    QTableWidget* m_salesTable = nullptr;
    QTableWidget* m_servicesTable = nullptr;
    QPushButton* m_newService = nullptr;
    QLineEdit* m_search = nullptr;
};

} // namespace app::ui
