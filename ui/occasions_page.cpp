#include "occasions_page.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QColor>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariant>
#include <QVector>
#include <QVBoxLayout>

#include "core/hijri_utils.h"
#include "data/date_utils.h"
#include "data/occasion_repository.h"
#include "data/occasion_service.h"
#include "data/setting_repository.h"
#include "scan_safe_dialog.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("OccasionsPage", text);
}

void disarmDefaults(QDialogButtonBox* buttons)
{
    for (QAbstractButton* button : buttons->buttons()) {
        if (auto* push = qobject_cast<QPushButton*>(button)) {
            push->setAutoDefault(false);
            push->setDefault(false);
        }
    }
}

QString displayDate(const QString& iso)
{
    const auto parsed = data::fromIso(iso);
    if (!parsed || !parsed->date().isValid()) {
        return iso;
    }
    return parsed->date().toString(Qt::ISODate);
}

QDate dateOf(const QString& iso, const QDate& fallback)
{
    const auto parsed = data::fromIso(iso);
    if (!parsed || !parsed->date().isValid()) {
        return fallback;
    }
    return parsed->date();
}

QString bannerStyle(const QString& key)
{
    const QString normalized = key.trimmed().toLower();
    const QString color = normalized == QStringLiteral("ramadan")
                              ? QStringLiteral("#6a1b9a")
                              : normalized == QStringLiteral("fitr")
                                    ? QStringLiteral("#1565c0")
                                    : normalized == QStringLiteral("adha")
                                          ? QStringLiteral("#b22222")
                                          : QStringLiteral("#2e7d32");
    return QStringLiteral("background-color: %1; color: #ffffff; border-radius: 8px; "
                          "padding: 10px; font-weight: 600;")
        .arg(color);
}

void compactActionButton(QPushButton* button)
{
    button->setMinimumHeight(21);
    button->setMaximumHeight(30);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { min-height: 21px; max-height: 30px; padding: 3px 8px; "
        "font-size: 9pt; }"));
}

} // namespace

