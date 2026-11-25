#include <QtTest/QtTest>

#include <algorithm>
#include <vector>

#include "core/product.h"
#include "data/product_cleanup.h"

class ProductCleanupTest : public QObject
{
    Q_OBJECT

private slots:
    void keepsOnlyInactiveUnused();
};

namespace {

app::core::Product product(int id, bool active)
{
    app::core::Product result;
    result.id = id;
    result.active = active;
    return result;
}

}

void ProductCleanupTest::keepsOnlyInactiveUnused()
{
    const std::vector<app::core::Product> products = {
        product(1, true),
        product(2, false),
        product(3, false),
        product(4, false),
        product(5, true),
    };
    const std::vector<int> usedIds = {2, 5};

    const std::vector<app::core::Product> candidates = app::data::inactiveUnusedProducts(
        products,
        [&usedIds](int id) {
            return std::find(usedIds.begin(), usedIds.end(), id) != usedIds.end();
        });

    QVERIFY(candidates.size() == 2);
    QCOMPARE(candidates.at(0).id, 3);
    QCOMPARE(candidates.at(1).id, 4);
}

QTEST_GUILESS_MAIN(ProductCleanupTest)
#include "tst_product_cleanup.moc"
