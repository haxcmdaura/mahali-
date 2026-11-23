#include "ui_helpers.h"

#include <QFrame>
#include <QDialog>
#include <QGuiApplication>
#include <QLayout>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>

#include "app_icon.h"
#include "theme_tokens.h"

namespace app::ui {

QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    return card;
}

QLabel* makeChip(const QString& text, const QString& state, QWidget* parent)
{
    auto* chip = new QLabel(text, parent);
    chip->setProperty("state", state);
    chip->setAlignment(Qt::AlignCenter);
    return chip;
}

void setChipState(QLabel* chip, const QString& state)
{
    if (chip->property("state").toString() == state) {
        return;
    }
    chip->setProperty("state", state);
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
    chip->update();
}

QLineEdit* makeSearchField(const QString& placeholder, QWidget* parent)
{
    auto* field = new QLineEdit(parent);
    field->setObjectName(QStringLiteral("searchField"));
    field->setPlaceholderText(placeholder);
    field->setClearButtonEnabled(true);
    field->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                     QLineEdit::LeadingPosition);
    return field;
}

QLabel* makeCardTitle(const QString& text, QWidget* parent)
{
    auto* title = new QLabel(text, parent);
    title->setObjectName(QStringLiteral("cardTitle"));
    return title;
}

void padPageLayout(QVBoxLayout* layout)
{
    layout->setContentsMargins(themeTokens::pageInset, themeTokens::pageInset,
                               themeTokens::pageInset, themeTokens::pageInset);
    layout->setSpacing(themeTokens::space12);
}

void padCardLayout(QLayout* layout)
{
    layout->setContentsMargins(themeTokens::space12, themeTokens::space12,
                               themeTokens::space12, themeTokens::space12);
    layout->setSpacing(themeTokens::space8);
}

void constrainDialogToAvailableGeometry(QDialog* dialog)
{
    if (!dialog) {
        return;
    }
    QScreen* screen = dialog->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }
    const QSize available = screen->availableGeometry().size() - QSize(32, 32);
    dialog->setMaximumSize(available);
    dialog->resize(dialog->size().boundedTo(available));
}

namespace {

QString kilogramsText(long long grams)
{
    const bool negative = grams < 0;
    const unsigned long long magnitude = negative
        ? static_cast<unsigned long long>(-(grams + 1)) + 1ULL
        : static_cast<unsigned long long>(grams);
    const unsigned long long hundredths = (magnitude + 5ULL) / 10ULL;
    const unsigned long long whole = hundredths / 100ULL;
    const unsigned long long fraction = hundredths % 100ULL;
    return QStringLiteral("%1%2.%3 kg")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(whole)
        .arg(static_cast<int>(fraction), 2, 10, QLatin1Char('0'));
}

} // namespace

SaleDetailsLine saleDetailsLine(const core::SaleItem& item)
{
    if (item.unitKind == QStringLiteral("weight")) {
        long long total = item.unitPriceCents * item.quantity;
        total /= 1000;
        return {kilogramsText(item.quantity), total};
    }
    return {QString::number(item.quantity), item.unitPriceCents * item.quantity};
}

} // namespace app::ui