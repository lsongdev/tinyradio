#include <Arduino.h>
#ifdef BATTERY_DEMO
#include "examples/battery_screen.h"
void setup() { examples::beginBatteryScreen(); }
void loop() { examples::updateBatteryScreen(); delay(1); }
#else
#include "app/receiver.h"
void setup() { app::beginReceiver(); }
void loop() { app::updateReceiver(); }
#endif
