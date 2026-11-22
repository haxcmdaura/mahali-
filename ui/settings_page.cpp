#include "settings_page.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "core/i18n.h"
#include "core/update_checker.h"
#include "data/backup_service.h"
#include "data/product_cleanup.h"
#include "data/product_repository.h"
#include "data/setting_repository.h"
#include "data/sync_key.h"

#include "format_utils.h"
#include "theme.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

// The settings column, and with it every field inside it. A settings form is a
// fixed set of known fields, so its width is a design decision rather than
// something to derive from the window: left to grow it became 960px wide and
// pushed the field away from its own label.
constexpr int kFormWidth = 500;

// The labels are captions now, sitting above the field they name, and a caption
// does not end in a colon. It is taken off the translated text rather than off
// the source string because the source is the key the French catalogue is
// matched on: editing it would leave the French labels falling back to Arabic
// until the catalogue was rebuilt.
QString caption(const QString& text)
{
    QString out = text.trimmed();
    if (out.endsWith(QLatin1Char(':'))) {
        out.chop(1);
    }
    return out.trimmed();
}

} // namespace

SettingsPage::SettingsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_shopName = new QLineEdit;
    m_currency = new QLineEdit;
    m_currency->setPlaceholderText(tr("مثال: دج  أو  DA"));
    m_zakat = new QCheckBox(tr("احتساب الزكاة (2.5%) في التقارير"));
    m_zakatDate = new QLineEdit;
    m_zakatDate->setPlaceholderText(QStringLiteral("YYYY-MM-DD"));
    m_syncKey = new QLineEdit;
    m_syncKey->setEchoMode(QLineEdit::Password);

    m_theme = new QComboBox;
    m_theme->addItem(tr("فاتح"), QStringLiteral("light"));
    m_theme->addItem(tr("داكن"), QStringLiteral("dark"));

    m_language = new QComboBox;
    m_language->setObjectName(QStringLiteral("languageCombo"));
    m_language->addItem(tr("العربية"), QStringLiteral("ar"));
    m_language->addItem(tr("Français"), QStringLiteral("fr"));
    m_language->addItem(tr("English"), QStringLiteral("en"));

    m_receiptPaperSize = new QComboBox;
    m_receiptPaperSize->setObjectName(QStringLiteral("receiptPaperSize"));
    m_receiptPaperSize->addItem(QStringLiteral("58 mm"), QStringLiteral("58"));
    m_receiptPaperSize->addItem(QStringLiteral("80 mm"), QStringLiteral("80"));
    m_receiptPaperSize->addItem(QStringLiteral("A5"), QStringLiteral("a5"));
    m_receiptPaperSize->addItem(QStringLiteral("A4"), QStringLiteral("a4"));

    m_receiptFooter = new QLineEdit;
    m_receiptFooter->setObjectName(QStringLiteral("receiptFooterText"));

    // A plain path for the folder where PDF receipts land. A browse button
    // can arrive later; the shopkeeper sets it once.
    m_receiptFolder = new QLineEdit;
    m_receiptFolder->setObjectName(QStringLiteral("receiptSaveFolder"));

    // A combo box asks for Preferred, so under an expanding field policy it kept
    // its size hint — 129px next to 466px line edits, which is the ragged column
    // this form is meant to replace. Both are marked Expanding so the two
    // dropdowns measure the same as every other field.
    for (QComboBox* combo : {m_theme, m_language, m_receiptPaperSize}) {
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    m_preview = new QLabel;
    m_preview->setWordWrap(true);
    m_preview->setObjectName(QStringLiteral("faintText"));

    m_save = new QPushButton(tr("حفظ الإعدادات"));
    m_save->setIcon(appIcon(Icon::Check, QColor(QStringLiteral("#ffffff")), 18));

    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setObjectName(QStringLiteral("noticeOk"));
    m_notice->setVisible(false);

    // The same shape the login dialog uses: WrapAllRows puts each label on its
    // own line above its field. Side by side they fought each other — an Arabic
    // label is short and a French one long, so a shared label column left a gap
    // in the middle of the card that neither language could use, and the zakat
    // checkbox had no label of its own to sit under, which left it adrift in
    // the field column.
    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(14);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->addRow(caption(tr("اسم المتجر:")), m_shopName);
    form->addRow(caption(tr("رمز العملة:")), m_currency);
    // The checkbox carries its own text and spans the row, so it reads as a
    // sentence in its own right rather than as a box with a gap beside it.
    form->addRow(m_zakat);
    // The date sits under the checkbox rather than beside it: both are about
    // zakat, and the hint has to sit under the field it explains, which a
    // WrapAllRows form cannot do for a bare row.
    auto* zakatDateHint = new QLabel(tr("Un rappel annuel s'affichera à cette date"));
    zakatDateHint->setObjectName(QStringLiteral("faintText"));
    auto* zakatDateBox = new QWidget;
    auto* zakatDateLayout = new QVBoxLayout(zakatDateBox);
    zakatDateLayout->setContentsMargins(0, 0, 0, 0);
    zakatDateLayout->setSpacing(2);
    zakatDateLayout->addWidget(m_zakatDate);
    zakatDateLayout->addWidget(zakatDateHint);
    form->addRow(caption(tr("تاريخ الزكاة السنوي:")), zakatDateBox);
    form->addRow(caption(tr("السمة:")), m_theme);
    form->addRow(caption(tr("اللغة:")), m_language);
    form->addRow(caption(tr("مفتاح المزامنة (يتطلب إعادة تشغيل):")), m_syncKey);

    // The printer is what decides these, not the till: the same sale printed
    // on a 58mm roll and on an A4 sheet carries the same figures. Storing the
    // choice in settings keeps it out of the receipt builder and lets the
    // paper change without rebuilding anything.
    form->addRow(caption(tr("حجم الورق:")), m_receiptPaperSize);
    form->addRow(caption(tr("نص أسفل الفاتورة:")), m_receiptFooter);
    form->addRow(caption(tr("مجلد حفظ الإيصالات:")), m_receiptFolder);

    m_receiptAutoPrint = new QCheckBox(tr("طبع الإيصال تلقائياً بعد كل عملية بيع"));
    m_receiptAutoPrint->setObjectName(QStringLiteral("receiptAutoPrint"));
    form->addRow(m_receiptAutoPrint);

    // The fields are bounded by the card; the wrapper is the cap on the form
    // itself, so the fields cannot outgrow the column if the card is ever
    // resized to something wider.
    auto* formBox = new QWidget;
    formBox->setMaximumWidth(kFormWidth);
    auto* formBoxLayout = new QVBoxLayout(formBox);
    formBoxLayout->setContentsMargins(0, 0, 0, 0);
    formBoxLayout->addLayout(form);

    m_checkUpdates = new QPushButton(tr("Vérifier les mises à jour"));
    m_checkUpdates->setObjectName(QStringLiteral("linkButton"));
    m_checkUpdates->setCursor(Qt::PointingHandCursor);

    auto* backupNow = new QPushButton(tr("نسخ احتياطي الآن"));
    backupNow->setObjectName(QStringLiteral("backupNowButton"));
    auto* restoreBackup = new QPushButton(tr("استعادة من نسخة"));
    restoreBackup->setObjectName(QStringLiteral("restoreBackupButton"));
    m_cleanupInactive = new QPushButton(tr("تنظيف المنتجات غير المستخدمة"));
    m_cleanupInactive->setObjectName(QStringLiteral("cleanupInactiveProductsButton"));
    auto* backupRow = new QHBoxLayout;
    backupRow->setContentsMargins(0, 0, 0, 0);
    backupRow->setSpacing(8);
    backupRow->addWidget(backupNow);
    backupRow->addWidget(restoreBackup);
    backupRow->addWidget(m_cleanupInactive);

    auto* card = makeCard();
    // Fixed rather than merely capped: a maximum alone leaves the card at its
    // size hint, which measured 140px in Arabic and 291px in French for the same
    // screen — two different columns depending on the language.
    card->setFixedWidth(kFormWidth);
    auto* cardLayout = new QVBoxLayout(card);
    padCardLayout(cardLayout);
    cardLayout->addWidget(formBox);
    cardLayout->addWidget(m_preview);
    cardLayout->addSpacing(21);
    cardLayout->addWidget(m_save);
    cardLayout->addLayout(backupRow);
    cardLayout->addWidget(m_checkUpdates);
    cardLayout->addWidget(m_notice);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(tr("الإعدادات"),
                                   tr("اسم المتجر، العملة، السمة ومفتاح المزامنة")));

    // Six stacked rows do not fit a short page: the card measures 863px against
    // the 748px the window gives it, and the save button and the update link sit
    // at the bottom of it, so left alone they were simply off the page. The card
    // scrolls instead. At a window tall enough to hold the form there is nothing
    // to scroll and no scrollbar appears.
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // A scroll area centres a widget narrower than its viewport, and AlignLeft
    // is mirrored by the layout direction, so it lands on the reading-start
    // edge: the right in the Arabic UI, the left in the French one.
    scroll->setAlignment(Qt::AlignLeft);
    // The theme hands the sidebar's scroll area a transparent surface by name
    // and this one is not in that rule, so without a background of its own it
    // would paint the window colour over the page.
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget"
                                         " { background: transparent; border: none; }"));
    scroll->setWidget(card);
    root->addWidget(scroll, 1);

    connect(m_currency, &QLineEdit::textChanged, m_preview,
            [this](const QString& symbol) {
                m_preview->setText(tr("معاينة: %1").arg(formatMoney(12345)));
                Q_UNUSED(symbol);
            });
    connect(m_theme, &QComboBox::currentIndexChanged, this,
            [this]() {
                applyTheme(m_theme->currentData().toString(), *qApp);
                // Announced before the notice, so the shell's icons are already
                // the new colour by the time the page says the theme changed.
                emit themeChanged();
                showNotice(tr("طُبّقت السمة الجديدة — احفظ للإبقاء عليها"), true);
            });

    // Phase A1: the choice is stored and the catalogue is swapped in, but the
    // already-built widgets keep their source strings until the restart prompt
    // is honoured (full in-place retranslate lands in A3).
    connect(m_language, &QComboBox::currentIndexChanged, this,
            [this]() {
                const QString code = m_language->currentData().toString();
                data::SettingRepository settings(m_db);
                settings.set(QStringLiteral("language"), code);
                core::applyLanguage(code);
                QMessageBox::information(
                    this, tr("اللغة"),
                    tr("أعد تشغيل البرنامج لتطبيق اللغة بالكامل"));
            });

    connect(m_save, &QPushButton::clicked, this, &SettingsPage::save);
    connect(m_checkUpdates, &QPushButton::clicked, this, &SettingsPage::checkForUpdates);
    connect(backupNow, &QPushButton::clicked, this, [this]() {
        const QString dir = data::BackupService::defaultBackupDirectory();
        QDir().mkpath(dir);
        const QString suggested =
            QDir(dir).filePath(QStringLiteral("mahali-%1.sqlite")
                                   .arg(QDateTime::currentDateTime().toString(
                                       QStringLiteral("yyyyMMdd-HHmmss"))));
        const QString path = QFileDialog::getSaveFileName(
            this, tr("حفظ النسخة الاحتياطية"), suggested,
            tr("قواعد بيانات SQLite (*.sqlite)"));
        if (path.isEmpty()) {
            return;
        }
        QString error;
        if (backupNowTo(path, &error)) {
            showNotice(tr("تم إنشاء النسخة الاحتياطية: %1")
                           .arg(QFileInfo(path).fileName()), true);
        } else {
            showNotice(tr("تعذّر إنشاء النسخة الاحتياطية: %1").arg(error), false);
        }
    });
    connect(restoreBackup, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("اختيار نسخة احتياطية"),
            data::BackupService::defaultBackupDirectory(),
            tr("قواعد بيانات SQLite (*.sqlite)"));
        if (path.isEmpty()) {
            return;
        }
        QString error;
        if (requestRestoreFrom(path, &error)) {
            showNotice(tr("ستُستعاد النسخة عند إعادة تشغيل البرنامج"), true);
        } else {
            showNotice(tr("تعذّرت الاستعادة: %1").arg(error), false);
        }
    });
    connect(m_cleanupInactive, &QPushButton::clicked,
            this, &SettingsPage::cleanupInactiveProducts);

    refresh();
}

