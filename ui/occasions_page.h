#pragma once

#include <optional>

#include <QWidget>

#include "core/occasion.h"
#include "data/database.h"

class QPushButton;
class QTableWidget;

namespace app::ui {

std::optional<core::Occasion> showOccasionDialog(QWidget* parent,
                                                const core::Occasion& initial = {});

class OccasionsPage : public QWidget {
    Q_OBJECT

public:
    explicit OccasionsPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();

    int rowCount() const;
    int occasionIdAt(int row) const;
    QString nameAt(int row) const;
    QString themeKeyAt(int row) const;
    bool activeAt(int row) const;

    QTableWidget* table() const { return m_table; }
    QPushButton* newButton() const { return m_newButton; }

private slots:
    void onAddClicked();
    void onRowActivated(int row, int column);

private:
    app::data::Database& m_db;
    QTableWidget* m_table;
    QPushButton* m_newButton;
};

} // namespace app::ui
