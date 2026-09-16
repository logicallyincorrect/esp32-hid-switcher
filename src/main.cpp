#include "Application.h"
#include "Board.h"
#include <Arduino.h>

namespace {
Application application;
}

void setup() {
  Serial.setTxBufferSize(2048);
  Serial.begin(Board::serialBaud);
  application.begin();
}

void loop() {
  application.tick();
  delay(1);
}
