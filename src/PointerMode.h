#pragma once
#ifndef HID_ABSOLUTE_POINTER
#define HID_ABSOLUTE_POINTER 0
#endif
#ifndef HID_ABSOLUTE_ONLY_TEST
#define HID_ABSOLUTE_ONLY_TEST 0
#endif
#ifndef HID_ABSOLUTE_GAIN_X
#define HID_ABSOLUTE_GAIN_X 16
#endif
#ifndef HID_ABSOLUTE_GAIN_Y
#define HID_ABSOLUTE_GAIN_Y 16
#endif
namespace PointerMode {
constexpr bool absolute = HID_ABSOLUTE_POINTER != 0;
constexpr bool absoluteOnly = HID_ABSOLUTE_ONLY_TEST != 0;
constexpr bool relativeReport = absolute && !absoluteOnly;
constexpr unsigned subscriptions = relativeReport ? 7 : 3;
static_assert(!absoluteOnly || absolute,"Absolute-only test requires absolute mode");
constexpr int gainX = HID_ABSOLUTE_GAIN_X, gainY = HID_ABSOLUTE_GAIN_Y;
static_assert(gainX > 0 && gainX <= 256 && gainY > 0 && gainY <= 256,"Absolute gains must be 1..256");
}
