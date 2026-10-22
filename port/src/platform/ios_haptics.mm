#include "ios_haptics.hpp"

#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "audio/synth.hpp"
#include "dualsense_haptics.hpp"

namespace {

constexpr int   kRate = audio::kSpuRate;
constexpr int   kTickFrames = kRate / 100;
constexpr auto  kTick = std::chrono::milliseconds(10);
constexpr int   kRetryTicks = 100;
// A one-pole low-pass at this frequency splits each side into a low and a high band: the 60 Hz
// large motor tone lands mostly below it and the 170 Hz small motor tone mostly above.
constexpr float kSplitHz = 100.0f;
// Energies are averaged over about two ticks, so a 60 Hz tone reads steady.
constexpr float kAverage = 0.5f;

std::atomic<bool> g_ready{false};

// One handle's engine and its continuous event, whose intensity and sharpness the thread moves.
struct Handle {
    CHHapticEngine                    *engine = nil;
    id<CHHapticPatternPlayer>          player = nil;
    std::shared_ptr<std::atomic<bool>> stopped = std::make_shared<std::atomic<bool>>(false);
    bool                               playing = false;
    float                              low = 0.0f;
    float                              energy = 0.0f;
    float                              low_energy = 0.0f;
    float                              sent_intensity = -1.0f;
    float                              sent_sharpness = -1.0f;
};

bool g_logged_failure = false;

void Fail(const char *what, NSError *error) {
    if (!g_logged_failure) {
        g_logged_failure = true;
        NSLog(@"DualSense haptics: %s failed: %@", what, error.localizedDescription ?: @"unknown error");
    }
}

bool Open(Handle &handle, GCController *controller, GCHapticsLocality locality) {
    NSError *error = nil;
    handle.engine = [controller.haptics createEngineWithLocality:locality];
    if (handle.engine == nil) {
        Fail("creating an engine", nil);
        return false;
    }
    auto stopped = handle.stopped;
    stopped->store(false);
    handle.engine.stoppedHandler = ^(CHHapticEngineStoppedReason) {
      stopped->store(true);
    };
    handle.engine.resetHandler = ^{
      stopped->store(true);
    };
    if (![handle.engine startAndReturnError:&error]) {
        Fail("starting an engine", error);
        return false;
    }
    CHHapticEvent *event = [[CHHapticEvent alloc]
        initWithEventType:CHHapticEventTypeHapticContinuous
               parameters:@[
                   [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1.0f],
                   [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:0.5f],
               ]
             relativeTime:0
                 duration:GCHapticDurationInfinite];
    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[ event ] parameters:@[] error:&error];
    if (pattern == nil) {
        Fail("building the pattern", error);
        return false;
    }
    handle.player = [handle.engine createPlayerWithPattern:pattern error:&error];
    if (handle.player == nil) {
        Fail("creating a player", error);
        return false;
    }
    handle.playing = false;
    handle.sent_intensity = handle.sent_sharpness = -1.0f;
    return true;
}

void Close(Handle &handle) {
    if (handle.player != nil && handle.playing) {
        [handle.player stopAtTime:CHHapticTimeImmediate error:nil];
    }
    [handle.engine stopWithCompletionHandler:nil];
    handle.player = nil;
    handle.engine = nil;
    handle.playing = false;
}

// Measures one side's share of the tick and plays it: the level as intensity, how much of it sits
// above the split as sharpness.
void Play(Handle &handle, const float *left_right, int side) {
    const float split = 1.0f - std::exp(-6.28318530718f * kSplitHz / kRate);
    float       energy = 0.0f;
    float       low_energy = 0.0f;
    for (int i = 0; i < kTickFrames; i++) {
        const float x = left_right[i * 2 + side];
        handle.low += (x - handle.low) * split;
        energy += x * x;
        low_energy += handle.low * handle.low;
    }
    handle.energy += (energy / kTickFrames - handle.energy) * kAverage;
    handle.low_energy += (low_energy / kTickFrames - handle.low_energy) * kAverage;

    const float intensity = std::clamp(std::sqrt(2.0f * handle.energy), 0.0f, 1.0f);
    const float high = handle.energy > 1e-6f ? std::max(0.0f, 1.0f - handle.low_energy / handle.energy) : 0.0f;
    const float sharpness = std::clamp((high - 0.2f) / 0.6f, 0.0f, 1.0f);

    NSError *error = nil;
    if (intensity < 0.01f) {
        if (handle.playing) {
            [handle.player stopAtTime:CHHapticTimeImmediate error:&error];
            handle.playing = false;
        }
        return;
    }
    if (std::fabs(intensity - handle.sent_intensity) < 0.01f && std::fabs(sharpness - handle.sent_sharpness) < 0.02f &&
        handle.playing) {
        return;
    }
    NSArray *parameters = @[
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                                                        value:intensity
                                                 relativeTime:0],
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl
                                                        value:sharpness - 0.5f
                                                 relativeTime:0],
    ];
    [handle.player sendParameters:parameters atTime:CHHapticTimeImmediate error:&error];
    if (!handle.playing && [handle.player startAtTime:CHHapticTimeImmediate error:&error]) {
        handle.playing = true;
    }
    handle.sent_intensity = intensity;
    handle.sent_sharpness = sharpness;
}

GCController *FindDualSense() {
    for (GCController *controller in GCController.controllers) {
        if ([controller.productCategory isEqualToString:@"DualSense"] && controller.haptics != nil) {
            return controller;
        }
    }
    return nil;
}

void Run() {
    std::vector<float> signal(kTickFrames * 2);
    GCController      *controller = nil;
    Handle             handles[2];
    int                wait = 0;
    for (;;) {
        std::this_thread::sleep_for(kTick);
        @autoreleasepool {
            // An engine also stops when its pad disconnects (CHHapticEngineStoppedReasonGameControllerDisconnect).
            if (controller != nil && (handles[0].stopped->load() || handles[1].stopped->load())) {
                Close(handles[0]);
                Close(handles[1]);
                controller = nil;
                g_ready.store(false);
            }
            if (controller == nil) {
                if (wait-- > 0) {
                    continue;
                }
                wait = kRetryTicks;
                GCController *found = FindDualSense();
                if (found == nil) {
                    continue;
                }
                if (!Open(handles[0], found, GCHapticsLocalityLeftHandle) ||
                    !Open(handles[1], found, GCHapticsLocalityRightHandle)) {
                    Close(handles[0]);
                    Close(handles[1]);
                    continue;
                }
                NSLog(@"DualSense haptics: playing on \"%@\" through Core Haptics", found.vendorName);
                controller = found;
                g_ready.store(true);
            }
            HapticsRender(signal.data(), kTickFrames);
            Play(handles[0], signal.data(), 0);
            Play(handles[1], signal.data(), 1);
        }
    }
}

} // namespace

bool IosHapticsReady() {
    static std::once_flag started;
    std::call_once(started, [] { std::thread(Run).detach(); });
    return g_ready.load();
}
