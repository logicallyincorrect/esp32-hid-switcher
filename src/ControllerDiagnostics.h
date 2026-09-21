#pragma once
#include <ArduinoJson.h>
namespace ControllerDiagnostics {
void reports(uint16_t keyboard,uint16_t mouse,uint16_t relativeMouse=0xffff);
void printStatus();
void appendStatus(JsonObject out);
}
