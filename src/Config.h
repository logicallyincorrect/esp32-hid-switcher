#pragma once
#include <Arduino.h>

#define DEVICE_NAME "Moonlander BLE"
#define DEVICE_MANUFACTURER "Moonlander USB Bridge"
#define NUM_DEVICE_SLOTS 3

// Control + Command + normal number-row 1/2/3 (either left/right modifiers).
#define ENABLE_DEVICE_SWITCHING true

// Freenove ESP32-S3 WROOM/Lite LED pins. -1 disables the corresponding LED.
#define LED_FEEDBACK_PIN 2
#define LED_RGB_PIN 48
