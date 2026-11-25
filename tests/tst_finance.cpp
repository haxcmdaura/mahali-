#include <QtTest/QtTest>

#include "core/cash_session_calculator.h"
#include "core/profit_loss_calculator.h"
#include "core/weight_parser.h"
#include "core/zakat_calculator.h"

class FinanceTest : public QObject
{
    Q_OBJECT

private slots:
    void profitLoss();
    void cashSessionVariance();
    void zakatBaseTreatsDebtAsCash();
    void zakatBaseStockCashReceivables();
    void parseWeightGrams();
};

void FinanceTest::profitLoss()
{
    using app::core::ProfitLossCalculator;

    const app::core::ProfitLossReport report =
        ProfitLossCalculator::compute(/*revenue=*/100000,
                                      /*cogs=*/60000,
                                      /*expenses=*/15000,
                                      /*drawings=*/5000);

    QCOMPARE(report.revenueCents, 100000);
    QCOMPARE(report.cogsCents, 60000);
    QCOMPARE(report.grossProfitCents, 40000);
    QCOMPARE(report.expensesCents, 15000);
    QCOMPARE(report.netProfitCents, 25000);
    QCOMPARE(report.drawingsCents, 5000);
}

void FinanceTest::cashSessionVariance()
{
    using app::core::CashSessionCalculator;

    const long long expected = CashSessionCalculator::expectedTotalCents(20000, 15000 + (-4000));
    QCOMPARE(expected, 31000);
    QCOMPARE(CashSessionCalculator::varianceCents(31500, expected), 500);
    QCOMPARE(CashSessionCalculator::varianceCents(30000, expected), -1000);
}

void FinanceTest::zakatBaseTreatsDebtAsCash()
{
    using app::core::ZakatCalculator;

    QCOMPARE(ZakatCalculator::zakatBaseCents(100000, 50000), 150000);
    QCOMPARE(ZakatCalculator::zakatBaseCents(0, 70000), 70000);
}

void FinanceTest::zakatBaseStockCashReceivables()
{
    using app::core::ZakatCalculator;

    app::core::ZakatInputs inputs;
    inputs.stockValueCents = 100000;
    inputs.cashCents = 50000;
    inputs.receivablesCents = 30000;
    QCOMPARE(ZakatCalculator::zakatBase(inputs), 180000LL);

    // Each leg on its own is enough to make the base, and a shop holding stock
    // with an empty till is not a shop that owes no zakat.
    app::core::ZakatInputs stockOnly;
    stockOnly.stockValueCents = 40000;
    QCOMPARE(ZakatCalculator::zakatBase(stockOnly), 40000LL);

    app::core::ZakatInputs nothing;
    QCOMPARE(ZakatCalculator::zakatBase(nothing), 0LL);
}

void FinanceTest::parseWeightGrams()
{
    struct Case {
        QString text;
        std::optional<long long> grams;
    };
    const QVector<Case> cases = {
        {QStringLiteral("2.5"), 2500},
        {QStringLiteral("2,5"), 2500},
        {QStringLiteral("٢٫٥"), 2500},
        {QStringLiteral("0.250"), 250},
        {QStringLiteral("0.001"), 1},
        {QStringLiteral("0.0005"), std::nullopt},
        {QStringLiteral("2.5000"), 2500},
        {QStringLiteral("2.5001"), std::nullopt},
        {QStringLiteral("1,000"), 1000},
        {QStringLiteral("1.000"), 1000},
        {QStringLiteral("1,234.5"), std::nullopt},
        {QStringLiteral("0"), std::nullopt},
        {QStringLiteral("-1"), std::nullopt},
        {QStringLiteral(""), std::nullopt},
        {QStringLiteral("10001"), std::nullopt},
        {QStringLiteral("10000"), 10000000},
    };

    for (const Case& item : cases) {
        const std::optional<long long> actual = app::core::parseWeightGrams(item.text);
        if (item.grams.has_value()) {
            QVERIFY2(actual.has_value(), qPrintable(item.text));
            QCOMPARE(*actual, *item.grams);
        } else {
            QVERIFY2(!actual.has_value(), qPrintable(item.text));
        }
    }
}

QTEST_GUILESS_MAIN(FinanceTest)
#include "tst_finance.moc"
