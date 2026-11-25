#include "service_invoice_dialog.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "data/date_utils.h"
#include "data/service_invoice_repository.h"
#include "format_utils.h"
#include "scan_safe_dialog.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ServiceInvoiceDialog", text);
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

QDate dateOf(const QString& iso, const QDate& fallback)
{
    const auto parsed = data::fromIso(iso);
    if (!parsed || !parsed->date().isValid()) {
        return fallback;
    }
    return parsed->date();
}

} // namespace

int showServiceInvoiceDialog(QWidget* parent, app::data::Database& db,
                             const core::ServiceInvoice& initial)
{
    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(initial.id == 0 ? tr("Nouvelle facture service")
                                         : tr("Modifier la facture service"));
    dialog.setModal(true);

    auto* type = new QComboBox;
    type->setObjectName(QStringLiteral("serviceInvoiceTypeCombo"));
    type->addItem(tr("كهرباء"), QStringLiteral("electricity"));
    type->addItem(tr("غاز"), QStringLiteral("gas"));
    type->addItem(tr("ماء"), QStringLiteral("water"));
    const int typeIndex = type->findData(initial.type);
    type->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);

    auto* invoiceDate = new QDateEdit;
    invoiceDate->setObjectName(QStringLiteral("serviceInvoiceDateField"));
    invoiceDate->setCalendarPopup(true);
    invoiceDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    invoiceDate->setDate(dateOf(initial.invoiceDate, QDate::currentDate()));

    auto* amount = new QLineEdit;
    amount->setObjectName(QStringLiteral("serviceInvoiceAmountField"));
    amount->setPlaceholderText(QStringLiteral("0"));
    if (initial.amountCents > 0) {
        amount->setText(formatMoney(initial.amountCents));
    }

    auto* dueDate = new QDateEdit;
    dueDate->setObjectName(QStringLiteral("serviceInvoiceDueDateField"));
    dueDate->setCalendarPopup(true);
    dueDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dueDate->setDate(dateOf(initial.dueDate, QDate::currentDate().addDays(15)));

    auto* status = new QComboBox;
    status->setObjectName(QStringLiteral("serviceInvoiceStatusCombo"));
    status->addItem(tr("غير مدفوعة"), QStringLiteral("unpaid"));
    status->addItem(tr("مدفوعة"), QStringLiteral("paid"));
    const int statusIndex = status->findData(initial.status);
    status->setCurrentIndex(statusIndex >= 0 ? statusIndex : 0);

    auto* notes = new QLineEdit;
    notes->setObjectName(QStringLiteral("serviceInvoiceNotesField"));
    notes->setText(initial.notes);

    auto* form = new QFormLayout;
    form->addRow(tr("النوع"), type);
    form->addRow(tr("تاريخ الفاتورة"), invoiceDate);
    form->addRow(tr("المبلغ"), amount);
    form->addRow(tr("تاريخ الاستحقاق"), dueDate);
    form->addRow(tr("الحالة"), status);
    form->addRow(tr("ملاحظات"), notes);

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
        const std::optional<long long> parsed = parseMoney(amount->text());
        if (!parsed.has_value() || *parsed <= 0) {
            QMessageBox::warning(&dialog, tr("خطأ"),
                                 tr("المبلغ يجب أن يكون أكبر من صفر"));
            amount->setFocus();
            amount->selectAll();
            return;
        }

        core::ServiceInvoice invoice = initial;
        invoice.type = type->currentData().toString();
        invoice.invoiceDate = invoiceDate->date().toString(Qt::ISODate);
        invoice.amountCents = *parsed;
        invoice.dueDate = dueDate->date().toString(Qt::ISODate);
        invoice.status = status->currentData().toString();
        invoice.notes = notes->text().trimmed();

        const int id = data::ServiceInvoiceRepository(db).save(invoice);
        if (id <= 0) {
            QMessageBox::warning(&dialog, tr("خطأ"), tr("تعذر حفظ الفاتورة"));
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
