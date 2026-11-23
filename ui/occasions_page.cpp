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
#include <QVector>
#include <QVBoxLayout>

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
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({tr("ID"), tr("Nom"), tr("Date de début"),
                                        tr("Date de fin"), tr("Thème"), tr("Actif")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(42);

    auto* gridCard = makeCard();
    auto* gridLayout = new QVBoxLayout(gridCard);
    padCardLayout(gridLayout);
    gridLayout->addWidget(makeCardTitle(tr("Liste des occasions")));
    gridLayout->addWidget(m_table, 1);

    root->addWidget(toolbarCard);
    root->addWidget(gridCard, 1);

    connect(m_newButton, &QPushButton::clicked, this, &OccasionsPage::onAddClicked);
    connect(m_manualButton, &QPushButton::clicked, this, &OccasionsPage::onManualClicked);
    connect(m_autoButton, &QPushButton::clicked, this, &OccasionsPage::onAutoClicked);
    connect(m_disableButton, &QPushButton::clicked, this, &OccasionsPage::onDisableClicked);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &OccasionsPage::onRowActivated);
    refresh();
}

void OccasionsPage::refresh()
{
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
    }

    const QString modeText = mode == QStringLiteral("manual")
                                 ? tr("manuel")
                                 : mode == QStringLiteral("disabled") ? tr("désactivé")
                                                                      : tr("automatique");
    m_modeLabel->setText(tr("Mode: %1").arg(modeText));
    m_modeLabel->setProperty("mode", mode);
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
    data::OccasionRepository repo(m_db);
    const auto current = repo.findById(occasionIdAt(row));
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

void OccasionsPage::onManualClicked()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, tr("Erreur"), tr("Sélectionnez une occasion"));
        return;
    }

    data::OccasionRepository repo(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, repo, settings);
    if (!service.activate(occasionIdAt(row))) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'activer l'occasion"));
        return;
    }
    refresh();
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
