#pragma once

#include <optional>

#include <QDate>
#include <QWidget>

#include "core/occasion.h"
#include "data/database.h"

class QLabel;
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
    void refresh(const QDate& today);

    int rowCount() const;
    int occasionIdAt(int row) const;
    QString nameAt(int row) const;
    QString themeKeyAt(int row) const;
    bool activeAt(int row) const;

    QTableWidget* table() const { return m_table; }
    QPushButton* newButton() const { return m_newButton; }
    QPushButton* manualButton() const { return m_manualButton; }
    QPushButton* autoButton() const { return m_autoButton; }
    QPushButton* disableButton() const { return m_disableButton; }
    QLabel* modeLabel() const { return m_modeLabel; }
    QLabel* hijriLabel() const { return m_hijriLabel; }
    QLabel* autoReasonBanner() const { return m_autoReasonBanner; }
    QPushButton* editDatesButton() const { return m_editDatesButton; }

private slots:
    void onAddClicked();
    void onRowActivated(int row, int column);
    void onManualClicked();
    void onAutoClicked();
    void onDisableClicked();
    void deactivateOccasionId(int occasionId);
    void activateOccasionId(int occasionId);
    void editOccasionId(int occasionId);
    void onEditDatesClicked();

private:
    app::data::Database& m_db;
    QDate m_today;
    QTableWidget* m_table;
    QPushButton* m_newButton;
    QPushButton* m_manualButton;
    QPushButton* m_autoButton;
    QPushButton* m_disableButton;
    QLabel* m_modeLabel;
    QLabel* m_hijriLabel;
    QWidget* m_autoBannerRow;
    QLabel* m_autoReasonBanner;
    QPushButton* m_editDatesButton;
};

} // namespace app::ui