// A manual check, unlike the startup one, reports its result: the user asked,
// so an answer — even a failure — is owed. The checker is parented to the page
// and simply dies with it once the answer is in.
void SettingsPage::checkForUpdates()
{
    m_checkUpdates->setEnabled(false);
    m_checkUpdates->setText(tr("جارٍ التحقق..."));

    auto* checker = new core::UpdateChecker(this);
    connect(checker, &core::UpdateChecker::updateAvailable, this,
            [this, checker](const QString& tag, const QString& notes) {
                Q_UNUSED(notes);
                QMessageBox::information(
                    this, tr("تحديث متاح"),
                    tr("الإصدار %1 متاح. افتح Mahali من جديد لاحقًا لتثبيته.").arg(tag));
                finishUpdateCheck(checker);
            });
    connect(checker, &core::UpdateChecker::upToDate, this, [this, checker]() {
        QMessageBox::information(this, tr("التحديث"), tr("أنت تستخدم أحدث إصدار."));
        finishUpdateCheck(checker);
    });
    connect(checker, &core::UpdateChecker::checkFailed, this, [this, checker](const QString& reason) {
        Q_UNUSED(reason);
        QMessageBox::warning(this, tr("التحديث"), tr("تعذّر الاتصال. تحقق من الإنترنت."));
        finishUpdateCheck(checker);
    });

    checker->check();
}

