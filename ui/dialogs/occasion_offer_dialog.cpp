#include "occasion_offer_dialog.h"

#include <functional>
#include <optional>
#include <vector>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "data/occasion_offer_repository.h"
#include "data/product_repository.h"
#include "format_utils.h"
#include "scan_safe_dialog.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("OccasionOfferDialog", text);
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

void compactActionButton(QPushButton* button)
{
    button->setMinimumHeight(21);
    button->setMaximumHeight(30);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { min-height: 21px; max-height: 30px; padding: 3px 8px; "
        "font-size: 9pt; }"));
}

std::optional<core::OccasionOffer> promptForOffer(QWidget* parent,
                                                  app::data::Database& db,
                                                  int occasionId)
{
    data::ProductRepository products(db);
    const std::vector<core::Product> available = products.findAll();
    if (available.empty()) {
        QMessageBox::warning(parent, tr("Erreur"),
                             tr("لا توجد منتجات لإضافتها إلى العرض"));
        return std::nullopt;
    }

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(tr("إضافة عرض"));
    dialog.setModal(true);

    auto* productCombo = new QComboBox;
    productCombo->setObjectName(QStringLiteral("occasionOfferProductCombo"));
    for (const core::Product& product : available) {
        productCombo->addItem(product.name, product.id);
    }

    auto* priceField = new QLineEdit;
    priceField->setObjectName(QStringLiteral("occasionOfferPriceField"));
    priceField->setPlaceholderText(tr("اتركه فارغاً لعدم تغيير السعر"));

    auto* discountField = new QLineEdit;
    discountField->setObjectName(QStringLiteral("occasionOfferDiscountField"));
    discountField->setPlaceholderText(tr("0 - 100"));

    auto* bonusField = new QLineEdit;
    bonusField->setObjectName(QStringLiteral("occasionOfferBonusField"));
    bonusField->setPlaceholderText(tr("0"));

    auto* form = new QFormLayout;
    form->addRow(tr("المنتج"), productCombo);
    form->addRow(tr("سعر العرض"), priceField);
    form->addRow(tr("نسبة الخصم %"), discountField);
    form->addRow(tr("حبات إضافية"), bonusField);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    QPushButton* save = buttons->button(QDialogButtonBox::Save);
    QPushButton* cancel = buttons->button(QDialogButtonBox::Cancel);
    save->setText(tr("حفظ"));
    save->setObjectName(QStringLiteral("primary"));
    cancel->setText(tr("إلغاء"));
    disarmDefaults(buttons);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    core::OccasionOffer offer;
    offer.occasionId = occasionId;

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const int productId = productCombo->currentData().toInt();
        if (productId <= 0) {
            QMessageBox::warning(&dialog, tr("Erreur"), tr("اختر منتجاً"));
            return;
        }

        if (!priceField->text().trimmed().isEmpty()) {
            const std::optional<long long> price = parseMoney(priceField->text());
            if (!price.has_value()) {
                QMessageBox::warning(&dialog, tr("Erreur"), tr("سعر العرض غير صالح"));
                return;
            }
            offer.offerPriceCents = *price;
        }

        if (!discountField->text().trimmed().isEmpty()) {
            bool ok = false;
            const long long percent = discountField->text().trimmed().toLongLong(&ok);
            if (!ok || percent < 0 || percent > 100) {
                QMessageBox::warning(&dialog, tr("Erreur"),
                                     tr("نسبة الخصم يجب أن تكون بين 0 و 100"));
                return;
            }
            offer.discountPercent = percent;
        }

        if (!bonusField->text().trimmed().isEmpty()) {
            bool ok = false;
            const long long pieces = bonusField->text().trimmed().toLongLong(&ok);
            if (!ok || pieces < 0) {
                QMessageBox::warning(&dialog, tr("Erreur"),
                                     tr("عدد الحبات الإضافية غير صالح"));
                return;
            }
            offer.bonusPieces = pieces;
        }

        offer.productId = productId;
        dialog.accept();
    });

    constrainDialogToAvailableGeometry(&dialog);
    if (dialog.exec() != QDialog::Accepted || offer.productId <= 0) {
        return std::nullopt;
    }
    return offer;
}

} // namespace

