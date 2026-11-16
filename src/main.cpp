// Stub entry point: prints the firmware version. Replaced by the app in T8.
//
// The esp32dev env builds src/ into embedded tests (test_build_src = yes) so the
// adapters under src/ are testable; each Unity test then brings its own
// setup()/loop(), so this file must stay out of test builds.
#ifndef PIO_UNIT_TESTING

#include <Arduino.h>

#include "pins.h"

void setup() {
  Serial.begin(115200);
  Serial.printf("olvar-firmware %s\n", OLVAR_FW_VERSION);
}

void loop() { delay(1000); }

#endif  // PIO_UNIT_TESTING
