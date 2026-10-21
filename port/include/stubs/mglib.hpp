#pragma once

#include "platform/clock.hpp"

// Retail Exit__2 is a separate symbol from Exit; the port terminates through the SDK's Exit.
#define Exit__2 Exit

// Stand in for the T0 HBLANK counter behind mgNowFrameRate: wall-clock time since the frame began, in fields.
void mgTimeFrameBegin();
float mgTimeFrameFields();

// Waits until `frames` VSync ticks after `start`, calling `hook` while it waits.
void mgTimeWaitTicks(int start, int frames, ClockWaitHook hook);

int VSyncCallBack(int field);
