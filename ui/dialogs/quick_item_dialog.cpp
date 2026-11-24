#include "quick_item_dialog.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/product.h"
#include "data/product_repository.h"
#include "format_utils.h"
#include "scan_safe_dialog.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("QuickItemDialog", text);
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

} // namespace

int showQuickItemDialog(QWidget* parent, app::data::Database& db)
{
    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(tr("إضافة منتج سريع"));
    dialog.setModal(true);

    auto* nameField = new QLineEdit;
    nameField->setObjectName(QStringLiteral("quickItemNameField"));

    auto* priceField = new QLineEdit;
    priceField->setObjectName(QStringLiteral("quickItemPriceField"));
    priceField->setPlaceholderText(QStringLiteral("0"));

    auto* restockDays = new QSpinBox;
    restockDays->setObjectName(QStringLiteral("quickItemRestockDays"));
    restockDays->setRange(1, 3650);
    restockDays->setValue(1);

    auto* form = new QFormLayout;
    form->addRow(tr("الاسم"), nameField);
    form->addRow(tr("السعر"), priceField);
    form->addRow(tr("أيام التعبئة"), restockDays);

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

    int savedId = 0;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString name = nameField->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(&dialog, tr("خطأ"), tr("الاسم مطلوب"));
            nameField->setFocus();
            return;
        }

        const std::optional<long long> price = parseMoney(priceField->text());
        if (!price.has_value() || *price <= 0) {
            QMessageBox::warning(&dialog, tr("خطأ"), tr("السعر يجب أن يكون أكبر من صفر"));
            priceField->setFocus();
            priceField->selectAll();
            return;
        }

        core::Product product;
        product.barcode = QString();
        product.name = name;
        product.salePriceCents = *price;
        product.isQuickItem = true;
        product.restockDays = restockDays->value();
        product.active = true;
        product.unit = QStringLiteral("piece");

        const int id = data::ProductRepository(db).save(product);
        if (id <= 0) {
            QMessageBox::warning(&dialog, tr("خطأ"), tr("تعذر حفظ المنتج السريع"));
            return;
        }
        savedId = id;
        dialog.accept();
    });

    constrainDialogToAvailableGeometry(&dialog);
    if (dialog.exec() != QDialog::Accepted) {
        return 0;
    }
    return savedId;
}

}