std::optional<core::Occasion> showOccasionDialog(QWidget* parent, const core::Occasion& initial)
{
    const bool forNew = initial.id == 0;
    const QDate today = QDate::currentDate();

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(forNew ? tr("Nouvelle occasion") : tr("Modifier l'occasion"));
    dialog.setModal(true);

    auto* name = new QLineEdit(initial.name);
    name->setObjectName(QStringLiteral("occasionNameField"));
    auto* start = new QDateEdit(dateOf(initial.startsAt, today));
    start->setObjectName(QStringLiteral("occasionStartField"));
    start->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    start->setCalendarPopup(true);
    auto* end = new QDateEdit(dateOf(initial.endsAt, today.addDays(30)));
    end->setObjectName(QStringLiteral("occasionEndField"));
    end->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    end->setCalendarPopup(true);

    auto* theme = new QComboBox;
    theme->setObjectName(QStringLiteral("occasionThemeCombo"));
    theme->addItem(QStringLiteral("ramadan"), QStringLiteral("ramadan"));
    theme->addItem(QStringLiteral("adha"), QStringLiteral("adha"));
    theme->addItem(QStringLiteral("fitr"), QStringLiteral("fitr"));
    theme->addItem(QStringLiteral("none"), QString());
    const int themeIndex = theme->findData(initial.themeKey);
    theme->setCurrentIndex(themeIndex >= 0 ? themeIndex : theme->count() - 1);

    auto* form = new QFormLayout;
    form->addRow(tr("Nom"), name);
    form->addRow(tr("Date de début"), start);
    form->addRow(tr("Date de fin"), end);
    form->addRow(tr("Clé de thème"), theme);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    QPushButton* save = buttons->button(QDialogButtonBox::Save);
    QPushButton* cancel = buttons->button(QDialogButtonBox::Cancel);
    save->setText(tr("Enregistrer"));
    cancel->setText(tr("Annuler"));
    save->setIcon(QIcon());
    cancel->setIcon(QIcon());
    save->setObjectName(QStringLiteral("primary"));
    disarmDefaults(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    constrainDialogToAvailableGeometry(&dialog);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    if (name->text().trimmed().isEmpty()) {
        QMessageBox::warning(&dialog, tr("Erreur"), tr("Le nom est requis"));
        return std::nullopt;
    }
    if (end->date() < start->date()) {
        QMessageBox::warning(&dialog, tr("Erreur"),
                             tr("La date de fin doit suivre la date de début"));
        return std::nullopt;
    }

    core::Occasion occasion = initial;
    occasion.name = name->text().trimmed();
    occasion.startsAt = data::toIso(QDateTime(start->date(), QTime(0, 0, 0)));
    occasion.endsAt = data::widenToDayEnd(end->date().toString(Qt::ISODate));
    occasion.themeKey = theme->currentData().toString();
    if (forNew) {
        occasion.createdAt = data::nowIso();
        occasion.active = true;
    }
    return occasion;
}

OccasionsPage::OccasionsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
    , m_today(QDate::currentDate())
{
    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(tr("المناسبات"), tr("إدارة المناسبات والثيمات")));

    m_newButton = new QPushButton(tr("Nouvelle Occasion"));
    m_newButton->setObjectName(QStringLiteral("primary"));
    m_newButton->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    m_newButton->setCursor(Qt::PointingHandCursor);

    m_manualButton = new QPushButton(tr("Activer manuellement"));
    m_manualButton->setObjectName(QStringLiteral("occasionManualButton"));
    m_manualButton->setCursor(Qt::PointingHandCursor);
    m_autoButton = new QPushButton(tr("Mode automatique"));
    m_autoButton->setObjectName(QStringLiteral("occasionAutoButton"));
    m_autoButton->setCursor(Qt::PointingHandCursor);
    m_disableButton = new QPushButton(tr("Quitter le mode occasion"));
    m_disableButton->setObjectName(QStringLiteral("occasionDisableButton"));
    m_disableButton->setCursor(Qt::PointingHandCursor);

    m_modeLabel = new QLabel;
    m_modeLabel->setObjectName(QStringLiteral("occasionModeLabel"));

    m_hijriLabel = new QLabel;
    m_hijriLabel->setObjectName(QStringLiteral("occasionHijriLabel"));
    m_hijriLabel->setVisible(false);

    m_autoReasonBanner = new QLabel;
    m_autoReasonBanner->setObjectName(QStringLiteral("occasionAutoReasonBanner"));
    m_autoReasonBanner->setWordWrap(true);
    m_autoReasonBanner->setVisible(false);

    m_editDatesButton = new QPushButton(tr("تعديل التواريخ"));
    m_editDatesButton->setObjectName(QStringLiteral("occasionEditDatesButton"));
    m_editDatesButton->setCursor(Qt::PointingHandCursor);
    compactActionButton(m_editDatesButton);

    m_autoBannerRow = new QWidget;
    auto* bannerLayout = new QHBoxLayout(m_autoBannerRow);
    bannerLayout->setContentsMargins(0, 0, 0, 0);
    bannerLayout->setSpacing(8);
    bannerLayout->addWidget(m_autoReasonBanner, 1);
    bannerLayout->addWidget(m_editDatesButton);
    m_autoBannerRow->setVisible(false);

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(m_modeLabel);
    toolbar->addWidget(m_manualButton);
    toolbar->addWidget(m_autoButton);
    toolbar->addWidget(m_disableButton);
    toolbar->addStretch(1);
    toolbar->addWidget(m_newButton);

    auto* toolbarCard = makeCard();
    auto* toolbarLayout = new QVBoxLayout(toolbarCard);
    padCardLayout(toolbarLayout);
    toolbarLayout->addLayout(toolbar);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("occasionTable"));
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({tr("ID"), tr("Nom"), tr("Date de début"),
                                        tr("Date de fin"), tr("Thème"), tr("Actif"),
                                        tr("Actions")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(42);

    auto* gridCard = makeCard();
    auto* gridLayout = new QVBoxLayout(gridCard);
    padCardLayout(gridLayout);
    gridLayout->addWidget(makeCardTitle(tr("Liste des occasions")));
    gridLayout->addWidget(m_table, 1);

    root->addWidget(toolbarCard);
    root->addWidget(m_hijriLabel);
    root->addWidget(m_autoBannerRow);
    root->addWidget(gridCard, 1);

    connect(m_newButton, &QPushButton::clicked, this, &OccasionsPage::onAddClicked);
    connect(m_manualButton, &QPushButton::clicked, this, &OccasionsPage::onManualClicked);
    connect(m_autoButton, &QPushButton::clicked, this, &OccasionsPage::onAutoClicked);
    connect(m_disableButton, &QPushButton::clicked, this, &OccasionsPage::onDisableClicked);
    connect(m_editDatesButton, &QPushButton::clicked, this, &OccasionsPage::onEditDatesClicked);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &OccasionsPage::onRowActivated);
    refresh();
}

