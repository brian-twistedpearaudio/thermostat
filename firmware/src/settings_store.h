// Settings saved in flash (NVS).
#pragma once
#include "settings.h"

namespace settings_store {
thermo::Settings load();                    // missing or bad values become valid ones
void changed(const thermo::Settings& s);    // queue a save; written after a short quiet time
void poll();                                // call often from loop()
}  // namespace settings_store
