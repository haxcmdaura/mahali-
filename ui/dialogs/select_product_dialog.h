#pragma once

#include <optional>
#include <vector>

#include <QWidget>

#include "core/product.h"

namespace app::ui {

std::optional<core::Product> showSelectProductDialog(QWidget* parent,
                                                    const std::vector<core::Product>& products);

}