void OccasionsPage::refresh()
{
    refresh(QDate::currentDate());
}

void OccasionsPage::refresh(const QDate& today)
{
    m_today = today;
    data::OccasionRepository repo(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, repo, settings);
    const QString mode = service.mode();
    const QVector<core::Occasion> occasions = repo.findAll();
    m_table->setRowCount(occasions.size());

    for (int row = 0; row < occasions.size(); ++row) {
        const core::Occasion& occasion = occasions.at(row);
        const QString theme = occasion.themeKey.isEmpty() ? QStringLiteral("none")
                                                          : occasion.themeKey;
        const QStringList cells = {
            QString::number(occasion.id),
            occasion.name,
            displayDate(occasion.startsAt),
            displayDate(occasion.endsAt),
            theme,
            occasion.active ? tr("نعم") : tr("لا"),
        };
        for (int column = 0; column < cells.size(); ++column) {
            m_table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        }

        auto* actionCell = new QWidget;
        auto* actionLayout = new QHBoxLayout(actionCell);
        actionLayout->setContentsMargins(4, 0, 4, 0);
        actionLayout->setSpacing(4);
        auto* toggle = new QPushButton(occasion.active ? tr("تعطيل") : tr("تفعيل"));
        toggle->setObjectName(occasion.active ? QStringLiteral("occasionDeactivateButton")
                                              : QStringLiteral("occasionActivateButton"));
        toggle->setCursor(Qt::PointingHandCursor);
        compactActionButton(toggle);
        auto* edit = new QPushButton(tr("تعديل"));
        edit->setObjectName(QStringLiteral("occasionEditButton"));
        edit->setCursor(Qt::PointingHandCursor);
        compactActionButton(edit);
        actionLayout->addWidget(toggle);
        actionLayout->addWidget(edit);
        m_table->setCellWidget(row, 6, actionCell);

        const int occasionId = occasion.id;
        if (occasion.active) {
            connect(toggle, &QPushButton::clicked, this,
                    [this, occasionId]() { deactivateOccasionId(occasionId); });
        } else {
            connect(toggle, &QPushButton::clicked, this,
                    [this, occasionId]() { activateOccasionId(occasionId); });
        }
        connect(edit, &QPushButton::clicked, this,
                [this, occasionId]() { editOccasionId(occasionId); });
    }

    const QString modeText = mode == QStringLiteral("manual")
                                 ? tr("manuel")
                                 : mode == QStringLiteral("disabled") ? tr("désactivé")
                                                                      : tr("automatique");
    m_modeLabel->setText(tr("Mode: %1").arg(modeText));
    m_modeLabel->setProperty("mode", mode);

    const QString hijriDate = core::formatHijriDate(today);
    if (mode == QStringLiteral("auto") && !hijriDate.isEmpty()) {
        m_hijriLabel->setText(tr("الوضع التلقائي: اليوم هو %1").arg(hijriDate));
        m_hijriLabel->setProperty("mode", mode);
        m_hijriLabel->setVisible(true);
    } else {
        m_hijriLabel->clear();
        m_hijriLabel->setProperty("mode", QVariant());
        m_hijriLabel->setVisible(false);
    }

    const std::optional<core::Occasion> current =
        mode == QStringLiteral("auto") ? service.current(today) : std::nullopt;
    if (mode == QStringLiteral("auto") && !hijriDate.isEmpty()) {
        if (current.has_value()) {
            m_autoReasonBanner->setText(
                tr("تم تفعيل %1 تلقائياً (التاريخ الهجري: %2)")
                    .arg(current->name, hijriDate));
            m_autoReasonBanner->setProperty("occasionKey", current->themeKey);
            m_autoReasonBanner->setStyleSheet(bannerStyle(current->themeKey));
        } else {
            m_autoReasonBanner->setText(
                tr("لا توجد مناسبة نشطة حالياً. التاريخ الهجري اليوم: %1").arg(hijriDate));
            m_autoReasonBanner->setProperty("occasionKey", QVariant());
            m_autoReasonBanner->setStyleSheet(bannerStyle(QString()));
        }
        m_autoReasonBanner->setVisible(true);
        m_editDatesButton->setVisible(true);
        m_autoBannerRow->setVisible(true);
    } else {
        m_autoReasonBanner->clear();
        m_autoReasonBanner->setProperty("occasionKey", QVariant());
        m_autoReasonBanner->setStyleSheet(QString());
        m_autoReasonBanner->setVisible(false);
        m_editDatesButton->setVisible(false);
        m_autoBannerRow->setVisible(false);
    }
}

