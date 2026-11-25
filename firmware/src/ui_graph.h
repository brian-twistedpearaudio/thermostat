// Graph screen: temperature and set point over 1 day, 5 days or 1 month, from the server.
#pragma once

namespace ui {

void graphCreate(void (*onBack)());
void graphShow();
void graphPoll();  // call often: shows answers, and refreshes while the screen is open

}  // namespace ui
