#pragma once

#include <QWidget>

#include "data/database.h"

namespace app::ui {

void showOccasionOfferDialog(QWidget* parent, app::data::Database& db, int occasionId);

}
