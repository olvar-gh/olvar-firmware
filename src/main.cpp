// Embedded tests build src/ too (test_build_src = yes) and bring their own setup()/loop().
#ifndef PIO_UNIT_TESTING

#include <Arduino.h>

#include "app.h"

namespace {
olvar::App app;
}

void setup() { app.setup(); }

void loop() { app.loop(); }

#endif  // PIO_UNIT_TESTING
