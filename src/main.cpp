// Arduino declares setup() and loop() with C++ linkage; everything else is C.
extern "C" {
#include "app/receiver.h"
}
void setup() { receiver_begin(); }
void loop() { receiver_update(); }
