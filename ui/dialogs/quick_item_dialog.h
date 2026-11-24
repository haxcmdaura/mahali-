#pragma once

#include <QWidget>

namespace app::data {
class Database;
}

namespace app::ui {

// Asks for the three facts a quick item carries, saves it as an explicit member
// of the POS bar, and returns the new product id. Returns 0 on cancel or failure.
int showQuickItemDialog(QWidget* parent, app::data::Database& db);

}
