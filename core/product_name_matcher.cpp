#include "core/product_name_matcher.h"

namespace app::core {

std::vector<Product> matchProductsByName(const std::vector<Product>& products, const QString& text)
{
    const QString needle = text.trimmed();
    if (needle.isEmpty()) {
        return {};
    }
    for (const Product& product : products) {
        if (product.active && product.name == needle) {
            return {product};
        }
    }
    std::vector<Product> partial;
    for (const Product& product : products) {
        if (product.active && product.name.contains(needle, Qt::CaseInsensitive)) {
            partial.push_back(product);
        }
    }
    return partial;
}

}