int OccasionsPage::rowCount() const
{
    return m_table->rowCount();
}

int OccasionsPage::occasionIdAt(int row) const
{
    QTableWidgetItem* item = m_table->item(row, 0);
    return item ? item->text().toInt() : 0;
}

QString OccasionsPage::nameAt(int row) const
{
    QTableWidgetItem* item = m_table->item(row, 1);
    return item ? item->text() : QString();
}

QString OccasionsPage::themeKeyAt(int row) const
{
    QTableWidgetItem* item = m_table->item(row, 4);
    return item ? item->text() : QString();
}

bool OccasionsPage::activeAt(int row) const
{
    QTableWidgetItem* item = m_table->item(row, 5);
    return item && item->text() == tr("نعم");
}

void OccasionsPage::onAddClicked()
{
    const auto saved = showOccasionDialog(this);
    if (!saved) {
        return;
    }
    data::OccasionRepository repo(m_db);
    repo.insert(*saved);
    refresh();
}

void OccasionsPage::onRowActivated(int row, int column)
{
    Q_UNUSED(column);
    editOccasionId(occasionIdAt(row));
}

void OccasionsPage::deactivateOccasionId(int occasionId)
{
    data::OccasionRepository repo(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, repo, settings);
    const auto current = service.current(m_today);

    if (!repo.setActive(occasionId, false)) {
        QMessageBox::warning(this, tr("Erreur"),
                             tr("Impossible de désactiver l'occasion"));
        return;
    }
    if (current && current->id == occasionId && service.mode() == QStringLiteral("manual")) {
        service.disable();
    }
    refresh();
}

void OccasionsPage::activateOccasionId(int occasionId)
{
    data::OccasionRepository repo(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, repo, settings);
    if (!service.activate(occasionId)) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'activer l'occasion"));
        return;
    }
    refresh();
}

void OccasionsPage::editOccasionId(int occasionId)
{
    data::OccasionRepository repo(m_db);
    const auto current = repo.findById(occasionId);
    if (!current) {
        return;
    }
    const auto edited = showOccasionDialog(this, *current);
    if (!edited) {
        return;
    }
    repo.update(*edited);
    refresh();
}

void OccasionsPage::onEditDatesClicked()
{
    data::OccasionRepository repo(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, repo, settings);

    const auto current = service.current(m_today);
    if (current) {
        editOccasionId(current->id);
        return;
    }

    const QString hijriKey = core::getCurrentHijriOccasion(m_today);
    if (!hijriKey.isEmpty()) {
        for (const core::Occasion& occasion : repo.findAll()) {
            if (occasion.themeKey.trimmed().toLower() == hijriKey.trimmed().toLower()) {
                editOccasionId(occasion.id);
                return;
            }
        }
    }
    QMessageBox::warning(this, tr("Erreur"), tr("لا توجد مناسبة لتعديل تواريخها"));
}

void OccasionsPage::onManualClicked()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, tr("Erreur"), tr("Sélectionnez une occasion"));
        return;
    }

    activateOccasionId(occasionIdAt(row));
}

void OccasionsPage::onAutoClicked()
{
    data::OccasionRepository repo(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, repo, settings);
    if (!service.setAutoMode()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de passer en mode automatique"));
        return;
    }
    refresh();
}

void OccasionsPage::onDisableClicked()
{
    data::OccasionRepository repo(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, repo, settings);
    if (!service.disable()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de quitter le mode occasion"));
        return;
    }
    refresh();
}

} // namespace app::ui