void showOccasionOfferDialog(QWidget* parent, app::data::Database& db, int occasionId)
{
    data::ProductRepository products(db);
    data::OccasionOfferRepository offers(db);

    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(tr("إدارة العروض"));
    dialog.setModal(true);
    dialog.resize(720, 460);

    auto* title = new QLabel(tr("عروض المناسبة"));
    title->setObjectName(QStringLiteral("cardTitle"));

    auto* table = new QTableWidget(0, 5);
    table->setObjectName(QStringLiteral("occasionOfferTable"));
    table->setHorizontalHeaderLabels({tr("اسم المنتج"), tr("سعر العرض"), tr("نسبة الخصم"),
                                      tr("حبات إضافية"), tr("إجراءات")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    auto* addButton = new QPushButton(tr("إضافة عرض"));
    addButton->setObjectName(QStringLiteral("addOccasionOfferButton"));
    addButton->setCursor(Qt::PointingHandCursor);

    auto* closeButton = new QPushButton(tr("إغلاق"));
    closeButton->setObjectName(QStringLiteral("secondary"));
    closeButton->setCursor(Qt::PointingHandCursor);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(addButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(title);
    layout->addWidget(table, 1);
    layout->addLayout(buttonRow);

    std::function<void()> refresh;
    refresh = [&]() {
        const QVector<core::OccasionOffer> rows = offers.findByOccasionId(occasionId);
        table->setRowCount(rows.size());
        for (int row = 0; row < rows.size(); ++row) {
            const core::OccasionOffer& offer = rows.at(row);
            const std::optional<core::Product> product = products.findById(offer.productId);
            const QString name = product.has_value()
                                     ? product->name
                                     : tr("منتج #%1").arg(offer.productId);

            table->setItem(row, 0, new QTableWidgetItem(name));
            table->setItem(row, 1, new QTableWidgetItem(
                                       offer.offerPriceCents.has_value()
                                           ? formatMoney(*offer.offerPriceCents)
                                           : QStringLiteral("-")));
            table->setItem(row, 2, new QTableWidgetItem(
                                       offer.discountPercent.has_value()
                                           ? QStringLiteral("%1%").arg(*offer.discountPercent)
                                           : QStringLiteral("-")));
            table->setItem(row, 3, new QTableWidgetItem(
                                       offer.bonusPieces > 0
                                           ? QString::number(offer.bonusPieces)
                                           : QStringLiteral("-")));

            auto* removeButton = new QPushButton(tr("حذف"));
            removeButton->setObjectName(QStringLiteral("occasionOfferDeleteButton"));
            removeButton->setCursor(Qt::PointingHandCursor);
            compactActionButton(removeButton);
            const int offerId = offer.id;
            QObject::connect(removeButton, &QPushButton::clicked, &dialog,
                             [&, offerId]() {
                                 if (!offers.remove(offerId)) {
                                     QMessageBox::warning(&dialog, tr("Erreur"),
                                                          tr("تعذر حذف العرض"));
                                     return;
                                 }
                                 refresh();
                             });
            table->setCellWidget(row, 4, removeButton);
        }
    };

    QObject::connect(addButton, &QPushButton::clicked, &dialog, [&]() {
        const std::optional<core::OccasionOffer> offer = promptForOffer(&dialog, db, occasionId);
        if (!offer.has_value()) {
            return;
        }
        if (offers.insert(*offer) == 0) {
            QMessageBox::warning(&dialog, tr("Erreur"), tr("تعذر حفظ العرض"));
            return;
        }
        refresh();
    });
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    refresh();
    constrainDialogToAvailableGeometry(&dialog);
    dialog.exec();
}

} // namespace app::ui
