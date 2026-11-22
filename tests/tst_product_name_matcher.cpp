#include <QtTest/QtTest>

#include <QString>
#include <vector>

#include "core/product.h"
#include "core/product_name_matcher.h"

class ProductNameMatcherTest : public QObject
{
    Q_OBJECT

private slots:
    void exactMatchWins();
    void singlePartialMatch();
    void ambiguousPartialMatch();
    void noMatch();
};

namespace {

app::core::Product product(int id, const QString& name, bool active = true)
{
    app::core::Product result;
    result.id = id;
    result.name = name;
    result.active = active;
    return result;
}

}

void ProductNameMatcherTest::exactMatchWins()
{
    const std::vector<app::core::Product> products = {
        product(1, QStringLiteral("Coca"), false),
        product(2, QStringLiteral("Coca Cola")),
        product(3, QStringLiteral("Coca")),
    };

    const std::vector<app::core::Product> matches =
        app::core::matchProductsByName(products, QStringLiteral("Coca"));

    QVERIFY(matches.size() == 1);
    QCOMPARE(matches.front().id, 3);
}

void ProductNameMatcherTest::singlePartialMatch()
{
    const std::vector<app::core::Product> products = {
        product(1, QStringLiteral("Coca Cola")),
        product(2, QStringLiteral("Coca Light"), false),
        product(3, QStringLiteral("Pepsi")),
    };

    const std::vector<app::core::Product> matches =
        app::core::matchProductsByName(products, QStringLiteral("cola"));

    QVERIFY(matches.size() == 1);
    QCOMPARE(matches.front().id, 1);
}

void ProductNameMatcherTest::ambiguousPartialMatch()
{
    const std::vector<app::core::Product> products = {
        product(1, QStringLiteral("Coca Cola")),
        product(2, QStringLiteral("Diet Cola")),
        product(3, QStringLiteral("Cola Zero"), false),
    };

    const std::vector<app::core::Product> matches =
        app::core::matchProductsByName(products, QStringLiteral("cola"));

    QVERIFY(matches.size() == 2);
    QCOMPARE(matches.at(0).id, 1);
    QCOMPARE(matches.at(1).id, 2);
}

void ProductNameMatcherTest::noMatch()
{
    const std::vector<app::core::Product> products = {
        product(1, QStringLiteral("Coca Cola")),
    };

    const std::vector<app::core::Product> matches =
        app::core::matchProductsByName(products, QStringLiteral("zzz"));

    QVERIFY(matches.empty());
}

QTEST_GUILESS_MAIN(ProductNameMatcherTest)
#include "tst_product_name_matcher.moc"
