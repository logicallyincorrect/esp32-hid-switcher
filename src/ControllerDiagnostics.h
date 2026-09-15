#pragma once
#include <ArduinoJson.h>
namespace ControllerDiagnostics {
void reports(uint16_t keyboard,uint16_t mouse);
void printStatus();
void appendStatus(JsonObject out);
}
