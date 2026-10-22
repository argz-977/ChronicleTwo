#pragma once

// A DualSense's HD haptics on iOS. iOS gives apps no audio endpoint for the pad, so the actuator
// signal of dualsense_haptics.hpp cannot be played as sound; Core Haptics drives the actuators
// through GameController instead. A thread renders that signal and plays, on each handle, its
// strength as the intensity and its pitch as the sharpness of a continuous haptic event.

// Whether a DualSense's handle engines are running. Starts the thread on its first call.
bool IosHapticsReady();
