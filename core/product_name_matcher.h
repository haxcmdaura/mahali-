#pragma once

#include <QString>
#include <vector>

#include "core/product.h"

namespace app::core {

std::vector<Product> matchProductsByName(const std::vector<Product>& products, const QString& text);

}