void SettingsPage::finishUpdateCheck(core::UpdateChecker* checker)
{
    m_checkUpdates->setEnabled(true);
    m_checkUpdates->setText(tr("Vérifier les mises à jour"));
    checker->deleteLater();
}

void SettingsPage::cleanupInactiveProducts()
{
    m_cleanupInactive->setEnabled(false);

    const QString dir = data::BackupService::defaultBackupDirectory();
    QDir().mkpath(dir);
    const QString backupPath =
        QDir(dir).filePath(QStringLiteral("mahali-cleanup-%1.sqlite")
                               .arg(QDateTime::currentDateTime().toString(
                                   QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    QString error;
    if (!backupNowTo(backupPath, &error)) {
        m_cleanupInactive->setEnabled(true);
        showNotice(tr("تعذّر إنشاء النسخة الاحتياطية: %1").arg(error), false);
        return;
    }

    data::ProductRepository products(m_db);
    const std::vector<core::Product> all =
        products.findAll(data::ProductRepository::Visibility::All);
    std::vector<core::Product> inactive;
    for (const core::Product& product : all) {
        if (!product.active) {
            inactive.push_back(product);
        }
    }

    const auto makeProgress = [this](const QString& label, int maximum) {
        auto* progress = new QProgressDialog(label, tr("إلغاء"), 0, maximum, this);
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
        progress->setValue(0);
        return progress;
    };
    const auto enableButton = [this]() {
        m_cleanupInactive->setEnabled(true);
    };

    struct ScanState {
        std::size_t index = 0;
        std::vector<core::Product> candidates;
        int skipped = 0;
    };
    struct DeleteState {
        std::size_t index = 0;
        int deleted = 0;
        int failed = 0;
    };

    auto* scanProgress = makeProgress(tr("فحص المنتجات غير النشطة..."),
                                      static_cast<int>(inactive.size()));
    auto scanState = std::make_shared<ScanState>();
    auto* scanTimer = new QTimer(this);

    QObject::connect(scanTimer, &QTimer::timeout, this,
                     [this, scanTimer, scanProgress, scanState, products, inactive, makeProgress,
                      enableButton, backupPath]() {
                         if (scanProgress->wasCanceled()) {
                             scanTimer->stop();
                             scanTimer->deleteLater();
                             scanProgress->hide();
                             scanProgress->deleteLater();
                             enableButton();
                             showNotice(tr("تم إلغاء فحص المنتجات."), false);
                             return;
                         }

                         const std::size_t begin = scanState->index;
                         const std::size_t end = std::min(begin + 100, inactive.size());
                         const std::vector<core::Product> chunk(
                             inactive.begin() + static_cast<std::ptrdiff_t>(begin),
                             inactive.begin() + static_cast<std::ptrdiff_t>(end));
                         const std::vector<core::Product> unused =
                             data::inactiveUnusedProducts(chunk, [&products](int id) {
                                 return !products.canDeletePermanently(id);
                             });
                         scanState->candidates.insert(scanState->candidates.end(),
                                                      unused.begin(), unused.end());
                         scanState->skipped += static_cast<int>(chunk.size() - unused.size());
                         scanState->index = end;
                         scanProgress->setValue(static_cast<int>(scanState->index));
                         if (scanState->index < inactive.size()) {
                             return;
                         }

                         scanTimer->stop();
                         scanTimer->deleteLater();
                         scanProgress->hide();
                         scanProgress->deleteLater();

                         QMessageBox confirm(this);
                         confirm.setIcon(QMessageBox::Warning);
                         confirm.setWindowTitle(tr("تنظيف المنتجات غير المستخدمة"));
                         confirm.setText(tr("سيتم حذف %1 منتج غير نشط وغير مستخدم نهائياً.")
                                             .arg(QString::number(scanState->candidates.size())));
                         confirm.setInformativeText(
                             tr("سيتم تخطي %1 منتج مستخدم. النسخة الاحتياطية: %2")
                                 .arg(scanState->skipped)
                                 .arg(QFileInfo(backupPath).fileName()));
                         confirm.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
                         confirm.setDefaultButton(QMessageBox::Cancel);
                         if (confirm.exec() != QMessageBox::Ok) {
                             enableButton();
                             showNotice(tr("تم إلغاء التنظيف."), false);
                             return;
                         }
                         if (scanState->candidates.empty()) {
                             enableButton();
                             showNotice(tr("لا توجد منتجات غير مستخدمة للحذف."), true);
                             return;
                         }

                         auto* deleteProgress = makeProgress(tr("حذف المنتجات..."),
                                                             static_cast<int>(
                                                                 scanState->candidates.size()));
                         auto deleteState = std::make_shared<DeleteState>();
                         auto* deleteTimer = new QTimer(this);
                         QObject::connect(deleteTimer, &QTimer::timeout, this,
                                          [this, deleteTimer, deleteProgress, deleteState,
                                           scanState, products, enableButton]() {
                                              if (deleteProgress->wasCanceled()) {
                                                  deleteTimer->stop();
                                                  deleteTimer->deleteLater();
                                                  deleteProgress->hide();
                                                  deleteProgress->deleteLater();
                                                  enableButton();
                                                  showNotice(tr("تم إلغاء التنظيف بعد حذف %1 منتج.")
                                                                 .arg(deleteState->deleted),
                                                             false);
                                                  return;
                                              }

                                              const std::size_t begin = deleteState->index;
                                              const std::size_t end =
                                                  std::min(begin + 100,
                                                           scanState->candidates.size());
                                              for (std::size_t index = begin; index < end;
                                                   ++index) {
                                                  if (products.removePermanently(
                                                          scanState->candidates.at(index).id)) {
                                                      ++deleteState->deleted;
                                                  } else {
                                                      ++deleteState->failed;
                                                  }
                                              }
                                              deleteState->index = end;
                                              deleteProgress->setValue(
                                                  static_cast<int>(deleteState->index));
                                              if (deleteState->index
                                                  < scanState->candidates.size()) {
                                                  return;
                                              }

                                              deleteTimer->stop();
                                              deleteTimer->deleteLater();
                                              deleteProgress->hide();
                                              deleteProgress->deleteLater();
                                              enableButton();
                                              showNotice(tr("تم حذف %1 منتج، وتخطي %2 منتج مستخدم، وتعذّر حذف %3 منتج.")
                                                             .arg(deleteState->deleted)
                                                             .arg(scanState->skipped)
                                                             .arg(deleteState->failed),
                                                         deleteState->failed == 0);
                                          });
                         deleteTimer->start();
                     });
    scanTimer->start();
}

void SettingsPage::refresh()
{
    data::SettingRepository settings(m_db);
    m_shopName->setText(settings.value(QStringLiteral("shop_name")).value_or(QString()));
    m_currency->setText(settings.value(QStringLiteral("currency_symbol")).value_or(QString()));
    // ensure() rather than value_or(): the field must show the key the process
    // actually signs with. On a first launch that mints and persists it right
    // here; on every later launch main() already did, so this reads back.
    m_syncKey->setText(QString::fromUtf8(data::SyncKey::ensure(m_db)));
    // Blocked for the same reason as the language below: restoring the stored
    // value is not a change the operator made, and letting the handler run would
    // re-apply the theme and re-announce it every time the page is opened.
    const QString theme = settings.value(QStringLiteral("theme")).value_or(QStringLiteral("light"));
    const int idx = m_theme->findData(theme);
    const QSignalBlocker themeBlocker(m_theme);
    m_theme->setCurrentIndex(idx >= 0 ? idx : m_theme->findData(QStringLiteral("light")));

    // Blocked so restoring the stored value does not fire the change handler
    // (which would pop the restart dialog every time the page is opened).
    const QString language =
        settings.value(QStringLiteral("language")).value_or(core::defaultLanguage());
    const int langIdx = m_language->findData(language);
    const QSignalBlocker blocker(m_language);
    m_language->setCurrentIndex(langIdx >= 0 ? langIdx
                                             : m_language->findData(core::defaultLanguage()));

    const auto enabledRow = settings.value(QStringLiteral("enabled"));
    m_zakat->setChecked(!enabledRow.has_value() || *enabledRow == QLatin1String("1"));
    m_zakatDate->setText(settings.value(QStringLiteral("zakat_date")).value_or(QString()));

    const QString receiptPaper =
        settings.value(QStringLiteral("receipt_paper_size")).value_or(QStringLiteral("80"));
    const int paperIdx = m_receiptPaperSize->findData(receiptPaper);
    m_receiptPaperSize->setCurrentIndex(
        paperIdx >= 0 ? paperIdx : m_receiptPaperSize->findData(QStringLiteral("80")));
    m_receiptFooter->setText(settings.value(QStringLiteral("receipt_footer_text")).value_or(QString()));
    m_receiptFolder->setText(settings.value(QStringLiteral("receipt_save_folder")).value_or(QString()));
    const auto autoPrintRow = settings.value(QStringLiteral("receipt_auto_print"));
    m_receiptAutoPrint->setChecked(autoPrintRow.has_value() && *autoPrintRow == QLatin1String("1"));

    showNotice(QString(), true);
    m_preview->setText(tr("معاينة: %1").arg(formatMoney(12345)));
}

QString SettingsPage::shopName() const
{
    return m_shopName->text().trimmed();
}

QString SettingsPage::currencySymbol() const
{
    return m_currency->text().trimmed();
}

bool SettingsPage::zakatEnabled() const
{
    return m_zakat->isChecked();
}

QString SettingsPage::zakatDate() const
{
    return m_zakatDate->text().trimmed();
}

QString SettingsPage::syncKey() const
{
    return m_syncKey->text().trimmed();
}

QString SettingsPage::noticeText() const
{
    return m_notice->text();
}

bool SettingsPage::backupNowTo(const QString& destinationPath, QString* error)
{
    const auto result = data::BackupService(m_db).backupTo(destinationPath);
    if (!result.ok && error) {
        *error = result.error;
    }
    return result.ok;
}

bool SettingsPage::requestRestoreFrom(const QString& sourcePath, QString* error)
{
    QString reason;
    if (!data::BackupService::stageRestore(sourcePath, m_db.filePath(), &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }
    return true;
}

void SettingsPage::showNotice(const QString& text, bool ok)
{
    m_notice->setObjectName(ok ? QStringLiteral("noticeOk") : QStringLiteral("noticeErr"));
    m_notice->style()->unpolish(m_notice);
    m_notice->style()->polish(m_notice);
    m_notice->setText(text);
    m_notice->setVisible(!text.isEmpty());
}

void SettingsPage::setShopName(const QString& name)
{
    m_shopName->setText(name);
}

void SettingsPage::setCurrencySymbol(const QString& symbol)
{
    m_currency->setText(symbol);
}

void SettingsPage::setZakatEnabled(bool enabled)
{
    m_zakat->setChecked(enabled);
}

void SettingsPage::setZakatDate(const QString& date)
{
    m_zakatDate->setText(date);
}

void SettingsPage::setSyncKey(const QString& key)
{
    m_syncKey->setText(key);
}

void SettingsPage::save()
{
    data::SettingRepository settings(m_db);
    settings.set(QStringLiteral("shop_name"), shopName());
    settings.set(QStringLiteral("currency_symbol"), currencySymbol());
    settings.set(QStringLiteral("zakat_date"), zakatDate());
    settings.set(QStringLiteral("theme"), m_theme->currentData().toString());
    if (!syncKey().isEmpty()) {
        settings.set(QStringLiteral("sync_hmac_key"), syncKey());
    }
    settings.set(QStringLiteral("enabled"), zakatEnabled() ? QStringLiteral("1") : QStringLiteral("0"));
    settings.set(QStringLiteral("receipt_paper_size"), m_receiptPaperSize->currentData().toString());
    settings.set(QStringLiteral("receipt_footer_text"), m_receiptFooter->text().trimmed());
    settings.set(QStringLiteral("receipt_save_folder"), m_receiptFolder->text().trimmed());
    settings.set(QStringLiteral("receipt_auto_print"), m_receiptAutoPrint->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));

    app::ui::setCurrencySymbol(currencySymbol());
    m_preview->setText(tr("معاينة: %1").arg(formatMoney(12345)));
    showNotice(tr("حُفظت الإعدادات"), true);
}

} // namespace app::ui