#pragma once

#include <vector>

#include "core/product.h"

namespace app::data {

template <typename IsUsed>
std::vector<core::Product> inactiveUnusedProducts(const std::vector<core::Product>& products,
                                                 IsUsed isUsed)
{
    std::vector<core::Product> result;
    for (const core::Product& product : products) {
        if (!product.active && !isUsed(product.id)) {
            result.push_back(product);
        }
    }
    return result;
}

}
