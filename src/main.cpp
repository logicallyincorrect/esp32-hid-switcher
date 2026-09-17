#include "Application.h"
#include "Board.h"
#include <Arduino.h>

namespace {
Application application;
}

void setup() {
  Serial.setTxBufferSize(2048);
  Serial.begin(Board::serialBaud);
  // Preserve the proven startup interval before the USB host resets the hub.
  // The hub and downstream devices can share the board's power supply.
  delay(1000);
  application.begin();
}

void loop() {
  application.tick();
  delay(1);
}
